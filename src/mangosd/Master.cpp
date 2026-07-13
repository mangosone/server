/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

/// \addtogroup mangosd
/// @{
/// \file

#include "Master.h"

#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "ProgressBar.h"
#include "MapManager.h"
#include "MassMailMgr.h"
#include "ObjectAccessor.h"
#include "SystemConfig.h"
#include "Timer.h"
#include "Util.h"
#include "World.h"
#include "WorldSocketMgr.h"

#ifdef ENABLE_SOAP
#include "SOAP/SoapThread.h"
#endif

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include "ServiceWin32.h"
extern int m_ServiceStatus;
#else
#include "PosixDaemon.h"     // detachDaemon()
#include <unistd.h>          // usleep(), STDIN_FILENO
#include <sys/select.h>      // select(), fd_set
#endif

extern DatabaseType WorldDatabase;
extern DatabaseType CharacterDatabase;
extern DatabaseType LoginDatabase;
extern uint32 realmID;

/// Shortest interval between two world ticks, in milliseconds.
#define WORLD_SLEEP_CONST 50

namespace
{
    /// Forward a fully-built progress-bar redraw to the off-thread console writer
    /// (verbatim: no prefix, colour or newline), so the bar shares one serialised stdout
    /// with the log lines and cannot tear against them.
    void BarConsoleSink(char const* bytes, size_t len)
    {
        sLog.ConsoleEmitRaw(std::string(bytes, len));
    }

    /**
     * @brief RAII handle for one opened database.
     *
     * The point of this: opening the three databases is a ladder, and every rung can
     * fail. Done by hand, each failure has to remember to halt the delay threads of
     * everything opened so far — which is where the old code's repeated (and easy to get
     * wrong) HaltDelayThread() cascades came from. Here, unwinding is automatic, and
     * Release() is called only once every database is up.
     */
    class DatabaseGuard
    {
        public:

            explicit DatabaseGuard(DatabaseType& db) : m_db(&db) {}

            ~DatabaseGuard()
            {
                if (m_db)
                {
                    m_db->HaltDelayThread();
                }
            }

            DatabaseGuard(const DatabaseGuard&) = delete;
            DatabaseGuard& operator=(const DatabaseGuard&) = delete;

            /// The database is now owned by the caller; stop tracking it.
            void Release() { m_db = nullptr; }

        private:

            DatabaseType* m_db;
    };

    /**
     * @brief Open one database and verify its schema version.
     */
    bool OpenDatabase(DatabaseType& db, const char* infoKey, const char* connKey,
                      const char* label, DatabaseTypes versionCheck)
    {
        const std::string dbstring = sConfig.GetStringDefault(infoKey, "");
        if (dbstring.empty())
        {
            sLog.outError("%s not specified in configuration file", label);
            return false;
        }

        const int nConnections = sConfig.GetIntDefault(connKey, 1);
        sLog.outString("%s total connections: %i", label, nConnections + 1);

        if (!db.Initialize(dbstring.c_str(), nConnections))
        {
            sLog.outError("Can not connect to %s %s", label, dbstring.c_str());
            return false;
        }

        return db.CheckDatabaseVersion(versionCheck);
    }
}

// ── Databases ────────────────────────────────────────────────────────────────────

bool Master::StartDatabases()
{
    if (!OpenDatabase(WorldDatabase, "WorldDatabaseInfo", "WorldDatabaseConnections",
                      "World Database", DATABASE_WORLD))
    {
        return false;
    }
    DatabaseGuard worldGuard(WorldDatabase);

    if (!OpenDatabase(CharacterDatabase, "CharacterDatabaseInfo", "CharacterDatabaseConnections",
                      "Character Database", DATABASE_CHARACTER))
    {
        return false;
    }
    DatabaseGuard characterGuard(CharacterDatabase);

    if (!OpenDatabase(LoginDatabase, "LoginDatabaseInfo", "LoginDatabaseConnections",
                      "Login Database", DATABASE_REALMD))
    {
        return false;
    }
    DatabaseGuard loginGuard(LoginDatabase);

    sLog.outString();

    ///- Get the realm Id from the configuration file
    realmID = sConfig.GetIntDefault("RealmID", 0);
    if (!realmID)
    {
        sLog.outError("Realm ID not defined in configuration file");
        return false;
    }

    sLog.outString("Realm running as realm ID %d", realmID);
    sLog.outString();

    ///- Clean the database before starting
    ClearOnlineAccounts();

    sWorld.LoadDBVersion();
    sLog.outString("Using World DB: %s", sWorld.GetDBVersion());
    sLog.outString();

    // Everything is up: the databases are ours to keep, so the guards must not unwind.
    loginGuard.Release();
    characterGuard.Release();
    worldGuard.Release();

    return true;
}

void Master::StopDatabases()
{
    sLog.outString("[shutdown] halting DB delay threads (Character/World/Login)...");

    CharacterDatabase.HaltDelayThread();
    WorldDatabase.HaltDelayThread();
    LoginDatabase.HaltDelayThread();

    sLog.outString("[shutdown] DB delay threads halted");
}

void Master::ClearOnlineAccounts()
{
    // Cleanup online status for characters hosted at current realm
    /// \todo Only accounts with characters logged on *this* realm should have online
    /// status reset. Move the online column from 'account' to 'realmcharacters'?
    LoginDatabase.PExecute("UPDATE `account` SET `active_realm_id` = 0, `os` = ''  WHERE `active_realm_id` = '%u'", realmID);

    CharacterDatabase.Execute("UPDATE `characters` SET `online` = 0 WHERE `online`<>0");

    // Battleground instance ids reset at server restart
    CharacterDatabase.Execute("UPDATE `character_battleground_data` SET `instance_id` = 0");
}

// ── Threads ──────────────────────────────────────────────────────────────────────

/**
 * @brief The world heartbeat.
 *
 * Runs on the caller's thread (main), so there is no world thread to spawn or join any
 * more. Ticks the world at most every WORLD_SLEEP_CONST ms, then unwinds the world in
 * order: kick the players, flush their sessions, stop the network, unload the maps.
 */
void Master::WorldLoop()
{
    uint32 realPrevTime = getMSTime();

    sLog.outString("World Updater started (%dms min update interval)", WORLD_SLEEP_CONST);

    while (!World::IsStopped())
    {
        ++World::m_worldLoopCounter;

        const uint32 realCurrTime = getMSTime();
        const uint32 diff = getMSTimeDiff(realPrevTime, realCurrTime);

        sWorld.Update(diff);
        realPrevTime = realCurrTime;

        // Sleep off whatever is left of this tick's budget.
        const uint32 executionTimeDiff = getMSTimeDiff(realCurrTime, getMSTime());
        if (executionTimeDiff < WORLD_SLEEP_CONST)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(WORLD_SLEEP_CONST - executionTimeDiff));
        }

#ifdef _WIN32
        if (m_ServiceStatus == 0)               // service stopped
        {
            World::StopNow(SHUTDOWN_EXIT_CODE);
        }

        while (m_ServiceStatus == 2)            // service paused
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
#endif
    }

    sLog.outString("[shutdown] world loop stopped; entering world shutdown tail");

    sLog.outString("[shutdown] KickAll: saving + kicking players...");
    sWorld.KickAll();                           // save and kick all players
    sLog.outString("[shutdown] KickAll done");

    sLog.outString("[shutdown] final UpdateSessions...");
    sWorld.UpdateSessions(1);                   // real players unload requires this call
    sLog.outString("[shutdown] final UpdateSessions done");

    sLog.outString("[shutdown] StopNetwork: closing listener + joining network threads...");
    sWorldSocketMgr.StopNetwork();
    sLog.outString("[shutdown] StopNetwork done");

    sLog.outString("[shutdown] UnloadAll: unloading maps + MapUpdater teardown...");
    sMapMgr.UnloadAll();                        // unload all grids (including locked ones)
    sLog.outString("[shutdown] UnloadAll returned");

    sLog.outString("World Updater stopped");
}

/**
 * @brief Watchdog: abort the process if the world loop stops advancing.
 *
 * A frozen world is worse than a dead one — clients hang, nothing saves — so this
 * deliberately crashes the process rather than letting it sit there.
 */
void Master::FreezeDetector(uint32 maxStuckMs)
{
    sLog.outString("AntiFreeze thread started (%u seconds max stuck time)", maxStuckMs / 1000);

    uint32 lastLoops  = 0;
    uint32 lastChange = 0;

    while (!World::IsStopped())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        const uint32 curtime = getMSTime();
        const uint32 loops   = World::m_worldLoopCounter.load();

        if (loops != lastLoops)                 // normal progress
        {
            lastLoops  = loops;
            lastChange = curtime;
        }
        else if (getMSTimeDiff(lastChange, curtime) > maxStuckMs)
        {
            sLog.outError("World Thread hangs, kicking out server!");
            *((uint32 volatile*)NULL) = 0;      // bang crash
        }
    }

    sLog.outString("AntiFreeze thread stopped");
}

namespace
{
    /// Print the interactive prompt through the console writer, so it shares the single
    /// serialised stdout with log lines and progress bars and cannot overtake them.
    void CliPrompt(void* /*callbackArg*/ = nullptr, bool /*status*/ = true)
    {
        sLog.ConsoleEmitRaw("mangos>");
    }

#if PLATFORM != PLATFORM_WINDOWS
    /// Non-blocking check for pending console input.
    int kb_hit_return()
    {
        struct timeval tv;
        fd_set fds;
        tv.tv_sec  = 0;
        tv.tv_usec = 0;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        return FD_ISSET(STDIN_FILENO, &fds);
    }
#endif
}

/// Console reader: turns stdin lines into CLI commands queued to the world thread.
void Master::CliLoop(bool beep)
{
    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (beep)
    {
        sLog.ConsoleEmitRaw("\a");              // \a = Alert, via the single-owner stdout
    }

    CliPrompt();

    char buffer[256];

    while (!World::IsStopped())
    {
#if PLATFORM != PLATFORM_WINDOWS
        // Poll rather than block, so shutdown does not have to interrupt a parked read.
        // Caps the console at ~10 commands/second, which is plenty for a human.
        while (!kb_hit_return() && !World::IsStopped())
        {
            usleep(100);
        }

        if (World::IsStopped())
        {
            break;
        }
#endif
        char* command_str = fgets(buffer, sizeof(buffer), stdin);
        if (!command_str)
        {
            if (feof(stdin))
            {
                World::StopNow(SHUTDOWN_EXIT_CODE);
            }
            continue;
        }

        for (int x = 0; command_str[x]; ++x)
        {
            if (command_str[x] == '\r' || command_str[x] == '\n')
            {
                command_str[x] = 0;
                break;
            }
        }

        if (!*command_str)
        {
            CliPrompt();
            continue;
        }

        std::string command;
        if (!consoleToUtf8(command_str, command))   // convert from console encoding to utf8
        {
            CliPrompt();
            continue;
        }

        sWorld.QueueCliCommand(new CliCommandHolder(0, SEC_CONSOLE, NULL, command.c_str(),
                                                    &utf8print, &CliPrompt));
    }
}

void Master::StopCli()
{
    if (!m_cliThread.joinable())
    {
        return;
    }

#ifdef _WIN32
    // The reader is parked in fgets(); feed it a synthetic Return so it wakes, sees that
    // the world has stopped, and leaves. (On POSIX it polls instead, so it exits on its
    // own.)
    INPUT_RECORD record;
    record.EventType                        = KEY_EVENT;
    record.Event.KeyEvent.bKeyDown          = TRUE;
    record.Event.KeyEvent.dwControlKeyState = 0;
    record.Event.KeyEvent.uChar.AsciiChar   = '\r';
    record.Event.KeyEvent.wVirtualKeyCode   = VK_RETURN;
    record.Event.KeyEvent.wRepeatCount      = 1;
    record.Event.KeyEvent.wVirtualScanCode  = 0x1c;

    DWORD written = 0;
    WriteConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &record, 1, &written);
#endif

    m_cliThread.join();
}

// ── Run ──────────────────────────────────────────────────────────────────────────

int Master::Run()
{
    ///- Start the databases
    if (!StartDatabases())
    {
        return 1;
    }

    // Move console output off the world/map-update threads. Started only after the
    // fallible init above, so an early-return error path never leaves a writer thread
    // running into stdio teardown — but before SetInitialWorldSettings(), whose spawn
    // burst is exactly the hot console path this exists to cover.
    sLog.StartConsoleThread();

    // The writer now owns stdout, so route progress-bar redraws through it too: the bars
    // were previously raw printf from the loading thread and could tear against it. Must
    // follow StartConsoleThread, since ConsoleEmitRaw falls back to a synchronous write
    // whenever the writer is not running.
    BarGoLink::SetConsoleSink(&BarConsoleSink);

    ///- Set Realm to Offline, in case a crash happened. Only used once.
    LoginDatabase.DirectPExecute("UPDATE `realmlist` SET `realmflags` = `realmflags` | %u WHERE `id` = '%u'",
                                 REALM_FLAG_OFFLINE, realmID);

    ///- Initialize the World
    sWorld.SetInitialWorldSettings();

#ifndef _WIN32
    detachDaemon();
#endif

    // Set the realm flags from configuration, and mark the realm online.
    const uint8 recommendedornew = sWorld.getConfig(CONFIG_BOOL_REALM_RECOMMENDED_OR_NEW)
                                 ? REALM_FLAG_NEW_PLAYERS : REALM_FLAG_RECOMMENDED;
    const uint8 realmstatus = sWorld.getConfig(CONFIG_BOOL_REALM_RECOMMENDED_OR_NEW_ENABLED)
                            ? recommendedornew : uint8(REALM_FLAG_NONE);

    std::string builds = AcceptableClientBuildsListStr();
    LoginDatabase.escape_string(builds);
    LoginDatabase.DirectPExecute("UPDATE `realmlist` SET `realmflags` = %u, `population` = 0, `realmbuilds` = '%s' WHERE `id` = '%u'",
                                 realmstatus, builds.c_str(), realmID);

    // The server is up: async DB requests are allowed from here (they are forbidden
    // during startup, which is why this is not done any earlier).
    WorldDatabase.ThreadStart();
    CharacterDatabase.AllowAsyncTransactions();
    WorldDatabase.AllowAsyncTransactions();
    LoginDatabase.AllowAsyncTransactions();

    ///- Start the world listener
    const std::string bindIp = sConfig.GetStringDefault("BindIP", "0.0.0.0");
    const uint16 worldPort = uint16(sWorld.getConfig(CONFIG_UINT32_PORT_WORLD));

    if (sWorldSocketMgr.StartNetwork(worldPort, bindIp) == -1)
    {
        sLog.outError("Failed to start network");
        World::StopNow(ERROR_EXIT_CODE);
        StopDatabases();
        return 1;
    }

    ///- Start the remote access listener
    if (sConfig.GetBoolDefault("Ra.Enable", false))
    {
        m_raServer.Start(uint16(sConfig.GetIntDefault("Ra.Port", 3443)),
                         sConfig.GetStringDefault("Ra.IP", "0.0.0.0"));
    }

    ///- Start the SOAP listener
#ifdef ENABLE_SOAP
    std::thread soapThread;
    if (sConfig.GetBoolDefault("SOAP.Enabled", false))
    {
        soapThread = std::thread(SoapThread,
                                 sConfig.GetStringDefault("SOAP.IP", "127.0.0.1"),
                                 uint16(sConfig.GetIntDefault("SOAP.Port", 7878)));
    }
#else
    if (sConfig.GetBoolDefault("SOAP.Enabled", false))
    {
        sLog.outError("SOAP is enabled but wasn't included during compilation, not activating it.");
    }
#endif

    ///- Start the freeze detector
    const uint32 maxStuckMs = 1000 * uint32(sConfig.GetIntDefault("MaxCoreStuckTime", 0));
    if (maxStuckMs)
    {
        m_freezeThread = std::thread([this, maxStuckMs] { FreezeDetector(maxStuckMs); });
    }

    ///- Start the console
#ifdef _WIN32
    const bool consoleEnabled = sConfig.GetBoolDefault("Console.Enable", true) &&
                                (m_ServiceStatus == -1);    // no console when run as a service
#else
    const bool consoleEnabled = sConfig.GetBoolDefault("Console.Enable", true);
#endif
    if (consoleEnabled)
    {
        const bool beep = sConfig.GetBoolDefault("BeepAtStart", true);
        m_cliThread = std::thread([this, beep] { CliLoop(beep); });
    }

    ///- Run the world. Returns once World::StopNow() has been called and the world has
    ///  finished unwinding.
    WorldLoop();

    sLog.outString("[shutdown] world loop returned; joining auxiliary threads");

    StopCli();

    if (m_freezeThread.joinable())
    {
        m_freezeThread.join();
    }

    m_raServer.Stop();

#ifdef ENABLE_SOAP
    if (soapThread.joinable())
    {
        soapThread.join();
    }
#endif

    sLog.outString("Halting process...");

    ///- Set the realm offline again
    LoginDatabase.DirectPExecute("UPDATE `realmlist` SET `realmflags` = `realmflags` | %u WHERE `id` = '%u'",
                                 REALM_FLAG_OFFLINE, realmID);

    ///- Clean the account database before leaving
    ClearOnlineAccounts();

    // Send any still-queued mass mails before the DB connections go down.
    sMassMailMgr.Update(true);

    StopDatabases();

    return World::GetExitCode();
}

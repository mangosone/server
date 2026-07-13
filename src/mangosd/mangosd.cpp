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

/// \addtogroup mangosd MaNGOS Daemon
/// @{
/// \file
///
/// Entry point of the world daemon: parse the command line, load the config, handle the
/// service/daemon plumbing, then hand over to Master, which owns everything with a
/// lifetime and runs the world to completion.

#include "Auth/OpenSSLProvider.h"
#include "Common.h"
#include "Config/Config.h"
#include "ConsoleStyle.h"
#include "Database/DatabaseEnv.h"
#include "GitRevision.h"
#include "Log.h"
#include "Master.h"
#include "ProgressBar.h"
#include "ScriptMgr.h"
#include "StartupUI.h"
#include "SystemConfig.h"
#include "Util.h"
#include "World.h"
#include "AuctionHouseBot.h"
#include "revision_data.h"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include "ServiceWin32.h"
#include "WheatyExceptionReport.h"

char serviceName[]        = "MaNGOS";               // service short name
char serviceLongName[]    = "MaNGOS World Service"; // service long name
char serviceDescription[] = "MaNGOS World Service - no description available";

/// -1 = not a service, 0 = stopped, 2 = paused
int m_ServiceStatus = -1;

#else
#include "PosixDaemon.h"
#endif

DatabaseType WorldDatabase;                                 ///< Accessor to the world database
DatabaseType CharacterDatabase;                             ///< Accessor to the character database
DatabaseType LoginDatabase;                                 ///< Accessor to the realm/login database

uint32 realmID = 0;                                         ///< Id of the realm

namespace
{
    /// Handle termination signals by asking the world to stop; the shutdown itself
    /// unwinds on the main thread, where it is safe to do real work.
    void OnSignal(int s)
    {
        switch (s)
        {
            case SIGINT:
                World::StopNow(RESTART_EXIT_CODE);
                break;
            case SIGTERM:
#ifdef _WIN32
            case SIGBREAK:
#endif
                World::StopNow(SHUTDOWN_EXIT_CODE);
                break;
        }

        signal(s, OnSignal);
    }

    void HookSignals()
    {
        signal(SIGINT,  OnSignal);
        signal(SIGTERM, OnSignal);
#ifdef _WIN32
        signal(SIGBREAK, OnSignal);
#endif
    }

    void UnhookSignals()
    {
        signal(SIGINT,  0);
        signal(SIGTERM, 0);
#ifdef _WIN32
        signal(SIGBREAK, 0);
#endif
    }

    void Usage(const char* prog)
    {
        sLog.outString("Usage: \n %s [<options>]\n"
            "    -v, --version              print version and exit\n\r"
            "    -c <config_file>           use config_file as configuration file\n\r"
            "    -a, --ahbot <config_file>  use config_file as ahbot configuration file\n\r"
            "    --console-demo [<style>]   draw a sample startup on this console and exit;\n\r"
            "                               style is auto (default), fancy or plain\n\r"
#ifdef _WIN32
            "    Running as service functions:\n\r"
            "    -s run                     run as service\n\r"
            "    -s install                 install service\n\r"
            "    -s uninstall               uninstall service\n\r"
#else
            "    Running as daemon functions:\n\r"
            "    -s run                     run as daemon\n\r"
            "    -s stop                    stop daemon\n\r"
#endif
            , prog);
    }

    /**
     * @brief Announce who we are: a boxed panel on an interactive console, the
     *        classic wordmark and version lines everywhere else.
     */
    void ShowBanner(const char* cfg_file)
    {
        if (!ConsoleStyle::Fancy())
        {
            sLog.outString("%s [world-daemon]", GitRevision::GetProjectRevision());
            sLog.outString("%s", GitRevision::GetFullRevision());
            sLog.outString("%s", GitRevision::GetDepElunaFullRevisionStr());
            sLog.outString("%s", GitRevision::GetDepSD3FullRevisionStr());
            print_banner();
            sLog.outString("Using configuration file %s.", cfg_file);
            return;
        }

        std::vector<StartupUI::Row> rows;
        rows.push_back(StartupUI::Row("build", GitRevision::GetFullRevision()));
        rows.push_back(StartupUI::Row("eluna", GitRevision::GetDepElunaFullRevision()));
        rows.push_back(StartupUI::Row("sd3", GitRevision::GetDepSD3FullRevision()));
        rows.push_back(StartupUI::Row("system", GitRevision::GetRunningSystem()));
        rows.push_back(StartupUI::Row("config", cfg_file));

        const std::string title = std::string("MaNGOS One  ") + ConsoleStyle::G().separator +
                                  "  World Daemon  " + ConsoleStyle::G().separator + "  WoW 2.4.3";

        StartupUI::Panel(title, GitRevision::GetProjectRevision(), rows);
        sLog.outString("<Ctrl-C> to stop.");
    }

    /**
     * @brief Parsed command line.
     */
    struct Options
    {
        const char* configFile   = MANGOSD_CONFIG_LOCATION;
        char        serviceMode  = '\0';   ///< 'r' run, 'i' install, 'u' uninstall, 's' stop
        bool        printVersion = false;
        bool        consoleDemo  = false;  ///< --console-demo: draw a sample startup and exit
        const char* demoStyle    = "auto"; ///< style the demo renders with
        bool        failed       = false;
    };

    /**
     * @brief Minimal option parser, replacing ACE_Get_Opt.
     *
     * Recognised: -c <file>, -a/--ahbot <file>, -s <mode>, -v/--version.
     */
    Options ParseCommandLine(int argc, char** argv)
    {
        Options opts;

        for (int i = 1; i < argc; ++i)
        {
            const char* arg = argv[i];

            auto value = [&](const char* name) -> const char*
            {
                if (i + 1 >= argc)
                {
                    sLog.outError("Runtime-Error: %s option requires an input argument", name);
                    opts.failed = true;
                    return nullptr;
                }
                return argv[++i];
            };

            if (!strcmp(arg, "-v") || !strcmp(arg, "--version"))
            {
                opts.printVersion = true;
                return opts;
            }
            else if (!strcmp(arg, "--console-demo"))
            {
                opts.consoleDemo = true;

                // The style is optional: "--console-demo" alone means "auto".
                if (i + 1 < argc && argv[i + 1][0] != '-')
                {
                    opts.demoStyle = argv[++i];
                }

                return opts;
            }
            else if (!strcmp(arg, "-c"))
            {
                if (const char* v = value("-c")) { opts.configFile = v; } else { return opts; }
            }
            else if (!strcmp(arg, "-a") || !strcmp(arg, "--ahbot"))
            {
                if (const char* v = value("--ahbot")) { sAuctionBotConfig.SetConfigFileName(v); } else { return opts; }
            }
            else if (!strcmp(arg, "-s"))
            {
                const char* mode = value("-s");
                if (!mode)
                {
                    return opts;
                }

                if (!strcmp(mode, "run"))
                {
                    opts.serviceMode = 'r';
                }
#ifdef _WIN32
                else if (!strcmp(mode, "install"))
                {
                    opts.serviceMode = 'i';
                }
                else if (!strcmp(mode, "uninstall"))
                {
                    opts.serviceMode = 'u';
                }
#else
                else if (!strcmp(mode, "stop"))
                {
                    opts.serviceMode = 's';
                }
#endif
                else
                {
                    sLog.outError("Runtime-Error: -s unsupported argument %s", mode);
                    opts.failed = true;
                    return opts;
                }
            }
            else
            {
                sLog.outError("Runtime-Error: bad format of commandline arguments");
                opts.failed = true;
                return opts;
            }
        }

        return opts;
    }
}

/// Launch the mangos server
int main(int argc, char** argv)
{
#ifdef _WIN32
    // Install the exception handler for unhandled exceptions in the main thread
    static WheatyExceptionReport exceptionReport;
    SetUnhandledExceptionFilter(WheatyExceptionReport::WheatyUnhandledExceptionFilter);
#endif

    const Options opts = ParseCommandLine(argc, argv);

    if (opts.printVersion)
    {
        printf("%s\n", GitRevision::GetProjectRevision());
        return 0;
    }

    if (opts.failed)
    {
        Usage(argv[0]);
        Log::WaitBeforeContinueIfNeed();
        return 1;
    }

    ///- Draw a sample startup and leave. Deliberately ahead of the config and the
    ///  databases: the point is to look at this console, and neither has to exist
    ///  for that. Runs on the same off-thread writer the real startup uses, so what
    ///  it shows is what a real startup shows.
    if (opts.consoleDemo)
    {
        sLog.StartConsoleThread();
        StartupUI::Demo(opts.demoStyle);
        sLog.StopConsoleThread();
        return 0;
    }

#ifdef _WIN32
    // Service commands must run before the config is read.
    switch (opts.serviceMode)
    {
        case 'i':
            if (WinServiceInstall())
            {
                sLog.outString("Installing service");
            }
            return 1;
        case 'u':
            if (WinServiceUninstall())
            {
                sLog.outString("Uninstalling service");
            }
            return 1;
        case 'r':
            WinServiceRun();
            break;
    }
#endif

    const char* cfg_file = opts.configFile;
    if (!sConfig.SetSource(cfg_file))
    {
        // Fall back to the current folder if the SYSCONFDIR path does not resolve.
        if (!sConfig.SetSource(MANGOSD_CONFIG_NAME))
        {
            sLog.outError("Could not find configuration file %s.", cfg_file);
            Log::WaitBeforeContinueIfNeed();
            return 1;
        }
        cfg_file = MANGOSD_CONFIG_NAME;
    }

#ifndef _WIN32
    switch (opts.serviceMode)
    {
        case 'r': startDaemon(); break;
        case 's': stopDaemon();  break;
    }
#endif

    ///- Probe the console and install the startup UI hooks. Must precede the first
    ///  line drawn, and follows the config, which decides the style.
    StartupUI::Initialize(sConfig.GetStringDefault("Console.Style", "auto"));

    ShowBanner(cfg_file);

    DETAIL_LOG("Using SSL version: %s (Library: %s)", OPENSSL_VERSION_TEXT, OpenSSL_version(OPENSSL_VERSION));

    // RAII provider management - automatically handles cleanup
    OpenSSLProviderManager providerManager;
    if (!providerManager.IsInitialized())
    {
        Log::WaitBeforeContinueIfNeed();
        return 0;
    }

    ///- Set progress bars show mode
    BarGoLink::SetOutputState(sConfig.GetBoolDefault("ShowProgressBars", true));

    /// worldd PID file creation
    const std::string pidfile = sConfig.GetStringDefault("PidFile", "");
    if (!pidfile.empty())
    {
        const uint32 pid = CreatePIDFile(pidfile);
        if (!pid)
        {
            sLog.outError("Can not create PID file %s.\n", pidfile.c_str());
            Log::WaitBeforeContinueIfNeed();
            return 1;
        }

        sLog.outString("Daemon PID: %u\n", pid);
    }

    ///- Catch termination signals
    HookSignals();

    ///- Run the server. Master owns the databases, the listeners and every thread, and
    ///  returns only once the world has stopped and all of them have been joined.
    Master master;
    const int code = master.Run();

    UnhookSignals();

    // Unload the script library explicitly: ~ScriptMgr() runs too late, as it has static
    // storage duration and the shared object would already have been unloaded.
    sLog.outString("[shutdown] unloading script library...");
    sScriptMgr.UnloadScriptLibrary();
    sLog.outString("[shutdown] script library unloaded");

#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    // Stop and join the off-thread console writer only once every console-producing
    // thread is gone (Master::Run joined them all before returning), so no producer can
    // race the writer's destruction. Lines logged after this take the synchronous path.
    sLog.StopConsoleThread();

    sLog.outString("Bye!");

    // Final flush of the buffered file log, so "Bye!" and any late shutdown lines reach
    // disk before exit.
    sLog.Flush();

    return code;
}

/// @}

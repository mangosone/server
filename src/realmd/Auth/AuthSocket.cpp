/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2025 MaNGOS <https://www.getmangos.eu>
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

/** \file
    \ingroup realmd
*/

#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "Config/Config.h"
#include "Log.h"
#include "Realm/RealmList.h"
#include "AuthSocket.h"
#include "AuthCodes.h"
#include "Patch/PatchHandler.h"
#include "packets/AuthPackets.h"

//#include "Util.h" -- for commented utf8ToUpperOnlyLatin

#include <ace/OS_NS_unistd.h>
#include <ace/OS_NS_fcntl.h>
#include <ace/OS_NS_sys_stat.h>

extern DatabaseType LoginDatabase;

enum AccountFlags
{
    ACCOUNT_FLAG_GM         = 0x00000001,
    ACCOUNT_FLAG_TRIAL      = 0x00000008,
    ACCOUNT_FLAG_PROPASS    = 0x00800000,
};

typedef struct AuthHandler
{
    eAuthCmd cmd;
    uint32 status;
    bool (AuthSocket::*handler)(void);
} AuthHandler;


/// Constructor - set the N and g values for SRP6
AuthSocket::AuthSocket() : _status(STATUS_CHALLENGE), _accountSecurityLevel(SEC_PLAYER), _build(0), patch_(ACE_INVALID_HANDLE)
{
    N.SetHexStr("894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7");
    g.SetDword(7);
}

/// Close patch file descriptor before leaving
AuthSocket::~AuthSocket()
{
    if (patch_ != ACE_INVALID_HANDLE)
    {
        ACE_OS::close(patch_);
    }
}

/// Accept the connection and set the s random value for SRP6
void AuthSocket::OnAccept()
{
    BASIC_LOG("Accepting connection from '%s'", get_remote_address().c_str());
}

/// Read the packet from the client
void AuthSocket::OnRead()
{
    const static AuthHandler table[] =
    {
        { CMD_AUTH_LOGON_CHALLENGE,     STATUS_CHALLENGE,   &AuthSocket::_HandleLogonChallenge    },
        { CMD_AUTH_LOGON_PROOF,         STATUS_LOGON_PROOF, &AuthSocket::_HandleLogonProof        },
        { CMD_AUTH_RECONNECT_CHALLENGE, STATUS_CHALLENGE,   &AuthSocket::_HandleReconnectChallenge},
        { CMD_AUTH_RECONNECT_PROOF,     STATUS_RECON_PROOF, &AuthSocket::_HandleReconnectProof    },
        { CMD_REALM_LIST,               STATUS_AUTHED,      &AuthSocket::_HandleRealmList         },
        { CMD_XFER_ACCEPT,              STATUS_PATCH,       &AuthSocket::_HandleXferAccept        },
        { CMD_XFER_RESUME,              STATUS_PATCH,       &AuthSocket::_HandleXferResume        },
        { CMD_XFER_CANCEL,              STATUS_PATCH,       &AuthSocket::_HandleXferCancel        }
    };

    const int AUTH_TOTAL_COMMANDS = sizeof(table)/sizeof(AuthHandler);
    uint8 _cmd;

    while (1)
    {
        if (!recv_soft((char*)&_cmd, 1))
        {
            return;
        }

        size_t i;
        ///- Circle through known commands and call the correct command handler
        for (i = 0; i < AUTH_TOTAL_COMMANDS; ++i)
        {
            if ((uint8)table[i].cmd != _cmd)
            {
                continue;
            }

            if (table[i].status != _status)
            {
                DEBUG_LOG("[Auth] Received unauthorized command %u, length %u", (uint32)_cmd, (uint32)recv_len());
                return;
            }

            DEBUG_LOG("[Auth] Received command %u, length %u", (uint32)_cmd, (uint32)recv_len());
            if (!(*this.*table[i].handler)())
            {
                DEBUG_LOG("[Auth] Command handler failed for cmd %u, length %u", (uint32)_cmd, (uint32)recv_len());
                return;
            }
            break;
        }

        ///- Report unknown commands in the debug log
        if (i == AUTH_TOTAL_COMMANDS)
        {
            DEBUG_LOG("[Auth] Got unknown command %u", (uint32)_cmd);
            return;
        }
    }
}

/// Make the SRP6 calculation from hash in dB
void AuthSocket::_SetVSFields(const std::string& rI)
{
    s.SetRand(s_BYTE_SIZE * 8);

    BigNumber I;
    I.SetHexStr(rI.c_str());

    // In case of leading zeros in the rI hash, restore them
    uint8 mDigest[SHA_DIGEST_LENGTH];
    memset(mDigest, 0, SHA_DIGEST_LENGTH);
    if (I.GetNumBytes() <= SHA_DIGEST_LENGTH)
    {
        memcpy(mDigest, I.AsByteArray(), I.GetNumBytes());
    }

    std::reverse(mDigest, mDigest + SHA_DIGEST_LENGTH);

    Sha1Hash sha;
    sha.UpdateData(s.AsByteArray(), s.GetNumBytes());
    sha.UpdateData(mDigest, SHA_DIGEST_LENGTH);
    sha.Finalize();
    BigNumber x;
    x.SetBinary(sha.GetDigest(), sha.GetLength());
    v = g.ModExp(x, N);
    // No SQL injection (username escaped)
    const char* v_hex, *s_hex;
    v_hex = v.AsHexStr();
    s_hex = s.AsHexStr();
    LoginDatabase.PExecute("UPDATE `account` SET `v` = '%s', `s` = '%s' WHERE `username` = '%s'", v_hex, s_hex, _safelogin.c_str());
    OPENSSL_free((void*)v_hex);
    OPENSSL_free((void*)s_hex);
}

void AuthSocket::SendProof(Sha1Hash sha)
{
    switch (_build)
    {
        case 5875:                                          // 1.12.1
        case 6005:                                          // 1.12.2
        case 6141:                                          // 1.12.3
        {
            sAuthLogonProof_S_BUILD_6005 proof;
            memcpy(proof.M2, sha.GetDigest(), 20);
            proof.cmd = CMD_AUTH_LOGON_PROOF;
            proof.error = 0;
            proof.unk2 = 0x00;

            send((char*)&proof, sizeof(proof));
            break;
        }
        case 8606:                                          // 2.4.3
        case 12340:                                         // 3.3.5a
        case 15595:                                         // 4.3.4
        case 18273:                                         // 5.4.8
        case 18414:                                         // 5.4.8
        case 21742:                                         // 6.2.4
        case 25549:                                         // 7.3.2
        case 32790:                                         // 8.2.5
        case 40000:                                         // 9.0.0
        default:                                            // or later
        {
            sAuthLogonProof_S proof;
            memcpy(proof.M2, sha.GetDigest(), 20);
            proof.cmd = CMD_AUTH_LOGON_PROOF;
            proof.error = 0;
            proof.accountFlags = ACCOUNT_FLAG_PROPASS;
            proof.surveyId = 0x00000000;
            proof.unkFlags = 0x0000;

            send((char*)&proof, sizeof(proof));
            break;
        }
    }
}

/// Logon Challenge command handler
bool AuthSocket::_HandleLogonChallenge()
{
    DEBUG_LOG("Entering _HandleLogonChallenge");
    if (recv_len() < sizeof(sAuthLogonChallenge_C))
    {
        return false;
    }

    ///- Read the first 4 bytes (header) to get the length of the remaining of the packet
    std::vector<uint8> buf;
    buf.resize(4);

    recv((char*)&buf[0], 4);

#ifdef MANGOS_BIGENDIAN
    EndianConvert(*((uint16*)(buf[0])));
#endif

    uint16 remaining = ((sAuthLogonChallenge_C*)&buf[0])->size;
    DEBUG_LOG("[AuthChallenge] got header, body is %#04x bytes", remaining);

    if ((remaining < sizeof(sAuthLogonChallenge_C) - buf.size()) || (recv_len() < remaining))
    {
        return false;
    }

    ///- Session is closed unless overriden
    _status = STATUS_CLOSED;

    // No big fear of memory outage (size is int16, i.e. < 65536)
    buf.resize(remaining + buf.size() + 1);
    buf[buf.size() - 1] = 0;
    sAuthLogonChallenge_C* ch = (sAuthLogonChallenge_C*)&buf[0];

    ///- Read the remaining of the packet
    recv((char*)&buf[4], remaining);
    DEBUG_LOG("[AuthChallenge] got full packet, %#04x bytes", ch->size);
    DEBUG_LOG("[AuthChallenge] name(%d): '%s'", ch->I_len, ch->I);

    // BigEndian code, nop in little endian case
    // size already converted
    EndianConvert(*((uint32*)(&ch->gamename[0])));
    EndianConvert(ch->build);
    EndianConvert(*((uint32*)(&ch->platform[0])));
    EndianConvert(*((uint32*)(&ch->os[0])));
    EndianConvert(*((uint32*)(&ch->country[0])));
    EndianConvert(ch->timezone_bias);
    EndianConvert(ch->ip);

    ByteBuffer pkt;

    _login = (const char*)ch->I;
    _build = ch->build;
    _os = (const char*)ch->os;

    if (_os.size() > 4)
    {
        return false;
    }

    // Restore string order as its byte order is reversed
    std::reverse(_os.begin(), _os.end());

    ///- Normalize account name
    // utf8ToUpperOnlyLatin(_login); -- client already send account in expected form

    // Escape the user login to avoid further SQL injection
    // Memory will be freed on AuthSocket object destruction
    _safelogin = _login;
    LoginDatabase.escape_string(_safelogin);

    pkt << (uint8) CMD_AUTH_LOGON_CHALLENGE;
    pkt << (uint8) 0x00;

    ///- Verify that this IP is not in the ip_banned table
    // No SQL injection possible (paste the IP address as passed by the socket)
    std::string address = get_remote_address();
    LoginDatabase.escape_string(address);
    QueryResult* result = LoginDatabase.PQuery("SELECT `unbandate` FROM `ip_banned` WHERE "
                          //    permanent                    still banned
                          "(`unbandate` = `bandate` OR `unbandate` > UNIX_TIMESTAMP()) AND `ip` = '%s'", address.c_str());
    if (result)
    {
        pkt << (uint8)WOW_FAIL_BANNED;
        BASIC_LOG("[AuthChallenge] Banned ip %s tries to login!", get_remote_address().c_str());
        delete result;
    }
    else
    {
        ///- Get the account details from the account table
        // No SQL injection (escaped user name)

        result = LoginDatabase.PQuery("SELECT `sha_pass_hash`,`id`,`locked`,`last_ip`,`gmlevel`,`v`,`s` FROM `account` WHERE `username` = '%s'", _safelogin.c_str());
        if (result)
        {
            ///- If the IP is 'locked', check that the player comes indeed from the correct IP address
            bool locked = false;
            if ((*result)[2].GetUInt8() == 1)               // if ip is locked
            {
                DEBUG_LOG("[AuthChallenge] Account '%s' is locked to IP - '%s'", _login.c_str(), (*result)[3].GetString());
                DEBUG_LOG("[AuthChallenge] Player address is '%s'", get_remote_address().c_str());
                if (strcmp((*result)[3].GetString(), get_remote_address().c_str()))
                {
                    DEBUG_LOG("[AuthChallenge] Account IP differs");
#if defined(CLASSIC)
                    pkt << (uint8)WOW_FAIL_DB_BUSY;
#else
                    pkt << (uint8)WOW_FAIL_LOCKED_ENFORCED;
#endif
                    locked = true;
                }
                else
                {
                    DEBUG_LOG("[AuthChallenge] Account IP matches");
                }
            }
            else
            {
                DEBUG_LOG("[AuthChallenge] Account '%s' is not locked to ip", _login.c_str());
            }

            if (!locked)
            {
                ///- If the account is banned, reject the logon attempt
                QueryResult* banresult = LoginDatabase.PQuery("SELECT `bandate`,`unbandate` FROM `account_banned` WHERE "
                                         "`id` = %u AND `active` = 1 AND (`unbandate` > UNIX_TIMESTAMP() OR `unbandate` = `bandate`)", (*result)[1].GetUInt32());
                if (banresult)
                {
                    if ((*banresult)[0].GetUInt64() == (*banresult)[1].GetUInt64())
                    {
                        pkt << (uint8) WOW_FAIL_BANNED;
                        BASIC_LOG("[AuthChallenge] Banned account %s tries to login!", _login.c_str());
                    }
                    else
                    {
                        pkt << (uint8) WOW_FAIL_SUSPENDED;
                        BASIC_LOG("[AuthChallenge] Temporarily banned account %s tries to login!", _login.c_str());
                    }

                    delete banresult;
                }
                else
                {
                    ///- Get the password from the account table, upper it, and make the SRP6 calculation
                    std::string rI = (*result)[0].GetCppString();

                    ///- Don't calculate (v, s) if there are already some in the database
                    std::string databaseV = (*result)[5].GetCppString();
                    std::string databaseS = (*result)[6].GetCppString();

                    DEBUG_LOG("database authentication values: v='%s' s='%s'", databaseV.c_str(), databaseS.c_str());

                    // multiply with 2, bytes are stored as hexstring
                    if (databaseV.size() != s_BYTE_SIZE * 2 || databaseS.size() != s_BYTE_SIZE * 2)
                    {
                        _SetVSFields(rI);
                    }
                    else
                    {
                        s.SetHexStr(databaseS.c_str());
                        v.SetHexStr(databaseV.c_str());
                    }

                    b.SetRand(19 * 8);
                    BigNumber gmod = g.ModExp(b, N);
                    B = ((v * 3) + gmod) % N;

                    MANGOS_ASSERT(gmod.GetNumBytes() <= 32);

                    BigNumber unk3;
                    unk3.SetRand(16 * 8);

                    ///- Fill the response packet with the result
                    pkt << uint8(WOW_SUCCESS);

                    // B may be calculated < 32B so we force minimal length to 32B
                    pkt.append(B.AsByteArray(32), 32);      // 32 bytes
                    pkt << uint8(1);
                    pkt.append(g.AsByteArray(), 1);
                    pkt << uint8(32);
                    pkt.append(N.AsByteArray(32), 32);
                    pkt.append(s.AsByteArray(), s.GetNumBytes());// 32 bytes
                    pkt.append(unk3.AsByteArray(16), 16);
                    uint8 securityFlags = 0;
                    pkt << uint8(securityFlags);            // security flags (0x0...0x04)

                    if (securityFlags & 0x01)               // PIN input
                    {
                        pkt << uint32(0);
                        pkt << uint64(0) << uint64(0);      // 16 bytes hash?
                    }

                    if (securityFlags & 0x02)               // Matrix input
                    {
                        pkt << uint8(0);
                        pkt << uint8(0);
                        pkt << uint8(0);
                        pkt << uint8(0);
                        pkt << uint64(0);
                    }

                    if (securityFlags & 0x04)               // Security token input
                    {
                        pkt << uint8(1);
                    }

                    uint8 secLevel = (*result)[4].GetUInt8();
                    _accountSecurityLevel = secLevel <= SEC_ADMINISTRATOR ? AccountTypes(secLevel) : SEC_ADMINISTRATOR;

                    _localizationName.resize(4);
                    for (int i = 0; i < 4; ++i)
                    {
                        _localizationName[i] = ch->country[4 - i - 1];
                    }

                    BASIC_LOG("[AuthChallenge] account %s is using '%c%c%c%c' locale (%u)", _login.c_str(), ch->country[3], ch->country[2], ch->country[1], ch->country[0], GetLocaleByName(_localizationName));

                    _status = STATUS_LOGON_PROOF;
                }
            }
            delete result;
        }
        else                                                // no account
        {
            pkt << (uint8) WOW_FAIL_UNKNOWN_ACCOUNT;
        }
    }
    send((char const*)pkt.contents(), pkt.size());
    return true;
}

/// Logon Proof command handler
bool AuthSocket::_HandleLogonProof()
{
    DEBUG_LOG("Entering _HandleLogonProof");
    ///- Read the packet
    sAuthLogonProof_C lp;
    if (!recv((char*)&lp, sizeof(sAuthLogonProof_C)))
    {
        return false;
    }

    _status = STATUS_CLOSED;

    ///- Check if the client has one of the expected version numbers
    bool valid_version = FindBuildInfo(_build) != NULL;

    /// <ul><li> If the client has no valid version
    if (!valid_version)
    {
        if (this->patch_ != ACE_INVALID_HANDLE)
        {
            return false;
        }

        ///- Check if we have the apropriate patch on the disk
        // file looks like: 65535enGB.mpq
        char tmp[64];

        snprintf(tmp, 24, "./patches/%d%s.mpq", _build, _localizationName.c_str());

        char filename[PATH_MAX];
        if (ACE_OS::realpath(tmp, filename) != NULL)
        {
            patch_ = ACE_OS::open(filename, GENERIC_READ | FILE_FLAG_SEQUENTIAL_SCAN);
        }

        if (patch_ == ACE_INVALID_HANDLE)
        {
            // no patch found
            ByteBuffer pkt;
            pkt << (uint8) CMD_AUTH_LOGON_CHALLENGE;
            pkt << (uint8) 0x00;
            pkt << (uint8) WOW_FAIL_VERSION_INVALID;
            DEBUG_LOG("[AuthChallenge] %u is not a valid client version!", _build);
            DEBUG_LOG("[AuthChallenge] Patch %s not found", tmp);
            send((char const*)pkt.contents(), pkt.size());
            return true;
        }

        XFER_INIT xferh;

        ACE_OFF_T file_size = ACE_OS::filesize(this->patch_);

        if (file_size == -1)
        {
            close_connection();
            return false;
        }

        if (!PatchCache::instance()->GetHash(tmp, (uint8*)&xferh.md5))
        {
            // calculate patch md5, happens if patch was added while realmd was running
            PatchCache::instance()->LoadPatchMD5(tmp);
            PatchCache::instance()->GetHash(tmp, (uint8*)&xferh.md5);
        }

        uint8 data[2] = { CMD_AUTH_LOGON_PROOF, WOW_FAIL_VERSION_UPDATE};
        send((const char*)data, sizeof(data));

        memcpy(&xferh, "0\x05Patch", 7);
        xferh.cmd = CMD_XFER_INITIATE;
        xferh.file_size = file_size;

        send((const char*)&xferh, sizeof(xferh));

        InitPatch();

        return true;
    }
    /// </ul>

    ///- Continue the SRP6 calculation based on data received from the client
    BigNumber A;

    A.SetBinary(lp.A, 32);

    // SRP safeguard: abort if A==0
    if ((A % N).isZero())
    {
        return false;
    }

    Sha1Hash sha;
    sha.UpdateBigNumbers(&A, &B, NULL);
    sha.Finalize();
    BigNumber u;
    u.SetBinary(sha.GetDigest(), 20);
    BigNumber S = (A * (v.ModExp(u, N))).ModExp(b, N);

    uint8 t[32];
    uint8 t1[16];
    uint8 vK[40];
    memcpy(t, S.AsByteArray(32), 32);
    for (int i = 0; i < 16; ++i)
    {
        t1[i] = t[i * 2];
    }
    sha.Initialize();
    sha.UpdateData(t1, 16);
    sha.Finalize();
    for (int i = 0; i < 20; ++i)
    {
        vK[i * 2] = sha.GetDigest()[i];
    }
    for (int i = 0; i < 16; ++i)
    {
        t1[i] = t[i * 2 + 1];
    }
    sha.Initialize();
    sha.UpdateData(t1, 16);
    sha.Finalize();
    for (int i = 0; i < 20; ++i)
    {
        vK[i * 2 + 1] = sha.GetDigest()[i];
    }
    K.SetBinary(vK, 40);

    uint8 hash[20];

    sha.Initialize();
    sha.UpdateBigNumbers(&N, NULL);
    sha.Finalize();
    memcpy(hash, sha.GetDigest(), 20);
    sha.Initialize();
    sha.UpdateBigNumbers(&g, NULL);
    sha.Finalize();
    for (int i = 0; i < 20; ++i)
    {
        hash[i] ^= sha.GetDigest()[i];
    }
    BigNumber t3;
    t3.SetBinary(hash, 20);

    sha.Initialize();
    sha.UpdateData(_login);
    sha.Finalize();
    uint8 t4[SHA_DIGEST_LENGTH];
    memcpy(t4, sha.GetDigest(), SHA_DIGEST_LENGTH);

    sha.Initialize();
    sha.UpdateBigNumbers(&t3, NULL);
    sha.UpdateData(t4, SHA_DIGEST_LENGTH);
    sha.UpdateBigNumbers(&s, &A, &B, &K, NULL);
    sha.Finalize();
    BigNumber M;
    M.SetBinary(sha.GetDigest(), 20);

    ///- Check if SRP6 results match (password is correct), else send an error
    if (!memcmp(M.AsByteArray(), lp.M1, 20))
    {
        BASIC_LOG("User '%s' successfully authenticated", _login.c_str());

        ///- Update the sessionkey, last_ip, last login time and reset number of failed logins in the account table for this account
        // No SQL injection (escaped user name) and IP address as received by socket
        const char* K_hex = K.AsHexStr();

        // Use synchronous write to help ensure mangosd gets the correct key
        LoginDatabase.Execute("START TRANSACTION");
        char updateQuery[512];
        snprintf(updateQuery, sizeof(updateQuery),
            "UPDATE `account` SET `sessionkey` = '%s', `last_ip` = '%s', `last_login` = NOW(), `locale` = '%u', `os` = '%s', `failed_logins` = 0 WHERE `username` = '%s'",
            K_hex, get_remote_address().c_str(), GetLocaleByName(_localizationName), _os.c_str(), _safelogin.c_str());
        LoginDatabase.Execute(updateQuery);
        LoginDatabase.Execute("COMMIT");
        LoginDatabase.Execute("FLUSH TABLES");

        // Verify the new key is available for reads before mangosd tries, gets the old key, and fails
        bool keyVerified = false;
        for (int attempts = 0; attempts < 1000 && !keyVerified; attempts++)
        {
            QueryResult* verify = LoginDatabase.PQuery("SELECT `sessionkey` FROM `account` WHERE `username` = '%s'", _safelogin.c_str());
            if (verify)
            {
                Field* vf = verify->Fetch();
                if (strcmp(vf->GetString(), K_hex) == 0)
                    keyVerified = true;
                delete verify;
            }
            if (!keyVerified)
                ACE_OS::sleep(ACE_Time_Value(0, 10000));
        }
        // Write of new key should be verified, so allow the client to proceed to mangosd
        OPENSSL_free((void*)K_hex);

        ///- Finish SRP6 and send the final result to the client
        sha.Initialize();
        sha.UpdateBigNumbers(&A, &M, &K, NULL);
        sha.Finalize();

        SendProof(sha);

        ///- Set _status to authenticated
        _status = STATUS_AUTHED;
    }
    else
    {
        if (_build > 6005)                                  // > 1.12.2
        {
            char data[4] = { CMD_AUTH_LOGON_PROOF, WOW_FAIL_UNKNOWN_ACCOUNT, 3, 0};
            send(data, sizeof(data));
        }
        else
        {
            // 1.x not react incorrectly at 4-byte message use 3 as real error
            char data[2] = { CMD_AUTH_LOGON_PROOF, WOW_FAIL_UNKNOWN_ACCOUNT};
            send(data, sizeof(data));
        }
        BASIC_LOG("[AuthChallenge] account %s tried to login with wrong password!", _login.c_str());

        uint32 MaxWrongPassCount = sConfig.GetIntDefault("WrongPass.MaxCount", 0);
        if (MaxWrongPassCount > 0)
        {
            // Increment number of failed logins by one and if it reaches the limit temporarily ban that account or IP
            LoginDatabase.PExecute("UPDATE `account` SET `failed_logins` = `failed_logins` + 1 WHERE `username` = '%s'", _safelogin.c_str());

            if (QueryResult* loginfail = LoginDatabase.PQuery("SELECT `id`, `failed_logins` FROM `account` WHERE `username` = '%s'", _safelogin.c_str()))
            {
                Field* fields = loginfail->Fetch();
                uint32 failed_logins = fields[1].GetUInt32();

                if (failed_logins >= MaxWrongPassCount)
                {
                    uint32 WrongPassBanTime = sConfig.GetIntDefault("WrongPass.BanTime", 600);
                    bool WrongPassBanType = sConfig.GetBoolDefault("WrongPass.BanType", false);

                    if (WrongPassBanType)
                    {
                        uint32 acc_id = fields[0].GetUInt32();
                        LoginDatabase.PExecute("INSERT INTO `account_banned` VALUES ('%u',UNIX_TIMESTAMP(),UNIX_TIMESTAMP()+'%u','MaNGOS realmd','Failed login autoban',1)",
                                               acc_id, WrongPassBanTime);
                        BASIC_LOG("[AuthChallenge] account %s got banned for '%u' seconds because it failed to authenticate '%u' times",
                                  _login.c_str(), WrongPassBanTime, failed_logins);
                    }
                    else
                    {
                        std::string current_ip = get_remote_address();
                        LoginDatabase.escape_string(current_ip);
                        LoginDatabase.PExecute("INSERT INTO `ip_banned` VALUES ('%s',UNIX_TIMESTAMP(),UNIX_TIMESTAMP()+'%u','MaNGOS realmd','Failed login autoban')",
                                               current_ip.c_str(), WrongPassBanTime);
                        BASIC_LOG("[AuthChallenge] IP %s got banned for '%u' seconds because account %s failed to authenticate '%u' times",
                                  current_ip.c_str(), WrongPassBanTime, _login.c_str(), failed_logins);
                    }
                }
                delete loginfail;
            }
        }
    }
    return true;
}

/// Reconnect Challenge command handler
bool AuthSocket::_HandleReconnectChallenge()
{
    DEBUG_LOG("Entering _HandleReconnectChallenge");
    if (recv_len() < sizeof(sAuthLogonChallenge_C))
    {
        return false;
    }

    ///- Read the first 4 bytes (header) to get the length of the remaining of the packet
    std::vector<uint8> buf;
    buf.resize(4);

    recv((char*)&buf[0], 4);

    EndianConvert(*((uint16*)(buf[0])));
    uint16 remaining = ((sAuthLogonChallenge_C*)&buf[0])->size;
    DEBUG_LOG("[ReconnectChallenge] got header, body is %#04x bytes", remaining);

    if ((remaining < sizeof(sAuthLogonChallenge_C) - buf.size()) || (recv_len() < remaining))
    {
        return false;
    }

    _status = STATUS_CLOSED;

    // No big fear of memory outage (size is int16, i.e. < 65536)
    buf.resize(remaining + buf.size() + 1);
    buf[buf.size() - 1] = 0;
    sAuthLogonChallenge_C* ch = (sAuthLogonChallenge_C*)&buf[0];

    ///- Read the remaining of the packet
    recv((char*)&buf[4], remaining);
    DEBUG_LOG("[ReconnectChallenge] got full packet, %#04x bytes", ch->size);
    DEBUG_LOG("[ReconnectChallenge] name(%d): '%s'", ch->I_len, ch->I);

    _login = (const char*)ch->I;

    _safelogin = _login;
    LoginDatabase.escape_string(_safelogin);

    EndianConvert(ch->build);
    _build = ch->build;
    _os = (const char*)ch->os;

    if (_os.size() > 4)
    {
        return false;
    }

    // Restore string order as its byte order is reversed
    std::reverse(_os.begin(), _os.end());

    QueryResult* result = LoginDatabase.PQuery("SELECT `sessionkey` FROM `account` WHERE `username` = '%s'", _safelogin.c_str());

    // Stop if the account is not found
    if (!result)
    {
        sLog.outError("[ERROR] user %s tried to login and we can not find his session key in the database.", _login.c_str());
        close_connection();
        return false;
    }

    Field* fields = result->Fetch();
    K.SetHexStr(fields[0].GetString());
    delete result;

    _status = STATUS_RECON_PROOF;

    ///- Sending response
    ByteBuffer pkt;
    pkt << (uint8)  CMD_AUTH_RECONNECT_CHALLENGE;
    pkt << (uint8)  0x00;
    _reconnectProof.SetRand(16 * 8);
    pkt.append(_reconnectProof.AsByteArray(16), 16);        // 16 bytes random
    pkt << (uint64) 0x00 << (uint64) 0x00;                  // 16 bytes zeros
    send((char const*)pkt.contents(), pkt.size());
    return true;
}

/// Reconnect Proof command handler
bool AuthSocket::_HandleReconnectProof()
{
    DEBUG_LOG("Entering _HandleReconnectProof");
    ///- Read the packet
    sAuthReconnectProof_C lp;
    if (!recv((char*)&lp, sizeof(sAuthReconnectProof_C)))
    {
        return false;
    }

    _status = STATUS_CLOSED;

    if (_login.empty() || !_reconnectProof.GetNumBytes() || !K.GetNumBytes())
    {
        return false;
    }

    BigNumber t1;
    t1.SetBinary(lp.R1, 16);

    Sha1Hash sha;
    sha.Initialize();
    sha.UpdateData(_login);
    sha.UpdateBigNumbers(&t1, &_reconnectProof, &K, NULL);
    sha.Finalize();

    if (!memcmp(sha.GetDigest(), lp.R2, SHA_DIGEST_LENGTH))
    {
        ///- Sending response
        ByteBuffer pkt;
        pkt << (uint8)  CMD_AUTH_RECONNECT_PROOF;
        pkt << (uint8)  0x00;
        //If we keep from sending this we don't receive Session Expired on the client when
        //changing realms after being logged on to the world
        if (_build > 6141) // Last vanilla, 1.12.3
        {
            pkt << (uint16) 0x00;                               // 2 bytes zeros
        }
        send((char const*)pkt.contents(), pkt.size());

        ///- Set _status to authenticated!
        _status = STATUS_AUTHED;

        return true;
    }
    else
    {
        sLog.outError("[ERROR] user %s tried to login, but session invalid.", _login.c_str());
        close_connection();
        return false;
    }
}

ACE_INET_Addr const& AuthSocket::GetAddressForClient(Realm const& realm, ACE_INET_Addr const& clientAddr)
{
    // Attempt to send best address for client
    if (clientAddr.is_loopback())
    {
        // Try guessing if realm is also connected locally
        if (realm.LocalAddress.is_loopback() || realm.ExternalAddress.is_loopback())
        {
            return clientAddr;
        }

        // Assume that user connecting from the machine that authserver is located on
        // has all realms available in his local network
        return realm.LocalAddress;
    }

    // Check if connecting client is in the same network
    if (IsIPAddrInNetwork(realm.LocalAddress, clientAddr, realm.LocalSubnetMask))
    {
        return realm.LocalAddress;
    }

    // Return external IP
        return realm.ExternalAddress;
}

/// %Realm List command handler
bool AuthSocket::_HandleRealmList()
{
    DEBUG_LOG("Entering _HandleRealmList");
    if (recv_len() < 5)
    {
        return false;
    }
    recv_skip(5);

    ///- Get the user id (else close the connection)
    // No SQL injection (escaped user name)

    QueryResult* result = LoginDatabase.PQuery("SELECT `id`,`sha_pass_hash` FROM `account` WHERE `username` = '%s'", _safelogin.c_str());
    if (!result)
    {
        sLog.outError("[ERROR] user %s tried to login and we can not find him in the database.", _login.c_str());
        close_connection();
        return false;
    }

    uint32 id = (*result)[0].GetUInt32();
    std::string rI = (*result)[1].GetCppString();
    delete result;

    ///- Update realm list if need
    sRealmList.UpdateIfNeed();

    ///- Circle through realms in the RealmList and construct the return packet (including # of user characters in each realm)
    ByteBuffer pkt;
    LoadRealmlist(pkt, id);

    ByteBuffer hdr;
    hdr << (uint8) CMD_REALM_LIST;
    hdr << (uint16)pkt.size();
    hdr.append(pkt);

    send((char const*)hdr.contents(), hdr.size());
    return true;
}

void AuthSocket::LoadRealmlist(ByteBuffer& pkt, uint32 acctid)
{
    RealmList::RealmListIterators iters;
    iters = sRealmList.GetIteratorsForBuild(_build);
    uint32 numRealms = sRealmList.NumRealmsForBuild(_build);

    ACE_INET_Addr clientAddr;
    peer().get_remote_addr(clientAddr);

    switch (_build)
    {
        case 5875:                                          // 1.12.1
        case 6005:                                          // 1.12.2
        case 6141:                                          // 1.12.3
        {
            pkt << uint32(0);                               // unused value
            pkt << uint8(numRealms);

            for (RealmList::RealmStlList::const_iterator itr = iters.first; itr != iters.second; ++itr)
            {
                clientAddr.set_port_number((*itr)->ExternalAddress.get_port_number());
                uint8 AmountOfCharacters;

                // No SQL injection. id of realm is controlled by the database.
                QueryResult* result = LoginDatabase.PQuery("SELECT `numchars` FROM `realmcharacters` WHERE `realmid` = '%d' AND `acctid`='%u'", (*itr)->m_ID, acctid);
                if (result)
                {
                    Field* fields = result->Fetch();
                    AmountOfCharacters = fields[0].GetUInt8();
                    delete result;
                }
                else
                {
                    AmountOfCharacters = 0;
                }

                bool ok_build = std::find((*itr)->realmbuilds.begin(), (*itr)->realmbuilds.end(), _build) != (*itr)->realmbuilds.end();

                RealmBuildInfo const* buildInfo = ok_build ? FindBuildInfo(_build) : NULL;
                if (!buildInfo)
                {
                    buildInfo = &(*itr)->realmBuildInfo;
                }

                RealmFlags realmflags = (*itr)->realmflags;

                // 1.x clients not support explicitly REALM_FLAG_SPECIFYBUILD, so manually form similar name as show in more recent clients
                std::string name = (*itr)->name;
                if (realmflags & REALM_FLAG_SPECIFYBUILD)
                {
                    char buf[20];
                    snprintf(buf, 20, " (%u,%u,%u)", buildInfo->major_version, buildInfo->minor_version, buildInfo->bugfix_version);
                    name += buf;
                }

                // Show offline state for unsupported client builds and locked realms (1.x clients not support locked state show)
                if (!ok_build || ((*itr)->allowedSecurityLevel > _accountSecurityLevel))
                {
                    realmflags = RealmFlags(realmflags | REALM_FLAG_OFFLINE);
                }

                pkt << uint32((*itr)->icon);                                        // realm type
                pkt << uint8(realmflags);                                           // realmflags
                pkt << name;                                                        // name
                pkt << GetAddressString(GetAddressForClient((**itr), clientAddr));  // address
                pkt << float((*itr)->populationLevel);
                pkt << uint8(AmountOfCharacters);
                pkt << uint8((*itr)->timezone);                                     // realm category
                pkt << uint8(0x00);                                                 // unk, may be realm number/id?
            }

            pkt << uint16(0x0002);                          // unused value (why 2?)
            break;
        }

        case 8606:                                          // 2.4.3
        case 12340:                                         // 3.3.5a
        case 15595:                                         // 4.3.4
        case 18273:                                         // 5.4.8
        case 18414:                                         // 5.4.8
        case 21742:                                         // 6.2.4
        case 25549:                                         // 7.3.2
        case 32790:                                         // 8.2.5
        case 40000:                                         // 9.0.0
        default:                                            // and later
        {
            uint16 tempRealm = uint16(numRealms);           // Force the cast here to prevent a compile fail in VS2017/32Bit
            pkt << uint32(0);                               // unused value
            pkt << tempRealm;

            for (RealmList::RealmStlList::const_iterator itr = iters.first; itr != iters.second; ++itr)
            {
                clientAddr.set_port_number((*itr)->ExternalAddress.get_port_number());
                uint8 AmountOfCharacters;

                // No SQL injection. id of realm is controlled by the database.
                QueryResult* result = LoginDatabase.PQuery("SELECT `numchars` FROM `realmcharacters` WHERE `realmid` = '%d' AND `acctid`='%u'", (*itr)->m_ID, acctid);
                if (result)
                {
                    Field* fields = result->Fetch();
                    AmountOfCharacters = fields[0].GetUInt8();
                    delete result;
                }
                else
                {
                    AmountOfCharacters = 0;
                }

                bool ok_build = std::find((*itr)->realmbuilds.begin(), (*itr)->realmbuilds.end(), _build) != (*itr)->realmbuilds.end();

                RealmBuildInfo const* buildInfo = ok_build ? FindBuildInfo(_build) : NULL;
                if (!buildInfo)
                {
                    buildInfo = &(*itr)->realmBuildInfo;
                }

                uint8 lock = ((*itr)->allowedSecurityLevel > _accountSecurityLevel) ? 1 : 0;

                RealmFlags realmFlags = (*itr)->realmflags;

                // Show offline state for unsupported client builds
                if (!ok_build)
                {
                    realmFlags = RealmFlags(realmFlags | REALM_FLAG_OFFLINE);
                }

                if (!buildInfo)
                {
                    realmFlags = RealmFlags(realmFlags & ~REALM_FLAG_SPECIFYBUILD);
                }

                pkt << uint8((*itr)->icon);                                         // realm type (this is second column in Cfg_Configs.dbc)
                pkt << uint8(lock);                                                 // flags, if 0x01, then realm locked
                pkt << uint8(realmFlags);                                           // see enum RealmFlags
                pkt << (*itr)->name;                                                // name
                pkt << GetAddressString(GetAddressForClient((**itr), clientAddr));  // address
                pkt << float((*itr)->populationLevel);
                pkt << uint8(AmountOfCharacters);
                pkt << uint8((*itr)->timezone);                                     // realm category (Cfg_Categories.dbc)
                pkt << uint8(0x2C);                                                 // unk, may be realm number/id?

                if (realmFlags & REALM_FLAG_SPECIFYBUILD)
                {
                    pkt << uint8(buildInfo->major_version);
                    pkt << uint8(buildInfo->minor_version);
                    pkt << uint8(buildInfo->bugfix_version);
                    pkt << uint16(_build);
                }
            }

            pkt << uint16(0x0010);                          // unused value (why 10?)
            break;
        }
    }
}

/// Resume patch transfer
bool AuthSocket::_HandleXferResume()
{
    DEBUG_LOG("Entering _HandleXferResume");

    if (recv_len() < 9)
    {
        return false;
    }

    recv_skip(1);

    uint64 start_pos;
    recv((char*)&start_pos, 8);

    if (patch_ == ACE_INVALID_HANDLE)
    {
        close_connection();
        return false;
    }

    ACE_OFF_T file_size = ACE_OS::filesize(patch_);

    if (file_size == -1 || start_pos >= (uint64)file_size)
    {
        close_connection();
        return false;
    }

    if (ACE_OS::lseek(patch_, start_pos, SEEK_SET) == -1)
    {
        close_connection();
        return false;
    }

    InitPatch();

    return true;
}

/// Cancel patch transfer
bool AuthSocket::_HandleXferCancel()
{
    DEBUG_LOG("Entering _HandleXferCancel");

    recv_skip(1);
    close_connection();

    return true;
}

/// Accept patch transfer
bool AuthSocket::_HandleXferAccept()
{
    DEBUG_LOG("Entering _HandleXferAccept");

    recv_skip(1);

    InitPatch();

    return true;
}

void AuthSocket::InitPatch()
{
    PatchHandler* handler = new PatchHandler(ACE_OS::dup(get_handle()), patch_);

    patch_ = ACE_INVALID_HANDLE;

    if (handler->open() == -1)
    {
        handler->close();
        close_connection();
    }
}


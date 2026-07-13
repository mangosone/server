#include "immersivepch.h"
#include "ImmersiveServer.h"
#include "ImmersiveConfig.h"

#include "net/Server.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

using namespace std;

namespace
{
    /**
     * @brief One connection to the immersive command server.
     *
     * Line-oriented: each line is a command, answered with a single line. The shared
     * networking engine owns the socket and the threads (this used to be a blocking
     * ACE_SOCK_Acceptor loop on its own ACE task), so all that is left here is the
     * protocol.
     */
    class ImmersiveCommandSession : public net::ISession
    {
        public:

            void setSender(net::Sender sender) override { m_sender = std::move(sender); }

            std::vector<uint8_t> onData(const uint8_t* data, size_t len) override
            {
                m_buffer.append(reinterpret_cast<const char*>(data), len);

                std::string::size_type eol;
                while ((eol = m_buffer.find('\n')) != std::string::npos)
                {
                    const std::string request = m_buffer.substr(0, eol);
                    m_buffer.erase(0, eol + 1);

                    const std::string response = sImmersiveServer.HandleCommand(request);
                    if (m_sender)
                    {
                        m_sender(reinterpret_cast<const uint8_t*>(response.data()), response.size());
                    }
                }

                return {};
            }

            bool closed() const override { return false; }

        private:

            net::Sender m_sender;
            std::string m_buffer;   ///< bytes not yet forming a complete line
    };

    /// Owns the listening socket for the lifetime of the process.
    net::Server s_server;
}

string ImmersiveServer::HandleCommand(string request)
{
    string::iterator pos = find(request.begin(), request.end(), ',');
    if (pos == request.end())
    {
        ostringstream out; out << "invalid request: " << request << "\n";
        return out.str();
    }

    string command = string(request.begin(), pos);
    uint64 account = atoi(string(pos + 1, request.end()).c_str());
    Player* player = NULL;

    QueryResult* result = CharacterDatabase.PQuery("SELECT guid FROM characters WHERE account = '%u'", account);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint64 guid = fields[0].GetUInt64();
            player = sObjectMgr.GetPlayer(guid);
            if (!player || player->GetPlayerbotAI())
            {
                player = NULL;
                continue;
            }
            else
            {
                break;
            }
        }
        while (result->NextRow());
        delete result;
    }

    if (!player
#ifdef ENABLE_PLAYERBOTS
            || player->GetPlayerbotAI()
#endif
       )
    {
        ostringstream out; out << "No online players for account " << account << "\n";
        return out.str();
    }

    ostringstream out;
    if (command == "state")
    {
        if (player->GetDeathState() != ALIVE)
        {
            out << "dead";
        }
        else if (player->IsInCombat())
        {
            out << "combat";
        }
        else if (player->GetRestType() == REST_TYPE_IN_TAVERN)
        {
            out << "rest";
        }
        else
        {
            out << "default";
        }

        uint32 area = player->GetAreaId();
        if (area)
        {
            const AreaTableEntry* entry = sAreaStore.LookupEntry(area);
            if (entry)
            {
                out << "|" << entry->AreaName_lang[0];
            }
        }
    }
    else
    {
        out << "Invalid command " << command;
    }

    out << "\n";
    return out.str();
}

void ImmersiveServer::Start()
{
    const int serverPort = sImmersiveConfig.serverPort;
    if (!serverPort)
    {
        return;
    }

    ostringstream s;
    s << "Starting Immersive Server on port " << serverPort;
    sLog.outString(s.str().c_str());

    s_server.start(uint16_t(serverPort), []() -> std::shared_ptr<net::ISession>
    {
        return std::make_shared<ImmersiveCommandSession>();
    });
}

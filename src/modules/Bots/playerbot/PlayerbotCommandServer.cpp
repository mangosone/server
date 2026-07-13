#include "../botpch.h"
#include "playerbot.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotCommandServer.h"

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
     * @brief One connection to the playerbot command server.
     *
     * Line-oriented: each line is a command, answered with a single line. The shared
     * networking engine owns the socket and the threads (this used to be a blocking
     * ACE_SOCK_Acceptor loop on its own ACE task), so all that is left here is the
     * protocol — split the stream into lines, answer each one.
     */
    class PlayerbotCommandSession : public net::ISession
    {
        public:

            void setSender(net::Sender sender) override { m_sender = std::move(sender); }

            std::vector<uint8_t> onData(const uint8_t* data, size_t len) override
            {
                m_buffer.append(reinterpret_cast<const char*>(data), len);

                // A read may carry a partial line, several lines, or both; anything left
                // over stays buffered for the next one.
                std::string::size_type eol;
                while ((eol = m_buffer.find('\n')) != std::string::npos)
                {
                    const std::string request = m_buffer.substr(0, eol);
                    m_buffer.erase(0, eol + 1);

                    const std::string response = sRandomPlayerbotMgr.HandleRemoteCommand(request) + "\n";
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

void PlayerbotCommandServer::Start()
{
    const uint32 port = sPlayerbotAIConfig.commandServerPort;
    if (!port)
    {
        return;
    }

    ostringstream s;
    s << "Starting Playerbot Command Server on port " << port;
    sLog.outString(s.str().c_str());

    s_server.start(uint16_t(port), []() -> std::shared_ptr<net::ISession>
    {
        return std::make_shared<PlayerbotCommandSession>();
    });
}

#include "immersivepch.h"
#include "ImmersiveServer.h"
#include "ImmersiveConfig.h"
#include <cstdlib>
#include <iostream>

INSTANTIATE_SINGLETON_1(ImmersiveServer);

using namespace std;

#ifdef ENABLE_PLAYERBOTS
bool ReadLine(ACE_SOCK_Stream& client_stream, string* buffer, string* line);
#else
bool ReadLine(ACE_SOCK_Stream& client_stream, string* buffer, string* line)
{
    // Do the real reading from fd until buffer has '\n'.
    string::iterator pos;
    while ((pos = find(buffer->begin(), buffer->end(), '\n')) == buffer->end())
    {
        char buf[33];
        size_t n = client_stream.recv_n(buf, 1, 0);
        if (n == -1)
        {
            return false;
        }

        buf[n] = 0;
        *buffer += buf;
    }

    *line = string(buffer->begin(), pos);
    *buffer = string(pos + 1, buffer->end());
    return true;
}
#endif

class ImmersiveServerThread: public ACE_Task <ACE_MT_SYNCH>
{
public:
    int svc(void) {
        int serverPort = sImmersiveConfig.serverPort;
        if (!serverPort)
        {
            return 0;
        }

        ostringstream s; s << "Starting Immersive Server on port " << serverPort;
        sLog.outString(s.str().c_str());

        ACE_INET_Addr server(serverPort);
        ACE_SOCK_Acceptor client_responder(server);

		while (true)
		{
			ACE_SOCK_Stream client_stream;
			ACE_Time_Value timeout(5);
			ACE_INET_Addr client;
			if (-1 != client_responder.accept(client_stream, &client, &timeout))
			{
				string buffer, request;
				while (ReadLine(client_stream, &buffer, &request))
				{
				    string response = sImmersiveServer.HandleCommand(request);
                    client_stream.send_n(response.c_str(), response.size(), 0);
					request = "";
				}
				client_stream.close();
			}
		}

        return 0;
    }
};

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
    Player *player = NULL;

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
            } else break;
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
        if (player->GetDeathState() != ALIVE) out << "dead";
        {
            else if (player->IsInCombat()) out << "combat";
        }
        else if (player->GetRestType() == REST_TYPE_IN_TAVERN) out << "rest";
        {
            else out << "default";
        }

        uint32 area = player->GetAreaId();
        if (area)
        {
            const AreaTableEntry* entry = sAreaStore.LookupEntry(area);
            if (entry)
            {
                out << "|" << entry->area_name[0];
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
    ImmersiveServerThread *thread = new ImmersiveServerThread();
    thread->activate();
}

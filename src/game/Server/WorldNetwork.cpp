#include "WorldNetwork.h"

#include "Log.h"
#include "OpcodeTable.h"

WorldNetwork::WorldNetwork()
    : m_listener(m_gateway)
{
}

WorldNetwork::~WorldNetwork()
{
    Stop();
}

bool WorldNetwork::Start(uint16 port, const std::string& bindIp)
{
    if (m_started)
        return false;

    InitializeOpcodes();
    if (!m_listener.Start(port, bindIp))
    {
        sLog.outError("WorldNetwork::Start: failed to listen on %s:%u",
            bindIp.empty() ? "0.0.0.0" : bindIp.c_str(), unsigned(port));
        return false;
    }

    m_started = true;
    return true;
}

void WorldNetwork::Stop()
{
    if (!m_started)
        return;

    m_listener.Stop();
    m_started = false;
}

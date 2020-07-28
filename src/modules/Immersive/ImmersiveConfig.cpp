#include "immersivepch.h"
#include "../../shared/SystemConfig.h"
#include "ImmersiveConfig.h"

using namespace std;

INSTANTIATE_SINGLETON_1(ImmersiveConfig);

ImmersiveConfig::ImmersiveConfig()
{
}

bool ImmersiveConfig::Initialize()
{
    sLog.outString("Initializing Immersive by ike3");

    if (!config.SetSource(SYSCONFDIR"immersive.conf"))
    {
        sLog.outString("Immersive is Disabled. Unable to open configuration file immersive.conf");
        return false;
    }

    serverPort = config.GetIntDefault("Immersive.ServerPort", 0);

    sLog.outString("Immersive configuration loaded");
    return true;
}

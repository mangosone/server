#ifndef _ImmersiveServer_H
#define _ImmersiveServer_H

using namespace std;

class ImmersiveServer
{
public:
    ImmersiveServer() {}
    virtual ~ImmersiveServer() {}
    static ImmersiveServer& instance()
    {
        static ImmersiveServer instance;
        return instance;
    }

    void Start();
    string HandleCommand(string request);
};

#define sImmersiveServer ImmersiveServer::instance()

#endif

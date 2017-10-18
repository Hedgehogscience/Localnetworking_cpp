/*
    Initial author: Convery (tcn@ayria.se)
    Started: 18-10-2017
    License: MIT
    Notes:
        Creates servers for the hostnames.
*/

#include "../Stdinclude.h"

namespace Localnetworking
{
    std::unordered_map<std::string /* Hostname */, void * /* Module */> Modulecache;
    std::vector<std::string /* Hostname */> Blacklist;
    std::vector<void * /* Module */> Networkmodules;

    // Forward declarations for  platform specific functionality.
    void *Findfunction(void *Module, const char *Function);
    void *Loadmodule(const char *Path);

    // Create a new server-instance.
    IServer *Createinstance(std::string Hostname)
    {
        // Don't waste time on blacklisted hostnames.
        if (Blacklist.end() != std::find(Blacklist.begin(), Blacklist.end(), Hostname))
            return nullptr;

        // Create a new instance.
        auto Createserver = [&](void *Module) -> IServer *
        {
            // Find the export in the module.
            auto pFunction = Findfunction(Module, "Createserver");
            if (!pFunction) return nullptr;

            // Ask the module to create a new instance for the hostname.
            auto Function = (IServer * (*)(const char *))pFunction;
            auto Result = Function(Hostname.c_str());

            return Result;
        };

        // Check if we know which module to call.
        auto Cache = Modulecache.find(Hostname);
        if (Cache != Modulecache.end())
        {
            return Createserver(Cache->second);
        }

        // Iterate over all the known modules.
        for (auto &Item : Networkmodules)
        {
            auto Server = Createserver(Item);
            if (Server) return Server;
        }

        // Blacklist the hostname so we don't spam.
        Blacklist.push_back(Hostname);
        return nullptr;
    }

    // The platform specific functionality.
    #if defined (_WIN32)
    void *Findfunction(void *Module, const char *Function)
    {
        return (void *)GetProcAddress(HMODULE(Module), Function);
    }
    void *Loadmodule(const char *Path)
    {
        return (void *)LoadLibraryA(Path);
    }
    #else

    void *Findfunction(void *Module, const char *Function)
    {
        return (void *)dlsym(Module, Function);
    }
    void *Loadmodule(const char *Path)
    {
        return (void *)dlopen(Path, RTLD_LAZY);
    }

    #endif
}

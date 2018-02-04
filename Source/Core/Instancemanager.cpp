/*
    Initial author: Convery (tcn@hedgehogscience.com)
    Started: 09-01-2018
    License: MIT
    Notes:
        Provides initialization and management for servers.
*/

#include "../Stdinclude.hpp"

namespace Localnetworking
{
    constexpr const char *Moduleextension = sizeof(void *) == sizeof(uint32_t) ?  ".LN32" : ".LN64";
    std::unordered_map<std::string /* IP */, std::string /* Hostname */> Resolvercache;
    std::unordered_map<std::string /* Hostname */, void * /* Module */> Modulecache;
    std::unordered_map<std::string /* Hostname */, IServer *> Serverinstances;
    std::vector<std::string /* Hostname */> Blacklist;
    std::vector<void * /* Module */> Networkmodules;

    // Platform functionality.
    std::string Temporarydir();
    void *Loadmodule(std::string_view Modulename);
    void *Getfunction(void *Modulehandle, std::string_view Function);

    // Create a new instance of a server.
    IServer *Createserver(std::string_view Hostname)
    {
        // Don't waste time on blacklisted hostnames.
        if (Blacklist.end() != std::find(Blacklist.begin(), Blacklist.end(), Hostname))
            return nullptr;

        // Create a new server-instance.
        auto Lambda = [&](void *Module) -> IServer *
        {
            // Find the export in the module.
            auto pFunction = Getfunction(Module, "Createserver");
            if (!pFunction) return nullptr;

            // Ask the module to create a new instance for the hostname.
            auto Function = (IServer * (*)(const char *))pFunction;
            auto Result = Function(Resolvercache[Hostname.data()].c_str());
            if(!Result) Result = Function(Hostname.data());

            return Result;
        };

        // Check if we have cached which module is associated.
        auto Cache = Modulecache.find(Hostname.data());
        if (Cache != Modulecache.end())
        {
            return Lambda(Cache->second);
        }

        // Iterate over all the known modules.
        for (auto &Item : Networkmodules)
        {
            auto Server = Lambda(Item);
            if (Server) return Server;
        }

        // Blacklist the hostname so we don't spam.
        Blacklist.push_back(Hostname.data());
        return nullptr;
    }
    void Duplicateserver(std::string_view Hostname, IServer *Instance)
    {
        Serverinstances.emplace(Hostname, Instance);
    }

    // Find a server by criteria.
    IServer *Findserver(std::string_view Hostname)
    {
        auto Entry = Serverinstances.find(Hostname.data());
        if(Entry != Serverinstances.end())
            return Entry->second;
        else
            return nullptr;
    }

    // Reverse lookup and debugging information.
    void Forceresolvehost(std::string IP, std::string Hostname)
    {
        Resolvercache[IP] = Hostname;
    }
    std::string Findhostname(IServer *Server)
    {
        for (auto &Item : Serverinstances)
        {
            if (Item.second == Server)
                return Item.first;
        }

        return "";
    }

    // Initialize the modules.
    void Loadallmodules()
    {
        // Enumerate all modules in the directory.
        auto Modulenames = Findfiles("./Plugins/", ".Localnet");
        Infoprint(va("Found %i modules.", Modulenames.size()));

        // Extract and load the plugins.
        for (auto &Item : Modulenames)
        {
            auto Archive = Package::Loadarchive("./Plugins/" + Item);
            auto List = Package::Findfiles(Archive, Moduleextension);
            if (0 == List.size()) continue;

            // Remove any already extracted plugin.
            auto Path = Temporarydir() + "/" + List[0];
            std::remove(Path.c_str());

            // Write the file to disk.
            Writefile(Path, Package::Read(Archive, List[0]));

            // Load the module from disk.
            Networkmodules.push_back(Loadmodule(Path.c_str()));
        }

        // Sideload any developer plugin.
        #if defined(_WIN32)
        if(Fileexists("./Plugins/Developermodule.dll"))
            Networkmodules.push_back(Loadmodule("./Plugins/Developermodule"));
        #else
        if (Fileexists("./Plugins/Developerplugin.so"))
            Networkmodules.push_back(Loadmodule("./Plugins/Developermodule"));
        #endif
    }

    // Platform functionality.
    #if defined(_WIN32)

    std::string Temporarydir()
    {
        char Buffer[1024]{};
        GetTempPathA(1024, Buffer);
        return std::move(Buffer);
    }
    void *Loadmodule(std::string_view Modulename)
    {
        return LoadLibraryA(Modulename.data());
    }
    void *Getfunction(void *Modulehandle, std::string_view Functionname)
    {
        return GetProcAddress(HMODULE(Modulehandle), Functionname.data());
    }

    #else

    std::string Temporarydir()
    {
        auto Folder = getenv("TMPDIR");
        if(Folder) return { Folder };

        Folder = getenv("TMP");
        if(Folder) return { Folder };

        Folder = getenv("TEMP");
        if(Folder) return { Folder };

        Folder = getenv("TEMPDIR");
        if(Folder) return { Folder };

        return "/tmp";
    }
    void *Loadmodule(std::string_view Modulename)
    {
        return dlopen(Modulename.data(), RTLD_LAZY);
    }
    void *Getfunction(void *Modulehandle, std::string_view Functionname)
    {
        return dlsym(Modulehandle, Functionname.data());
    }

    #endif
}

/*
    Initial author: Convery (tcn@ayria.se)
    Started: 18-10-2017
    License: MIT
    Notes:
        Creates servers for the hostnames.
*/

#include "../Stdinclude.h"

#define Address Server.Plainaddress

namespace Localnetworking
{
    using Frame_t = struct { IPAddress_t From; std::string Data; };

    std::unordered_map<std::string /* Hostname */, void * /* Module */> Modulecache;
    std::unordered_map<std::string /* Hostname */, IServer *> Serverinstances;
    std::unordered_map<size_t /* Socket */, std::vector<IPAddress_t>> Filters;
    std::unordered_map<size_t /* Socket */, std::queue<Frame_t>> Framequeue;
    std::unordered_map<IServer *, std::vector<size_t>> Connectedsockets;
    std::vector<std::string /* Hostname */> Blacklist;
    std::vector<void * /* Module */> Networkmodules;

    // Forward declarations for  platform specific functionality.
    void *Findfunction(void *Module, const char *Function);
    void *Loadmodule(const char *Path);

    // Create a new server instance.
    IServer *Createserver(std::string Hostname)
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

    // Modify a servers properties.
    bool isAssociated(size_t Socket)
    {
        for (auto &Entry : Connectedsockets)
        {
            for (auto &Berkeley : Entry.second)
            {
                if (Berkeley == Socket)
                {
                    return true;
                }
            }
        }

        return false;
    }
    std::vector<size_t> Activesockets()
    {
        static std::vector<size_t> Sockets;
        static uint32_t Timestamp = 0;

        // Throttle the updating.
        if (Timestamp + 5 < time(NULL)) return Sockets;
        Timestamp = time(NULL);
        Sockets.clear();

        // Get all connected sockets.
        for (auto &List : Connectedsockets)
            for(auto &Item : List.second)
                Sockets.push_back(Item);

        // Remove duplicates.
        std::sort(Sockets.begin(), Sockets.end());
        auto Last = std::unique(Sockets.begin(), Sockets.end());
        Sockets.erase(Last, Sockets.end());

        return Sockets;
    }
    void Addfilter(size_t Socket, IPAddress_t Filter)
    {
        auto Entry = &Filters[Socket];
        Entry->push_back(Filter);
    }
    std::vector<IPAddress_t> &Getfilters(size_t Socket)
    {
        return Filters[Socket];
    }
    void Associatesocket(IServer *Server, size_t Socket)
    {
        Connectedsockets[Server].push_back(Socket);
    }
    void Disassociatesocket(IServer *Server, size_t Socket)
    {
        for (auto &Item : Connectedsockets[Server])
        {
            if (Item == Socket)
            {
                Item = 0;
                break;
            }
        }
    }

    // Query the servermaps.
    IServer *Findserver(size_t Socket)
    {
        for (auto &Server : Connectedsockets)
        {
            for (auto &Berkeley : Server.second)
            {
                if (Berkeley == Socket)
                    return Server.first;
            }
        }

        return nullptr;
    }
    IServer *Findserver(std::string Hostname)
    {
        auto Entry = Serverinstances.find(Hostname);
        if(Entry != Serverinstances.end())
            return Entry->second;
        else
            return nullptr;
    }
    size_t Findsocket(IPAddress_t Server, size_t Offset)
    {
        for(auto &Collection : Filters)
        {
            for(auto &Item : Collection.second)
            {
                if(Item.Port != Server.Port) continue;
                if(0 != std::strcmp(Item.Plainaddress, Address)
                && 0 != std::strcmp(Item.Plainaddress, "0.0.0.0")
                && 0 != std::strcmp(Item.Plainaddress, "::")) continue;

                if(Offset)
                {
                    Offset--;
                    continue;
                }

                return Collection.first;
            }
        }

        return 0;
    }

    // Map packets to and from the internal lists.
    void Enqueueframe(IPAddress_t From, std::string &Packet)
    {
        size_t Socket = 0;
        size_t Offset = 0;

        do
        {
            Socket = Findsocket(From, Offset++);
            Framequeue[Socket].push({ From, Packet });
        } while (Socket != 0);
    }
    bool Dequeueframe(size_t Socket, IPAddress_t &From, std::string &Packet)
    {
        if (Framequeue[Socket].empty()) return false;

        auto Frame = Framequeue[Socket].front();
        Framequeue[Socket].pop();
        Packet = Frame.Data;
        From = Frame.From;
        return true;
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

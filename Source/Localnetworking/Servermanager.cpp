/*
    Initial author: Convery (tcn@ayria.se)
    Started: 18-10-2017
    License: MIT
    Notes:
        Creates servers for the hostnames.
*/

#include "../Stdinclude.h"

#define Address Server.Plainaddress
constexpr const char *Archetecture = sizeof(void *) == sizeof(uint64_t) ? "64" : "32";

namespace Localnetworking
{
    using Frame_t = struct { IPAddress_t From; std::string Data; };

    std::unordered_map<std::string /* IP */, std::string /* Hostname */> Resolvercache;
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
    std::string Temporarydir();

    // Create a new server instance.
    IServer *Createserver(std::string Hostname)
    {
        // Don't waste time on blacklisted hostnames.
        if (Blacklist.end() != std::find(Blacklist.begin(), Blacklist.end(), Hostname))
            return nullptr;

        // Don't create a new instance if we already have created one.
        if (Serverinstances.end() != Serverinstances.find(Hostname))
            return Serverinstances[Hostname];

        // Create a new instance.
        auto Lambda = [&](void *Module) -> IServer *
        {
            // Find the export in the module.
            auto pFunction = Findfunction(Module, "Createserver");
            if (!pFunction) return nullptr;

            // Ask the module to create a new instance for the hostname.
            auto Function = (IServer * (*)(const char *))pFunction;
            auto Result = Function(Resolvercache[Hostname].c_str());
            if(!Result) Result = Function(Hostname.c_str());

            return Result;
        };

        // Check if we know which module to call.
        auto Cache = Modulecache.find(Hostname);
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
        Blacklist.push_back(Hostname);
        return nullptr;
    }
    void Emplaceserver(std::string Hostname, IServer *Server)
    {
        Serverinstances.emplace(Hostname, Server);
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
        static int64_t Timestamp = 0;

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
        for (auto &Item : *Entry)
        {
            if (std::strcmp(Item.Plainaddress, Filter.Plainaddress) == 0
                && Item.Port == Filter.Port)
                return;
        }
        Entry->push_back(Filter);
    }
    std::vector<IPAddress_t> &Getfilters(size_t Socket)
    {
        return Filters[Socket];
    }
    void Associatesocket(IServer *Server, size_t Socket)
    {
        auto Entry = &Connectedsockets[Server];
        for (auto &Item : *Entry)
        {
            if (Item == Socket)
                return;
        }
        Entry->push_back(Socket);
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
    void Associateaddress(std::string IP, std::string Hostname)
    {
        Resolvercache[IP] = Hostname;
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
    std::string Findhostname(IServer *Server)
    {
        for (auto &Item : Serverinstances)
        {
            if (Item.second == Server)
                return Item.first;
        }

        return "";
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

    // Periodically poll the servers for datagrams.
    void Datagrampollthread()
    {
        auto Buffer = std::make_unique<char []>(10240);
        uint32_t Buffersize = 10240;

        while(true)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));

            for(auto &Instance : Serverinstances)
            {
                Buffersize = 10240;
                IPAddress_t Serveraddress;
                if(Instance.second->onPacketread(Serveraddress, Buffer.get(), &Buffersize))
                {
                    auto Packet = std::string(Buffer.get(), Buffersize);
                    Enqueueframe(Serveraddress, Packet);
                }
            }
        }
    }
    void Startpollthread()
    {
        std::thread(Datagrampollthread).detach();
    }

    // Load all modules from the plugins directory.
    void Loadmodules()
    {
        std::vector<std::string> Filenames;
        std::string Path = "./Plugins/";

        // Enumerate all modules in the directory.
        if (Findfiles(Path, &Filenames, ".LNmodule"))
        {
            for (auto &Item : Filenames)
            {
                std::vector<std::string> Archivefiles;
                auto Archive = Package::Loadarchive("./Plugins/" + Item);
                if (Package::Findfiles(Archive, va(".LN%s", Archetecture), &Archivefiles))
                {
                    // Write the file to disk as loading modules from memory is not very portable.
                    auto Path = Temporarydir() + "/" + Archivefiles[0]; std::remove(Path.c_str());
                    Writefile(Path, Package::Read(Archive, Archivefiles[0]));

                    // Load the module from temp storage.
                    Networkmodules.push_back(Loadmodule(Path.c_str()));
                }
            }
        }

        // Sideload any developer module.
        Networkmodules.push_back(Loadmodule("./Plugins/Developermodule"));
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
    std::string Temporarydir()
    {
        char Buffer[1024]{};
        GetTempPathA(1024, Buffer);
        return { Buffer };
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
    #endif
}

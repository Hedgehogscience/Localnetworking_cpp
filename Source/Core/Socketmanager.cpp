/*
    Initial author: Convery (tcn@hedgehogscience.com)
    Started: 09-01-2018
    License: MIT
    Notes:
        Provides IO routing and association.
*/

#include "../Stdinclude.hpp"

namespace Localnetworking
{
    #define Address Server.Plainaddress
    using Frame_t = struct { Address_t From; std::string Data; };

    extern std::unordered_map<std::string /* Hostname */, IServer *> Serverinstances;
    std::unordered_map<size_t /* Socket */, std::vector<Address_t>> Filters;
    std::unordered_map<size_t /* Socket */, std::queue<Frame_t>> Framequeue;
    std::unordered_map<IServer *, std::vector<size_t>> Connectedsockets;

    // Find a server by criteria.
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

    // Manage filters for packet-based IO.
    void Addfilter(size_t Socket, Address_t Filter)
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
    std::vector<Address_t> &Getfilters(size_t Socket)
    {
        return Filters[Socket];
    }

    // Manage the internal sockets.
    bool isInternalsocket(size_t Socket)
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
    std::vector<size_t> Internalsockets()
    {
        static std::vector<size_t> Sockets;
        static int64_t Timestamp = 0;

        // Throttle the updating.
        if (Timestamp + 5 > time(NULL)) return Sockets;
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
    void Createsocket(IServer *Server, size_t Socket)
    {
        auto Entry = &Connectedsockets[Server];
        for (auto &Item : *Entry)
        {
            if (Item == Socket)
                return;
        }
        Entry->push_back(Socket);
    }
    void Destroysocket(IServer *Server, size_t Socket)
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
    size_t Findinternalsocket(Address_t Server, size_t Offset)
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
    void Enqueueframe(Address_t From, std::string &Packet)
    {
        size_t Socket = 0;
        size_t Offset = 0;

        do
        {
            Socket = Findinternalsocket(From, Offset++);
            Framequeue[Socket].push({ From, Packet });
        } while (Socket != 0);
    }
    bool Dequeueframe(size_t Socket, Address_t &From, std::string &Packet)
    {
        if (Framequeue[Socket].empty()) return false;

        auto Frame = Framequeue[Socket].front();
        Framequeue[Socket].pop();
        Packet = Frame.Data;
        From = Frame.From;
        return true;
    }

    // Initialize the datagram IO.
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
                Address_t Serveraddress;
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
}

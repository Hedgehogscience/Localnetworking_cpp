/*
    Initial author: Convery (tcn@ayria.se)
    Started: 18-10-2017
    License: MIT
    Notes:
        Endpoint to endpoint IO.
*/

#include "../Stdinclude.h"

namespace Localnetworking::Layer3
{
    std::unordered_map<size_t /* Identifier */, std::vector<IPAddress_t>> Filters;
    std::unordered_map<size_t /* Identifier */, std::vector<Frame_t>> Nodes;
    std::queue<Frame_t> Framequeue;

    // Set endpoints for the networking.
    void Addendpoint(size_t Identifier)
    {
        Filters[Identifier];
        Nodes[Identifier];
    }
    void Removeendpoint(size_t Identifier)
    {
        Filters.erase(Identifier);
        Nodes.erase(Identifier);
    }

    // Set filters for the endpoints.
    void Addfilter(size_t Identifier, IPAddress_t Filter)
    {
        Filters[Identifier].push_back(Filter);
    }
    void Removefilter(size_t Identifier, IPAddress_t Filter)
    {
        std::vector<IPAddress_t> Newlist;
        for (auto &Item : Filters[Identifier])
        {
            if (Item.Port != Filter.Port)
            {
                Newlist.push_back(Item);
                continue;
            }

            if (0 != std::memcmp(Item.Plainaddress, Filter.Plainaddress, 64))
            {
                Newlist.push_back(Item);
            }
        }
        Filters[Identifier] = Newlist;
    }

    // Append data to the network.
    void Appendframe(Frame_t &Frame)
    {
        Framequeue.push(Frame);
    }

    // Internal processing of the queue.
    void doFrame()
    {
        bool Delivered = false;

        while (true)
        {
            // 100 frames a second should be plenty.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            // Process as many frames as available.
            size_t Count = Framequeue.size();
            for (size_t i = 0; i < Count; ++i)
            {
                // Dequeue the frame.
                auto Frame = Framequeue.front();
                Framequeue.pop();

                // Copy the frame to the first filtered node.
                for (auto &Node : Filters)
                {
                    for (auto &Filter : Node.second)
                    {
                        if (Filter.Port != Frame.To.Port) continue;
                        if (0 == std::memcmp(Filter.Plainaddress, Frame.To.Plainaddress, 64))
                        {
                            Nodes[Node.first].push_back(Frame);
                            Delivered = true;
                            break;
                        }
                    }

                    if (Delivered) break;
                }

                // Broadcast to the listen-nodes.
                if (!Delivered)
                {
                    for (auto &Node : Filters)
                    {
                        for (auto &Filter : Node.second)
                        {
                            if (Filter.Port != Frame.To.Port) continue;
                            if (0 == std::strcmp(Filter.Plainaddress, "0.0.0.0") || 0 == std::strcmp(Filter.Plainaddress, "::"))
                            {
                                Nodes[Node.first].push_back(Frame);
                                break;
                            }
                        }
                    }
                }

            }

            // Reset the state.
            Delivered = false;
        }
    }
    namespace { struct Startup { Startup() { std::thread(doFrame).detach(); } }; static Startup Start{}; }
}

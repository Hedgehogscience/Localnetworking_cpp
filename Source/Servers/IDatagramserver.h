/*
    Initial author: Convery
    Started: 2017-4-13
    License: Apache 2.0
*/

#pragma once
#include "IServer.h"
#include <algorithm>
#include <vector>
#include <mutex>

// The servers capabilities.
#define ISERVER_DATAGRAM    32

// The datagram server functions very much like UDP.
struct IDatagramserver : IServer
{
    // Packet based buffer and guard.
    std::vector<std::string> Outgoingpackets;
    std::mutex Packetguard;

    // Return the capabilities of this server.
    virtual const uint64_t Capabilities() { return ISERVER_BASE | ISERVER_DATAGRAM; };

    // Callback on incoming data, wrapper unlocks the mutex if the user forgets.
    virtual void onPacket(std::string &Packet) = 0;
    static void onPacketwrapper(IDatagramserver *Server, std::string Packet)
    {
        Server->onPacket(Packet);
        Server->Packetguard.try_lock();
        Server->Packetguard.unlock();
    }

    // Returns false if there's an error, like there being no data or connection.
    virtual bool onWriterequest(const void *Databuffer, const uint32_t Datasize)
    {
        Packetguard.lock();
        {
            // Callback to usercode.
            auto Pointer = reinterpret_cast<const char *>(Databuffer);
            std::thread(onPacketwrapper, this, std::string(Pointer, Datasize)).detach();
        }
        // Unlocked in the new thread.

        return true;
    }
    virtual bool onReadrequest(void *Databuffer, uint32_t *Datasize)
    {
        // Don't waste time if we don't have any data available.
        if (0 == Outgoingpackets.size())
            return false;

        Packetguard.lock();
        {
            // Get the first packet.
            auto Packet = Outgoingpackets.front();
            Outgoingpackets.erase(Outgoingpackets.begin());

            // The data that will fit into the buffer.
            uint32_t Readcount = std::min(*Datasize, uint32_t(Packet.size()));
            *Datasize = Readcount;

            // Copy as much data as the buffer allows, drop the rest.
            std::copy_n(Packet.begin(), Readcount, reinterpret_cast<char *>(Databuffer));
        }
        Packetguard.unlock();

        return true;
    }

    // Send data from usercode.
    virtual void Send(std::string Databuffer)
    {
        Packetguard.lock();
        {
            Outgoingpackets.push_back(Databuffer);
        }
        Packetguard.unlock();
    }
    virtual void Send(const void *Databuffer, const uint32_t Datasize)
    {
        return Send({ reinterpret_cast<const char *>(Databuffer), Datasize });
    }
};


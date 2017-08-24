/*
    Initial author: Convery (tcn@ayria.se)
    Started: 24-08-2017
    License: MIT
    Notes:
*/

#pragma once
#include "IServer.h"
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <mutex>

// The servers capabilities.
#define ISERVER_STREAMED    16

// The stream server functions very much like TCP.
struct IStreamserver : IServerEx
{
    // The streams we are currently managing, socket as key.
    std::unordered_map<size_t, std::vector<uint8_t>> Incomingstream;
    std::unordered_map<size_t, std::vector<uint8_t>> Outgoingstream;
    std::unordered_map<size_t, std::mutex> Streamguard;
    std::unordered_map<size_t, bool> Connectedstreams;

    // Return the capabilities of this server.
    virtual const uint64_t Capabilities() { return ISERVER_EXTENDED | ISERVER_STREAMED; };

    // Socket state callbacks.
    virtual void onDisconnect(const size_t Socket)
    {
        // Keep the outgoing data for lingering connections.
        Streamguard[Socket].lock();
        {
            Incomingstream[Socket].clear();
            Connectedstreams[Socket] = false;
        }
        Streamguard[Socket].unlock();
    }
    virtual void onConnect(const size_t Socket, const uint16_t Port)
    {
        // Clear the streams when a new connection is made.
        Streamguard[Socket].lock();
        {
            Outgoingstream[Socket].clear();
            Incomingstream[Socket].clear();
            Connectedstreams[Socket] = true;
        }
        Streamguard[Socket].unlock();
    }

    // Callback on incoming data.
    virtual void onStreamupdate(const size_t Socket, std::vector<uint8_t> &Stream) = 0;

    // Returns false if there's an error, like there being no data or connection.
    virtual bool onWriterequestEx(const size_t Socket, const void *Databuffer, const uint32_t Datasize)
    {
        // When the server isn't connected, we just drop the data.
        if (false == Connectedstreams[Socket])
            return false;

        Streamguard[Socket].lock();
        {
            auto Pointer = reinterpret_cast<const uint8_t *>(Databuffer);
            std::copy_n(Pointer, Datasize, std::back_inserter(Incomingstream[Socket]));

            // Callback to usercode.
            onStreamupdate(Socket, Incomingstream[Socket]);
        }
        // Unlock if needed.
        Streamguard[Socket].try_lock();
        Streamguard[Socket].unlock();

        return true;
    }
    virtual bool onReadrequestEx(const size_t Socket, void *Databuffer, uint32_t *Datasize)
    {
        // To support lingering connections, we return data even if disconnected.
        if (false == Connectedstreams[Socket] && 0 == Outgoingstream[Socket].size())
            return false;

        // While it should never be null, just to be safe.
        if (!Databuffer || !Datasize)
            return false;

        Streamguard[Socket].lock();
        {
            // The data that will fit into the buffer.
            uint32_t Readcount = std::min(*Datasize, uint32_t(Outgoingstream[Socket].size()));
            *Datasize = Readcount;

            // Copy the elements to the buffer and remove them from our stream.
            std::copy_n(Outgoingstream[Socket].begin(), Readcount, reinterpret_cast<uint8_t *>(Databuffer));
            Outgoingstream[Socket].erase(Outgoingstream[Socket].begin(), Outgoingstream[Socket].begin() + Readcount);
        }
        Streamguard[Socket].unlock();

        return true;
    }

    // Send data from usercode, if no socket is specified it's broadcast.
    virtual void Send(const size_t Socket, const void *Databuffer, const uint32_t Datasize)
    {
        auto Lambda = [&](const size_t lSocket) -> void
        {
            Streamguard[lSocket].lock();
            {
                auto Pointer = reinterpret_cast<const uint8_t *>(Databuffer);
                std::copy_n(Pointer, Datasize, std::back_inserter(Outgoingstream[Socket]));
            }
            Streamguard[lSocket].unlock();
        };

        // If the socket is defined, we just send the data to it.
        if (0 != Socket) return Lambda(Socket);

        // Else we broadcast to all connected streams.
        for (auto &Item : Connectedstreams)
        {
            if (true == Item.second)
            {
                Lambda(Item.first);
            }
        }
    }
    virtual void Send(const size_t Socket, std::string &Databuffer)
    {
        return Send(Socket, Databuffer.data(), uint32_t(Databuffer.size()));
    }
};

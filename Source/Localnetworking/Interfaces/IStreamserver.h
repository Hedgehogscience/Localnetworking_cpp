/*
    Initial author: Convery (tcn@ayria.se)
    Started: 17-10-2017
    License: MIT
    Notes:
        Implements a stream-based form of IO.
*/

#pragma once
#include "IServer.h"
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <mutex>

struct IStreamserver : IServer
{
    // Per socket state-information where the Berkeley socket is the key.
    std::unordered_map<size_t, std::vector<uint8_t>> Incomingstream;
    std::unordered_map<size_t, std::vector<uint8_t>> Outgoingstream;
    std::unordered_map<size_t, bool> Validconnection;
    std::mutex Threadguard;

    // Usercode interaction.
    virtual void Send(const size_t Socket, const void *Databuffer, const uint32_t Datasize)
    {
        auto Lambda = [&](const size_t lSocket) -> void
        {
            // Enqueue the data at the end of the stream.
            Threadguard.lock();
            {
                auto Pointer = reinterpret_cast<const uint8_t *>(Databuffer);
                std::copy_n(Pointer, Datasize, std::back_inserter(Outgoingstream[lSocket]));
            }
            Threadguard.unlock();
        };

        // If there is a socket, just enqueue to its stream.
        if (0 != Socket) return Lambda(Socket);

        // Else we treat it as a broadcast request.
        for (auto &Item : Validconnection)
            if (Item.second == true)
                Lambda(Item.first);
    }
    virtual void Send(const size_t Socket, std::string Databuffer)
    {
        return Send(Socket, Databuffer.data(), uint32_t(Databuffer.size()));
    }
    virtual void onData(const Localsocket_t &Socket, std::vector<uint8_t> &Data) = 0;

    // Socket state update-notifications.
    virtual void onConnect(const Localsocket_t &Socket)
    {
        Threadguard.lock();
        {
            // Clear the streams to be ready for new data.
            Incomingstream[Socket.Berkeley].clear();
            Outgoingstream[Socket.Berkeley].clear();

            // Set the connection-state.
            Validconnection[Socket.Berkeley] = true;
        }
        Threadguard.unlock();
    }
    virtual void onDisconnect(const Localsocket_t &Socket)
    {
        Threadguard.lock();
        {
            // Clear the incoming stream, but keep the outgoing.
            Incomingstream[Socket.Berkeley].clear();
            Incomingstream[Socket.Berkeley].shrink_to_fit();

            // Set the connection-state.
            Validconnection[Socket.Berkeley] = false;
        }
        Threadguard.unlock();
    }

    // Returns false if the request could not be completed for any reason.
    virtual bool onReadrequest(const Localsocket_t &Socket, void *Databuffer, uint32_t *Datasize)
    {
        // To support lingering sockets, we transmit data even if the socket is technically disconnected.
        if (0 == Outgoingstream[Socket.Berkeley].size() && Validconnection[Socket.Berkeley]) return false;
        if (0 != Outgoingstream[Socket.Berkeley].size() && !Validconnection[Socket.Berkeley]) return false;

        // Verify the pointers, although they should always be valid.
        if (!Databuffer || !Datasize) return false;

        // Copy the stream to the buffer.
        Threadguard.lock();
        {
            // Validate the state, unlikely to change.
            if (0 != Outgoingstream[Socket.Berkeley].size())
            {
                // Copy as much data as we can fit in the buffer.
                *Datasize = std::min(*Datasize, uint32_t(Outgoingstream[Socket.Berkeley].size()));
                std::copy_n(Outgoingstream[Socket.Berkeley].begin(), *Datasize, reinterpret_cast<char *>(Databuffer));
                Outgoingstream[Socket.Berkeley].erase(Outgoingstream[Socket.Berkeley].begin(), Outgoingstream[Socket.Berkeley].begin() + *Datasize);
            }
        }
        Threadguard.unlock();
    }
    virtual bool onWriterequest(const Localsocket_t &Socket, const void *Databuffer, const uint32_t Datasize)
    {
        // If there is no valid connection, we just ignore the data.
        if (Validconnection[Socket.Berkeley] == false) return false;

        // Append the data to the stream and notify usercode.
        Threadguard.lock();
        {
            auto Pointer = reinterpret_cast<const uint8_t *>(Databuffer);
            std::copy_n(Pointer, Datasize, std::back_inserter(Incomingstream[Socket.Berkeley]));
            onData(Socket, Incomingstream[Socket.Berkeley]);

            // Ensure that the mutex is locked as usercode is unpredictable.
            Threadguard.try_lock();
        }
        Threadguard.unlock();

        return true;
    }
};

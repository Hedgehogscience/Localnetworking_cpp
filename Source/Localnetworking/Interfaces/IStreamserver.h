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
    virtual void onData(const Requestheader_t &Header, std::vector<uint8_t> &Data) = 0;

    // Header state update-notifications.
    virtual void onConnect(const Requestheader_t &Header)
    {
        Threadguard.lock();
        {
            // Clear the streams to be ready for new data.
            Incomingstream[Header.Socket].clear();
            Outgoingstream[Header.Socket].clear();

            // Set the connection-state.
            Validconnection[Header.Socket] = true;
        }
        Threadguard.unlock();
    }
    virtual void onDisconnect(const Requestheader_t &Header)
    {
        Threadguard.lock();
        {
            // Clear the incoming stream, but keep the outgoing.
            Incomingstream[Header.Socket].clear();
            Incomingstream[Header.Socket].shrink_to_fit();

            // Set the connection-state.
            Validconnection[Header.Socket] = false;
        }
        Threadguard.unlock();
    }

    // Returns false if the request could not be completed for any reason.
    virtual bool onReadrequest(const Requestheader_t &Header, void *Databuffer, uint32_t *Datasize)
    {
        // To support lingering sockets, we transmit data even if the socket is disconnected.
        if (0 == Outgoingstream[Header.Socket].size()) return false;

        // Verify the pointers, although they should always be valid.
        if (!Databuffer || !Datasize) return false;

        // Copy the stream to the buffer.
        Threadguard.lock();
        {
            // Validate the state, unlikely to change.
            if (0 != Outgoingstream[Header.Socket].size())
            {
                // Copy as much data as we can fit in the buffer.
                *Datasize = std::min(*Datasize, uint32_t(Outgoingstream[Header.Socket].size()));
                std::copy_n(Outgoingstream[Header.Socket].begin(), *Datasize, reinterpret_cast<char *>(Databuffer));
                Outgoingstream[Header.Socket].erase(Outgoingstream[Header.Socket].begin(), Outgoingstream[Header.Socket].begin() + *Datasize);
            }
        }
        Threadguard.unlock();
    }
    virtual bool onWriterequest(const Requestheader_t &Header, const void *Databuffer, const uint32_t Datasize)
    {
        // If there is no valid connection, we just ignore the data.
        if (Validconnection[Header.Socket] == false) return false;

        // Append the data to the stream and notify usercode.
        Threadguard.lock();
        {
            auto Pointer = reinterpret_cast<const uint8_t *>(Databuffer);
            std::copy_n(Pointer, Datasize, std::back_inserter(Incomingstream[Header.Socket]));
            onData(Header, Incomingstream[Header.Socket]);

            // Ensure that the mutex is locked as usercode is unpredictable.
            Threadguard.try_lock();
        }
        Threadguard.unlock();

        return true;
    }
};

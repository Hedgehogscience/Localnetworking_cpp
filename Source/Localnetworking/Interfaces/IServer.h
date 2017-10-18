/*
    Initial author: Convery (tcn@ayria.se)
    Started: 17-10-2017
    License: MIT
    Notes:
        The base servertype, doesn't handle IO directly.
*/

#pragma once
#include <cstdint>

// Universal representation of an address.
struct IPAddress_t
{
    uint16_t Port;
    char Plainaddress[65];
};

// Information about the socket-state.
struct Requestheader_t
{
    size_t Socket;
    IPAddress_t Client;
    IPAddress_t Server;
};

// The base servertype that all others will derive from.
struct IServer
{
    // Socket state update-notifications.
    virtual void onConnect(const Requestheader_t &Header) = 0;
    virtual void onDisconnect(const Requestheader_t &Header) = 0;

    // Returns false if the request could not be completed for any reason.
    virtual bool onReadrequest(const Requestheader_t &Header, void *Databuffer, uint32_t *Datasize) = 0;
    virtual bool onWriterequest(const Requestheader_t &Header, const void *Databuffer, const uint32_t Datasize) = 0;
};

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
struct IPAddress
{
    uint16_t Port;
    char Plainaddress[65];
};

// Abstraction of the socket-state.
struct Localsocket
{
    size_t Berkeley;
    IPAddress Client;
    IPAddress Server;
};

// The base servertype that all others will derive from.
struct IServer
{
    // Socket state update-notifications.
    virtual void onConnect(const Localsocket &Socket) = 0;
    virtual void onDisconnect(const Localsocket &Socket) = 0;

    // Returns false if the request could not be completed for any reason.
    virtual bool onReadrequest(const Localsocket &Socket, void *Databuffer, uint32_t *Datasize) = 0;
    virtual bool onWriterequest(const Localsocket &Socket, const void *Databuffer, const uint32_t Datasize) = 0;
};

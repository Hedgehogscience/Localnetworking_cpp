/*
    Initial author: Convery (tcn@ayria.se)
    Started: 17-10-2017
    License: MIT
    Notes:
        The base servertype, handles IO per socket.
*/

#pragma once
#include <cstdint>

// Core datatypes for client-server IO.
#pragma pack(1)
struct IPAddress
{
    uint16_t Port;
    uint16_t Addresslength;
    union
    {
        uint8_t IPv4[4];
        uint8_t IPv6[16];
    };
};
struct Localsocket
{
    size_t Berkeley;
    IPAddress Hostname;
};
#pragma pack()

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

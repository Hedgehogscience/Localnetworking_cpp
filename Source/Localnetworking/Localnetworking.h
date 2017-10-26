/*
    Initial author: Convery (tcn@ayria.se)
    Started: 18-10-2017
    License: MIT
    Notes:
        The internal networking system.
*/

#pragma once
#include "../Stdinclude.h"
#include "Interfaces/IServer.h"
#include "Interfaces/IStreamserver.h"
#include "Interfaces/IDatagramserver.h"

namespace Localnetworking
{
    // Create a new server instance.
    IServer *Createserver(std::string Hostname);

    // Modify a servers properties.
    void Addfilter(size_t Socket, IPAddress_t Filter);
    void Associatesocket(IServer *Server, size_t Socket);
    void Disassociatesocket(IServer *Server, size_t Socket);
    bool isAssociated(size_t Socket);

    // Query the servermaps.
    IServer *Findserver(size_t Socket);
    IServer *Findserver(std::string Hostname);
    size_t Findsocket(IPAddress_t Server, size_t Offset = 0);

    // Map packets to and from the internal lists.
    void Enqueueframe(IPAddress_t From, std::string &Packet);
    bool Dequeueframe(size_t Socket, IPAddress_t &From, std::string &Packet);
}

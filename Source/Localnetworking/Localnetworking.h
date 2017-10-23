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
    void Addfilter(IServer *Server, IPAddress_t Filter);
    void Associatesocket(IServer *Server, size_t Socket);

    // Query the servermaps.
    IServer *Findserver(size_t Socket);
    IServer *Findserver(std::string Hostname);
    IServer *Findserver(IPAddress_t Server, size_t Offset = 0);
}

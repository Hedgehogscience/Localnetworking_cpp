/*
    Initial author: Convery (tcn@ayria.se)
    Started: 09-01-2018
    License: MIT
    Notes:
        Provides offline networking between IServers.
*/

#pragma once
#include "../Stdinclude.hpp"
#include "Interfaces/IServer.hpp"
#include "Interfaces/IStreamserver.hpp"
#include "Interfaces/IDatagramserver.hpp"

namespace Localnetworking
{
    // Create a new instance of a server.
    IServer *Createserver(std::string_view Hostname);
    void Duplicateserver(std::string_view Hostname, IServer *Instance);

    // Find a server by criteria.
    IServer *Findserver(size_t Socket);
    IServer *Findserver(std::string_view Hostname);

    // Reverse lookup for debugging / information.
    std::string Findhostname(IServer *Server);

    // Load all server-modules from disk.
    void Loadallmodules();
}


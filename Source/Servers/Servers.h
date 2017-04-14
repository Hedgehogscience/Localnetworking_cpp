/*
    Initial author: Convery
    Started: 2017-4-13
    License: Apache 2.0
*/

#pragma once
#include "Interfaces/IServer.h"
#include "Interfaces/IStreamserver.h"
#include "Interfaces/IDatagramserver.h"

// Create a server based on the hostname, returns null if there's no handler.
IServer *Createserver(const size_t Socket, const char *Hostname);
IServer *Createserver(const char *Hostname);

// Find a server by criteria.
IServer *Findserver(const size_t Socket);
IServer *Findserver(const char *Address);

// Return all active sockets.
std::vector<size_t> Activesockets();

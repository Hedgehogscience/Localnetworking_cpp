/*
    Initial author: Convery (tcn@ayria.se)
    Started: 24-08-2017
    License: MIT
    Notes:
*/

#pragma once
#include "Interfaces/IServer.h"
#include "Interfaces/IStreamserver.h"
#include "Interfaces/IDatagramserver.h"

// Create a server based on the hostname, returns null if there's no handler.
IServer *Createserver(const size_t Socket, std::string Hostname);
IServer *Createserver(std::string Hostname);

// Find a server by criteria.
IServer *Findserver(const size_t Socket);
IServer *Findserver(std::string Address);

// Find the address associated with a server.
std::string Findaddress(const size_t Socket);
std::string Findaddress(const IServer *Server);

// Erase an entry from the list.
void Disconnectserver(const size_t Socket);
void Disconnectserver(const IServer *Server);

// Return all active sockets.
std::vector<size_t> Activesockets();

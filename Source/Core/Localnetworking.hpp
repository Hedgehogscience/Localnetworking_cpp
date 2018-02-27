/*
    Initial author: Convery (tcn@hedgehogscience.com)
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

    // Manage filters for packet-based IO.
    void Addfilter(size_t Socket, Address_t Filter);
    std::vector<Address_t> &Getfilters(size_t Socket);

    // Manage the internal sockets.
    bool isInternalsocket(size_t Socket);
    std::vector<size_t> Internalsockets();
    void Createsocket(IServer *Server, size_t Socket);
    void Destroysocket(IServer *Server, size_t Socket);
    size_t Findinternalsocket(Address_t Server, size_t Offset);

    // Map packets to and from the internal lists.
    void Enqueueframe(Address_t From, std::string &Packet);
    bool Dequeueframe(size_t Socket, Address_t &From, std::string &Packet);

    // Reverse lookup and debugging information.
    void Forceresolvehost(std::string IP, std::string Hostname);
    std::string Findhostname(IServer *Server);

    // Initialize the modules and datagram IO.
    void Startpollthread();
    void Loadallmodules();

    // Initialize the platform hooks.
    void Addplatform(std::function<void()> Callback);
    void Initializeplatforms();

    // Handle HTTP operations and pass it to POSIX.
    size_t HTTPCreaterequest();
    void HTTPSendrequest(size_t Handle);
    void HTTPDeleterequest(size_t Handle);
    uint16_t HTTPGetstatuscode(size_t Handle);
    size_t HTTPGetresponsedatasize(size_t Handle);
    std::string HTTPGetresponsedata(size_t Handle);
    void HTTPSenddata(size_t Handle, std::string Data);
    void HTTPSetmethod(size_t Handle, std::string Method);
    void HTTPSetuseragent(size_t Handle, std::string Agent);
    void HTTPSetresource(size_t Handle, std::string Resource);
    void HTTPSethostname(size_t Handle, std::string Hostname);
    void HTTPSetheader(size_t Handle, std::string Key, std::string Value);
}

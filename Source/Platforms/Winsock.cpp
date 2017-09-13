/*
    Initial author: Convery (tcn@ayria.se)
    Started: 24-08-2017
    License: MIT
    Notes:
        Interface shim for windows sockets.
*/

#include "../Stdinclude.h"

#if defined (_WIN32)

// Windows annoyance.
#if defined(min) || defined(max)
    #undef min
    #undef max
#endif

namespace Winsock
{
    #pragma region Hooking
    // Track all the hooks installed into ws2_32.dll by name.
    std::unordered_map<std::string, void *> WSHooks;

    // Macros to make calling WS a little easier.
    #define CALLWS(_Function, _Result, ...) {                           \
    auto Pointer = WSHooks[__func__];                                   \
    auto Hook = (Hooking::StomphookEx<decltype(_Function)> *)Pointer;   \
    Hook->Function.first.lock();                                        \
    Hook->Removehook();                                                 \
    *_Result = Hook->Function.second(__VA_ARGS__);                      \
    Hook->Reinstall();                                                  \
    Hook->Function.first.unlock(); }
    #define CALLWS_NORET(_Function, ...) {                              \
    auto Pointer = WSHooks[__func__];                                   \
    auto Hook = (Hooking::StomphookEx<decltype(_Function)> *)Pointer;   \
    Hook->Function.first.lock();                                        \
    Hook->Removehook();                                                 \
    Hook->Function.second(__VA_ARGS__);                                 \
    Hook->Reinstall();                                                  \
    Hook->Function.first.unlock(); }
    #pragma endregion

    #pragma region Helpers
    std::unordered_map<size_t /* Socket */, bool /* Blocking */> Shouldblock;
    std::unordered_map<size_t /* Socket */, std::string /* Hostinfo */> Hostinfo;
    std::string Plainaddress(const struct sockaddr *Sockaddr)
    {
        auto Address = std::make_unique<char[]>(INET6_ADDRSTRLEN);

        if (Sockaddr->sa_family == AF_INET6)
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)Sockaddr)->sin6_addr), Address.get(), INET6_ADDRSTRLEN);
        else
            inet_ntop(AF_INET, &(((struct sockaddr_in *)Sockaddr)->sin_addr), Address.get(), INET6_ADDRSTRLEN);

        return std::string(Address.get());
    }
    #pragma endregion

    #pragma region Shims
    int __stdcall Bind(size_t Socket, const struct sockaddr *Name, int Namelength)
    {
        int Result = 0;
        IServer *Server = Findserver(Socket);
        if (!Server) Server = Findserver(Plainaddress(Name));
        if (!Server) CALLWS(bind, &Result, Socket, Name, Namelength);

        return Result;
    }
    int __stdcall Connect(size_t Socket, const struct sockaddr *Name, int Namelength)
    {
        int Result = 0;
        short Port = 0;
        IServer *Server = Findserver(Socket);

        // Disconnect any existing server instance.
        if (Server && Server->Capabilities() & ISERVER_EXTENDED)
            (reinterpret_cast<IServerEx *>(Server))->onDisconnect(Socket);

        // Get the port the game wants to use.
        if (Name->sa_family == AF_INET6)
            Port = ntohs(((sockaddr_in6 *)Name)->sin6_port);
        else
            Port = ntohs(((sockaddr_in *)Name)->sin_port);

        // If there's no socket connected, try to create one by address.
        if (!Server) Server = Createserver(Socket, Plainaddress(Name));
        if (Server && Server->Capabilities() & ISERVER_EXTENDED)
        {
            (reinterpret_cast<IServerEx *>(Server))->onConnect(Socket, Port);
        }

        // Even if we handle the socket, we'll call connect to mark it as active.
        CALLWS(connect, &Result, Socket, Name, Namelength);

        // Debug information.
        Debugprint(va("%s to %s:%u", Server || 0 == Result ? "Connected" : "Failed to connect", Plainaddress(Name).c_str(), Port));
        return Server || 0 == Result ? 0 : -1;
    }
    int __stdcall IOControlsocket(size_t Socket, uint32_t Command, unsigned long *pArgument)
    {
        int Result = 0;
        const char *Readable = "UNKNOWN";

        switch (Command)
        {
            case FIONBIO:
            {
                Readable = "FIONBIO";
                Shouldblock[Socket] = *pArgument == 0;
                break;
            }
            case FIONREAD: Readable = "FIONREAD"; break;
            case FIOASYNC: Readable = "FIOASYNC"; break;
            case SIOCSHIWAT: Readable = "SIOCSHIWAT"; break;
            case SIOCGHIWAT: Readable = "SIOCGHIWAT"; break;
            case SIOCSLOWAT: Readable = "SIOCSLOWAT"; break;
            case SIOCGLOWAT: Readable = "SIOCGLOWAT"; break;
            case SIOCATMARK: Readable = "SIOCATMARK"; break;
        }

        // Debug information.
        Debugprint(va("Socket 0x%X modified %s", Socket, Readable));

        // Call the IOControl on the actual socket.
        CALLWS(ioctlsocket, &Result, Socket, Command, pArgument);
        return Result;
    }
    int __stdcall Receive(size_t Socket, char *Buffer, int Length, int Flags)
    {
        uint32_t Result = Length;
        IServer *Server = Findserver(Socket);
        if (!Server) CALLWS(recv, &Result, Socket, Buffer, Length, Flags);

        // While on a blocking server, poll the server every 10ms.
        if (Server)
        {
            if (Server->Capabilities() & ISERVER_EXTENDED)
            {
                IServerEx *ServerEx = reinterpret_cast<IServerEx *>(Server);

                while (false == ServerEx->onReadrequestEx(Socket, Buffer, &Result) && Shouldblock[Socket])
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    Result = Length;
                }
            }
            else
            {
                while (false == Server->onReadrequest(Buffer, &Result) && Shouldblock[Socket])
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    Result = Length;
                }
            }
        }

        if (Result == uint32_t(-1)) return -1;
        return std::min(Result, uint32_t(INT32_MAX));
    }
    int __stdcall Receivefrom(size_t Socket, char *Buffer, int Length, int Flags, struct sockaddr *From, int *Fromlength)
    {
        uint32_t Result = Length;
        IServer *Server = Findserver(Socket);
        if (!Server) CALLWS(recvfrom, &Result, Socket, Buffer, Length, Flags, From, Fromlength);

        // While on a blocking server, poll the server every 10ms.
        if (Server)
        {
            if (Server->Capabilities() & ISERVER_EXTENDED)
            {
                IServerEx *ServerEx = reinterpret_cast<IServerEx *>(Server);

                while (false == ServerEx->onReadrequestEx(Socket, Buffer, &Result) && Shouldblock[Socket])
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    Result = Length;
                }
            }
            else
            {
                while (false == Server->onReadrequest(Buffer, &Result) && Shouldblock[Socket])
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    Result = Length;
                }
            }

            // Return the host information.
            std::memcpy(From, Hostinfo[Socket].data(), Hostinfo[Socket].size());
            *Fromlength = int(Hostinfo[Socket].size());
        }

        if (Result == uint32_t(-1)) return -1;
        return std::min(Result, uint32_t(INT32_MAX));
    }
    int __stdcall Select(int fdsCount, fd_set *Readfds, fd_set *Writefds, fd_set *Exceptfds, timeval *Timeout)
    {
        int Result = 0;
        std::vector<size_t> Readsockets;
        std::vector<size_t> Writesockets;

        for (auto &Item : Activesockets())
        {
            if (Readfds)
            {
                if (FD_ISSET(Item, Readfds))
                {
                    Readsockets.push_back(Item);
                    FD_CLR(Item, Readfds);
                }
            }

            if (Writefds)
            {
                if (FD_ISSET(Item, Writefds))
                {
                    Writesockets.push_back(Item);
                    FD_CLR(Item, Writefds);
                }
            }

            if (Exceptfds)
            {
                if (FD_ISSET(Item, Exceptfds))
                {
                    FD_CLR(Item, Exceptfds);
                }
            }
        }

        if ((!Readfds || Readfds->fd_count == 0) && (!Writefds || Writefds->fd_count == 0))
        {
            Timeout->tv_sec = 0;
            Timeout->tv_usec = 0;
        }

        CALLWS(select, &Result, fdsCount, Readfds, Writefds, Exceptfds, Timeout);
        if (Result < 0) Result = 0;

        for (size_t i = 0; i < Readsockets.size(); i++)
        {
            if (Readfds)
            {
                FD_SET(Readsockets.at(i), Readfds);
                Result++;
            }
        }

        for (size_t i = 0; i < Writesockets.size(); i++)
        {
            if (Writefds)
            {
                FD_SET(Writesockets.at(i), Writefds);
                Result++;
            }
        }

        return Result;
    }
    int __stdcall Send(size_t Socket, const char *Buffer, int Length, int Flags)
    {
        IServer *Server = Findserver(Socket);
        if (!Server) CALLWS(send, &Length, Socket, Buffer, Length, Flags);

        if (Server)
        {
            if (Server->Capabilities() & ISERVER_EXTENDED)
            {
                IServerEx *ServerEx = reinterpret_cast<IServerEx *>(Server);

                if(false == ServerEx->onWriterequestEx(Socket, Buffer, Length))
                    return int(-1);
            }
            else
            {
                if(false == Server->onWriterequest(Buffer, Length))
                    return int(-1);
            }
        }

        // Debug information.
        Debugprint(va("Sending %i bytes to server %s", Length, Server ? Findaddress(Socket).c_str() : va("external (socket %X)", Socket).c_str()));
        return Length;
    }
    int __stdcall Sendto(size_t Socket, const char *Buffer, int Length, int Flags, const struct sockaddr *To, int Tolength)
    {
        IServer *Server = Findserver(Socket);
        if (!Server) Server = Findserver(Plainaddress(To));
        if (!Server) Server = Createserver(Socket, Plainaddress(To));
        if (!Server) CALLWS(sendto, &Length, Socket, Buffer, Length, Flags, To, Tolength);

        if (Server)
        {
            // Create the hostinfo for recvfrom.
            Hostinfo[Socket] = { (char *)To, size_t(Tolength) };

            if (Server->Capabilities() & ISERVER_EXTENDED)
            {
                IServerEx *ServerEx = reinterpret_cast<IServerEx *>(Server);

                if(false == ServerEx->onWriterequestEx(Socket, Buffer, Length))
                    return int(-1);
            }
            else
            {
                if(false == Server->onWriterequest(Buffer, Length))
                    return int(-1);
            }
        }

        // Debug information.
        Debugprint(va("Sending %i bytes to server %s:%u", Length, !Server ? Plainaddress(To).c_str() : "Internal", To->sa_family == AF_INET6 ? ntohs(((sockaddr_in6 *)To)->sin6_port) : ntohs(((sockaddr_in *)To)->sin_port)));
        return Length;
    }
    hostent *__stdcall Gethostbyname(const char *Hostname)
    {
        IServer *Server = Createserver(Hostname);
        if (!Server)
        {
            static hostent *Resolvedhost;
            CALLWS(gethostbyname, &Resolvedhost, Hostname);

            Debugprint(va("%s: \"%s\" -> %s", __func__, Hostname, Resolvedhost ? inet_ntoa(*(in_addr*)Resolvedhost->h_addr_list[0]) : "Could not resolve"));
            return Resolvedhost;
        }

        // Create a fake IP from the hostname.
        uint32_t IPv4 = Hash::FNV1a_32(Hostname);
        uint8_t *IP = (uint8_t *)&IPv4;

        // Create the address struct.
        in_addr *Localaddress = new in_addr();
        in_addr *LocalsocketAddresslist[2];
        Localaddress->S_un.S_addr = inet_addr(va("%u.%u.%u.%u", IP[0], IP[1], IP[2], IP[3]).c_str());
        LocalsocketAddresslist[0] = Localaddress;
        LocalsocketAddresslist[1] = nullptr;

        hostent *Localhost = new hostent();
        Localhost->h_aliases = NULL;
        Localhost->h_addrtype = AF_INET;
        Localhost->h_length = sizeof(in_addr);
        Localhost->h_name = const_cast<char *>(Hostname);
        Localhost->h_addr_list = (char **)LocalsocketAddresslist;

        Debugprint(va("%s: \"%s\" -> %s", __func__, Hostname, inet_ntoa(*(in_addr*)Localhost->h_addr_list[0])));
        return Localhost;
    }
    int __stdcall Getaddrinfo(const char *Nodename, const char *Servicename, ADDRINFOA *Hints, ADDRINFOA **Result)
    {
        int WSResult = 0;
        IServer *Server = Createserver(Nodename);

        // Resolve the hostname through winsock to allocate the result struct.
        if (Hints) Hints->ai_family = PF_INET;
        if (!Server) CALLWS(getaddrinfo, &WSResult, Nodename, Servicename, Hints, Result);

        // Modify the structure to match our serverinfo.
        if (Server)
        {
            // Resolve a known hostname if the previous call failed.
            if (0 != WSResult) CALLWS(getaddrinfo, &WSResult, "localhost", nullptr, Hints, Result);
            if (0 != WSResult) return WSResult;

            // Create a fake IP from the hostname.
            uint32_t IPv4 = Hash::FNV1a_32(Nodename);
            uint8_t *IP = (uint8_t *)&IPv4;

            // Set the IP for all records.
            for (ADDRINFOA *ptr = *Result; ptr != NULL; ptr = ptr->ai_next)
            {
                ((sockaddr_in *)ptr->ai_addr)->sin_addr.S_un.S_addr = inet_addr(va("%u.%u.%u.%u", IP[0], IP[1], IP[2], IP[3]).c_str());
            }
        }

        Debugprint(va("%s: \"%s\" -> %s", __func__, Nodename, inet_ntoa(((sockaddr_in *)(*Result)->ai_addr)->sin_addr)));
        return WSResult;
    }
    int __stdcall Getpeername(size_t Socket, struct sockaddr *Name, int *Namelength)
    {
        int Result = 0;
        IServer *Server = Findserver(Socket);
        if(!Server) CALLWS(getpeername, &Result, Socket, Name, Namelength);

        // For our servers we just return the IP.
        if (Server)
        {
            sockaddr_in *Localname = reinterpret_cast<sockaddr_in *>(Name);
            *Namelength = sizeof(sockaddr_in);

            Localname->sin_family = AF_INET;
            Localname->sin_port = 0;
            Localname->sin_addr.S_un.S_addr = inet_addr(Findaddress(Server).c_str());
        }

        return Result;
    }
    int __stdcall Getsockname(size_t Socket, struct sockaddr *Name, int *Namelength)
    {
        int Result = 0;
        IServer *Server = Findserver(Socket);
        if(!Server) CALLWS(getsockname, &Result, Socket, Name, Namelength);

        // For our servers we just return a fake IP.
        if (Server)
        {
            sockaddr_in *Localname = reinterpret_cast<sockaddr_in *>(Name);
            *Namelength = sizeof(sockaddr_in);
            uint8_t *IP = (uint8_t *)&Socket;

            Localname->sin_family = AF_INET;
            Localname->sin_port = 0;
            Localname->sin_addr.S_un.S_addr = inet_addr(va("%u.%u.%u.%u", 192, 168, IP[2], IP[3]).c_str());
        }

        return Result;
    }
    int __stdcall Closesocket(size_t Socket)
    {
        IServer *Server = Findserver(Socket);
        CALLWS_NORET(closesocket, Socket);
        Disconnectserver(Socket);

        if (Server && Server->Capabilities() & ISERVER_EXTENDED)
        {
            IServerEx *ServerEx = reinterpret_cast<IServerEx *>(Server);
            ServerEx->onDisconnect(Socket);
        }
        return 0;
    }
    int __stdcall Shutdown(size_t Socket, int How)
    {
        IServer *Server = Findserver(Socket);
        CALLWS_NORET(shutdown, Socket, How);

        if (SD_BOTH == How) Disconnectserver(Socket);
        if (Server && Server->Capabilities() & ISERVER_EXTENDED)
        {
            IServerEx *ServerEx = reinterpret_cast<IServerEx *>(Server);
            ServerEx->onDisconnect(Socket);
        }
        return 0;
    }

    /*
        TODO(Convery):
        Implement more shims as needed.
        The async ones can be interesting.
        Remember to add new ones to the installer!
    */
    #pragma endregion

    #pragma region Installer
    struct WSInstaller
    {
        WSInstaller()
        {
            // Another macro to save my fingers.
            #define INSTALL_HOOK(_Function, _Replacement) {                                                                                 \
            auto Address = (void *)GetProcAddress(GetModuleHandleA("wsock32.dll"), _Function);                                              \
            if(!Address) Address = (void *)GetProcAddress(GetModuleHandleA("WS2_32.dll"), _Function);                                       \
            if(Address) {                                                                                                                   \
            Winsock::WSHooks[#_Replacement] = new Hooking::StomphookEx<decltype(_Replacement)>();                                           \
            ((Hooking::StomphookEx<decltype(_Replacement)> *)Winsock::WSHooks[#_Replacement])->Installhook(Address, (void *)&_Replacement); \
            }}

            // Place the hooks directly in winsock.
            INSTALL_HOOK("bind", Bind);
            INSTALL_HOOK("connect", Connect);
            INSTALL_HOOK("ioctlsocket", IOControlsocket);
            INSTALL_HOOK("recv", Receive);
            INSTALL_HOOK("recvfrom", Receivefrom);
            INSTALL_HOOK("select", Select);
            INSTALL_HOOK("send", Send);
            INSTALL_HOOK("sendto", Sendto);
            INSTALL_HOOK("gethostbyname", Gethostbyname);
            INSTALL_HOOK("getaddrinfo", Getaddrinfo);
            INSTALL_HOOK("getpeername", Getpeername);
            INSTALL_HOOK("getsockname", Getsockname);
            INSTALL_HOOK("closesocket", Closesocket);
            INSTALL_HOOK("shutdown", Shutdown);
        }
    };

    // Install the hooks on startup.
    static WSInstaller Installer{};
    #pragma endregion
}

#undef INSTALL_HOOK
#undef CALLWS_NORET
#undef CALLWS
#endif

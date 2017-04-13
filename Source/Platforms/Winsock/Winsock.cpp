/*
    Initial author: Convery
    Started: 2017-4-13
    License: Apache 2.0
*/

#include "../../StdInclude.h"
#include "../../Servers/Servers.h"

#ifdef _WIN32
#include <Windows.h>
#include <WinSock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#undef min
#undef max

namespace Winsock
{
    // The hooks installed in ws2_32.dll
    std::unordered_map<std::string, void *> WSHooks;
    #define CALLWS(_Function, _Result, ...) {                           \
    auto Pointer = WSHooks[__FUNCTION__];                               \
    auto Hook = (Hooking::StomphookEx<decltype(_Function)> *)Pointer;   \
    Hook->Removehook();                                                 \
    *_Result = Hook->Function(__VA_ARGS__);                             \
    Hook->Reinstall(); }
    #define CALLWS_NORET(_Function, ...) {                              \
    auto Pointer = WSHooks[__FUNCTION__];                               \
    auto Hook = (Hooking::StomphookEx<decltype(_Function)> *)Pointer;   \
    Hook->Removehook();                                                 \
    Hook->Function(__VA_ARGS__);                                        \
    Hook->Reinstall(); }

    // Helpers.
    std::unordered_map<size_t /* Socket */, bool /* Blocking */> Shouldblock;
    std::unordered_map<size_t /* Socket */, std::string /* Hostinfo */> Hostinfo;
    std::unordered_map<size_t /* Socket */, IServer * /* Serverinfo */> Serverinfo;
    const char *Plainaddress(const struct sockaddr *Sockaddr)
    {
        auto Address = std::make_unique<char[]>(INET6_ADDRSTRLEN);

        if (Sockaddr->sa_family == AF_INET6)
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)Sockaddr)->sin6_addr), Address.get(), INET6_ADDRSTRLEN);
        else
            inet_ntop(AF_INET, &(((struct sockaddr_in *)Sockaddr)->sin_addr), Address.get(), INET6_ADDRSTRLEN);

        return Address.get();
    }

    // Winsock replacements.
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
        IServer *Server = Findserver(Socket);

        // Disconnect any existing server instance.
        if (Server && Server->Capabilities() & ISERVER_EXTENDED)
            ((IServerEx *)Server)->onDisconnect(Socket);

        // If there's no socket connected, try to create one by address.
        if (!Server) Server = Createserver(Socket, Plainaddress(Name));
        if (!Server) CALLWS(connect, &Result, Socket, Name, Namelength);
        if (Server && Server->Capabilities() & ISERVER_EXTENDED)
        {
            Serverinfo[Socket] = Server;

            if (Name->sa_family == AF_INET6)
                ((IServerEx *)Server)->onConnect(Socket, ntohs(((sockaddr_in6 *)Name)->sin6_port));
            else
                ((IServerEx *)Server)->onConnect(Socket, ntohs(((sockaddr_in *)Name)->sin_port));
        }

        // Debug information.
        DebugPrint(va("Connected to %s", Plainaddress(Name)).c_str());
        return Result;
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
        DebugPrint(va("Socket 0x%X modified %s", Socket, Readable).c_str());
        
        CALLWS(ioctlsocket, &Result, Socket, Command, pArgument);
        return Result;
    }
    int __stdcall Receive(size_t Socket, char *Buffer, int Length, int Flags)
    {
        uint32_t Result = 0;
        IServer *Server = Findserver(Socket);
        if (!Server) CALLWS(recv, &Result, Socket, Buffer, Length, Flags);

        // While on a blocking server, poll the server every 10ms.
        if (Server)
        {
            if (Server->Capabilities() & ISERVER_EXTENDED)
            {
                IServerEx *ServerEx = reinterpret_cast<IServerEx *>(Server);

                while (false == ServerEx->onReadrequestEx(Socket, Buffer, &Result) && Shouldblock[Socket])
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            else
            {
                while (false == Server->onReadrequest(Buffer, &Result) && Shouldblock[Socket])
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        return std::min(Result, uint32_t(INT32_MAX));
    }
    int __stdcall Receivefrom(size_t Socket, char *Buffer, int Length, int Flags, struct sockaddr *From, int *Fromlength)
    {
        uint32_t Result = 0;
        IServer *Server = Findserver(Socket);
        if (!Server) CALLWS(recvfrom, &Result, Socket, Buffer, Length, Flags, From, Fromlength);

        // While on a blocking server, poll the server every 10ms.
        if (Server)
        {
            if (Server->Capabilities() & ISERVER_EXTENDED)
            {
                IServerEx *ServerEx = reinterpret_cast<IServerEx *>(Server);

                while (false == ServerEx->onReadrequestEx(Socket, Buffer, &Result) && Shouldblock[Socket])
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            else
            {
                while (false == Server->onReadrequest(Buffer, &Result) && Shouldblock[Socket])
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            // Return the host information.
            std::memcpy(From, Hostinfo[Socket].data(), Hostinfo[Socket].size());
            *Fromlength = int(Hostinfo[Socket].size());
        }

        return std::min(Result, uint32_t(INT32_MAX));
    }
    int __stdcall Select(int fdsCount, fd_set *Readfds, fd_set *Writefds, fd_set *Exceptfds, timeval *Timeout)
    {
        int Result = 0;
        std::vector<size_t> Readsockets;
        std::vector<size_t> Writesockets;

        for (auto &Item : Serverinfo)
        {
            if (Readfds)
            {
                if (FD_ISSET(Item.first, Readfds))
                {
                    Readsockets.push_back(Item.first);
                    FD_CLR(Item.first, Readfds);
                }
            }

            if (Writefds)
            {
                if (FD_ISSET(Item.first, Writefds))
                {
                    Writesockets.push_back(Item.first);
                    FD_CLR(Item.first, Writefds);
                }
            }

            if (Exceptfds)
            {
                if (FD_ISSET(Item.first, Exceptfds))
                {
                    FD_CLR(Item.first, Exceptfds);
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

        for (int i = 0; i < Readsockets.size(); i++)
        {
            if (Serverinfo[Readsockets[i]] && Readfds)
            {
                FD_SET(Readsockets.at(i), Readfds);
                Result++;
            }
        }

        for (int i = 0; i < Writesockets.size(); i++)
        {
            if (Writefds)
            {
                FD_SET(Writesockets.at(i), Writefds);
                Result++;
            }
        }

        return Result;
    }

    // TODO(Convery): Implement all WS functions.
}

// Initialize winsock hooks on startup.
namespace
{
    struct WSLoader
    {
        WSLoader()
        {
            #define INSTALL_HOOK(_Function, _Replacement) {                                                                                 \
            auto Address = (void *)GetProcAddress(GetModuleHandleA("wsock32.dll"), _Function);                                              \
            if(!Address) Address = (void *)GetProcAddress(GetModuleHandleA("WS2_32.dll"), _Function);                                       \
            if(Address) {                                                                                                                   \
            Winsock::WSHooks[#_Replacement] = new Hooking::StomphookEx<decltype(_Replacement)>();                                           \
            ((Hooking::StomphookEx<decltype(_Replacement)> *)Winsock::WSHooks[#_Replacement])->Installhook(Address, (void *)&_Replacement); \
            }}

            // Winsock hooks.
            INSTALL_HOOK("bind", Winsock::Bind);
            INSTALL_HOOK("connect", Winsock::Connect);
            INSTALL_HOOK("ioctlsocket", Winsock::IOControlsocket);
            INSTALL_HOOK("recv", Winsock::Receive);
            INSTALL_HOOK("recvfrom", Winsock::Receivefrom);
            INSTALL_HOOK("select", Winsock::Select);

            // TODO(Convery): Hook all WS functions.
        }
    };
    WSLoader Loader{};
}
#endif

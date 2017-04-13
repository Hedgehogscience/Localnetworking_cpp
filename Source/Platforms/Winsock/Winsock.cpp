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
    const char *Plaintext(const struct sockaddr *Sockaddr)
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
        if (!Server) Server = Findserver(Plaintext(Name));
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
        if (!Server) Server = Createserver(Socket, Plaintext(Name));
        if (!Server) CALLWS(connect, &Result, Socket, Name, Namelength);
        if (Server && Server->Capabilities() & ISERVER_EXTENDED)
        {
            if (Name->sa_family == AF_INET6)
                ((IServerEx *)Server)->onConnect(Socket, ntohs(((sockaddr_in6 *)Name)->sin6_port));
            else
                ((IServerEx *)Server)->onConnect(Socket, ntohs(((sockaddr_in *)Name)->sin_port));
        }

        // Debug information.
        DebugPrint(va("Connected to %s", Plaintext(Name)).c_str());
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

            // TODO(Convery): Hook all WS functions.
        }
    };
    WSLoader Loader{};
}
#endif

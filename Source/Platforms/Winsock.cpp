/*
    Initial author: Convery (tcn@ayria.se)
    Started: 09-01-2018
    License: MIT
    Notes:
        Provides an interface-shim for Windows sockets.
*/

#if defined(_WIN32)
#include "../Stdinclude.hpp"
#include <WinSock2.h>
#include <Ws2tcpip.h>

namespace Winsock
{
#pragma region Hooking
    // Track all the hooks installed into WS2_32 and wsock32 by name.
    std::unordered_map<std::string, void *> WSHooks1;
    std::unordered_map<std::string, void *> WSHooks2;

    // Save the state of WSAErrors.
    uint32_t Lasterror;

    // Macros to make calling WS a little easier.
#define CALLWS(_Function, _Result, ...) {                           \
    auto Pointer = WSHooks1[__func__];                                  \
    if(!Pointer) Pointer = WSHooks2[__func__];                          \
    auto Hook = (Hooking::StomphookEx<decltype(_Function)> *)Pointer;   \
    Hook->Function.first.lock();                                        \
    Hook->Removehook();                                                 \
    *_Result = Hook->Function.second(__VA_ARGS__);                      \
    Lasterror = WSAGetLastError();                                      \
    Hook->Reinstall();                                                  \
    Hook->Function.first.unlock(); }
#define CALLWS_NORET(_Function, ...) {                              \
    auto Pointer = WSHooks1[__func__];                                  \
    if(!Pointer) Pointer = WSHooks2[__func__];                          \
    auto Hook = (Hooking::StomphookEx<decltype(_Function)> *)Pointer;   \
    Hook->Function.first.lock();                                        \
    Hook->Removehook();                                                 \
    Hook->Function.second(__VA_ARGS__);                                 \
    Lasterror = WSAGetLastError();                                      \
    Hook->Reinstall();                                                  \
    Hook->Function.first.unlock(); }
#pragma endregion

#pragma region Helpers
    std::unordered_map<size_t /* Socket */, bool> Blockingsockets;

    std::string Plainaddress(const struct sockaddr *Sockaddr)
    {
        auto Address = std::make_unique<char[]>(INET6_ADDRSTRLEN);

        if (Sockaddr->sa_family == AF_INET6)
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)Sockaddr)->sin6_addr), Address.get(), INET6_ADDRSTRLEN);
        else
            inet_ntop(AF_INET, &(((struct sockaddr_in *)Sockaddr)->sin_addr), Address.get(), INET6_ADDRSTRLEN);

        return std::string(Address.get());
    }
    uint16_t WSPort(const struct sockaddr *Sockaddr)
    {
        if (Sockaddr->sa_family == AF_INET6) return ntohs(((struct sockaddr_in6 *)Sockaddr)->sin6_port);
        else return ntohs(((struct sockaddr_in *)Sockaddr)->sin_port);
    }
    Address_t Localaddress(const struct sockaddr *Sockaddr)
    {
        Address_t Result{};
        Result.Port = WSPort(Sockaddr);
        auto Address = Plainaddress(Sockaddr);
        std::memcpy(Result.Plainaddress, Address.c_str(), Address.size());

        return Result;
    }
#pragma endregion

#pragma region Shims
    int __stdcall Bind(size_t Socket, const struct sockaddr *Name, int Namelength)
    {
        int Result = 0;

        // Create a server if needed.
        auto Server = Localnetworking::Findserver(Plainaddress(Name));
        if (!Server) Server = Localnetworking::Createserver(Plainaddress(Name));
        if (!Server) CALLWS(bind, &Result, Socket, Name, Namelength);
        if (Server) Localnetworking::Createsocket(Server, Socket);
        Localnetworking::Addfilter(Socket, Localaddress(Name));

        Debugprint(va("Listening on port %u", WSPort(Name)));
        if (Result == -1) WSASetLastError(Lasterror);
        return Result;
    }
    int __stdcall Connect(size_t Socket, const struct sockaddr *Name, int Namelength)
    {
        int Result = 0;

        // Check if we have any server with this socket and disconnect it.
        auto Server = Localnetworking::Findserver(Socket);
        if (Server)
        {
            Server->onDisconnect(Socket);
            Localnetworking::Destroysocket(Server, Socket);
        }

        // Create a new server instance from the hostname, even if it's the same host.
        Server = Localnetworking::Createserver(Plainaddress(Name));
        if (Server) Localnetworking::Createsocket(Server, Socket);
        if (Server) Server->onConnect(Socket, WSPort(Name));

        // Ask Windows to connect the socket if there's no server.
        if (!Server) CALLWS(connect, &Result, Socket, Name, Namelength);

        // Debug information.
        Debugprint(va("%s to %s:%u", Server || 0 == Result ? "Connected" : "Failed to connect", Plainaddress(Name).c_str(), WSPort(Name)));
        if (Result == -1) WSASetLastError(Lasterror);
        return Server || 0 == Result ? 0 : -1;
    }
    int __stdcall IOControlsocket(size_t Socket, uint32_t Command, unsigned long *Argument)
    {
        int Result = 0;
        const char *Readable = "UNKNOWN";

        // TODO(Convery): Implement more socket options here.
        switch (Command)
        {
        case FIONBIO:
        {
            Readable = "FIONBIO";
            Blockingsockets[Socket] = *Argument == 0;
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
        CALLWS(ioctlsocket, &Result, Socket, Command, Argument);
        if (Result == -1) WSASetLastError(Lasterror);
        return Result;
    }
    int __stdcall Receive(size_t Socket, char *Buffer, int Length, int Flags)
    {
        bool Successful = false;
        uint32_t Result = Length;

        // Pointer checking because few professional game-developers know their shit.
        if (!Buffer)
        {
            WSASetLastError(EFAULT);
            return -1;
        }

        // Find a server associated with this socket and poll.
        auto Server = Localnetworking::Findserver(Socket);
        if (Server)
        {
            // Notify the developer that they'll have to deal with this.
            if (Flags)
            {
                static bool Hasprinted = false;
                if (!Hasprinted)
                {
                    Hasprinted = true;
                    Infoprint(va("\n%s\n%s\n%s\n%s\n%s",
                        "##############################################################",
                        "The current application is using special flags for Receive.",
                        "Feel free to implement that in Localnetworking/Winsock.cpp.",
                        "Or just hack it into your module.",
                        "##############################################################"));
                }
            }

            // If we are on a blocking socket, poll until successful.
            do
            {
                Successful = Server->onStreamread(Socket, Buffer, &Result);
                if (!Successful) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } while (!Successful && Blockingsockets[Socket]);

            // Ensure that any errors are non-fatal.
            if (!Successful) WSASetLastError(WSAEWOULDBLOCK);
        }

        // Ask Windows to fetch some data from the socket if it's not ours.
        if (!Server) CALLWS(recv, &Result, Socket, Buffer, Length, Flags);
        if (!Server) if (Result == -1) WSASetLastError(Lasterror);

        // Return the length or error.
        if (Server && !Successful) return -1;
        if (Result == uint32_t(-1)) return -1;
        return std::min(Result, uint32_t(INT32_MAX));
    }
    int __stdcall Receivefrom(size_t Socket, char *Buffer, int Length, int Flags, struct sockaddr *From, int *Fromlength)
    {
        uint32_t Result;

        // Pointer checking because few professional game-developers know their shit.
        if (!Buffer)
        {
            WSASetLastError(EFAULT);
            return -1;
        }

        // Check if it's a socket associated with our network.
        if (Localnetworking::isInternalsocket(Socket))
        {
            Address_t Localfrom; std::string Packet;

            // Check if there's any data on the socket and return that.
            do
            {
                if (Localnetworking::Dequeueframe(Socket, Localfrom, Packet))
                {
                    // Notify the developer that they'll have to deal with this.
                    if (Flags)
                    {
                        static bool Hasprinted = false;
                        if (!Hasprinted)
                        {
                            Hasprinted = true;
                            Infoprint(va("\n%s\n%s\n%s\n%s\n%s",
                                "##############################################################",
                                "The current application is using special flags for Receivefrom.",
                                "Feel free to implement that in Localnetworking/Winsock.cpp.",
                                "Or just hack it into your module.",
                                "##############################################################"));
                        }
                    }

                    // Copy the sender information.
                    if (From && Fromlength)
                    {
                        if (*Fromlength == sizeof(sockaddr_in6))
                        {
                            From->sa_family = AF_INET6;
                            ((struct sockaddr_in6 *)From)->sin6_port = htons(Localfrom.Port);
                            inet_pton(From->sa_family, Localfrom.Plainaddress, &((struct sockaddr_in6 *)From)->sin6_addr);
                        }
                        else
                        {
                            From->sa_family = AF_INET;
                            ((struct sockaddr_in *)From)->sin_port = htons(Localfrom.Port);
                            inet_pton(From->sa_family, Localfrom.Plainaddress, &((struct sockaddr_in *)From)->sin_addr);
                        }
                    }

                    // Copy the data to the buffer and return how much was copied.
                    std::memcpy(Buffer, Packet.data(), std::min(size_t(Length), Packet.size()));
                    return std::min(uint32_t(std::min(size_t(Length), Packet.size())), uint32_t(INT32_MAX));
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } while (Blockingsockets[Socket]);

            // Send an error if there's no data.
            WSASetLastError(WSAEWOULDBLOCK);
            return -1;
        }

        // Ask Windows to fetch some data from the socket if it's not managed by us.
        CALLWS(recvfrom, &Result, Socket, Buffer, Length, Flags, From, Fromlength);
        if (Result == uint32_t(-1)) WSASetLastError(Lasterror);
        if (Result == uint32_t(-1)) return -1;
        return std::min(Result, uint32_t(INT32_MAX));
    }
    int __stdcall Select(int fdsCount, fd_set *Readfds, fd_set *Writefds, fd_set *Exceptfds, timeval *Timeout)
    {
        int Result = 0;
        std::vector<size_t> Readsockets;
        std::vector<size_t> Writesockets;

        for (auto &Item : Localnetworking::Internalsockets())
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
        bool Successful = false;
        uint32_t Result = Length;

        // Pointer checking because few professional game-developers know their shit.
        if (!Buffer)
        {
            WSASetLastError(EFAULT);
            return -1;
        }

        // Find a server associated with this socket and send.
        auto Server = Localnetworking::Findserver(Socket);
        if (Server)
        {
            // Notify the developer that they'll have to deal with this.
            if (Flags)
            {
                static bool Hasprinted = false;
                if (!Hasprinted)
                {
                    Hasprinted = true;
                    Infoprint(va("\n%s\n%s\n%s\n%s\n%s",
                        "##############################################################",
                        "The current application is using special flags for Send.",
                        "Feel free to implement that in Localnetworking/Winsock.cpp.",
                        "Or just hack it into your module.",
                        "##############################################################"));
                }
            }

            // If we are on a blocking socket, poll until successful.
            do
            {
                Successful = Server->onStreamwrite(Socket, Buffer, Result);
                if (!Successful) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } while (!Successful && Blockingsockets[Socket]);
        }

        // Ask Windows to send the data from the socket if it's not ours.
        if (!Server) CALLWS(send, &Result, Socket, Buffer, Length, Flags);
        if (!Server) if (Result == -1) WSASetLastError(Lasterror);

        // Return the length or error.
        if (Server && !Successful) return -1;
        if (Result == uint32_t(-1)) return -1;
        return std::min(Result, uint32_t(INT32_MAX));
    }
    int __stdcall Sendto(size_t Socket, const char *Buffer, int Length, int Flags, const struct sockaddr *To, int Tolength)
    {
        bool Successful = false;
        uint32_t Result = Length;

        // Pointer checking because few professional game-developers know their shit.
        if (!Buffer || !To)
        {
            WSASetLastError(EFAULT);
            return -1;
        }

        // Find a server associated with this socket or address.
        auto Server = Localnetworking::Findserver(Plainaddress(To));
        if (!Server) Server = Localnetworking::Findserver(Socket);
        if (Server)
        {
            // Notify the developer that they'll have to deal with this.
            if (Flags)
            {
                static bool Hasprinted = false;
                if (!Hasprinted)
                {
                    Hasprinted = true;
                    Infoprint(va("\n%s\n%s\n%s\n%s\n%s",
                        "##############################################################",
                        "The current application is using special flags for Sendto.",
                        "Feel free to implement that in Localnetworking/Winsock.cpp.",
                        "Or just hack it into your module.",
                        "##############################################################"));
                }
            }

            // Associate the socket if we haven't.
            Localnetworking::Createsocket(Server, Socket);
            Localnetworking::Addfilter(Socket, Localaddress(To));

            // Convert to the universal representation.
            auto Address = Localaddress(To);

            // If we are on a blocking socket, poll until successful.
            do
            {
                Successful = Server->onPacketwrite(Address, Buffer, Result);
                if (!Successful) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } while (!Successful && Blockingsockets[Socket]);
        }

        // Ask Windows to send the data from the socket if it's not ours.
        if (!Server) CALLWS(sendto, &Result, Socket, Buffer, Length, Flags, To, Tolength);
        if (!Server) if (Result == -1) WSASetLastError(Lasterror);

        // Return the length or error.
        if (Server && !Successful) return -1;
        if (Result == uint32_t(-1)) return -1;
        return std::min(Result, uint32_t(INT32_MAX));
    }

    hostent *__stdcall Gethostbyname(const char *Hostname)
    {
        // Create a server from the hostname, or ask Windows for it.
        auto Server = Localnetworking::Createserver(Hostname);
        if (!Server) 
        {
            static hostent *Resolvedhost;
            CALLWS(gethostbyname, &Resolvedhost, Hostname);

            Debugprint(va("%s: \"%s\" -> %s", __func__, Hostname, Resolvedhost ? inet_ntoa(*(in_addr*)Resolvedhost->h_addr_list[0]) : "Could not resolve"));
            if (!Resolvedhost) WSASetLastError(Lasterror);
            return Resolvedhost;
        }

        // Create a fake IP address from the hostname.
        uint32_t IPHash = Hash::FNV1a_32(Hostname);
        uint8_t *IP = (uint8_t *)&IPHash;

        // Associate the server instance with the fake IP address we created.
        auto ReadableIP = va("%u.%u.%u.%u", IP[0], IP[1], IP[2], IP[3]);
        Localnetworking::Forceresolvehost(ReadableIP, Hostname);
        Localnetworking::Duplicateserver(ReadableIP, Server);

        // Create the Winsock address struct.
        auto Localaddress = new in_addr();
        auto LocalsocketAddresslist = new in_addr*[2]();
        Localaddress->S_un.S_addr = inet_addr(ReadableIP.c_str());
        LocalsocketAddresslist[0] = Localaddress;
        LocalsocketAddresslist[1] = nullptr;

        // Create the Winsock hostentry struct.
        hostent *Localhost = new hostent();
        Localhost->h_aliases = NULL;
        Localhost->h_addrtype = AF_INET;
        Localhost->h_length = sizeof(in_addr);
        Localhost->h_name = const_cast<char *>(Hostname);
        Localhost->h_addr_list = (char **)LocalsocketAddresslist;

        // Notify the developer about this event.
        Debugprint(va("%s: \"%s\" -> %s", __func__, Hostname, inet_ntoa(*(in_addr*)Localhost->h_addr_list[0])));
        return Localhost;
    }
    int __stdcall Getaddrinfo(const char *Nodename, const char *Servicename, ADDRINFOA *Hints, ADDRINFOA **Result)
    {
        int WSResult = 0;

        // Create a server from the hostname, or ask Windows for it.
        auto Server = Localnetworking::Createserver(Nodename);

        // Resolve the hostname through Winsock to allocate the result struct.
        if (Hints) Hints->ai_family = PF_INET;
        CALLWS(getaddrinfo, &WSResult, Nodename, Servicename, Hints, Result);

        // Modify the allocated structure to match our server.
        if (Server)
        {
            // Resolve a known host if the previous call failed.
            if (0 != WSResult) CALLWS(getaddrinfo, &WSResult, "localhost", Servicename, Hints, Result);
            if (0 != WSResult) return WSResult;

            // Create a fake IP address from the hostname.
            uint32_t IPHash = Hash::FNV1a_32(Nodename);
            uint8_t *IP = (uint8_t *)&IPHash;

            // Associate the server instance with the fake IP address we created.
            auto ReadableIP = va("%u.%u.%u.%u", IP[0], IP[1], IP[2], IP[3]);
            Localnetworking::Forceresolvehost(ReadableIP, Nodename);
            Localnetworking::Duplicateserver(ReadableIP, Server);

            // Set the IP for all records.
            for (ADDRINFOA *ptr = *Result; ptr != NULL; ptr = ptr->ai_next)
            {
                ((sockaddr_in *)ptr->ai_addr)->sin_addr.S_un.S_addr = inet_addr(ReadableIP.c_str());
            }
        }

        // Notify the developer about this event.
        Debugprint(va("%s: \"%s\" -> %s", __func__, Nodename, WSResult != 0 ? "Error" : inet_ntoa(((sockaddr_in *)(*Result)->ai_addr)->sin_addr)));
        if (WSResult == -1) WSASetLastError(Lasterror);
        return WSResult;
    }
    int __stdcall GetaddrinfoW(const wchar_t *Nodename, const wchar_t *Servicename, ADDRINFOW *Hints, ADDRINFOW **Result)
    {
        int WSResult = 0;

        std::wstring Temp = Nodename;
        std::string Hostname = { Temp.begin(), Temp.end() };

        // Create a server from the hostname, or ask Windows for it.
        auto Server = Localnetworking::Createserver(Hostname);

        // Resolve the hostname through Winsock to allocate the result struct.
        if (Hints) Hints->ai_family = PF_INET;
        CALLWS(GetAddrInfoW, &WSResult, Nodename, Servicename, Hints, Result);

        // Modify the allocated structure to match our server.
        if (Server)
        {
            // Resolve a known host if the previous call failed.
            if (0 != WSResult) CALLWS(GetAddrInfoW, &WSResult, L"localhost", Servicename, Hints, Result);
            if (0 != WSResult) return WSResult;

            // Create a fake IP address from the hostname.
            uint32_t IPHash = Hash::FNV1a_32(Hostname.c_str());
            uint8_t *IP = (uint8_t *)&IPHash;

            // Associate the server instance with the fake IP address we created.
            auto ReadableIP = va("%u.%u.%u.%u", IP[0], IP[1], IP[2], IP[3]);
            Localnetworking::Forceresolvehost(ReadableIP, Hostname);
            Localnetworking::Duplicateserver(ReadableIP, Server);

            // Set the IP for all records.
            for (ADDRINFOW *ptr = *Result; ptr != NULL; ptr = ptr->ai_next)
            {
                ((sockaddr_in *)ptr->ai_addr)->sin_addr.S_un.S_addr = inet_addr(ReadableIP.c_str());
            }
        }

        // Notify the developer about this event.
        Debugprint(va("%s: \"%s\" -> %s", __func__, Hostname.c_str(), WSResult != 0 ? "Error" : inet_ntoa(((sockaddr_in *)(*Result)->ai_addr)->sin_addr)));
        if (WSResult == -1) WSASetLastError(Lasterror);
        return WSResult;
    }
    int __stdcall Getpeername(size_t Socket, struct sockaddr *Name, int *Namelength)
    {
        int Result = 0;

        // Find a server associated with this socket.
        auto Server = Localnetworking::Findserver(Socket);
        if (!Server) CALLWS(getpeername, &Result, Socket, Name, Namelength);
        if (Server)
        {
            // Create a fake address.
            sockaddr_in *Localname = reinterpret_cast<sockaddr_in *>(Name);
            *Namelength = sizeof(sockaddr_in);

            auto Hostname = Localnetworking::Findhostname(Server);
            auto Address = inet_addr(Hostname.c_str());
            if (!Address) Address = Hash::FNV1a_32(Hostname.c_str());

            Localname->sin_port = 0;
            Localname->sin_family = AF_INET;
            Localname->sin_addr.S_un.S_addr = Address;
        }

        if (!Server && Result == -1) WSASetLastError(Lasterror);
        return Result;
    }
    int __stdcall Getsockname(size_t Socket, struct sockaddr *Name, int *Namelength)
    {
        int Result = 0;

        // Find a server associated with this socket.
        auto Server = Localnetworking::Findserver(Socket);
        if (!Server) CALLWS(getsockname, &Result, Socket, Name, Namelength);
        if (Server)
        {
            // Create a fake address.
            sockaddr_in *Localname = reinterpret_cast<sockaddr_in *>(Name);
            *Namelength = sizeof(sockaddr_in);

            auto Hostname = Localnetworking::Findhostname(Server);
            auto Address = inet_addr(Hostname.c_str());
            if (!Address) Address = Hash::FNV1a_32(Hostname.c_str());

            Localname->sin_port = 0;
            Localname->sin_family = AF_INET;
            Localname->sin_addr.S_un.S_addr = Address;
        }

        if(!Server && Result == -1) WSASetLastError(Lasterror);
        return Result;
    }
    int __stdcall Closesocket(size_t Socket)
    {
        // Find a server associated with this socket and disconnect it.
        auto Server = Localnetworking::Findserver(Socket);
        if (Server) Server->onDisconnect(Socket);
        CALLWS_NORET(closesocket, Socket);

        return 0;
    }
    int __stdcall Shutdown(size_t Socket, int How)
    {
        // Find a server associated with this socket and disconnect it.
        auto Server = Localnetworking::Findserver(Socket);
        if (Server) Server->onDisconnect(Socket);
        CALLWS_NORET(shutdown, Socket, How);

        return 0;
    }
    hostent *__stdcall Gethostbyaddr(const char *Address, int Addresslength, int Addresstype)
    {
        sockaddr Localaddress;
        Localaddress.sa_family = uint16_t(Addresstype);
        std::memcpy(Localaddress.sa_data, Address, Addresslength);

        return Gethostbyname(Plainaddress(&Localaddress).c_str());
    }

    /*
        TODO(Convery):
        Implement more shims as needed.
        The async ones can be interesting.
        Remember to add new ones to the installer!
    */
#pragma endregion

#pragma region Installer
    void WSInstaller()
    {
        // Helper-macro to save the developers fingers.
        #define INSTALL_HOOK(_Function, _Replacement) {                                                                                     \
        auto Address = (void *)GetProcAddress(GetModuleHandleA("wsock32.dll"), _Function);                                                  \
        if(Address) {                                                                                                                       \
        Winsock::WSHooks1[#_Replacement] = new Hooking::StomphookEx<decltype(_Replacement)>();                                              \
        ((Hooking::StomphookEx<decltype(_Replacement)> *)Winsock::WSHooks1[#_Replacement])->Installhook(Address, (void *)&_Replacement);}   \
        Address = (void *)GetProcAddress(GetModuleHandleA("WS2_32.dll"), _Function);                                                        \
        if(Address) {                                                                                                                       \
        Winsock::WSHooks2[#_Replacement] = new Hooking::StomphookEx<decltype(_Replacement)>();                                              \
        ((Hooking::StomphookEx<decltype(_Replacement)> *)Winsock::WSHooks2[#_Replacement])->Installhook(Address, (void *)&_Replacement);}   \
        }                                                                                                                                   \

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
        INSTALL_HOOK("gethostbyaddr", Gethostbyaddr);
        INSTALL_HOOK("GetAddrInfoW", GetaddrinfoW);
    };

    // Add the installer on startup.
    struct Installer { Installer() { Localnetworking::Addplatform(WSInstaller); }; };
    static Installer Startup{};
#pragma endregion
}

#undef INSTALL_HOOK
#undef CALLWS_NORET
#undef CALLWS
#endif

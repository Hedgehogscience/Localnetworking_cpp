/*
    Initial author: Convery (tcn@ayria.se)
    Started: 26-10-2017
    License: MIT
    Notes:
        Provides an interface-shim for Windows sockets.
*/

// Naturally this file is for Windows only.
#if defined (_WIN32)
#include "../Stdinclude.h"

// Remove some Windows annoyance.
#if defined(min) || defined(max)
    #undef min
    #undef max
#endif

namespace Winsock
{
    #pragma region Hooking
    // Track all the hooks installed into WS2_32 and wsock32 by name.
    std::unordered_map<std::string, void *> WSHooks1;
    std::unordered_map<std::string, void *> WSHooks2;

    // Macros to make calling WS a little easier.
    #define CALLWS(_Function, _Result, ...) {                           \
    auto Pointer = WSHooks1[__func__];                                  \
    if(!Pointer) Pointer = WSHooks2[__func__];                          \
    auto Hook = (Hooking::StomphookEx<decltype(_Function)> *)Pointer;   \
    Hook->Function.first.lock();                                        \
    Hook->Removehook();                                                 \
    *_Result = Hook->Function.second(__VA_ARGS__);                      \
    Hook->Reinstall();                                                  \
    Hook->Function.first.unlock(); }
    #define CALLWS_NORET(_Function, ...) {                              \
    auto Pointer = WSHooks1[__func__];                                  \
    if(!Pointer) Pointer = WSHooks2[__func__];                          \
    auto Hook = (Hooking::StomphookEx<decltype(_Function)> *)Pointer;   \
    Hook->Function.first.lock();                                        \
    Hook->Removehook();                                                 \
    Hook->Function.second(__VA_ARGS__);                                 \
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
        if (Sockaddr->sa_family == AF_INET6) return ((struct sockaddr_in6 *)Sockaddr)->sin6_port;
        else return ((struct sockaddr_in *)Sockaddr)->sin_port;
    }
    IPAddress_t Localaddress(const struct sockaddr *Sockaddr)
    {
        IPAddress_t Result;
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
        if (Server) Localnetworking::Associatesocket(Server, Socket);
        Localnetworking::Addfilter(Socket, Localaddress(Name));

        return Result;
    }
    int __stdcall Connect(size_t Socket, const struct sockaddr *Name, int Namelength)
    {
        int Result = 0;

        // Check if we have any server with this socket and disconnect it.
        auto Server = Localnetworking::Findserver(Socket);
        if (Server) Server->onDisconnect(Socket);

        // If there's no server, try to create one from the hostname.
        if (!Server) Server = Localnetworking::Createserver(Plainaddress(Name));
        if (Server) Localnetworking::Disassociatesocket(Server, Socket);
        if (Server) Server->onConnect(Socket, WSPort(Name));

        // Ask Windows to connect the socket if there's no server.
        if (!Server) CALLWS(connect, &Result, Socket, Name, Namelength);

        // Debug information.
        Debugprint(va("%s to %s:%u", Server || 0 == Result ? "Connected" : "Failed to connect", Plainaddress(Name).c_str(), WSPort(Name)));
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
                Infoprint(va("\n%s\n%s\n%s\n%s\n%s",
                    "##############################################################",
                    "The current application is using special flags for Receive.",
                    "Feel free to implement that in Localnetworking/Winsock.cpp.",
                    "Or just hack it into your module.",
                    "##############################################################"));
            }

            // If we are on a blocking socket, poll until successful.
            do
            {
                Successful = Server->onStreamread(Socket, Buffer, &Result);
                if(!Successful) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } while (!Successful && Blockingsockets[Socket]);
        }

        // Ask Windows to fetch some data from the socket if it's not ours.
        if (!Server) CALLWS(recv, &Result, Socket, Buffer, Length, Flags);

        // Return the length or error.
        if (Server && !Successful) return -1;
        if (Result == uint32_t(-1)) return -1;
        return std::min(Result, uint32_t(INT32_MAX));
    }
    int __stdcall Receivefrom(size_t Socket, char *Buffer, int Length, int Flags, struct sockaddr *From, int *Fromlength)
    {
        uint32_t Result;

        // Pointer checking because few professional game-developers know their shit.
        if (!Buffer || !From)
        {
            WSASetLastError(EFAULT);
            return -1;
        }

        // Check if it's a socket associated with our network.
        if (Localnetworking::isAssociated(Socket))
        {
            IPAddress_t Localfrom; std::string Packet;

            // Check if there's any data on the socket and return that.
            do
            {
                if (Localnetworking::Dequeueframe(Socket, Localfrom, Packet))
                {
                    // Notify the developer that they'll have to deal with this.
                    if (Flags)
                    {
                        Infoprint(va("\n%s\n%s\n%s\n%s\n%s",
                            "##############################################################",
                            "The current application is using special flags for Receivefrom.",
                            "Feel free to implement that in Localnetworking/Winsock.cpp.",
                            "Or just hack it into your module.",
                            "##############################################################"));
                    }

                    // Copy the sender information.
                    if (From->sa_family == AF_INET6)
                    {
                        inet_pton(From->sa_family, Localfrom.Plainaddress, &((struct sockaddr_in6 *)From)->sin6_addr);
                        ((struct sockaddr_in6 *)From)->sin6_port = Localfrom.Port;
                    }
                    else
                    {
                        inet_pton(From->sa_family, Localfrom.Plainaddress, &((struct sockaddr_in *)From)->sin_addr);
                        ((struct sockaddr_in *)From)->sin_port = Localfrom.Port;
                    }

                    // Copy the data to the buffer and return how much was copied.
                    std::memcpy(Buffer, Packet.data(), std::min(size_t(Length), Packet.size()));
                    return std::min(size_t(Length), Packet.size());
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } while (Blockingsockets[Socket]);
        }

        // Ask Windows to fetch some data from the socket if it's not managed by us.
        CALLWS(recvfrom, &Result, Socket, Buffer, Length, Flags, From, Fromlength);
        return std::min(Result, uint32_t(INT32_MAX));
    }
    int __stdcall Select(int fdsCount, fd_set *Readfds, fd_set *Writefds, fd_set *Exceptfds, timeval *Timeout)
    {
        int Result = 0;
        std::vector<size_t> Readsockets;
        std::vector<size_t> Writesockets;

        for (auto &Item : Localnetworking::Activesockets())
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


    #pragma endregion

    #pragma region Installer
    #pragma endregion
}

#endif

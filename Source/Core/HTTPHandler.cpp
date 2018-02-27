/*
    Initial author: Convery (tcn@hedgehogscience.com)
    Started: 27-02-2018
    License: MIT
    Notes:
        Provides HTTP support.
*/

#include "../Stdinclude.hpp"
#if defined(_WIN32)
#include <WinSock2.h>
#include <Ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

struct HTTPRequest_t
{
    std::vector<std::pair<std::string, std::string>> Headers;
    std::string Hostname;
    std::string Resource;
    std::string Method;
    std::string Agent;
    std::string Body;
    size_t Socket;
    union
    {
        uint16_t Code;
        uint16_t Port;
    };
};

namespace Localnetworking
{
    std::unordered_map<size_t /* RequestID */, HTTPRequest_t> Responses;
    std::unordered_map<size_t /* RequestID */, HTTPRequest_t> Requests;
    std::atomic<size_t> RequestID = 10;

    // Download the response in the background.
    void Downloadresponse(size_t Handle)
    {
        Responses[Handle].Socket = Requests[Handle].Socket;

        int Size = 8196;
        char Buffer[8196]{};
        auto Result = recv(Responses[Handle].Socket, Buffer, Size, 0);
        if (Result != -1)
        {
            /*
                TODO(Convery):
                Parse this.
            */
            Responses[Handle].Code = 200;
            Responses[Handle].Body.append(std::strstr(Buffer, "\r\n\r\n"), Result);
        }
        else
        {
            Responses[Handle].Code = 404;
        }
    }

    // Handle HTTP operations and pass it to POSIX.
    size_t HTTPCreaterequest()
    {
        static bool Initialized = false;
        if (!Initialized)
        {
            WSADATA wsaData;
            Initialized = true;
            WSAStartup(MAKEWORD(2,2), &wsaData);
        }

        auto ID = RequestID++;
        unsigned long Argument = 0;
        Requests[ID].Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ioctlsocket(Requests[ID].Socket, FIONBIO, &Argument);
        return ID;
    }
    void HTTPSendrequest(size_t Handle)
    {
        auto Internal = Requests[Handle];

        // Connect to the server.
        sockaddr_in Serveraddress{};
        Serveraddress.sin_family = AF_INET;
        Serveraddress.sin_port = htons(Internal.Port);
        auto Resolved = gethostbyname(Internal.Hostname.c_str());
        Serveraddress.sin_addr.s_addr = *(ULONG *)Resolved->h_addr_list[0];
        connect(Internal.Socket, (struct sockaddr *)&Serveraddress, sizeof(Serveraddress));

        // Create the request as a string.
        std::string Request{};
        Request += va("%s %s HTTP/1.1\r\n", Internal.Method.c_str(), Internal.Resource.c_str());
        Request += va("User-Agent: %s\r\n", Internal.Agent.c_str());
        for (auto &Item : Internal.Headers) Request += va("%s: %s\r\n", Item.first.c_str(), Item.second.c_str());
        if(Internal.Body.size()) Request += va("Content-Length: %u\r\n", Internal.Body.size());
        Request += "\r\n";

        // Append the body if available.
        if (Internal.Body.size()) Request += Internal.Body;

        // Send to POSIX that forwards it to the server.
        send(Internal.Socket, Request.data(), (int)Request.size(), NULL);

        // Start fetching data for the result.
        std::thread(Downloadresponse, Handle).detach();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    void HTTPDeleterequest(size_t Handle)
    {
        Responses.erase(Handle);
        Requests.erase(Handle);
    }
    uint16_t HTTPGetstatuscode(size_t Handle)
    {
        return Responses[Handle].Code;
    }
    size_t HTTPGetresponsedatasize(size_t Handle)
    {
        return Responses[Handle].Body.size();
    }
    std::string HTTPGetresponsedata(size_t Handle)
    {
        return Responses[Handle].Body;
    }
    void HTTPSetport(size_t Handle, uint16_t Port)
    {
        Requests[Handle].Port = Port;
    }
    void HTTPSenddata(size_t Handle, std::string Data)
    {
        Requests[Handle].Body = Data;
    }
    void HTTPSetmethod(size_t Handle, std::string Method)
    {
        Requests[Handle].Method = Method;
    }
    void HTTPSetuseragent(size_t Handle, std::string Agent)
    {
        Requests[Handle].Agent = Agent;
    }
    void HTTPSetresource(size_t Handle, std::string Resource)
    {
        Requests[Handle].Resource = Resource;
    }
    void HTTPSethostname(size_t Handle, std::string Hostname)
    {
        Requests[Handle].Hostname = Hostname;
    }
    void HTTPSetheader(size_t Handle, std::string Key, std::string Value)
    {
        for (auto &Item : Requests[Handle].Headers)
        {
            if (0 == std::strcmp(Item.first.c_str(), Key.c_str()))
            {
                Item.second = Value;
                return;
            }
        }

        Requests[Handle].Headers.push_back({ Key, Value });
    }
}

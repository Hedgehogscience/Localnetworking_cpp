/*
    Initial author: Convery (tcn@hedgehogscience.com)
    Started: 09-01-2018
    License: MIT
    Notes:
        Provides an interface-shim for Windows INet and HTTP.
*/

#if defined(_WIN32)
#include "../Stdinclude.hpp"
#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <Wininet.h>

namespace Wininet
{
    struct Internetrequest
    {
        std::vector<std::string> Headers;
        std::string Useragent;
        std::string Hostname;
        std::string Username;
        std::string Password;
        std::string Location;
        std::string Version;
        std::string Method;
        IServer *Instance;
        uint16_t Port;
        size_t Socket;
    };
    std::unordered_map<size_t, Internetrequest> Activerequests;
    std::atomic<size_t> GlobalrequestID = 10;

    #pragma region Hooking
    // Track all the hooks installed into INET and HTTP.
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

    #pragma region Shims
    size_t __stdcall InternetopenA(LPCSTR lpszAgent, DWORD dwAccessType, LPCSTR lpszProxy, LPCSTR lpszProxyBypass, DWORD dwFlags)
    {
        Internetrequest Request;
        Request.Useragent = lpszAgent;

        // Return the RequestID as handle.
        size_t RequestID = GlobalrequestID.load();
        while (!GlobalrequestID.compare_exchange_strong(RequestID, RequestID + 1)) RequestID++;
        Activerequests[RequestID] = Request;
        return RequestID;
    }
    size_t __stdcall InternetopenW(LPCWSTR lpszAgent, DWORD dwAccessType, LPCWSTR lpszProxy, LPCWSTR lpszProxyBypass, DWORD dwFlags)
    {
        std::wstring Temporary = lpszAgent; std::string Agent = { Temporary.begin(), Temporary.end() };
        Temporary = lpszProxyBypass; std::string Proxybypass = { Temporary.begin(), Temporary.end() };
        Temporary = lpszProxy; std::string Proxy = { Temporary.begin(), Temporary.end() };

        return InternetopenA(Agent.c_str(), dwAccessType, Proxy.c_str(), Proxybypass.c_str(), dwFlags);
    }
    size_t __stdcall InternetconnectA(const size_t hInternet, LPCSTR lpszServerName, uint16_t nServerPort, LPCSTR lpszUserName, LPCSTR lpszPassword, DWORD dwService, DWORD dwFlags, DWORD_PTR dwContext)
    {
        // Resolve the host.
        Activerequests[hInternet].Hostname = lpszServerName;
        auto Hostname = gethostbyname(lpszServerName);
        if (Hostname == NULL) return NULL;

        // Resolve the port if not specified.
        Activerequests[hInternet].Port = nServerPort;
        if (INTERNET_INVALID_PORT_NUMBER == nServerPort)
        {
            if (INTERNET_SERVICE_FTP == dwService) Activerequests[hInternet].Port = 21;
            if (INTERNET_SERVICE_HTTP == dwService) Activerequests[hInternet].Port = 80;
            if (INTERNET_SERVICE_GOPHER == dwService) Activerequests[hInternet].Port = 70;
        }

        // Add the authentication info if available.
        Activerequests[hInternet].Username = lpszUserName ? lpszUserName : "anonymous";
        Activerequests[hInternet].Password = lpszPassword ? lpszPassword : "";

        // Convert to a sockaddr.
        sockaddr_in Address;
        Address.sin_family = AF_INET;
        Address.sin_port = htons(Activerequests[hInternet].Port);
        Address.sin_addr.s_addr = *(ULONG *)Hostname->h_addr_list[0];

        // Connect to the server.
        Activerequests[hInternet].Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (SOCKET_ERROR == connect(Activerequests[hInternet].Socket, (SOCKADDR *)&Address, Hostname->h_length))
            return NULL;

        return hInternet;
    }
    size_t __stdcall InternetconnectW(const size_t hInternet, LPCWSTR lpszServerName, uint16_t nServerPort, LPCWSTR lpszUserName, LPCWSTR lpszPassword, DWORD dwService, DWORD dwFlags, DWORD_PTR dwContext)
    {
        std::wstring Temporary = lpszServerName; std::string Hostname = { Temporary.begin(), Temporary.end() };
        Temporary = lpszUserName; std::string Username = { Temporary.begin(), Temporary.end() };
        Temporary = lpszPassword; std::string Password = { Temporary.begin(), Temporary.end() };

        return InternetconnectA(hInternet, Hostname.c_str(), nServerPort, Username.c_str(), Password.c_str(), dwService, dwFlags, dwContext);
    }
    DWORD __stdcall InternetAttemptconnect(DWORD Reserved)
    {
        return ERROR_SUCCESS;
    }
    size_t __stdcall HTTPOpenrequestA(const size_t hConnect, LPCSTR lpszVerb, LPCSTR lpszObjectName, LPCSTR lpszVersion, LPCSTR lpszReferrer, LPCSTR *lplpszAcceptTypes, DWORD dwFlags, DWORD_PTR dwContext)
    {
        // Build the request.
        Activerequests[hConnect].Method = lpszVerb ? lpszVerb : "GET";
        Activerequests[hConnect].Location = lpszObjectName ? lpszObjectName : "";
        Activerequests[hConnect].Version = lpszVersion ? lpszVersion : "HTTP/1.1";

        return hConnect;
    }
    size_t __stdcall HTTPOpenrequestW(const size_t hConnect, LPCWSTR lpszVerb, LPCWSTR lpszObjectName, LPCWSTR lpszVersion, LPCWSTR lpszReferrer, LPCWSTR *lplpszAcceptTypes, DWORD dwFlags, DWORD_PTR dwContext)
    {
        std::wstring Temporary = lpszVerb; std::string Method = { Temporary.begin(), Temporary.end() };
        Temporary = lpszObjectName; std::string Location = { Temporary.begin(), Temporary.end() };
        Temporary = lpszVersion; std::string Version = { Temporary.begin(), Temporary.end() };
        Temporary = lpszReferrer; std::string Referrer = { Temporary.begin(), Temporary.end() };

        return HTTPOpenrequestA(hConnect, Method.c_str(), Location.c_str(), Version.c_str(), Referrer.c_str(), { NULL }, dwFlags, dwContext);
    }
    BOOL __stdcall HTTPAddrequestheadersA(const size_t hRequest, LPCSTR lpszHeaders, DWORD dwHeadersLength, DWORD dwModifiers)
    {
        Activerequests[hRequest].Headers.push_back(lpszHeaders);
        return TRUE;
    }
    BOOL __stdcall HTTPAddrequestheadersW(const size_t hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength, DWORD dwModifiers)
    {
        std::wstring Temporary = lpszHeaders;

        Activerequests[hRequest].Headers.push_back({ Temporary.begin(), Temporary.end() });
        return TRUE;
    }
    BOOL __stdcall HTTPSendrequestExA(const size_t hRequest, LPINTERNET_BUFFERSA lpBuffersIn, LPINTERNET_BUFFERSA lpBuffersOut, DWORD dwFlags, DWORD_PTR dwContext)
    {
        /*
            NOTE(Convery):
            MSDN says that dwFlags is a reserved variable and must be zero.
            But wininet.h lists a number of flags and HSR_INITIATE has been
            observed in a number of modern AAA games.
        */

        // Create the request.
        std::string HTTPRequest;
        HTTPRequest += va("%s %s %s\r\n",
            Activerequests[hRequest].Method.c_str(),
            Activerequests[hRequest].Location.c_str(),
            Activerequests[hRequest].Version.c_str());
        HTTPRequest += va("User-Agent: %s\r\n",
            Activerequests[hRequest].Useragent.c_str());
        for (auto &Item : Activerequests[hRequest].Headers)
            HTTPRequest += va("%s\r\n", Item.c_str());
        HTTPRequest += "\r\n";

        // Send to Winsock that forwards it to the server.
        send(Activerequests[hRequest].Socket, HTTPRequest.c_str(), (int)HTTPRequest.size(), NULL);

        return TRUE;
    }
    BOOL __stdcall HTTPSendrequestExW(const size_t hRequest, LPINTERNET_BUFFERSW lpBuffersIn, LPINTERNET_BUFFERSW lpBuffersOut, DWORD dwFlags, DWORD_PTR dwContext)
    {
        /*
            NOTE(Convery):
            MSDN says that dwFlags is a reserved variable and must be zero.
            But wininet.h lists a number of flags and HSR_INITIATE has been
            observed in a number of modern AAA games.
        */

        // Create the request.
        std::string HTTPRequest;
        HTTPRequest += va("%s %s %s\r\n",
            Activerequests[hRequest].Method.c_str(),
            Activerequests[hRequest].Location.c_str(),
            Activerequests[hRequest].Version.c_str());
        HTTPRequest += va("User-Agent: %s\r\n",
            Activerequests[hRequest].Useragent.c_str());
        for (auto &Item : Activerequests[hRequest].Headers)
            HTTPRequest += va("%s\r\n", Item.c_str());
        HTTPRequest += "\r\n";

        // Send to Winsock that forwards it to the server.
        send(Activerequests[hRequest].Socket, HTTPRequest.c_str(), (int)HTTPRequest.size(), NULL);

        return TRUE;
    }
    BOOL __stdcall HTTPSendrequestA(const size_t hRequest, LPCSTR lpszHeaders, DWORD dwHeadersLength, LPVOID lpOptional, DWORD dwOptionalLength)
    {
        // Create the request.
        std::string HTTPRequest;
        HTTPRequest += va("%s %s %s\r\n",
            Activerequests[hRequest].Method.c_str(),
            Activerequests[hRequest].Location.c_str(),
            Activerequests[hRequest].Version.c_str());
        HTTPRequest += va("User-Agent: %s\r\n",
            Activerequests[hRequest].Useragent.c_str());
        for (auto &Item : Activerequests[hRequest].Headers)
            HTTPRequest += va("%s\r\n", Item.c_str());
        if(lpszHeaders)
            HTTPRequest += va("%s", std::string(lpszHeaders, dwHeadersLength).c_str());
        if(dwOptionalLength != NULL)
            HTTPRequest += va("Content-Length: %u\r\n", dwOptionalLength);
        HTTPRequest += "\r\n";

        // Create the body.
        if (lpOptional && dwOptionalLength != NULL)
            HTTPRequest.append((char *)lpOptional, dwOptionalLength);

        // Send to Winsock that forwards it to the server.
        send(Activerequests[hRequest].Socket, HTTPRequest.data(), (int)HTTPRequest.size(), NULL);

        return TRUE;
    }
    BOOL __stdcall HTTPSendrequestW(const size_t hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength, LPVOID lpOptional, DWORD dwOptionalLength)
    {
        if (!lpszHeaders) return HTTPSendrequestA(hRequest, nullptr, 0, lpOptional, dwOptionalLength);

        std::wstring Temporary = lpszHeaders;
        std::string Headers = { Temporary.begin(), Temporary.end() };

        return HTTPSendrequestA(hRequest, Headers.c_str(), (int)Headers.size(), lpOptional, dwOptionalLength);
    }

    BOOL __stdcall InternetQueryoptionA(const size_t hInternet, DWORD dwOption, LPVOID lpBuffer, LPDWORD lpdwBufferLength)
    {
        /*
            TODO(Convery):
            We may want to actually implement this.
        */
        return TRUE;
    }
    BOOL __stdcall InternetQueryoptionW(const size_t hInternet, DWORD dwOption, LPVOID lpBuffer, LPDWORD lpdwBufferLength)
    {
        /*
            TODO(Convery):
            We may want to actually implement this.
        */
        return TRUE;
    }
    BOOL __stdcall InternetSetoptionA(const size_t hInternet, DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength)
    {
        /*
            TODO(Convery):
            We may want to actually implement this.
        */
        return TRUE;
    }
    BOOL __stdcall InternetSetoptionW(const size_t hInternet, DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength)
    {
        /*
            TODO(Convery):
            We may want to actually implement this.
        */
        return TRUE;
    }

    BOOL __stdcall InternetWritefile(const size_t hFile, LPCVOID lpBuffer, DWORD dwNumberOfBytesToWrite, LPDWORD lpdwNumberOfBytesWritten)
    {
        // Legacy functionality.
        *lpdwNumberOfBytesWritten = 0;

        auto Result = send(Activerequests[hFile].Socket, (char *)lpBuffer, dwNumberOfBytesToWrite, NULL);
        if (-1 == Result) return FALSE;

        *lpdwNumberOfBytesWritten = Result;
        return TRUE;
    }
    BOOL __stdcall InternetReadfile(const size_t hFile, LPCVOID lpBuffer, DWORD dwNumberOfBytesToRead, LPDWORD lpdwNumberOfBytesRead)
    {
        // Legacy functionality.
        *lpdwNumberOfBytesRead = 0;

        auto Result = recv(Activerequests[hFile].Socket, (char *)lpBuffer, dwNumberOfBytesToRead, 0);
        if (Result == -1) return FALSE;

        *lpdwNumberOfBytesRead = Result;
        return TRUE;
    }
    #pragma endregion

    #pragma region Installer
    void INETInstaller()
    {
        // Helper-macro to save the developers fingers.
        #define INSTALL_HOOK(_Function, _Replacement) {                                                                                     \
        auto Address = (void *)GetProcAddress(GetModuleHandleA("wininet.dll"), _Function);                                                  \
        if(Address) {                                                                                                                       \
        Wininet::WSHooks1[#_Replacement] = new Hooking::StomphookEx<decltype(_Replacement)>();                                              \
        ((Hooking::StomphookEx<decltype(_Replacement)> *)Wininet::WSHooks1[#_Replacement])->Installhook(Address, (void *)&_Replacement);}   \
        Address = (void *)GetProcAddress(GetModuleHandleA("winhttp.dll"), _Function);                                                       \
        if(Address) {                                                                                                                       \
        Wininet::WSHooks2[#_Replacement] = new Hooking::StomphookEx<decltype(_Replacement)>();                                              \
        ((Hooking::StomphookEx<decltype(_Replacement)> *)Wininet::WSHooks2[#_Replacement])->Installhook(Address, (void *)&_Replacement);}   \
        }                                                                                                                                   \

        // Place the hooks directly in windows INET.
        INSTALL_HOOK("InternetOpenA", InternetopenA);
        INSTALL_HOOK("InternetOpenW", InternetopenW);
        INSTALL_HOOK("InternetConnectA", InternetconnectA);
        INSTALL_HOOK("InternetConnectW", InternetconnectW);
        INSTALL_HOOK("InternetAttemptConnect", InternetAttemptconnect);
        INSTALL_HOOK("HttpOpenRequestA", HTTPOpenrequestA);
        INSTALL_HOOK("HttpOpenRequestW", HTTPOpenrequestW);
        INSTALL_HOOK("HttpAddRequestHeadersA", HTTPAddrequestheadersA);
        INSTALL_HOOK("HttpAddRequestHeadersW", HTTPAddrequestheadersW);
        INSTALL_HOOK("HttpSendRequestExA", HTTPSendrequestExA);
        INSTALL_HOOK("HttpSendRequestExW", HTTPSendrequestExA);
        INSTALL_HOOK("InternetQueryOptionA", InternetQueryoptionA);
        INSTALL_HOOK("InternetQueryOptionW", InternetQueryoptionW);
        INSTALL_HOOK("InternetSetOptionA", InternetSetoptionA);
        INSTALL_HOOK("InternetSetOptionW", InternetSetoptionW);
        INSTALL_HOOK("HttpSendRequestA", HTTPSendrequestA);
        INSTALL_HOOK("HttpSendRequestW", HTTPSendrequestW);
        INSTALL_HOOK("InternetWriteFile", InternetWritefile);
        INSTALL_HOOK("InternetReadFile", InternetReadfile);
    };

    // Add the installer on startup.
    struct Installer { Installer() { Localnetworking::Addplatform(INETInstaller); }; };
    static Installer Startup{};
    #pragma endregion
}

#undef INSTALL_HOOK
#undef CALLWS_NORET
#undef CALLWS
#endif

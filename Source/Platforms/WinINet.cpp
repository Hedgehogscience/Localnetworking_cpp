/*
    Initial author: Convery (tcn@ayria.se)
    Started: 3-11-2017
    License: MIT
    Notes:
        Re-implements part of Windows INet and HTTP.
*/

// Naturally this file is for Windows only.
#if defined (_WIN32)
#include "../Stdinclude.h"
#include <Wininet.h>

// Remove some Windows annoyance.
#if defined(min) || defined(max)
    #undef min
    #undef max
#endif

namespace Wininet
{
    struct Internetrequest
    {
        std::vector<std::string> Headers;
        std::string Useragent;
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

#pragma endregion

    #pragma region Installer
    struct INETInstaller
    {
        INETInstaller()
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
        }
    };

    // Install the hooks on startup.
    static INETInstaller Installer{};
    #pragma endregion
}

#undef INSTALL_HOOK
#undef CALLWS_NORET
#undef CALLWS
#endif

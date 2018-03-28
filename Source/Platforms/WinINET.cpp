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
    #pragma region Hooking
    // Track all the hooks installed into INET and HTTP.
    std::unordered_map<std::string, std::mutex> Guards;
    std::unordered_map<std::string, void *> Hooks;

    // Macros to make calling WS a little easier.
    #define Gethook(_Function) (Hooking::Stomphook<decltype(_Function)> *)Hooks[__func__];
    #define Getguard() Guards[__func__]
    #define CALLWS(_Function, _Result, ...) {           \
    auto Pointer = Gethook(_Function);                  \
    Getguard().lock();                                  \
    Callhook_ret((*Pointer), _Result, __VA_ARGS__);     \
    Lasterror = WSAGetLastError();                      \
    Getguard().unlock(); }
    #define CALLWS_NORET(_Function, ...) {              \
    auto Pointer = Gethook(_Function);                  \
    Getguard().lock();                                  \
    Callhook_noret((*Pointer), __VA_ARGS__);            \
    Lasterror = WSAGetLastError();                      \
    Getguard().unlock(); }
    #pragma endregion

    #pragma region Shims
    size_t __stdcall InternetopenA(LPCSTR lpszAgent, DWORD dwAccessType, LPCSTR lpszProxy, LPCSTR lpszProxyBypass, DWORD dwFlags)
    {
        auto Handle = Localnetworking::HTTPCreaterequest();
        Localnetworking::HTTPSetuseragent(Handle, lpszAgent);
        return Handle;
    }
    size_t __stdcall InternetopenW(LPCWSTR lpszAgent, DWORD dwAccessType, LPCWSTR lpszProxy, LPCWSTR lpszProxyBypass, DWORD dwFlags)
    {
        std::wstring Temporary = lpszAgent; std::string Agent = { Temporary.begin(), Temporary.end() };
        Temporary = lpszProxyBypass; std::string Proxybypass = { Temporary.begin(), Temporary.end() };
        Temporary = lpszProxy; std::string Proxy = { Temporary.begin(), Temporary.end() };

        return InternetopenA(Agent.c_str(), dwAccessType, Proxy.c_str(), Proxybypass.c_str(), dwFlags);
    }
    size_t __stdcall InternetconnectA(const size_t Handle, LPCSTR lpszServerName, uint16_t nServerPort, LPCSTR lpszUserName, LPCSTR lpszPassword, DWORD dwService, DWORD dwFlags, DWORD_PTR dwContext)
    {
        // Set the hostname and port.
        Localnetworking::HTTPSetport(Handle, nServerPort);
        Localnetworking::HTTPSethostname(Handle, lpszServerName);
        if (INTERNET_INVALID_PORT_NUMBER == nServerPort)
        {
            if (INTERNET_SERVICE_FTP == dwService) Localnetworking::HTTPSetport(Handle, 21);
            if (INTERNET_SERVICE_HTTP == dwService) Localnetworking::HTTPSetport(Handle, 80);
            if (INTERNET_SERVICE_GOPHER == dwService) Localnetworking::HTTPSetport(Handle, 70);
        }

        // Add the authentication info if available.
        if (lpszUserName)
        {
            Localnetworking::HTTPSetheader(Handle, "Authorization",
                va("Basic %s", Base64::Encode(va("%s:%s", lpszUserName, lpszPassword)).c_str()));
        }

        return Handle;
    }
    size_t __stdcall InternetconnectW(const size_t Handle, LPCWSTR lpszServerName, uint16_t nServerPort, LPCWSTR lpszUserName, LPCWSTR lpszPassword, DWORD dwService, DWORD dwFlags, DWORD_PTR dwContext)
    {
        std::wstring Temporary = lpszServerName; std::string Hostname = { Temporary.begin(), Temporary.end() };
        Temporary = lpszUserName; std::string Username = { Temporary.begin(), Temporary.end() };
        Temporary = lpszPassword; std::string Password = { Temporary.begin(), Temporary.end() };

        return InternetconnectA(Handle, Hostname.c_str(), nServerPort, Username.c_str(), Password.c_str(), dwService, dwFlags, dwContext);
    }
    DWORD __stdcall InternetAttemptconnect(DWORD Reserved)
    {
        return ERROR_SUCCESS;
    }
    size_t __stdcall HTTPOpenrequestA(const size_t Handle, LPCSTR lpszVerb, LPCSTR lpszObjectName, LPCSTR lpszVersion, LPCSTR lpszReferrer, LPCSTR *lplpszAcceptTypes, DWORD dwFlags, DWORD_PTR dwContext)
    {
        // Build the request.
        Localnetworking::HTTPSetmethod(Handle, lpszVerb ? lpszVerb : "GET");
        Localnetworking::HTTPSetresource(Handle, lpszObjectName ? lpszObjectName : "/");

        return Handle;
    }
    size_t __stdcall HTTPOpenrequestW(const size_t Handle, LPCWSTR lpszVerb, LPCWSTR lpszObjectName, LPCWSTR lpszVersion, LPCWSTR lpszReferrer, LPCWSTR *lplpszAcceptTypes, DWORD dwFlags, DWORD_PTR dwContext)
    {
        std::wstring Temporary = lpszVerb; std::string Method = { Temporary.begin(), Temporary.end() };
        Temporary = lpszObjectName; std::string Location = { Temporary.begin(), Temporary.end() };
        Temporary = lpszVersion; std::string Version = { Temporary.begin(), Temporary.end() };
        Temporary = lpszReferrer; std::string Referrer = { Temporary.begin(), Temporary.end() };

        return HTTPOpenrequestA(Handle, Method.c_str(), Location.c_str(), Version.c_str(), Referrer.c_str(), { NULL }, dwFlags, dwContext);
    }
    BOOL __stdcall HTTPAddrequestheadersA(const size_t Handle, LPCSTR lpszHeaders, DWORD dwHeadersLength, DWORD dwModifiers)
    {
        std::string Input(lpszHeaders);
        Input.pop_back();
        Input.pop_back();
        Input.pop_back();
        Input.pop_back();

        Localnetworking::HTTPSetheader(Handle,
            Input.substr(0, Input.find_first_of(':')),
            Input.substr(Input.find_first_of(':') + 1));
        return TRUE;
    }
    BOOL __stdcall HTTPAddrequestheadersW(const size_t Handle, LPCWSTR lpszHeaders, DWORD dwHeadersLength, DWORD dwModifiers)
    {
        std::wstring Temporary = lpszHeaders;
        return HTTPAddrequestheadersA(Handle, std::string(Temporary.begin(), Temporary.end()).c_str(), dwHeadersLength, dwModifiers);
    }
    BOOL __stdcall HTTPSendrequestExA(const size_t Handle, LPINTERNET_BUFFERSA lpBuffersIn, LPINTERNET_BUFFERSA lpBuffersOut, DWORD dwFlags, DWORD_PTR dwContext)
    {
        /*
            NOTE(Convery):
            MSDN says that dwFlags is a reserved variable and must be zero.
            But wininet.h lists a number of flags and HSR_INITIATE has been
            observed in a number of modern AAA games.
        */

        // I'd rather not deal with this right now..
        assert(false);

        Localnetworking::HTTPSendrequest(Handle);
        return TRUE;
    }
    BOOL __stdcall HTTPSendrequestExW(const size_t Handle, LPINTERNET_BUFFERSW lpBuffersIn, LPINTERNET_BUFFERSW lpBuffersOut, DWORD dwFlags, DWORD_PTR dwContext)
    {
        /*
            NOTE(Convery):
            MSDN says that dwFlags is a reserved variable and must be zero.
            But wininet.h lists a number of flags and HSR_INITIATE has been
            observed in a number of modern AAA games.
        */

        // I'd rather not deal with this right now..
        assert(false);

        Localnetworking::HTTPSendrequest(Handle);
        return TRUE;
    }
    BOOL __stdcall HTTPSendrequestA(const size_t Handle, LPCSTR lpszHeaders, DWORD dwHeadersLength, LPVOID lpOptional, DWORD dwOptionalLength)
    {
        // Add the missing header and data.
        if (lpszHeaders) HTTPAddrequestheadersA(Handle, lpszHeaders, dwHeadersLength, 0);
        if (dwOptionalLength != NULL) Localnetworking::HTTPSenddata(Handle, { (char *)lpOptional, dwOptionalLength });
        Localnetworking::HTTPSendrequest(Handle);
        return TRUE;
    }
    BOOL __stdcall HTTPSendrequestW(const size_t Handle, LPCWSTR lpszHeaders, DWORD dwHeadersLength, LPVOID lpOptional, DWORD dwOptionalLength)
    {
        if (!lpszHeaders) return HTTPSendrequestA(Handle, nullptr, 0, lpOptional, dwOptionalLength);

        std::wstring Temporary = lpszHeaders;
        std::string Headers = { Temporary.begin(), Temporary.end() };

        return HTTPSendrequestA(Handle, Headers.c_str(), (int)Headers.size(), lpOptional, dwOptionalLength);
    }

    BOOL __stdcall InternetQueryoptionA(const size_t Handle, DWORD dwOption, LPVOID lpBuffer, LPDWORD lpdwBufferLength)
    {
        /*
            TODO(Convery):
            We may want to actually implement this.
        */
        return TRUE;
    }
    BOOL __stdcall InternetQueryoptionW(const size_t Handle, DWORD dwOption, LPVOID lpBuffer, LPDWORD lpdwBufferLength)
    {
        /*
            TODO(Convery):
            We may want to actually implement this.
        */
        return TRUE;
    }
    BOOL __stdcall InternetSetoptionA(const size_t Handle, DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength)
    {
        /*
            TODO(Convery):
            We may want to actually implement this.
        */
        return TRUE;
    }
    BOOL __stdcall InternetSetoptionW(const size_t Handle, DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength)
    {
        /*
            TODO(Convery):
            We may want to actually implement this.
        */
        return TRUE;
    }

    BOOL __stdcall InternetWritefile(const size_t Handle, LPCVOID lpBuffer, DWORD dwNumberOfBytesToWrite, LPDWORD lpdwNumberOfBytesWritten)
    {
        Localnetworking::HTTPSenddata(Handle, { (char *)lpBuffer, dwNumberOfBytesToWrite });
        *lpdwNumberOfBytesWritten = dwNumberOfBytesToWrite;
        Localnetworking::HTTPSendrequest(Handle);
        return TRUE;
    }
    BOOL __stdcall InternetReadfile(const size_t Handle, LPVOID lpBuffer, DWORD dwNumberOfBytesToRead, LPDWORD lpdwNumberOfBytesRead)
    {
        // Legacy functionality.
        *lpdwNumberOfBytesRead = 0;

        auto Response = Localnetworking::HTTPGetresponsedata(Handle);
        if (Response.size())
        {
            auto Length = std::min(Response.size(), size_t(dwNumberOfBytesToRead));
            std::memcpy(lpBuffer, Response.data(), Length);
            *lpdwNumberOfBytesRead = Length;
        }

        return TRUE;
    }

    BOOL __stdcall HTTPQueryinfoA(const size_t Handle, DWORD dwInfolevel, LPVOID lpvBuffer, LPDWORD lpdwBufferLength, LPDWORD lpdwIndex)
    {
        if (dwInfolevel & HTTP_QUERY_FLAG_NUMBER)
        {
            if (dwInfolevel & HTTP_QUERY_STATUS_CODE)
            {
                *(uint16_t *)lpvBuffer = Localnetworking::HTTPGetstatuscode(Handle);
                return TRUE;
            }

            if (dwInfolevel & HTTP_QUERY_CONTENT_LENGTH)
            {
                *(DWORD *)lpvBuffer = Localnetworking::HTTPGetresponsedatasize(Handle);
                return TRUE;
            }
        }

        return FALSE;
    }
    #pragma endregion

    #pragma region Installer
    void INETInstaller()
    {
        // Helper-macro to save the developers fingers.
        #define Getaddress(_Function) \
        GetProcAddress(GetModuleHandleA("wininet.dll"), _Function) ? \
        GetProcAddress(GetModuleHandleA("wininet.dll"), _Function) : \
        GetProcAddress(GetModuleHandleA("winhttp.dll"), _Function);

        #define Createhook(_Address, _Replacement)                                                      \
        auto Hook = Hooking::Stomphook<decltype(_Replacement)>::Install(_Address, &_Replacement);       \
        Wininet::Hooks[#_Replacement] = new Hooking::Stomphook<decltype(_Replacement)>(std::move(Hook));

        #define INSTALL_HOOK(_Function, _Replacement) {     \
        auto Address = Getaddress(_Function);               \
        if(Address) { Createhook(Address, _Replacement); } }

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
        INSTALL_HOOK("HttpQueryInfoA", HTTPQueryinfoA);
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

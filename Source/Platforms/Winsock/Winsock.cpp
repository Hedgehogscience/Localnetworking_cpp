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

            // TODO(Convery): Hook all WS functions.
        }
    };
    WSLoader Loader{};
}
#endif

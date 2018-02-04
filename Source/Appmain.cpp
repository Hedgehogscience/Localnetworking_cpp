/*
    Initial author: Convery (tcn@hedgehogscience.com)
    Started: 09-01-2018
    License: MIT
    Notes:
        Provides the entrypoint for Windows and Nix.
*/

#include "Stdinclude.hpp"

// Callbacks from the bootstrapper (Bootstrapmodule_cpp).
extern "C" EXPORT_ATTR void onInitializationStart(bool Reserved)
{
    /*
        ----------------------------------------------------------------------
        This callback is called when the game is initialized, which means that
        all other libraries are loaded; but maybe not all other plugins.
        Your plugins should take this time to modify the games .text segment
        as well as initializing all your own systems.
        ----------------------------------------------------------------------
    */

    // Initialize the modules and datagram IO.
    Localnetworking::Startpollthread();
    Localnetworking::Loadallmodules();

    // Initialize the platform hooks.
    Localnetworking::Initializeplatforms();
}
extern "C" EXPORT_ATTR void onInitializationDone(bool Reserved)
{
    /*
        ----------------------------------------------------------------------
        This callback is called when the platform notifies the bootstrapper,
        or at most 3 seconds after startup. This is the perfect time to start
        communicating with other plugins and modify the games .data segment.
        ----------------------------------------------------------------------
    */
}
extern "C" EXPORT_ATTR void onMessage(uint32_t MessageID, uint32_t Messagesize, const void *Messagedata)
{
    /*
        ----------------------------------------------------------------------
        This callback is called when another plugin broadcasts a message. They
        can safely be ignored, but if you want to make use of the system you
        should create a unique name for your messages. We recommend that you
        base it on your pluginname as shown below, we also recommend that you
        use the bytebuffer format for data.
        ----------------------------------------------------------------------
    */

    Bytebuffer Message{ Messagesize, Messagedata};

    // MessageID is a FNV1a_32 hash of a string.
    switch (MessageID)
    {
        case Hash::FNV1a_32(MODULENAME "::Enqueueframe"):
        {
            Address_t Sender;
            std::string Plainaddress;
            std::vector<uint8_t> Packetrawdata;

            // Deserialize the request.
            Message.Read(Sender.Port);
            Message.Read(Plainaddress);
            Message.Read(Packetrawdata);

            // Copy the plain address into the C buffer and enqueue it.
            std::memcpy(Sender.Plainaddress, Plainaddress.c_str(), Plainaddress.size());
            std::string Data{ Packetrawdata.begin(), Packetrawdata.end() };
            Localnetworking::Enqueueframe(Sender, Data);
            break;
        }
        case Hash::FNV1a_32(MODULENAME "::Default"):
        default: break;
    }
}

#if defined _WIN32
BOOLEAN WINAPI DllMain(HINSTANCE hDllHandle, DWORD nReason, LPVOID Reserved)
{
    switch (nReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            // Opt-out of further thread notifications.
            DisableThreadLibraryCalls(hDllHandle);

            // Clear the previous sessions logfile.
            Clearlog();
        }
    }

    return TRUE;
}
#else
__attribute__((constructor)) void DllMain()
{
    // Clear the previous sessions logfile.
    Clearlog();
}
#endif

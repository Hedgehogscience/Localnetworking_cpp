/*
    Initial author: Convery (tcn@ayria.se)
    Started: 29-07-2017
    License: MIT
    Notes:
        Module entrypoint.
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

    Localnetworking::Startpollthread();
    Localnetworking::Loadmodules();
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
        case Hash::FNV1a_32(MODULENAME "_Default"):
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

    // Initialize the database.
    Database::Load("Platformwrapper.db");
    std::atexit([]() { Database::Save("Platformwrapper.db"); std::remove("file"); });

    // For developers we sideload a bootstrapper.
    #if !defined (NDEBUG)
        dlopen("Localbootstrap", RTLD_LAZY);
    #endif
}
#endif



































// Delete the last sessions log on startup for windows.
#if defined (_WIN32)
    namespace { struct Deletelog { Deletelog() { Clearlog(); } }; static Deletelog Deleted{}; }
#endif

// The callback system for Ayria plugins.
extern "C"
{
    EXPORT_ATTR void onInitializationStart(bool Reserved)
    {
        /*
            ----------------------------------------------------------------------
            This callback is called when the game is initialized, which means that
            all other libraries are loaded; but maybe not all other plugins.
            Your plugins should take this time to modify the games .text segment
            as well as initializing all your own systems.
            ----------------------------------------------------------------------
        */

        Localnetworking::Startpollthread();
        Localnetworking::Loadmodules();
    }
    EXPORT_ATTR void onInitializationDone(bool Reserved)
    {
        /*
            ----------------------------------------------------------------------
            This callback is called when the platform notifies the bootstrapper,
            or at most 3 seconds after startup. This is the perfect time to start
            communicating with other plugins and modify the games .data segment.
            ----------------------------------------------------------------------
        */
    }
    EXPORT_ATTR void onMessage(uint32_t MessageID, uint32_t Messagesize, const void *Messagedata)
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

        // MessageID is a FNV1a_32 hash of a string.
        switch (MessageID)
        {
            case Hash::FNV1a_32(MODULENAME "_Enqueueframe"):
            {
                std::vector<uint8_t> Packetdata;
                std::string Plainaddress{};
                IPAddress_t Fromaddress{};

                Bytebuffer Reader(std::string((char *)Messagedata, Messagesize));
                Reader.Read(Fromaddress.Port);
                Reader.Read(Plainaddress);
                Reader.Read(Packetdata);

                std::memcpy(Fromaddress.Plainaddress, Plainaddress.c_str(), Plainaddress.size());
                std::string Data{ Packetdata.begin(), Packetdata.end() };
                Localnetworking::Enqueueframe(Fromaddress, Data);
                break;
            }

            case Hash::FNV1a_32(MODULENAME "_Default"):
            default: break;
        }
    }
}

// Default entrypoint for windows.
#ifdef _WIN32
BOOLEAN WINAPI DllMain(HINSTANCE hDllHandle, DWORD nReason, LPVOID Reserved)
{
    switch (nReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            // Rather not handle all thread updates.
            DisableThreadLibraryCalls(hDllHandle);
            break;
        }
    }

    return TRUE;
}
#endif

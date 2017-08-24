/*
    Initial author: Convery (tcn@ayria.se)
    Started: 24-08-2017
    License: MIT
    Notes:
*/

#include "../StdInclude.h"
#include "Servers.h"
#include <unordered_map>
#include <functional>

// Forward declarations for  platform specific functionality.
bool Findfiles(std::string Searchpath, std::vector<std::string> *Filenames);
void *FindFunction(const void *Module, const char *Function);
void *LoadModule(const char *Path);

// Module and server storage.
std::vector<void * /* Handles */> Networkmodules;
std::unordered_map<size_t /* Socket */, IServer * /* Server */> ServersbySocket;
std::unordered_map<std::string /* Address */, IServer * /* Server */> ServersbyAddress;
std::unordered_map<std::string /* Address */, std::string /* Hostname */> Resolvedservers;

// Create a server based on the hostname, returns null if there's no handler.
IServer *Createserver(const size_t Socket, std::string Hostname)
{
    auto Result = Findserver(Hostname);
    if (!Result) Result = Createserver(Hostname);
    if (Result) ServersbySocket[Socket] = Result;
    return Result;
}
IServer *Createserver(std::string Hostname)
{
    // Check if already resolved.
    if (Resolvedservers.find(Hostname) != Resolvedservers.end())
        Hostname = Resolvedservers[Hostname];

    for (auto &Item : Networkmodules)
    {
        // Find the export in the module.
        auto pFunction = FindFunction(Item, "Createserver");
        if (!pFunction) continue;

        // Ask the module to create a new instance for the hostname.
        auto Function = (IServer * (*)(const char *))pFunction;
        auto Result = Function(Hostname.c_str());

        if (Result)
        {
            uint32_t IPv4 = Hash::FNV1a_32(Hostname.c_str());
            uint8_t *IP = (uint8_t *)&IPv4;

            ServersbyAddress[Hostname] = Result;
            ServersbyAddress[va("%u.%u.%u.%u", IP[0], IP[1], IP[2], IP[3])] = Result;
            Resolvedservers[va("%u.%u.%u.%u", IP[0], IP[1], IP[2], IP[3])] = Hostname;
            return Result;
        }
    }

    return nullptr;
}

// Find a server by criteria.
IServer *Findserver(const size_t Socket)
{
    auto Result = ServersbySocket.find(Socket);
    if (Result != ServersbySocket.end())
        return Result->second;
    else
        return nullptr;
}
IServer *Findserver(std::string Address)
{
    auto Result = ServersbyAddress.find(Address);
    if (Result != ServersbyAddress.end())
        return Result->second;
    else
        return nullptr;
}

// Find the address associated with a server.
std::string Findaddress(const size_t Socket)
{
    auto Result = Findserver(Socket);
    if (Result) return Findaddress(Result);
    else return "";
}
std::string Findaddress(const IServer *Server)
{
    std::vector<std::string> Candidates;
    std::string Result;

    // Find all addresses.
    for (auto &Item : ServersbyAddress)
    {
        if (Item.second == Server)
        {
            Candidates.push_back(Item.first);
        }
    }

    // Find the one that's an IP.
    for (auto &Item : Candidates)
    {
        size_t i = 0;

        for (; i < Item.size(); ++i)
        {
            if (Item[i] != '.' && (Item[i] > '9' || Item[i] < '0'))
                break;
        }

        if (i != Item.size())
            continue;

        Result = Item;
        break;
    }

    // If there's no IP, create one.
    if (0 == Result.size() && Candidates.size())
    {
        uint32_t IPv4 = Hash::FNV1a_32(Candidates[0].c_str());
        uint8_t *IP = (uint8_t *)&IPv4;

        Result = va("%u.%u.%u.%u", IP[0], IP[1], IP[2], IP[3]);
    }

    return Result;
}

// Erase an entry from the list.
void Disconnectserver(const size_t Socket)
{
    auto Result = Findserver(Socket);
    if (Result)
    {
        Disconnectserver(Result);
        ServersbySocket.erase(Socket);
    }
}
void Disconnectserver(const IServer *Server)
{
    for (auto &Item : ServersbyAddress)
    {
        if (Item.second == Server)
        {
            ServersbyAddress.erase(Item.first);
            return;
        }
    }
}

// Return all active sockets.
std::vector<size_t> Activesockets()
{
    std::vector<size_t> Result;
    for (auto &Item : ServersbySocket)
        Result.push_back(Item.first);
    return Result;
}

// Load all modules from /Localnetworking/ on startup.
namespace
{
    struct Moduleloader
    {
        Moduleloader()
        {
            std::vector<std::string> Filenames;
            std::string Path = "./Plugins/Localnetworking/";

            // Enumerate all modules in the directory.
            if (Findfiles(Path, &Filenames))
            {
                for (auto &Item : Filenames)
                {
                    // Load the library into memory.
                    auto Module = LoadModule((Path + Item).c_str());
                    if (!Module)
                    {
                        Debugprint(va("Failed to load module: \"%s\"", (Path + Item).c_str()));
                        continue;
                    }

                    // Log this event.
                    Debugprint(va("Loaded module: \"%s\"", (Path + Item).c_str()));

                    // Add to the internal vector.
                    Networkmodules.push_back(Module);
                }
            }
        }
    };
    Moduleloader Loader{};
}

// The platform specific functionality.
#ifdef _WIN32
#include <Windows.h>
#include <direct.h>
void *FindFunction(const void *Module, const char *Function)
{
    return (void *)GetProcAddress(HMODULE(Module), Function);
}
void *LoadModule(const char *Path)
{
    return (void *)LoadLibraryA(Path);
}
bool Findfiles(std::string Searchpath, std::vector<std::string> *Filenames)
{
    WIN32_FIND_DATAA Filedata;
    HANDLE Filehandle;

    // Append trailing slash, asterisk and extension.
    if (Searchpath.back() != '/') Searchpath.append("/");
    Searchpath.append("*.LNmodule");

    // Find the first plugin.
    Filehandle = FindFirstFileA(Searchpath.c_str(), &Filedata);
    if (Filehandle == (void *)ERROR_INVALID_HANDLE || Filehandle == (void *)INVALID_HANDLE_VALUE)
    {
        if(Filehandle) FindClose(Filehandle);
        return false;
    }

    do
    {
        // Respect hidden files and folders.
        if (Filedata.cFileName[0] == '.')
            continue;

        // Add the file to the list.
        if (!(Filedata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            Filenames->push_back(Filedata.cFileName);

    } while (FindNextFileA(Filehandle, &Filedata));

    FindClose(Filehandle);
    return !!Filenames->size();
}
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <dlfcn.h>

void *FindFunction(const void *Module, const char *Function)
{
    return (void *)dlsym(Module, Function);
}
void *LoadModule(const char *Path)
{
    return (void *)dlopen(Path, RTLD_LAZY);
}
bool Findfiles(std::string Searchpath, std::vector<std::string> *Filenames)
{
    struct stat Fileinfo;
    dirent *Filedata;
    DIR *Filehandle;

    // Iterate through the directory.
    Filehandle = opendir(Searchpath.c_str());
    while ((Filedata = readdir(Filehandle)))
    {
        // Respect hidden files and folders.
        if (Filedata->d_name[0] == '.')
            continue;

        // Get extended fileinfo.
        std::string Filepath = Searchpath + "/" + Filedata->d_name;
        if (stat(Filepath.c_str(), &Fileinfo) == -1) continue;

        // Add the file to the list.
        if (!(Fileinfo.st_mode & S_IFDIR) && std::strstr(Filedata->d_name, ".LNmodule"))
            Filenames->push_back(Filedata->d_name);
    }
    closedir(Filehandle);

    return !!Filenames->size();
}
#endif

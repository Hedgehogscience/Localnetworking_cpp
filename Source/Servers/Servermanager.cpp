/*
    Initial author: Convery
    Started: 2017-4-13
    License: Apache 2.0
*/

#include "Servers.h"
#include <unordered_map>
#include <functional>

// Forward declarations for  platform specific functionality.
bool Listfiles(std::string Searchpath, std::vector<std::string> *Filenames, const char *Extension = 0);
void *FindFunction(const void *Module, const char *Function);
void *LoadModule(const char *Path);

// Module and server storage.
std::vector<void * /* Handles */> Networkmodules;
std::unordered_map<size_t /* Socket */, IServer * /* Server */> ServersbySocket;
std::unordered_map<std::string /* Address */, IServer * /* Server */> ServersbyAddress;

// Create a server based on the hostname, returns null if there's no handler.
IServer *Createserver(const size_t Socket, const char *Hostname)
{
    for (auto &Item : Networkmodules)
    {
        // Find the export in the module.
        auto pFunction = FindFunction(Item, "Createserver");
        if (!pFunction) continue;

        // Ask the module to create a new instance for the hostname.
        auto Function = (IServer * (*)(const char *))pFunction;
        auto Result = Function(Hostname);

        if (Result)
        {
            ServersbyAddress[Hostname] = Result;
            ServersbySocket[Socket] = Result;
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
IServer *Findserver(const char *Address)
{
    auto Result = ServersbyAddress.find(Address);
    if (Result != ServersbyAddress.end())
        return Result->second;
    else
        return nullptr;
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
            if (Listfiles(Path, &Filenames, "Networkmodule"))
            {
                for (auto &Item : Filenames)
                {
                    // Load the library into memory.
                    auto Module = LoadModule((Path + Item).c_str());
                    if (!Module) continue;

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
bool Listfiles(std::string Searchpath, std::vector<std::string> *Filenames, const char *Extension)
{
    WIN32_FIND_DATAA Filedata;
    HANDLE Filehandle;

    // Append trailing slash, asterisk and extension.
    if (Searchpath.back() != '/') Searchpath.append("/");
    Searchpath.append("*");
    if (Extension) Searchpath.append(".");
    if (Extension) Searchpath.append(Extension);

    // Find the first extension.
    Filehandle = FindFirstFileA(Searchpath.c_str(), &Filedata);
    if (Filehandle == (void *)ERROR_INVALID_HANDLE || Filehandle == (void *)INVALID_HANDLE_VALUE)
    {
        if(Filehandle) FindClose(Filehandle);
        return false;
    }

    do
    {
        // Respect hidden files.
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
void *FindFunction(const void *Module, const char *Function)
{
    return (void *)dlsym(Module, Function);
}
void *LoadModule(const char *Path)
{
    return (void *)dlopen(Path, RTLD_NOW);
}
bool Listfiles(std::string Searchpath, std::vector<std::string> *Filenames, const char *Extension)
{
    struct stat Fileinfo;
    dirent *Filedata;
    std::string Path;
    DIR *Filehandle;

    // Append trailing slash, asterisk and extension.
    if (Searchpath.back() != '/') Searchpath.append("/");
    Path = Searchpath;
    Searchpath.append("*");
    if (Extension) Searchpath.append(".");
    if (Extension) Searchpath.append(Extension);

    // Iterate through the directory.
    Filehandle = opendir(Searchpath.c_str());
    while ((Filedata = readdir(Filehandle)))
    {
        // Respect hidden files and folders.
        if (Filedata->d_name[0] == '.')
            continue;

        // Get extended fileinfo.
        std::string Filepath = Path + "/" + Filedata->d_name;
        if (stat(Filepath.c_str(), &Fileinfo) == -1) continue;

        // Add the file to the list.
        if (!(Fileinfo.st_mode & S_IFDIR))
            Filenames->push_back(Filedata->d_name);
    }
    closedir(Filehandle);

    return !!Filenames->size();
}
#endif
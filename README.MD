# Localnetworking - an AYRIA plugin.

This plugin is hosted at https://github.com/Hedgehogscience/Localnetworking_cpp.

This plugin intends to intercept network traffic and redirect it to a local [module](https://github.com/Hedgehogscience/Networkingtemplate_cpp) implementing the [IServer.hpp](https://github.com/Hedgehogscience/Localnetworking_cpp/blob/master/Source/Core/Interfaces/IServer.hpp) interface. Such modules are plain shared libraries with a single essential [export](https://github.com/Hedgehogscience/Networkingtemplate_cpp/blob/master/Source/Appmain.cpp#L11-L20) that creates a local server for a given hostname. The modules (shared libraries) use the file extension '.LN32/64' and is stored in a ZIP archive with the fileextension '.Localnet' in the applications Plugins directory. Developers may sideload a module by naming it 'Developermodule.dll/.so'.

This is a plugin for the AYRIA platform and as such it relies on the [bootstrapper](https://github.com/AyriaPublic/Bootstrapmodule_cpp) for initialization. We do aim to support more platforms in the future, but support is usually implemented when required for other plugins/applications.

- [x] Winsock_POSIX
- [ ] Winsock_async/WSA
- [ ] WinHTTP
- [ ] WinINET
- [ ] Unix_POSIX

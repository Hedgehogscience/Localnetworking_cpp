/*
    Initial author: Convery (tcn@ayria.se)
    Started: 18-10-2017
    License: MIT
    Notes:
        The internal networking system.
*/

#pragma once
#include "../Stdinclude.h"
#include "Interfaces/IServer.h"

namespace Localnetworking
{
    // Create a new server-instance.
    IServer *Createinstance(std::string Hostname);

    // Layered networking.
    namespace Layer3
    {
        struct Frame_t
        {
            IPAddress_t To;
            IPAddress_t From;
            std::string Databuffer;
        };

        // Set endpoints for the networking.
        void Addendpoint(size_t Identifier);
        void Removeendpoint(size_t Identifier);

        // Set filters for the endpoints.
        void Addfilter(size_t Identifier, IPAddress_t Filter);
        void Removefilter(size_t Identifier, IPAddress_t Filter);

        // Perform IO the network.
        void Appendframe(Frame_t &Frame);
        bool Removeframe(Frame_t &Frame);
    }
}

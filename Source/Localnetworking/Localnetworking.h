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
}

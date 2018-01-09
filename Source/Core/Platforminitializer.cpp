/*
    Initial author: Convery (tcn@ayria.se)
    Started: 09-01-2018
    License: MIT
    Notes:
        Provides initialization for the platforms.
*/

#include "../Stdinclude.hpp"

namespace Localnetworking
{
    std::vector<std::function<void()>> *Initializers;

    // Initialize the platform hooks.
    void Addplatform(std::function<void()> Callback)
    {
        if (!Initializers) Initializers = new std::vector<std::function<void()>>();
        Initializers->push_back(Callback);
    }
    void Initializeplatforms()
    {
        for (auto &Item : *Initializers)
        {
            Item();
        }
    }
}

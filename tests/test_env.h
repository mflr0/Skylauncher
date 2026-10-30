// Setting a variable for a test: SDL_getenv and getenv read two different environments on Windows, and the code under
// test uses both, so a variable set in one alone leaves it on the player's real paths.
#pragma once

#include <SDL3/SDL.h>
#include <cstdlib>

inline int TestSetEnv(const char* name, const char* value)
{
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value, true);
#ifdef _WIN32
    return _putenv_s(name, value);
#else
    return setenv(name, value, 1);
#endif
}

inline int TestUnsetEnv(const char* name)
{
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), name);
#ifdef _WIN32
    return _putenv_s(name, "");
#else
    return unsetenv(name);
#endif
}

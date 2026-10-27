#pragma once
#include "shared/env.h"
#include <SDL3/SDL.h>

// An environment variable of the launcher, read from SDL's environment (where the tests and the window set theirs) under
// its current name, else its older ones: LauncherEnv("SKY_LAUNCHER_DATA") also finds SKYLAUNCHER_DATA and
// SSA_LAUNCHER_DATA (shared/env.h).
inline const char* LauncherEnv(const char* name)
{
    return skyenv::Get(name, [](const char* variable) { return SDL_getenv(variable); });
}

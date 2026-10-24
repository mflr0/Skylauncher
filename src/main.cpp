// Skylauncher: shows the launcher, then starts the game runtime with the disc image the player chose.
#include "launcher/game_install.h"
#include "launcher/launcher.h"
#include "launcher/ui/launcher_ui.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <string>

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    const GameInfo* game = FindGame("ssa");
    if (game == nullptr)
        return 1;

    std::string configPath = LauncherConfigPath();
    LauncherConfig config = LoadLauncherConfig(configPath);
    std::string installed;
    std::string chosen = RunLauncherWindow(*game, configPath, config, config[game->id]["iso"], &installed);
    if (chosen.empty())
        return 0;

    // SKYLAUNCHER_RUNTIME, else the game built for this disc, else the player's own runtime.
    std::string runtime = FindRuntime(*game, config, true);
    if (runtime.empty())
        runtime = !installed.empty() ? installed : FindRuntime(*game, config);
    if (runtime.empty())
    {
        std::string message = std::string("The game runtime (") + game->runtime + ") was not found next to the launcher.\n\n"
            "Put it in the same folder, or set its path with \"runtime=\" in the [launcher] section of:\n" + configPath;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, game->name, message.c_str(), nullptr);
        return 1;
    }

    // Settings the runtime already takes from its environment (the rest it reads from launcher.ini; docs/settings.md).
    auto pass = [&](const char* variable, const char* section, const char* key) {
        auto entry = config.find(section);
        if (entry == config.end())
            return;
        auto value = entry->second.find(key);
        if (value != entry->second.end() && !value->second.empty())
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), variable, value->second.c_str(), true);
    };
    pass("SSA_PORTAL", "portal", "mode");
    pass("SSA_PORTAL_PLACE", "portal", "place");
    pass("SSA_LANGUAGE", game->id, "language");
    pass("SSA_VOICE_LANGUAGE", game->id, "voice_language");

    const char* arguments[] = { runtime.c_str(), chosen.c_str(), nullptr };
    SDL_Process* process = SDL_CreateProcess(arguments, false);
    if (process == nullptr)
    {
        std::string message = "Could not start " + runtime + ": " + SDL_GetError();
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, game->name, message.c_str(), nullptr);
        return 1;
    }
    SDL_DestroyProcess(process); // the game keeps running on its own
    SDL_Quit();
    return 0;
}

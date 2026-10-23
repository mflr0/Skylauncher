// Skylauncher: shows the launcher, then starts the game runtime with the disc image the player chose.
#include "launcher.h"
#include "launcher_ui.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    // The runtime that plays `game`: SKYLAUNCHER_RUNTIME, else "[launcher] runtime=" in the settings file, else the
    // game's runtime executable next to the launcher (or inside its macOS app bundle). Empty when none exists.
    std::string FindRuntime(const GameInfo& game, LauncherConfig& config)
    {
        std::vector<std::filesystem::path> candidates;
        if (const char* path = SDL_getenv("SKYLAUNCHER_RUNTIME"))
            candidates.emplace_back(path);
        if (auto section = config.find("launcher"); section != config.end())
            if (auto runtime = section->second.find("runtime"); runtime != section->second.end() && !runtime->second.empty())
                candidates.emplace_back(runtime->second);

        if (const char* base = SDL_GetBasePath())
        {
            std::filesystem::path directory = base;
            std::string name = game.runtime;
#if defined(_WIN32)
            candidates.push_back(directory / (name + ".exe"));
#else
            candidates.push_back(directory / name);
            candidates.push_back(directory / (name + ".app") / "Contents" / "MacOS" / name);
#endif
        }

        for (std::filesystem::path path : candidates)
        {
            // An app bundle names its executable after itself.
            if (path.extension() == ".app")
                path = path / "Contents" / "MacOS" / path.stem();
            std::error_code error;
            if (std::filesystem::is_regular_file(path, error))
                return path.string();
        }
        return {};
    }
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    const GameInfo* game = FindGame("ssa");
    if (game == nullptr)
        return 1;

    std::string configPath = LauncherConfigPath();
    LauncherConfig config = LoadLauncherConfig(configPath);
    std::string chosen = RunLauncherWindow(*game, configPath, config, config[game->id]["iso"]);
    if (chosen.empty())
        return 0;

    std::string runtime = FindRuntime(*game, config);
    if (runtime.empty())
    {
        std::string message = std::string("The game runtime (") + game->runtime + ") was not found next to the launcher.\n\n"
            "Put it in the same folder, or set its path with \"runtime=\" in the [launcher] section of:\n" + configPath;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, game->name, message.c_str(), nullptr);
        return 1;
    }

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

// Skylauncher: shows the launcher, then starts the game runtime with the disc image the player chose.
#include "launcher/game_install.h"
#include "shared/collection/datadir.h"
#include "launcher/game_run.h"
#include "launcher/launcher.h"
#include "launcher/ui/launcher_ui.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <fmt/format.h>
#include <string>

namespace
{
    std::string UrlEncode(const std::string& text)
    {
        std::string out;
        for (unsigned char c : text)
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                out += char(c);
            else
                out += fmt::format("%{:02X}", c);
        return out;
    }

    // "The game stopped unexpectedly": Copy log (the dialog stays), Report (opens a GitHub issue, the log is on the
    // clipboard), Close.
    void ShowGameStopped(const GameInfo& game, int exitCode)
    {
        SDL_InitSubSystem(SDL_INIT_VIDEO);
        std::string message = fmt::format("{} stopped unexpectedly (exit code {}).\n\nIts log has what happened: press Copy log, "
            "then Report to tell us about it. The log stays on your computer until you paste it.\n\nLogs folder: {}", game.name, exitCode,
            GameLogFolder().string());
        const SDL_MessageBoxButtonData buttons[] = { { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 2, "Report" }, { 0, 1, "Copy log" },
            { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Close" } };
        SDL_MessageBoxData box{ SDL_MESSAGEBOX_ERROR, nullptr, "The game stopped unexpectedly", message.c_str(), 3, buttons, nullptr };
        for (;;)
        {
            int pressed = 0;
            if (!SDL_ShowMessageBox(&box, &pressed) || pressed == 0)
                return;
            SDL_SetClipboardText(GameLogText().c_str());
            if (pressed == 2)
            {
                std::string title = fmt::format("The game stopped unexpectedly (exit code {})", exitCode);
                std::string body = "What I was doing:\n\n\nThe log is in my clipboard, I paste it below:\n\n```\n\n```\n";
                SDL_OpenURL(("https://github.com/mflr0/Skylauncher/issues/new?title=" + UrlEncode(title) + "&body=" + UrlEncode(body)).c_str());
                return;
            }
        }
    }
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    datadir::MigrateOnce(); // the older folders into <data root> (docs/install.md), before any path is used
    std::string configPath = LauncherConfigPath();
    LauncherConfig config = LoadLauncherConfig(configPath);

    // The game last shown ([launcher] game=), else Spyro's Adventure. Picking another game in the sidebar closes the
    // window and opens it again for that game (the window's state is one game's).
    std::string id = config["launcher"]["game"];
    const GameInfo* game = FindGame(id);
    if (game == nullptr)
        game = FindGame("ssa");
    if (game == nullptr)
        return 1;
    std::string installed;
    std::string chosen;
    for (;;)
    {
        SetActiveGame(game->id);
        std::string switchTo;
        installed.clear();
        chosen = RunLauncherWindow(*game, configPath, config, config[game->id]["iso"], &installed, &switchTo);
        const GameInfo* next = switchTo.empty() ? nullptr : FindGame(switchTo);
        if (next == nullptr)
            break;
        game = next;
    }
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

    // A game built before the one data folder would look for its saves and settings in the older folders, which the
    // migration emptied: point it at the new ones until "Update the game" rebuilds it (docs/install.md, "Older builds").
    std::filesystem::path gameFolder = GameInstallFolder({});
    if (IsOlderLayoutBuild(std::filesystem::path(std::u8string(runtime.begin(), runtime.end())), gameFolder))
    {
        auto set = [](const char* variable, const std::filesystem::path& path) {
            std::u8string text = path.u8string();
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), variable, std::string(text.begin(), text.end()).c_str(), true);
        };
        set("SSA_LAUNCHER_INI", std::filesystem::path(std::u8string(configPath.begin(), configPath.end())));
        set("SSA_SAVE_DIR", gameFolder / "saves");
        set("SSA_LOG", gameFolder / "SSARecomp.log");
        set("SSA_PORTRAITS", gameFolder / "portraits");
    }

    // The launcher stays until the game ends: it keeps the game's output in skylauncher-game.log, and says so if the game
    // stops with an error (docs/support.md).
    GameRunResult run = RunGame(runtime, chosen);
    if (!run.started)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, game->name, run.error.c_str(), nullptr);
        return 1;
    }
    if (run.exitCode != 0)
        ShowGameStopped(*game, run.exitCode);
    SDL_Quit();
    return 0;
}

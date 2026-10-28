// Arkeyan Launcher: shows the launcher, then starts the game runtime with the disc image the player chose.
#include "launcher/game_install.h"
#include "launcher/game_settings.h"
#include "launcher/i18n.h"
#include "shared/collection/datadir.h"
#include "launcher/game_run.h"
#include "launcher/launcher.h"
#include "launcher/shortcut.h"
#include "launcher/ui/launcher_ui.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cstdio>
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
        std::string message = Tf("{} stopped unexpectedly (exit code {}).\n\nPress Copy log, then Report.", game.name, exitCode);
        const SDL_MessageBoxButtonData buttons[] = { { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 2, Tr("Report") }, { 0, 1, Tr("Copy log") },
            { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, Tr("Close") } };
        SDL_MessageBoxData box{ SDL_MESSAGEBOX_ERROR, nullptr, Tr("The game stopped unexpectedly"), message.c_str(), 3, buttons, nullptr };
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
    LauncherArguments arguments = ParseLauncherArguments(argc, argv);
    if (arguments.help)
    {
        fprintf(stderr, "%s\n", LauncherUsage());
#ifdef _WIN32
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Arkeyan Launcher", LauncherUsage(), nullptr); // a Windows app has no console
#endif
        return 0;
    }
    for (const std::string& unknown : arguments.unknown)
        fprintf(stderr, "launcher: ignoring the option %s (--help lists them)\n", unknown.c_str());
    datadir::MigrateOnce(); // the older folders into <data root> (docs/install.md), before any path is used
    std::string configPath = LauncherConfigPath();
    LauncherConfig config = LoadLauncherConfig(configPath);

    // Shortcuts made by a launcher called Skylauncher start this one and carry its name.
    {
        std::vector<std::pair<std::string, std::string>> shortcutGames;
        for (const GameInfo& info : g_games)
            shortcutGames.emplace_back(info.id, info.name);
        RefreshShortcuts(DesktopFolder(), shortcutGames, SelfExecutable());
    }

    // The game last shown ([launcher] game=), else Spyro's Adventure. Picking another game in the sidebar switches the
    // window to it in place; the game that was shown when PLAY was pressed is the one that runs.
    const GameInfo* game = arguments.game.empty() ? FindGame(config["launcher"]["game"]) : FindGame(arguments.game);
    if (!arguments.game.empty() && game == nullptr)
        fprintf(stderr, "launcher: no game called \"%s\" (ssa, giants or swapforce)\n", arguments.game.c_str());
    if (game == nullptr)
        game = FindGame(config["launcher"]["game"]);
    if (game == nullptr)
        game = FindGame("ssa");
    if (game == nullptr)
        return 1;
    std::string installed;
    LauncherOptions options;
    options.openGame = !arguments.game.empty() && FindGame(arguments.game) != nullptr;
    options.play = arguments.play;
    std::string chosen = RunLauncherWindow(*game, configPath, config, config[game->id]["iso"], &installed, &game, options);
    if (chosen.empty())
        return 0;

    // SKY_LAUNCHER_RUNTIME, else the game built for this disc, else the player's own runtime.
    std::string runtime = FindRuntime(*game, config, true);
    if (runtime.empty())
        runtime = !installed.empty() ? installed : FindRuntime(*game, config);
    if (runtime.empty())
    {
        std::string message = Tf("The game ({}) wasn't found next to Arkeyan Launcher.\n\nPut it in the same folder.", game->runtime);
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
            SetGameVariable(variable, value->second.c_str());
    };
    // The video settings of this game are what the runtime reads from [video] (game_settings.h).
    ApplyGameVideo(config, game->id);
    SaveLauncherConfig(configPath, config);
    pass("SKY_PORTAL", "portal", "mode");
    // [portal] place is the older list of figures to start on the portal; once the seats moved to the session file
    // ([collection] moved) a left-over one would put its figures back over the seats the player chose.
    if (config["collection"]["moved"] != "1")
        pass("SKY_PORTAL_PLACE", "portal", "place");
    // A language the disc does not hold is not passed as is: the text takes the disc's first language and the voices
    // follow the text, which is what the Language tab shows then. Nothing known about the disc: passed as saved.
    const art::DiscLanguages onDisc = StoredDiscLanguages(config, game->id);
    auto passLanguage = [&](const char* variable, const char* key, const std::vector<std::string>& held, bool firstHeld) {
        auto entry = config.find(game->id);
        if (entry == config.end())
            return;
        auto value = entry->second.find(key);
        if (value == entry->second.end() || value->second.empty())
            return;
        if (held.empty() || std::find(held.begin(), held.end(), value->second) != held.end())
            SetGameVariable(variable, value->second.c_str());
        else if (firstHeld)
            SetGameVariable(variable, held.front().c_str());
    };
    passLanguage("SKY_LANGUAGE", "language", onDisc.text, true);
    passLanguage("SKY_VOICE_LANGUAGE", "voice_language", onDisc.voice, false);

    // A game built before the one data folder would look for its saves and settings in the older folders, which the
    // migration emptied: point it at the new ones until "Update the game" rebuilds it (docs/install.md, "Older builds").
    std::filesystem::path gameFolder = GameInstallFolder({});
    if (IsOlderLayoutBuild(std::filesystem::path(std::u8string(runtime.begin(), runtime.end())), gameFolder))
    {
        auto set = [](const char* variable, const std::filesystem::path& path) {
            std::u8string text = path.u8string();
            SetGameVariable(variable, std::string(text.begin(), text.end()).c_str());
        };
        set("SKY_LAUNCHER_INI", std::filesystem::path(std::u8string(configPath.begin(), configPath.end())));
        set("SKY_SAVE_DIR", gameFolder / "saves");
        set("SKY_LOG", gameFolder / "SSARecomp.log");
        set("SKY_PORTRAITS", gameFolder / "portraits");
    }

    // The launcher stays until the game ends: it keeps the game's output in skylauncher-game.log, and says so if the game
    // stops with an error (docs/support.md).
    GameRunResult run = RunGame(runtime, chosen);
    // What the game changed in [video] (its own Settings menu) is this game's again; the other game keeps its own.
    {
        LauncherConfig after = LoadLauncherConfig(configPath);
        StoreGameVideo(after, game->id);
        SaveLauncherConfig(configPath, after);
    }
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

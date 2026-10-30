#pragma once

#include "launcher/game_names.h"
#include "launcher/launcher.h"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// A game tile shown in the launcher sidebar. Only entries backed by a runtime in the game table can be played;
// the rest are placeholders for the other Skylanders games.
struct LauncherGameEntry
{
    const char* id;
    const char* name; // the short name (GameName)
    uint32_t accent; // 0xRRGGBB
    bool enabled;
};

// The console games in release order; experimental entries follow the saved setting.
std::span<const LauncherGameEntry> LauncherGameCatalog(const LauncherConfig& config = {});

// Shows the launcher window until the player quits, or the screenshot test script finishes (with `options.startGames`, PLAY
// starts the game and the window stays; without, PLAY closes it). Returns the chosen image path (empty when nothing was chosen).
// `config` is updated for settings, and `[<game>] iso=` is written when a valid image is chosen. `installed`, when set,
// receives the game the launcher built for the chosen disc (empty when there is none). When the player picks another
// enabled game in the sidebar the window stays and shows that game (each game's state is kept for the session); `played`
// receives the game the chosen image is for. The last game shown is remembered in `[launcher] game=`.
// `options` are the command line's (docs/launcher_ui.md, "Command line").
struct LauncherOptions
{
    bool openGame = false; // --game <id>: open on that game's Play page
    bool play = false;     // --play: start the game as soon as its disc is checked and it is built
    bool startGames = false; // PLAY starts the game and the window stays (GameSession); else PLAY closes it and returns the choice
};
std::string RunLauncherWindow(const GameInfo& game, const std::string& configPath, LauncherConfig& config,
    const std::string& previous, std::string* installed = nullptr, const GameInfo** played = nullptr, const LauncherOptions& options = {});

// Small pure helpers, also used by SSALauncherTest.
std::string LauncherLanguageLabel(const LauncherConfig& config, const GameInfo& game);
std::string LauncherVoiceLabel(const LauncherConfig& config, const GameInfo& game);
bool LauncherFullscreen(const LauncherConfig& config);
std::string LauncherFormatSize(uint64_t bytes);
std::string LauncherMiddleEllipsis(std::string_view text, size_t maxCharacters);
std::string LauncherDisplayName(std::string_view xdbfName, const GameInfo& game);


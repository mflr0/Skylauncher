#pragma once

#include "shared/art/disc_languages.h"
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

// A disc release a runtime was recompiled from, identified by its default.xex.
struct GameRelease
{
    const char* label;            // shown to the player, e.g. "USA, Europe"
    uint32_t titleId;
    uint32_t mediaId;
    uint32_t version;             // XEX execution info version
    const char* defaultXexSha256; // lowercase hex
};

struct GameInfo
{
    const char* id;             // settings file section
    const char* name;           // display name
    std::span<const GameRelease> releases;
    const char* runtime;        // executable that runs this game
};

// Supported games (host/launcher_games.cpp). Adding a game is adding a table entry.
extern const std::span<const GameInfo> g_games;
extern const char* const g_launcherName; // folder of the settings file in the user's data directory

const GameInfo* FindGame(std::string_view id);

enum class GameSourceStatus
{
    Ok,
    NotFound,
    Unreadable,
    Folder,         // extracted game folder: the disc reader only reads images for now
    NotXboxDisc,    // no XDVDFS volume, or the image is cut short before it
    NotXbox360Game, // XDVDFS volume without default.xex (original Xbox or data disc)
    Damaged,        // default.xex missing bytes or not an XEX2 executable
    WrongGame,      // another title ID
    WrongVersion,   // this title, but a default.xex no runtime was recompiled from
};

struct GameSourceCheck
{
    GameSourceStatus status{};
    std::string message; // why the source can't be used, for the player
    std::string xexSha256; // the verified default.xex (Ok only): which installed game belongs to this disc
    std::string otherGame; // WrongGame, when the disc is another supported game: its id ("This disc is Spyro's Adventure")
    art::DiscLanguages languages; // the text and voice languages the disc holds (Ok only; empty lists: not known)
};

std::string Sha256Hex(std::span<const uint8_t> data); // lowercase hex

// Checks that `path` is a disc image of one of `game`'s known releases, or the folder of the copied game data
// (shared/collection/game_data.h). `progress`, when set, receives
// a 0..1 fraction and a short description of the current step; it may be called from a worker thread.
using GameSourceProgress = std::function<void(float, std::string_view)>;
GameSourceCheck CheckGameSource(const std::string& path, const GameInfo& game, const GameSourceProgress& progress = {});

// Name and icon from the XDBF resource of a verified default.xex. Game content: keep it in memory only.
struct TitleMetadata
{
    std::string name;
    std::vector<uint8_t> iconPng;
    uint32_t titleId = 0;
};
TitleMetadata ReadTitleMetadata(const std::vector<uint8_t>& xex);

// Settings file: "[section]" lines, "key=value" lines, '#' comment lines. Keys before any section go to "".
using LauncherConfig = std::map<std::string, std::map<std::string, std::string>>;
std::string LauncherConfigPath();
LauncherConfig LoadLauncherConfig(const std::string& path);
bool SaveLauncherConfig(const std::string& path, const LauncherConfig& config);
// After "Uninstall everything" has removed the settings file: SaveLauncherConfig writes nothing any more.
void FreezeLauncherConfig();

// The environment a game or a builder is started with: the launcher's own, and every variable of the SKY_ family set under one of
// its names also under the others (SKY_X, SSA_X; SKY_LAUNCHER_X, SKYLAUNCHER_X, SSA_LAUNCHER_X), without replacing one already set. A
// game built by an older kit reads only the SSA_ names, a newer one the SKY_ names (shared/env.h). The launcher's own environment is
// not changed. The caller passes it as SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER and destroys it (SDL_DestroyEnvironment).
// `set`: variables given on top (a game's settings: GameLaunch), each under all its names, replacing what they held.
struct SDL_Environment;
SDL_Environment* CreateGameEnvironment(const std::vector<std::pair<std::string, std::string>>& set = {});
// Sets a variable under all its names, replacing what they held.
void SetGameVariable(const char* currentName, const char* value);
// Removes a variable under all its names ("SKY_LAUNCHER_SHOTS" also removes SKYLAUNCHER_SHOTS and SSA_LAUNCHER_SHOTS).
void UnsetGameVariable(const char* currentName);

// The command line: `--game <id>` opens that game, `--play` starts it once its disc is checked (the desktop shortcut runs
// `Arkeyan Launcher --game <id> --play`), `--help` shows the options. Anything else is listed in `unknown` and ignored.
struct LauncherArguments
{
    std::string game;
    bool play = false;
    bool help = false;
    std::vector<std::string> unknown;
};
LauncherArguments ParseLauncherArguments(int argc, const char* const* argv);
const char* LauncherUsage();

// The game's text and voice language codes (see language.h): SKY_LANGUAGE and SKY_VOICE_LANGUAGE, else the
// "language" and "voice_language" keys of the game's section in the settings file. Empty where neither picks one.
struct LanguageChoice
{
    std::string text;
    std::string voice;
};
LanguageChoice ResolveLanguages(const GameInfo& game);

// The languages the game's disc holds, kept in the game's section after each disc check ("disc_text_languages",
// "disc_voice_languages": codes joined by commas) so a restart shows them before the disc is read again. Empty lists remove the keys.
void StoreDiscLanguages(LauncherConfig& config, std::string_view game, const art::DiscLanguages& languages);
art::DiscLanguages StoredDiscLanguages(const LauncherConfig& config, std::string_view game);
// Whether a language list offers `code`: the empty list (not known) offers every code, and the empty code ("Same as text") is always offered.
bool OffersLanguage(const std::vector<std::string>& onDisc, std::string_view code);

// Picks the game's disc image: the command-line path, else the remembered one, else asks with a file dialog
// and remembers the answer. `--select-iso` or holding Option (macOS) at startup asks again.
// Returns an empty string when the program should quit. Never shows UI unless `interactive`.
std::string ResolveGameSource(int argc, char** argv, const GameInfo& game, bool interactive);

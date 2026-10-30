#pragma once

// Per-game settings and a game's data (docs/launcher_ui.md, "Settings", and docs/install.md, "Delete data and uninstall").
// No UI and no SDL: ArkeyanLauncherTest runs these on scratch folders.
//
// Video settings are per game. The runtime reads one [video] section of launcher.ini, so each game keeps its own copy in
// [<game>.video]: the game's settings page edits that copy, PLAY puts it in [video] for the runtime, and when the game
// ends whatever the runtime changed in [video] (its own Settings menu) is kept back in the copy. Input, the portal and the
// launcher's own keys are shared by both games and stay where they were.
#include "launcher/launcher.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// The [video] keys that belong to a game (docs/settings.md).
std::span<const char* const> GameVideoKeys();
std::string GameVideoSection(std::string_view gameId); // "giants.video"
// Starts a game's copy from the shared [video] the first time (nothing happens when the copy exists).
void SeedGameVideo(LauncherConfig& config, std::string_view gameId);
// PLAY: the game's copy replaces [video].
void ApplyGameVideo(LauncherConfig& config, std::string_view gameId);
// The game ended: [video] (what the runtime may have changed) is the game's copy again.
void StoreGameVideo(LauncherConfig& config, std::string_view gameId);

// Once per settings file ([launcher] frame_rates=1 says it was done): the frame rates 0, 120, 144 and 240, which older versions
// accepted and ran as 60, become 60 in [video] and [<game>.video], so an update does not change what a player had. A choice made
// afterwards stays.
void MigrateFrameRates(LauncherConfig& config);

// What a game keeps in its folder (<data>/SSA, <data>/Giants), told apart for the delete and uninstall buttons.
struct GameFolders
{
    std::filesystem::path game;  // the game's folder
    std::filesystem::path saves; // its saves (SKY_SAVE_DIR, else <game>/saves); the backups are <saves>-backup
};
GameFolders FoldersOf(std::string_view gameId);

// Sizes shown before anything is deleted.
struct GameDataSizes
{
    uint64_t saves = 0;     // the saves, their backups and the seats of the next session (portal.txt)
    uint64_t caches = 0;    // the shader cache, the art cache, the portraits, the logs: rebuilt by the game
    uint64_t installed = 0; // the built game and the rest of the folder, what uninstalling frees
};
GameDataSizes MeasureGameData(const GameFolders& folders);

struct DataResult
{
    bool ok = true;
    uint64_t freed = 0;
    std::string error; // why nothing happened (the game runs), or the first thing that could not go
};

// Caches only: nothing the player made. Refused while the game runs.
DataResult ClearGameCaches(const GameFolders& folders);
// The saves, their backups, the seats of the next session and the caches. Kept: the built game, the disc, the settings,
// the textures folder, and the shared figure collection (it is not in the game's folder).
DataResult DeleteGameData(const GameFolders& folders);
// The built game and its install logs go; the saves, backups, seats, textures and the art cache stay, so installing again
// brings the player back to where they were. Also forgets what the settings file knows of the build ([install.<sha>]).
DataResult UninstallGame(const GameFolders& folders, LauncherConfig& config);

bool GameIsRunning(const GameFolders& folders);

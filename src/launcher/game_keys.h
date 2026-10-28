#pragma once

// The keyboard keys of each game (docs/settings.md, "Keyboard actions"). The runtime reads one [input] section of
// launcher.ini (key_<action>=<SDL key name>), so each game keeps its own copy in [<game>.input]: the game's settings
// page edits that copy and PLAY puts it in [input]. A game lists only the actions it uses, named as the game names
// them. No UI: ArkeyanLauncherTest runs these on a settings map.
#include "launcher/launcher.h"
#include <SDL3/SDL_keycode.h>
#include <span>
#include <string>
#include <string_view>

struct GameKey
{
    const char* action; // the runtime's name: key_<action> in [input]
    const char* label;  // English, as the game words it (translated where it is shown)
    const char* pad;    // the pad button the key stands for (shown in brackets, not translated)
    SDL_Keycode fallback; // the game's default key
};

// The rows of a game's Keyboard tab, in display order (empty for a game this launcher does not know).
std::span<const GameKey> GameKeys(std::string_view gameId);
// Every action the runtime knows, used or not by a game.
std::span<const char* const> InputActions();
std::string GameInputSection(std::string_view gameId); // "giants.input"

// The key's name now: the game's copy, else its default.
std::string GameKeyName(const LauncherConfig& config, std::string_view gameId, const GameKey& key);
// True when another row of the same game has the same key.
bool GameKeyConflict(const LauncherConfig& config, std::string_view gameId, const GameKey& key);
// Back to the game's defaults: its copy loses every key it had.
void ResetGameKeys(LauncherConfig& config, std::string_view gameId);

// Starts a game's copy from the shared [input] keys the first time (nothing happens once it has been seeded), so a
// player keeps the keys they had.
void SeedGameInput(LauncherConfig& config, std::string_view gameId);
// PLAY: the game's keys replace the key_ lines of [input] (the device and the glyph style stay as they are).
void ApplyGameInput(LauncherConfig& config, std::string_view gameId);

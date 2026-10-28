#pragma once

// The keyboard keys of each game (docs/settings.md, "Keyboard actions"). The runtime reads one [input] section of
// launcher.ini (key_<action>=<SDL scancode name>: a key's physical place, as the game reads it), so each game keeps
// its own copy in [<game>.input]: the game's settings page edits that copy and PLAY puts it in [input]. A game lists only
// the actions it uses, named as the game names them. No UI: ArkeyanLauncherTest runs these on a settings map.
#include "launcher/launcher.h"
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>
#include <span>
#include <string>
#include <string_view>

struct GameKey
{
    const char* action; // the runtime's name: key_<action> in [input]
    const char* label;  // English, as the game words it (translated where it is shown)
    const char* pad;    // the Xbox 360 pad's button the key stands for (shown in brackets for the pad in use: PadButtonName)
    SDL_Scancode fallback; // the game's default key (a physical place: W is Z on an AZERTY keyboard)
};

// The rows of a game's Keyboard tab, in display order (empty for a game this launcher does not know).
std::span<const GameKey> GameKeys(std::string_view gameId);
// Every action the runtime knows, used or not by a game.
std::span<const char* const> InputActions();
std::string GameInputSection(std::string_view gameId); // "giants.input"

// The key's name now: the game's copy, else its default.
std::string GameKeyName(const LauncherConfig& config, std::string_view gameId, const GameKey& key);
// A stored key as the player reads it: the letter its place has on the player's keyboard layout (W's place is "Z" on
// AZERTY), the named keys in the player's language ("Espace"). The stored value stays SDL's English scancode name.
std::string KeyDisplayName(const std::string& sdlName);
// What a captured key stores: its place's SDL name (SDL_GetScancodeName), whatever the layout prints on it.
std::string CapturedKeyName(SDL_Scancode scancode);

// The pad families the button names follow (the game's glyphs do the same: docs/settings.md, "Keyboard actions").
enum class PadFamily { Xbox360, XboxOne, PlayStation, PlayStation3, Nintendo };
PadFamily PadFamilyOf(SDL_GamepadType type);
// The family to name buttons for: a fixed [input] glyphs style wins (xbox360, xboxone, playstation, nintendo; "xbox"
// keeps an Xbox One pad's names, else the 360's), else the pad's (`seen`: the chosen pad, or the last one seen).
PadFamily PadFamilyFor(std::string_view glyphs, PadFamily seen);
// The button a key stands for, named as that family's pad prints it, in the player's language where it is a word
// (PlayStation's shapes, the sticks, the touchpad): "A" on a 360 pad, "Cross" on a PS5, "B" on a Switch (its south).
std::string PadButtonName(const GameKey& key, PadFamily family);
// True when another row of the same game has the same key.
bool GameKeyConflict(const LauncherConfig& config, std::string_view gameId, const GameKey& key);
// Back to the game's defaults: its copy loses every key it had.
void ResetGameKeys(LauncherConfig& config, std::string_view gameId);

// Starts a game's copy from the shared [input] keys the first time (nothing happens once it has been seeded), so a
// player keeps the keys they had.
void SeedGameInput(LauncherConfig& config, std::string_view gameId);
// PLAY: the game's keys replace the key_ lines of [input] (the device and the glyph style stay as they are).
void ApplyGameInput(LauncherConfig& config, std::string_view gameId);

#include "launcher/game_keys.h"
#include "launcher/i18n.h"
#include <array>
#include <SDL3/SDL_keyboard.h>

namespace
{
    constexpr std::array<const char*, 22> Actions = { "a", "b", "x", "y", "start", "back", "lb", "rb", "lt", "rt", "left_up", "left_down",
        "left_left", "left_right", "right_up", "right_down", "right_left", "right_right", "dpad_up", "dpad_down", "dpad_left", "dpad_right" };

    // The defaults, shared by the games: the pad's buttons sit on the same keys in each (A Space, X C, Y V, B X).
    // Names and buttons: each game's own controls screen, read in the running game (docs/settings.md, "Where the names come from").
    #define MOVE_ROWS \
        { "left_up", N_("Move up"), N_("L stick"), SDLK_W }, { "left_down", N_("Move down"), N_("L stick"), SDLK_S }, \
        { "left_left", N_("Move left"), N_("L stick"), SDLK_A }, { "left_right", N_("Move right"), N_("L stick"), SDLK_D }

    constexpr GameKey Ssa[] = {
        MOVE_ROWS,
        { "a", N_("1st Power / Accept"), "A", SDLK_SPACE }, { "x", N_("2nd Power"), "X", SDLK_C },
        { "y", N_("Use / Talk"), "Y", SDLK_V }, { "b", N_("Special Power / Back"), "B", SDLK_X },
        { "back", N_("Skylander Info"), "Back", SDLK_BACKSPACE }, { "start", N_("Pause"), "Start", SDLK_RETURN },
        { "right_up", N_("Shake / Insert / Swing up"), N_("R stick"), SDLK_I }, { "right_down", N_("Shake / Insert / Swing down"), N_("R stick"), SDLK_K },
        { "right_left", N_("Shake / Insert / Swing left"), N_("R stick"), SDLK_J }, { "right_right", N_("Shake / Insert / Swing right"), N_("R stick"), SDLK_L },
    };
    constexpr GameKey Giants[] = {
        MOVE_ROWS,
        { "a", N_("Attack 1 / Accept"), "A", SDLK_SPACE }, { "x", N_("Attack 2"), "X", SDLK_C },
        { "y", N_("Action"), "Y", SDLK_V }, { "b", N_("Attack 3 / Back"), "B", SDLK_X },
        { "back", N_("Skylander Info"), "Back", SDLK_BACKSPACE }, { "start", N_("Pause"), "Start", SDLK_RETURN },
    };
    constexpr GameKey SwapForce[] = {
        MOVE_ROWS,
        { "a", N_("Jump"), "A", SDLK_SPACE }, { "x", N_("Attack 1"), "X", SDLK_C },
        { "b", N_("Attack 2"), "B", SDLK_X }, { "y", N_("Attack 3"), "Y", SDLK_V },
        { "rb", N_("Attack 2"), "RB", SDLK_E }, { "lb", N_("Attack 3"), "LB", SDLK_Q },
        { "lt", N_("Interact"), "LT", SDLK_1 }, { "rt", N_("Interact"), "RT", SDLK_3 },
        { "back", N_("Skylanders Stats"), "Back", SDLK_BACKSPACE }, { "start", N_("Pause"), "Start", SDLK_RETURN },
    };

    // The SDL names of the keys the launcher words in the player's language (the others, letters and digits, are the same everywhere).
    constexpr std::array<const char*, 16> KeyNames = { N_("Space"), N_("Return"), N_("Backspace"), N_("Escape"), N_("Up"), N_("Down"),
        N_("Left"), N_("Right"), N_("Left Shift"), N_("Right Shift"), N_("Left Ctrl"), N_("Right Ctrl"), N_("Left Alt"), N_("Right Alt"),
        N_("Caps Lock"), N_("Delete") };

    std::string KeyLine(const char* action) { return std::string("key_") + action; }

    const std::string* Find(const LauncherConfig& config, const std::string& section, const std::string& key)
    {
        auto entry = config.find(section);
        if (entry == config.end())
            return nullptr;
        auto value = entry->second.find(key);
        return value == entry->second.end() || value->second.empty() ? nullptr : &value->second;
    }

    const GameKey* Row(std::string_view gameId, std::string_view action)
    {
        for (const GameKey& key : GameKeys(gameId))
            if (action == key.action)
                return &key;
        return nullptr;
    }
}

std::span<const GameKey> GameKeys(std::string_view gameId)
{
    if (gameId == "ssa")
        return Ssa;
    if (gameId == "giants")
        return Giants;
    if (gameId == "swapforce")
        return SwapForce;
    return {};
}

std::span<const char* const> InputActions() { return Actions; }

std::string GameInputSection(std::string_view gameId) { return std::string(gameId) + ".input"; }

std::string GameKeyName(const LauncherConfig& config, std::string_view gameId, const GameKey& key)
{
    if (const std::string* value = Find(config, GameInputSection(gameId), KeyLine(key.action)))
        return *value;
    return SDL_GetKeyName(key.fallback);
}

std::string KeyDisplayName(const std::string& sdlName)
{
    for (const char* name : KeyNames)
        if (sdlName == name)
            return i18n::Tr(name);
    return sdlName;
}

bool GameKeyConflict(const LauncherConfig& config, std::string_view gameId, const GameKey& key)
{
    const std::string name = GameKeyName(config, gameId, key);
    for (const GameKey& other : GameKeys(gameId))
        if (&other != &key && GameKeyName(config, gameId, other) == name)
            return true;
    return false;
}

void ResetGameKeys(LauncherConfig& config, std::string_view gameId)
{
    auto section = config.find(GameInputSection(gameId));
    if (section == config.end())
        return;
    for (const char* action : Actions)
        section->second.erase(KeyLine(action));
}

void SeedGameInput(LauncherConfig& config, std::string_view gameId)
{
    const std::string section = GameInputSection(gameId);
    if (config.count(section) != 0 && config[section].count("seeded") != 0)
        return;
    auto shared = config.find("input");
    for (const char* action : Actions)
    {
        if (shared == config.end())
            break;
        if (auto value = shared->second.find(KeyLine(action)); value != shared->second.end())
            config[section].emplace(KeyLine(action), value->second); // a key the game already has stays
    }
    config[section]["seeded"] = "1"; // an empty copy is not written to the file: this is what says it was seeded
}

void ApplyGameInput(LauncherConfig& config, std::string_view gameId)
{
    const std::string section = GameInputSection(gameId);
    for (const char* action : Actions)
    {
        const std::string line = KeyLine(action);
        if (const std::string* value = Find(config, section, line))
            config["input"][line] = *value;
        else if (const GameKey* row = Row(gameId, action))
            config["input"][line] = SDL_GetKeyName(row->fallback);
        else if (auto shared = config.find("input"); shared != config.end())
            shared->second.erase(line); // not one of this game's actions: the runtime's own default
    }
}

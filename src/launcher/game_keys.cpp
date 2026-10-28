#include "launcher/game_keys.h"
#include "launcher/i18n.h"
#include <array>
#include <SDL3/SDL_keyboard.h>

namespace
{
    constexpr std::array<const char*, 22> Actions = { "a", "b", "x", "y", "start", "back", "lb", "rb", "lt", "rt", "left_up", "left_down",
        "left_left", "left_right", "right_up", "right_down", "right_left", "right_right", "dpad_up", "dpad_down", "dpad_left", "dpad_right" };

    // The defaults, shared by the games: the pad's buttons sit on the same keys in each (A Space, X C, Y V, B X).
    // Names: SSA from the PC key list on its disc ("1st Power/Accept", "Special Power/Back"...), Giants and Swap Force
    // from the controls screens of their disc (docs/settings.md, "Where the names come from").
    #define MOVE_ROWS \
        { "left_up", N_("Move up"), "L stick", SDLK_W }, { "left_down", N_("Move down"), "L stick", SDLK_S }, \
        { "left_left", N_("Move left"), "L stick", SDLK_A }, { "left_right", N_("Move right"), "L stick", SDLK_D }
    #define CAMERA_ROWS \
        { "right_up", N_("Camera up"), "R stick", SDLK_I }, { "right_down", N_("Camera down"), "R stick", SDLK_K }, \
        { "right_left", N_("Camera left"), "R stick", SDLK_J }, { "right_right", N_("Camera right"), "R stick", SDLK_L }

    constexpr GameKey Ssa[] = {
        MOVE_ROWS,
        { "a", N_("1st Power / Accept"), "A", SDLK_SPACE }, { "b", N_("2nd Power / Back"), "B", SDLK_X },
        { "x", N_("Use / Talk"), "X", SDLK_C }, { "y", N_("Special Power"), "Y", SDLK_V },
        { "back", N_("Skylander Info"), "Back", SDLK_BACKSPACE }, { "start", N_("Pause"), "Start", SDLK_RETURN },
        CAMERA_ROWS,
    };
    constexpr GameKey Giants[] = {
        MOVE_ROWS,
        { "a", N_("Action / Accept"), "A", SDLK_SPACE }, { "x", N_("Attack 1"), "X", SDLK_C },
        { "y", N_("Attack 2"), "Y", SDLK_V }, { "b", N_("Attack 3 / Back"), "B", SDLK_X },
        { "back", N_("Skylander Info"), "Back", SDLK_BACKSPACE }, { "start", N_("Pause"), "Start", SDLK_RETURN },
    };
    constexpr GameKey SwapForce[] = {
        MOVE_ROWS,
        { "a", N_("Jump"), "A", SDLK_SPACE }, { "x", N_("Attack 1"), "X", SDLK_C },
        { "y", N_("Attack 2"), "Y", SDLK_V }, { "b", N_("Attack 3"), "B", SDLK_X },
        { "back", N_("Skylanders Stats"), "Back", SDLK_BACKSPACE }, { "start", N_("Pause"), "Start", SDLK_RETURN },
        CAMERA_ROWS,
    };

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

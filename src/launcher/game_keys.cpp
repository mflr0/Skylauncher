#include "launcher/game_keys.h"
#include "launcher/i18n.h"
#include <array>
#include <SDL3/SDL_keyboard.h>
#include <utility>

namespace
{
    constexpr std::array<const char*, 22> Actions = { "a", "b", "x", "y", "start", "back", "lb", "rb", "lt", "rt", "left_up", "left_down",
        "left_left", "left_right", "right_up", "right_down", "right_left", "right_right", "dpad_up", "dpad_down", "dpad_left", "dpad_right" };

    // The defaults, shared by the games: the pad's buttons sit on the same keys in each (A Space, X C, Y V, B X).
    // Names and buttons: each game's own controls screen, read in the running game (docs/settings.md, "Where the names come from").
    #define MOVE_ROWS \
        { "left_up", N_("Move up"), N_("L stick"), SDL_SCANCODE_W }, { "left_down", N_("Move down"), N_("L stick"), SDL_SCANCODE_S }, \
        { "left_left", N_("Move left"), N_("L stick"), SDL_SCANCODE_A }, { "left_right", N_("Move right"), N_("L stick"), SDL_SCANCODE_D }

    constexpr GameKey Ssa[] = {
        MOVE_ROWS,
        { "a", N_("1st Power / Accept"), "A", SDL_SCANCODE_SPACE }, { "x", N_("2nd Power"), "X", SDL_SCANCODE_C },
        { "y", N_("Use / Talk"), "Y", SDL_SCANCODE_V }, { "b", N_("Special Power / Back"), "B", SDL_SCANCODE_X },
        { "back", N_("Skylander Info"), "Back", SDL_SCANCODE_BACKSPACE }, { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
        { "right_up", N_("Shake / Insert / Swing"), N_("R stick up"), SDL_SCANCODE_I }, { "right_down", N_("Same"), N_("R stick down"), SDL_SCANCODE_K },
        { "right_left", N_("Same"), N_("R stick left"), SDL_SCANCODE_J }, { "right_right", N_("Same"), N_("R stick right"), SDL_SCANCODE_L },
    };
    constexpr GameKey Giants[] = {
        MOVE_ROWS,
        { "a", N_("Attack 1 / Accept"), "A", SDL_SCANCODE_SPACE }, { "x", N_("Attack 2"), "X", SDL_SCANCODE_C },
        { "y", N_("Action"), "Y", SDL_SCANCODE_V }, { "b", N_("Attack 3 / Back"), "B", SDL_SCANCODE_X },
        { "back", N_("Skylander Info"), "Back", SDL_SCANCODE_BACKSPACE }, { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
    };
    constexpr GameKey SwapForce[] = {
        MOVE_ROWS,
        { "a", N_("Jump"), "A", SDL_SCANCODE_SPACE }, { "x", N_("Attack 1"), "X", SDL_SCANCODE_C },
        { "b", N_("Attack 2"), "B", SDL_SCANCODE_X }, { "y", N_("Attack 3"), "Y", SDL_SCANCODE_V },
        { "rb", N_("Attack 2"), "RB", SDL_SCANCODE_E }, { "lb", N_("Attack 3"), "LB", SDL_SCANCODE_Q },
        { "lt", N_("Interact"), "LT", SDL_SCANCODE_1 }, { "rt", N_("Interact"), "RT", SDL_SCANCODE_3 },
        { "back", N_("Skylanders Stats"), "Back", SDL_SCANCODE_BACKSPACE }, { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
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
    return SDL_GetScancodeName(key.fallback);
}

std::string KeyDisplayName(const std::string& sdlName)
{
    std::string shown = sdlName;
    if (SDL_Scancode place = SDL_GetScancodeFromName(sdlName.c_str()); place != SDL_SCANCODE_UNKNOWN)
        if (const char* name = SDL_GetKeyName(SDL_GetKeyFromScancode(place, SDL_KMOD_NONE, false)); name != nullptr && *name != 0)
            shown = name;
    for (const char* name : KeyNames)
        if (shown == name)
            return i18n::Tr(name);
    return shown;
}

std::string CapturedKeyName(SDL_Scancode scancode) { return SDL_GetScancodeName(scancode); }

PadFamily PadFamilyOf(SDL_GamepadType type)
{
    switch (type)
    {
    case SDL_GAMEPAD_TYPE_XBOXONE: return PadFamily::XboxOne;
    case SDL_GAMEPAD_TYPE_PS3: return PadFamily::PlayStation3;
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return PadFamily::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return PadFamily::Nintendo;
    default: return PadFamily::Xbox360;
    }
}

PadFamily PadFamilyFor(std::string_view glyphs, PadFamily seen)
{
    if (glyphs == "xbox360") return PadFamily::Xbox360;
    if (glyphs == "xboxone") return PadFamily::XboxOne;
    if (glyphs == "playstation") return PadFamily::PlayStation;
    if (glyphs == "nintendo") return PadFamily::Nintendo;
    if (glyphs == "xbox") return seen == PadFamily::XboxOne ? PadFamily::XboxOne : PadFamily::Xbox360;
    return seen;
}

std::string PadButtonName(const GameKey& key, PadFamily family) { return PadButtonName(std::string_view(key.pad), family); }

std::string PadButtonName(std::string_view pad, PadFamily family)
{
    // Per Xbox 360 name: Xbox One / Series, PlayStation 4 / 5, PlayStation 3, Nintendo Switch (by place: the 360's A is
    // the Switch's B). N_: the words are translated.
    static const std::pair<const char*, std::array<const char*, 4>> Names[] = {
        { "A", { "A", N_("Cross"), N_("Cross"), "B" } }, { "B", { "B", N_("Circle"), N_("Circle"), "A" } },
        { "X", { "X", N_("Square"), N_("Square"), "Y" } }, { "Y", { "Y", N_("Triangle"), N_("Triangle"), "X" } },
        { "Back", { "View", N_("Touchpad"), "Select", "-" } }, { "Start", { "Menu", "Options", "Start", "+" } },
        { "LB", { "LB", "L1", "L1", "L" } }, { "RB", { "RB", "R1", "R1", "R" } },
        { "LT", { "LT", "L2", "L2", "ZL" } }, { "RT", { "RT", "R2", "R2", "ZR" } },
        { "LS", { "LS", "L3", "L3", "LS" } }, { "RS", { "RS", "R3", "R3", "RS" } },
    };
    if (family != PadFamily::Xbox360)
    {
        size_t column = family == PadFamily::XboxOne ? 0 : family == PadFamily::PlayStation ? 1 : family == PadFamily::PlayStation3 ? 2 : 3;
        for (const auto& [xbox360, names] : Names)
            if (pad == xbox360)
                return i18n::Tr(names[column]);
    }
    // The 360's names as printed on the pad (Back stays Back, not the B button's "Back" action); the sticks' are words.
    return pad.find("stick") != std::string_view::npos || pad.starts_with("D-pad") ? i18n::Tr(std::string(pad).c_str()) : std::string(pad);
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
            config["input"][line] = SDL_GetScancodeName(row->fallback);
        else if (auto shared = config.find("input"); shared != config.end())
            shared->second.erase(line); // not one of this game's actions: the runtime's own default
    }
}

namespace
{
    // Display order = the runtime's g_padButtons; the 360's names, which the games' rows (GameKey::pad) use.
    constexpr std::array<const char*, PadButtonTotal> PadNames = { "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back", "ls", "rs",
        "dpad_up", "dpad_down", "dpad_left", "dpad_right" };
    constexpr std::array<const char*, PadButtonTotal> PadXbox = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Start", "Back", "LS", "RS",
        N_("D-pad up"), N_("D-pad down"), N_("D-pad left"), N_("D-pad right") };
    constexpr std::array<const char*, 4> Rumble = { "off", "low", "normal", "strong" };

    std::string PadLine(int button) { return std::string("pad_") + PadNames[size_t(button)]; }
}

std::span<const char* const> PadButtons() { return PadNames; }

std::string PadButtonLabel(int button, PadFamily family)
{
    return button >= 0 && button < PadButtonTotal ? PadButtonName(PadXbox[size_t(button)], family) : std::string();
}

std::string PadActionName(std::string_view gameId, int button)
{
    if (button < 0 || button >= PadButtonTotal)
        return {};
    for (const GameKey& key : GameKeys(gameId))
        if (std::string_view(key.pad) == PadXbox[size_t(button)])
            return i18n::Tr(key.label);
    return {};
}

int PadTarget(const LauncherConfig& config, std::string_view gameId, int button)
{
    const std::string* value = Find(config, GameInputSection(gameId), PadLine(button));
    if (value == nullptr)
        return button;
    for (int i = 0; i < PadButtonTotal; i++)
        if (*value == PadNames[size_t(i)])
            return i;
    return *value == "none" ? -1 : button;
}

void SetPadTarget(LauncherConfig& config, std::string_view gameId, int button, int target)
{
    const std::string section = GameInputSection(gameId);
    if (target == button)
    {
        if (auto entry = config.find(section); entry != config.end())
            entry->second.erase(PadLine(button));
        return;
    }
    config[section][PadLine(button)] = target < 0 ? "none" : PadNames[size_t(target)];
}

void ResetPadMap(LauncherConfig& config, std::string_view gameId)
{
    auto section = config.find(GameInputSection(gameId));
    if (section == config.end())
        return;
    for (int i = 0; i < PadButtonTotal; i++)
        section->second.erase(PadLine(i));
}

std::span<const char* const> RumbleLevels() { return Rumble; }

std::string RumbleLevel(const LauncherConfig& config, std::string_view gameId, int player)
{
    const std::string* value = Find(config, GameInputSection(gameId), player == 0 ? "rumble_p1" : "rumble_p2");
    for (const char* level : Rumble)
        if (value != nullptr && *value == level)
            return level;
    return "normal";
}

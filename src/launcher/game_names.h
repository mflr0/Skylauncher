#pragma once

// Every game's name, in one place: the full official name ("Skylanders: Giants") and the short one ("Giants"). No screen,
// message or file name builds a game's name itself; it asks GameName (docs/launcher_ui.md, "Consistency"):
// the full name in titles, headers, Home's cards, messages and file names; the short name where room is tight (the
// sidebar's rows, chips, filters, a select's rows). Names are never translated.
#include <string_view>

enum class GameNameForm { Full, Short };

struct GameNames
{
    const char* id;
    const char* full;
    const char* shortName;
};

// The console Skylanders games, in release order.
inline constexpr GameNames g_gameNames[] = {
    { "ssa", "Skylanders: Spyro's Adventure", "Spyro's Adventure" },
    { "giants", "Skylanders: Giants", "Giants" },
    { "swapforce", "Skylanders: Swap Force", "Swap Force" },
    { "trapteam", "Skylanders: Trap Team", "Trap Team" },
    { "superchargers", "Skylanders: SuperChargers", "SuperChargers" },
    { "racing", "Skylanders: SuperChargers Racing", "SuperChargers Racing" },
    { "imaginators", "Skylanders: Imaginators", "Imaginators" },
};

// The game's name by its id ("ssa", "giants"...); nullptr for an id that is not a game.
constexpr const char* GameName(std::string_view id, GameNameForm form = GameNameForm::Full)
{
    for (const GameNames& names : g_gameNames)
        if (id == names.id)
            return form == GameNameForm::Full ? names.full : names.shortName;
    return nullptr;
}

#pragma once

// What each bit of a figure's upgrade field means, with the names, descriptions and prices the game shows: read from
// the player's disc at install (art::Extract, art/game_text.cpp) into <data>/<Game>/upgrades.json (game text: never
// shipped), with a plain fallback ("Upgrade 1"...) when nothing was extracted. For the Toy Box and Skylauncher.
// docs/runtime/host/figures.md ("Upgrades"), docs/runtime/host/collection.md ("Upgrades").
#include "catalogue.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace collection
{
    // The upgrade field (figures::FigureStats::upgrades, block 8 +0x10) [V] in Spyro's Adventure and Giants.
    constexpr int UpgradePathChosenBit = 0; // a path is chosen
    constexpr int UpgradePathSecondBit = 1; // ...and it is the second one (clear: the first)
    constexpr int UpgradeSoulGemBit = 9;
    constexpr int UpgradeWowPowBit = 10;    // Giants, Series 2 figures [?] (figures.md)

    enum class UpgradePath
    {
        Basic = 0,  // bits 2-5
        First = 1,  // bits 6-8 when the first path is chosen
        Second = 2, // bits 6-8 when the second path is chosen
        SoulGem = 3,
        WowPow = 4,
    };

    struct Upgrade
    {
        int bit = 0;
        UpgradePath path = UpgradePath::Basic;
        int price = 0;           // gold
        std::string name;        // as the game writes it; its button marks are kept ("{" in Giants, "&ICON(...)" in SSA)
        std::string description;
    };

    struct UpgradeTable
    {
        std::vector<Upgrade> upgrades; // the game's order: basic, first path, second path, Soul Gem, Wow Pow
        std::string pathNames[2], pathDescriptions[2];
        bool fromDisc = false;         // false: the plain fallback
    };

    // The upgrades of `toy` in `game` ("ssa", "giants"), in `language` ("en", "fr"; English when the text is missing).
    // A toy type the game defines as another's alternate (Legendary Bash, Dark Spyro) gets its base toy's list.
    UpgradeTable Upgrades(const Toy& toy, const std::string& game, const std::string& language = "en");

    // <data>/<Game>/upgrades.json (SKYLANDERS_DATA moves the data root for tests).
    std::filesystem::path UpgradeFile(std::string_view game);
}

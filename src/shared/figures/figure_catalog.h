#pragma once

// Every toy the launcher knows, with what the collection filters and the figure creator need: the game that introduced
// it, its category, its variant and the plain toy it is a variant of.
//
// Only Spyro's Adventure is filled in. Its toy types are the ones its portal code accepts (sub_82643BE8 and
// sub_82643DA0 map them to per-toy ids: 0-32, the legendaries 404/416/419/430, magic items 200-207, adventure packs
// 300-304, and four Giants-era sidekicks); the order of characters 0-32 matches the title's internal name table
// (sub_829BC850). Display names are the toys' retail names (public). The other games' toys are added once their ids
// come from a verified source; the categories and games below already cover them.
#include <cstdint>
#include <span>
#include <string>

namespace figures
{
    enum class Element : uint8_t { Air, Earth, Fire, Water, Magic, Tech, Life, Undead, None };

    // The console games, in release order (SuperChargers Racing shipped alongside SuperChargers).
    enum class Game : uint8_t { SpyrosAdventure, Giants, SwapForce, TrapTeam, SuperChargers, SuperChargersRacing, Imaginators, Count };

    enum class Category : uint8_t
    {
        Skylander,      // a regular character
        Giant,
        SwapForce,      // swappable halves
        TrapMaster,
        Mini,           // sidekicks and minis
        Trap,
        Vehicle,
        MagicItem,
        AdventurePack,
        Imaginator,
        CreationCrystal,
        Count,
    };

    enum class Variant : uint8_t { Normal, Legendary, Dark, Crystal, Glow, Metallic, LightCore, Count };

    struct ToyInfo
    {
        uint16_t toyType;
        const char* name;
        const char* portrait; // the title's name for the character ("GillGrunt": UI image toy_gillgrunt_hud); null if none
        Element element;
        Category category;
        Variant variant = Variant::Normal;
        uint16_t base = 0xFFFF;   // toy type of the plain toy this one is a variant of; 0xFFFF: it is the plain toy
        uint16_t variantId = 0;   // the figure's variant field (block 1, +0x0C); 0 for every Spyro's Adventure toy
        Game game = Game::SpyrosAdventure; // the first game that accepts it
    };

    std::span<const ToyInfo> Catalog();
    const ToyInfo* FindToy(uint16_t toyType);
    // The plain toy of a variant (itself when it is plain).
    const ToyInfo& BaseToy(const ToyInfo& toy);

    // Characters keep progress (experience, gold, upgrades...); items, traps and adventure packs don't.
    bool HasProgress(const ToyInfo& toy);

    // Whether `game` accepts the toy. Characters and vehicles carry forward to every later main game; magic items,
    // traps, adventure packs and creation crystals only work in their own game; SuperChargers Racing takes vehicles only.
    // This is the general rule of the series, not a per-toy list.
    bool PlayableIn(const ToyInfo& toy, Game game);

    const char* ElementName(Element element);
    const char* GameName(Game game);
    const char* CategoryName(Category category);
    const char* VariantName(Variant variant);
    // Opaque RGBA (R in the low byte), for placeholders and frames.
    uint32_t ElementColor(Element element);

    // Name of the toy's portrait in the title's UI images ("toy_<portrait>_hud", lower case), empty if it has none.
    // Sidekicks use their full-size counterpart's.
    std::string PortraitImageName(const ToyInfo& toy);
    // Its large "versus" picture in the same package ("<portrait>_vs"), empty if it has none.
    std::string PortraitLargeImageName(const ToyInfo& toy);

    // File name of the starter figure for a toy: "<toy type> <name>.sky", e.g. "014 Gill Grunt.sky".
    std::string StarterFileName(const ToyInfo& toy);
}

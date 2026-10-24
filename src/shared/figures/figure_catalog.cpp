#include "shared/figures/figure_catalog.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace figures
{
    namespace
    {
        using enum Element;
        using enum Category;
        constexpr Variant Legendary = Variant::Legendary;
        constexpr Variant Dark = Variant::Dark;

        constexpr ToyInfo Toys[] = {
            { 0, "Whirlwind", "AirDragon", Air, Skylander },
            { 1, "Sonic Boom", "Griffin", Air, Skylander },
            { 2, "Warnado", "SkyTurtle", Air, Skylander },
            { 3, "Lightning Rod", "StormGiant", Air, Skylander },
            { 4, "Bash", "RockDragon", Earth, Skylander },
            { 5, "Terrafin", "LandShark", Earth, Skylander },
            { 6, "Dino-Rang", "DinoRang", Earth, Skylander },
            { 7, "Prism Break", "GemGolem", Earth, Skylander },
            { 8, "Sunburn", "PhoenixDragon", Fire, Skylander },
            { 9, "Eruptor", "Eruptor", Fire, Skylander },
            { 10, "Ignitor", "FlameKnight", Fire, Skylander },
            { 11, "Flameslinger", "FireElf", Fire, Skylander },
            { 12, "Zap", "WaterDragon", Water, Skylander },
            { 13, "Wham-Shell", "CrustBuckler", Water, Skylander },
            { 14, "Gill Grunt", "GillGrunt", Water, Skylander },
            { 15, "Slam Bam", "Yeti", Water, Skylander },
            { 16, "Spyro", "SpyroJr", Magic, Skylander },
            { 17, "Voodood", "SkullOrc", Magic, Skylander },
            { 18, "Double Trouble", "TikiWizard", Magic, Skylander },
            { 19, "Trigger Happy", "GoldenCheat", Tech, Skylander },
            { 20, "Drobot", "MetalDragon", Tech, Skylander },
            { 21, "Drill Sergeant", "DrillBot", Tech, Skylander },
            { 22, "Boomer", "BombTroll", Tech, Skylander },
            { 23, "Wrecking Ball", "ForceFieldGrub", Magic, Skylander },
            { 24, "Camo", "PlantDragon", Life, Skylander },
            { 25, "Zook", "Bambazooker", Life, Skylander },
            { 26, "Stealth Elf", "StealthElf", Life, Skylander },
            { 27, "Stump Smash", "StumpSmashEnt", Life, Skylander },
            { 28, "Dark Spyro", "DarkSpyro", Magic, Skylander, Dark, 16 },
            { 29, "Hex", "ShadowMaid", Undead, Skylander },
            { 30, "Chop Chop", "PandoranGuard", Undead, Skylander },
            { 31, "Ghost Roaster", "GhostEater", Undead, Skylander },
            { 32, "Cynder", "Cynder", Undead, Skylander },
            { 404, "Legendary Bash", "LegendaryRockDragon", Earth, Skylander, Legendary, 4 },
            { 416, "Legendary Spyro", "LegendarySpyro", Magic, Skylander, Legendary, 16 },
            { 419, "Legendary Trigger Happy", "LegendaryGoldenCheat", Tech, Skylander, Legendary, 19 },
            { 430, "Legendary Chop Chop", "LegendaryPandoranGuard", Undead, Skylander, Legendary, 30 },
            { 505, "Terrabite", "LandShark", Earth, Mini },
            { 514, "Gill Runt", "GillGrunt", Water, Mini },
            { 519, "Trigger Snappy", "GoldenCheat", Tech, Mini },
            { 526, "Whisper Elf", "StealthElf", Life, Mini },
            { 200, "Anvil Rain", nullptr, None, MagicItem },
            { 201, "Hidden Treasure", nullptr, None, MagicItem },
            { 202, "Healing Elixir", nullptr, None, MagicItem },
            { 203, "Ghost Pirate Swords", nullptr, None, MagicItem },
            { 204, "Time Twister", nullptr, None, MagicItem },
            { 205, "Sky-Iron Shield", nullptr, None, MagicItem },
            { 206, "Winged Boots", nullptr, None, MagicItem },
            { 207, "Sparx Dragonfly", nullptr, None, MagicItem },
            { 300, "Dragon's Peak", nullptr, None, AdventurePack },
            { 301, "Empire of Ice", nullptr, None, AdventurePack },
            { 302, "Pirate Seas", nullptr, None, AdventurePack },
            { 303, "Darklight Crypt", nullptr, None, AdventurePack },
            { 304, "Volcanic Vault", nullptr, None, AdventurePack },
        };

        std::string Lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(tolower(c)); });
            return text;
        }
    }

    std::span<const ToyInfo> Catalog()
    {
        return Toys;
    }

    const ToyInfo* FindToy(uint16_t toyType)
    {
        for (const ToyInfo& toy : Toys)
        {
            if (toy.toyType == toyType)
                return &toy;
        }
        return nullptr;
    }

    const ToyInfo& BaseToy(const ToyInfo& toy)
    {
        if (toy.base == 0xFFFF)
            return toy;
        const ToyInfo* base = FindToy(toy.base);
        return base != nullptr ? *base : toy;
    }

    bool HasProgress(const ToyInfo& toy)
    {
        switch (toy.category)
        {
        case Skylander: case Giant: case SwapForce: case TrapMaster: case Mini: case Imaginator: return true;
        default: return false;
        }
    }

    bool PlayableIn(const ToyInfo& toy, Game game)
    {
        if (game == Game::SuperChargersRacing)
            return toy.category == Vehicle;
        switch (toy.category)
        {
        case MagicItem: case Trap: case AdventurePack: case CreationCrystal: return game == toy.game;
        default: return game >= toy.game;
        }
    }

    const char* ElementName(Element element)
    {
        static const char* const Names[] = { "Air", "Earth", "Fire", "Water", "Magic", "Tech", "Life", "Undead", "" };
        return Names[size_t(element)];
    }

    const char* GameName(Game game)
    {
        static const char* const Names[] = { "Spyro's Adventure", "Giants", "Swap Force", "Trap Team", "SuperChargers",
            "SuperChargers Racing", "Imaginators" };
        return Names[size_t(game)];
    }

    const char* CategoryName(Category category)
    {
        static const char* const Names[] = { "Skylanders", "Giants", "Swap Force", "Trap Masters", "Minis", "Traps", "Vehicles",
            "Magic items", "Adventure packs", "Imaginators", "Creation crystals" };
        return Names[size_t(category)];
    }

    const char* VariantName(Variant variant)
    {
        static const char* const Names[] = { "", "Legendary", "Dark", "Crystal", "Glow", "Metallic", "LightCore" };
        return Names[size_t(variant)];
    }

    uint32_t ElementColor(Element element)
    {
        // 0xAABBGGRR
        static const uint32_t Colors[] = { 0xFFE8C46A, 0xFF2E6C9A, 0xFF2A4FE0, 0xFFD08A28, 0xFFC04AA8, 0xFF20B0E8,
            0xFF3CB04C, 0xFF7A5A6A, 0xFF707070 };
        return Colors[size_t(element)];
    }

    std::string PortraitImageName(const ToyInfo& toy)
    {
        return toy.portrait == nullptr ? std::string() : Lower(std::string("toy_") + toy.portrait + "_hud");
    }

    std::string PortraitLargeImageName(const ToyInfo& toy)
    {
        return toy.portrait == nullptr ? std::string() : Lower(std::string(toy.portrait) + "_vs");
    }

    std::string StarterFileName(const ToyInfo& toy)
    {
        char prefix[8];
        snprintf(prefix, sizeof(prefix), "%03u ", toy.toyType);
        return prefix + std::string(toy.name) + ".sky";
    }
}

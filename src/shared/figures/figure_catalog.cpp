#include "figures/figure_catalog.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace figures
{
    namespace
    {
        using enum Element;
        using enum ToyKind;

        constexpr ToyInfo Toys[] = {
            { 0, "Whirlwind", "AirDragon", Air, Character },
            { 1, "Sonic Boom", "Griffin", Air, Character },
            { 2, "Warnado", "SkyTurtle", Air, Character },
            { 3, "Lightning Rod", "StormGiant", Air, Character },
            { 4, "Bash", "RockDragon", Earth, Character },
            { 5, "Terrafin", "LandShark", Earth, Character },
            { 6, "Dino-Rang", "DinoRang", Earth, Character },
            { 7, "Prism Break", "GemGolem", Earth, Character },
            { 8, "Sunburn", "PhoenixDragon", Fire, Character },
            { 9, "Eruptor", "Eruptor", Fire, Character },
            { 10, "Ignitor", "FlameKnight", Fire, Character },
            { 11, "Flameslinger", "FireElf", Fire, Character },
            { 12, "Zap", "WaterDragon", Water, Character },
            { 13, "Wham-Shell", "CrustBuckler", Water, Character },
            { 14, "Gill Grunt", "GillGrunt", Water, Character },
            { 15, "Slam Bam", "Yeti", Water, Character },
            { 16, "Spyro", "SpyroJr", Magic, Character },
            { 17, "Voodood", "SkullOrc", Magic, Character },
            { 18, "Double Trouble", "TikiWizard", Magic, Character },
            { 19, "Trigger Happy", "GoldenCheat", Tech, Character },
            { 20, "Drobot", "MetalDragon", Tech, Character },
            { 21, "Drill Sergeant", "DrillBot", Tech, Character },
            { 22, "Boomer", "BombTroll", Tech, Character },
            { 23, "Wrecking Ball", "ForceFieldGrub", Magic, Character },
            { 24, "Camo", "PlantDragon", Life, Character },
            { 25, "Zook", "Bambazooker", Life, Character },
            { 26, "Stealth Elf", "StealthElf", Life, Character },
            { 27, "Stump Smash", "StumpSmashEnt", Life, Character },
            { 28, "Dark Spyro", "DarkSpyro", Magic, Character },
            { 29, "Hex", "ShadowMaid", Undead, Character },
            { 30, "Chop Chop", "PandoranGuard", Undead, Character },
            { 31, "Ghost Roaster", "GhostEater", Undead, Character },
            { 32, "Cynder", "Cynder", Undead, Character },
            { 404, "Legendary Bash", "LegendaryRockDragon", Earth, Legendary },
            { 416, "Legendary Spyro", "LegendarySpyro", Magic, Legendary },
            { 419, "Legendary Trigger Happy", "LegendaryGoldenCheat", Tech, Legendary },
            { 430, "Legendary Chop Chop", "LegendaryPandoranGuard", Undead, Legendary },
            { 505, "Terrabite", "LandShark", Earth, Sidekick },
            { 514, "Gill Runt", "GillGrunt", Water, Sidekick },
            { 519, "Trigger Snappy", "GoldenCheat", Tech, Sidekick },
            { 526, "Whisper Elf", "StealthElf", Life, Sidekick },
            { 200, "Anvil Rain", nullptr, None, MagicItem },
            { 201, "Hidden Treasure", nullptr, None, MagicItem },
            { 202, "Healing Elixir", nullptr, None, MagicItem },
            { 203, "Ghost Pirate Swords", nullptr, None, MagicItem },
            { 204, "Time Twister", nullptr, None, MagicItem },
            { 205, "Sky-Iron Shield", nullptr, None, MagicItem },
            { 206, "Winged Boots", nullptr, None, MagicItem },
            { 207, "Sparx Dragonfly", nullptr, None, MagicItem },
            { 300, "Dragon's Peak", nullptr, None, Adventure },
            { 301, "Empire of Ice", nullptr, None, Adventure },
            { 302, "Pirate Seas", nullptr, None, Adventure },
            { 303, "Darklight Crypt", nullptr, None, Adventure },
            { 304, "Volcanic Vault", nullptr, None, Adventure },
        };
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

    const char* ElementName(Element element)
    {
        static const char* const Names[] = { "Air", "Earth", "Fire", "Water", "Magic", "Tech", "Life", "Undead", "" };
        return Names[size_t(element)];
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
        if (toy.portrait == nullptr)
            return {};
        std::string name = std::string("toy_") + toy.portrait + "_hud";
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(tolower(c)); });
        return name;
    }

    std::string StarterFileName(const ToyInfo& toy)
    {
        char prefix[8];
        snprintf(prefix, sizeof(prefix), "%03u ", toy.toyType);
        return prefix + std::string(toy.name) + ".sky";
    }
}

#include "launcher/game_names.h"
#include "launcher/launcher.h"

// Hashes identify the player's own default.xex; they are not game content.
namespace
{
    const GameRelease SpyrosAdventureReleases[] =
    {
        // Redump "Skylanders - Spyro's Adventure (USA, Europe) (En,Fr,De,Es,It,Nl,Sv,No,Da,Fi)", XGD3, 8738846720 bytes.
        // SHA-256 of its default.xex (12595200 bytes), computed from the player's image.
        { "USA, Europe", 0x415608AB, 0x53CE7B97, 0x00000002, "e195f5130c1e23fb4654baf7c383ef97d02c89aeb7d4389705ae31235646ef7b" },
    };

    const GameRelease GiantsReleases[] =
    {
        // Redump "Skylanders - Giants (USA, Europe) (En,Fr,De,Es,It,Nl,Pt,Sv,No,Da,Fi)", XGD3, 8738846720 bytes.
        // SHA-256 of its default.xex (7020544 bytes), computed from the player's image.
        { "USA, Europe", 0x415608DA, 0x52FFE94B, 0x00000002, "12d1e29e4f07fbc559bc1a4988dd6bf346c3d16e30cbf862e2683e862f9fc09e" },
    };

    const GameRelease SwapForceReleases[] =
    {
        // Redump "Skylanders - Swap Force (USA, Australia) (En,Fr)", XGD3, 8738846720 bytes.
        // SHA-256 of its default.xex (18563072 bytes), computed from the player's image.
        { "USA, Australia", 0x415608E9, 0x22F1FB91, 0x00000021, "5d9a3bc3c2d85ed59684a9c5f94daf6bffcfdfedcef7650a866737edbf3f4ac3" },
        // Redump "Skylanders - Swap Force (Europe) (Es,It)", XGD3, 8738846720 bytes: the same game code, another XEX header.
        { "Europe", 0x415608E9, 0x74FDDE06, 0x0000001C, "3db455f6ee17e8cb3a4d58873d96583d9160396d360a0130e5eb88adad8a5518" },
    };

    const GameRelease TrapTeamReleases[] =
    {
        // Redump "Skylanders - Trap Team (Europe) (En,De)", 8738846720 bytes.
        { "Europe", 0x41560911, 0x7A74A451, 0x00000014, "f1d2373e4507e21cc371e6f22d02e909602a1b5bc4704896c3c099cc64002a35" },
    };

    // `runtime` is the executable the player kit builds for the game (its config/<id>/game.txt).
    const GameInfo Games[] =
    {
        { "ssa", GameName("ssa"), SpyrosAdventureReleases, "SSARecomp" },
        { "giants", GameName("giants"), GiantsReleases, "SGRecomp" },
        { "swapforce", GameName("swapforce"), SwapForceReleases, "SSFRecomp" },
        { "trapteam", GameName("trapteam"), TrapTeamReleases, "TTRecomp" },
    };
}

const std::span<const GameInfo> g_games = Games;
const char* const g_launcherName = "SSARecomp";

const GameInfo* FindGame(std::string_view id)
{
    for (const GameInfo& game : g_games)
    {
        if (id == game.id)
            return &game;
    }
    return nullptr;
}

bool ExperimentalTrapTeam(const LauncherConfig& config)
{
    auto section = config.find("launcher");
    if (section == config.end())
        return false;
    auto value = section->second.find("experimental_trapteam");
    return value != section->second.end() && value->second == "1";
}

const GameInfo* FindEnabledGame(std::string_view id, const LauncherConfig& config)
{
    return id == "trapteam" && !ExperimentalTrapTeam(config) ? nullptr : FindGame(id);
}

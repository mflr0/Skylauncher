#include "launcher.h"

// Hashes identify the player's own default.xex; they are not game content.
namespace
{
    const GameRelease SpyrosAdventureReleases[] =
    {
        // Redump "Skylanders - Spyro's Adventure (USA, Europe) (En,Fr,De,Es,It,Nl,Sv,No,Da,Fi)", XGD3, 8738846720 bytes.
        // SHA-256 of its default.xex (12595200 bytes), computed from the player's image.
        { "USA, Europe", 0x415608AB, 0x53CE7B97, 0x00000002, "e195f5130c1e23fb4654baf7c383ef97d02c89aeb7d4389705ae31235646ef7b" },
    };

    const GameInfo Games[] =
    {
        { "ssa", "Skylanders: Spyro's Adventure", SpyrosAdventureReleases, "SSARecomp" },
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

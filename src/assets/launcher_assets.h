#pragma once

#include "launcher/launcher.h"
#include "shared/vfs.h"
#include <string>

// The game's own pictures for the launcher, read from the player's verified disc by the shared provider
// (shared/art/game_art.h, the same code as the game's Toy Box) and cached in <data>/<Game>/launcher-cache/ (logo,
// background, title icon) and <data>/<Game>/portraits/ (the toys). One flow for every game: what differs per game is
// the provider's table. Nothing here is committed or shipped; before a disc is verified the drawn theme is used.
namespace LauncherAssets
{
    enum class Role
    {
        Logo,           // the logo, with "Skylanders" on it (game page, sidebar)
        HeroBackground, // the menus' painting (the game page's backdrop)
        TitleIcon,      // the dashboard icon of default.xex
    };

    struct Asset
    {
        std::string path;
        bool valid = false;
    };

    struct Provider
    {
        std::string game; // catalogue id, "ssa"
        bool enabled = false;

        void Init(const std::string& gameId);
        // Reads the game's pictures and its toys' portraits from the disc (what is cached already is kept).
        void ExtractFromDisc(DiscFileSystem& disc, const TitleMetadata& meta);
        Asset Get(Role role) const;
    };

    // The cached picture of `role` for a game, whether or not its disc was opened this session (empty if none).
    Asset Cached(const std::string& gameId, Role role);
} // namespace LauncherAssets

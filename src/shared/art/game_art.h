#pragma once

// Each game's own pictures, read from the player's disc and cached as PNG files in the game's data folder: its logo,
// its background art, its title icon and its toy portraits. One flow for every game; what differs per game (which
// archive, which picture) is the table in game_art.cpp. Used by the in-game Toy Box and by Skylauncher (game page,
// sidebar, collection). Nothing is shipped or downloaded. docs/runtime/host/game_art.md.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace art
{
    // The disc as this file reads it. The runtime's and Skylauncher's DiscFileSystem both fit: ReadFrom(disc).
    struct Disc
    {
        std::function<uint64_t(const std::string& path)> size; // 0: no such file
        std::function<bool(const std::string& path, uint64_t offset, size_t size, uint8_t* out)> read;
    };

    template <typename FileSystem>
    Disc ReadFrom(const FileSystem& disc)
    {
        return {
            [&disc](const std::string& path) -> uint64_t {
                auto entry = disc.Find(path);
                return entry != nullptr && !entry->directory ? entry->size : 0;
            },
            [&disc](const std::string& path, uint64_t offset, size_t size, uint8_t* out) {
                auto entry = disc.Find(path);
                return entry != nullptr && offset + size <= entry->size && disc.Read(*entry, offset, out, uint32_t(size)) == size;
            },
        };
    }

    enum class Role
    {
        Logo,       // the title's logo, with "Skylanders" on it, on a transparent background
        Background, // a full-screen painting of the game's menus
        TitleIcon,  // the dashboard icon of default.xex (its XDBF resource), given by the caller
        Count
    };

    // "logo.png", "background.png", "title_icon.png".
    const char* RoleFile(Role role);

    // <data>/<Game>/launcher-cache/ and <data>/<Game>/portraits/ (SSA_PORTRAITS overrides the second, for tests). Empty
    // when the data folder is unknown.
    std::filesystem::path CacheFolder(std::string_view game);
    std::filesystem::path PortraitFolder(std::string_view game);

    // The cached picture of `role` for `game`, empty if there is none yet.
    std::string CachedPath(std::string_view game, Role role);

    // True for a game whose pictures this file knows how to read ("ssa", "giants").
    bool Known(std::string_view game);

    enum Parts
    {
        Pictures = 1,  // logo, background, title icon
        Portraits = 2, // the toy portraits
        All = 3,
    };

    struct Extraction
    {
        bool logo = false, background = false, titleIcon = false; // present in the cache afterwards
        int portraits = 0;                                          // portrait files in the folder afterwards
    };

    // Reads `game`'s pictures from the player's disc into its caches; what is already cached (by this version of the
    // reader) is kept. `titleIconPng`: the XDBF icon of default.xex, written as title_icon.png when given.
    Extraction Extract(std::string_view game, const Disc& disc, const std::vector<uint8_t>& titleIconPng = {}, int parts = All);
}

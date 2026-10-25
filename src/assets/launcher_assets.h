#pragma once

#include "launcher/launcher.h"
#include "shared/vfs.h"
#include <SDL3/SDL.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Asset provider layer for the launcher: pulls official assets from the player's
// own verified ISO at runtime and caches decoded images in
// <data root>/SSA/launcher-cache/<titleid>/ (trimmed to 256 MB, oldest files first, when a game starts). Nothing here is committed or shipped.
// The procedural theme is the fallback before an ISO is loaded or if extraction fails.
namespace LauncherAssets
{
    enum class Role
    {
        Logo,
        HeroBackground,
        CharacterPortrait,
        ButtonTexture,
        Font,
        TitleIcon,
        LoadingBackground
    };

    struct Asset
    {
        std::string path;
        bool valid = false;
    };

    struct Provider
    {
        std::string isoPath;
        uint32_t titleId = 0;
        std::string cacheDir;
        bool enabled = false;

        void Init(const std::string& iso, uint32_t titleId);
        std::string CachePathFor(const std::string& roleKey) const;
        bool LoadFromCache(Role role, Asset& out) const;

        // Cache the official title icon from the player's XDBF and write a manifest that
        // documents where the other frontend art lives on the disc.
        void ExtractFromDisc(DiscFileSystem& disc, const TitleMetadata& meta);
        Asset Get(Role role);

        // Every toy's pictures from the disc's uigameimage.str: the 128 px HUD portrait ("toy_<name>_hud") and the
        // 512 px "versus" picture ("<name>_vs"), cached as figures/<image name>.png. Already cached ones are kept.
        void ExtractFigurePortraits(DiscFileSystem& disc);
        // The cached picture for an image name (figures::PortraitImageName / PortraitLargeImageName), empty if none.
        std::string FigurePortraitPath(const std::string& imageName) const;
    };

    // Decode a named APKF texture from a start-menu STRM into RGBA pixels. Exposed so tests can
    // exercise the disc format and Xbox 360 tiling without opening a window or writing a cache.
    bool DecodeFrontendTexture(std::span<const uint8_t> strmFile, std::string_view fileName,
        uint32_t width, uint32_t height, std::vector<uint8_t>& rgba);
} // namespace LauncherAssets

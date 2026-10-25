#include "assets/launcher_assets.h"
#include "shared/art/game_art.h"

namespace LauncherAssets
{
    namespace
    {
        art::Role ArtRole(Role role)
        {
            switch (role)
            {
            case Role::Logo: return art::Role::Logo;
            case Role::HeroBackground: return art::Role::Background;
            case Role::TitleIcon: return art::Role::TitleIcon;
            }
            return art::Role::Logo;
        }
    }

    void Provider::Init(const std::string& gameId)
    {
        game = gameId;
        enabled = !art::CacheFolder(gameId).empty();
    }

    void Provider::ExtractFromDisc(DiscFileSystem& disc, const TitleMetadata& meta)
    {
        if (enabled)
            art::Extract(game, art::ReadFrom(disc), meta.iconPng);
    }

    Asset Provider::Get(Role role) const
    {
        return enabled ? Cached(game, role) : Asset{};
    }

    Asset Cached(const std::string& gameId, Role role)
    {
        std::string path = art::CachedPath(gameId, ArtRole(role));
        return { path, !path.empty() };
    }
} // namespace LauncherAssets

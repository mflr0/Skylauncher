// See game_art.h; the per-game sources and the Giants logo composition are described in docs/runtime/host/game_art.md.
#include "game_art.h"
#include "alchemy_archive.h"
#include "lux_images.h"
#include "../collection/catalogue.h"
#include "../collection/datadir.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <set>

namespace art
{
    namespace
    {
        // Bump when a picture is read or composed differently: the cached pictures (not the portraits) are made again.
        constexpr int CacheVersion = 2;

        std::string Lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        }

        std::string Text(const std::filesystem::path& path)
        {
            return datadir::Text(path);
        }

        bool Exists(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::is_regular_file(path, error);
        }

        // Written next to the target first, so that an interrupted run leaves no truncated PNG behind.
        bool SavePng(const Image& image, const std::filesystem::path& path)
        {
            SDL_Surface* surface = SDL_CreateSurfaceFrom(int(image.width), int(image.height), SDL_PIXELFORMAT_RGBA32,
                const_cast<uint32_t*>(image.rgba.data()), int(image.width * 4));
            if (surface == nullptr)
                return false;
            std::string target = Text(path), temporary = target + ".part";
            bool saved = SDL_SavePNG(surface, temporary.c_str()) && SDL_RenamePath(temporary.c_str(), target.c_str());
            SDL_DestroySurface(surface);
            if (!saved)
                fprintf(stderr, "[art] cannot write %s\n", target.c_str());
            return saved;
        }

        bool SaveBytes(const std::vector<uint8_t>& bytes, const std::filesystem::path& path)
        {
            std::string target = Text(path), temporary = target + ".part";
            {
                std::ofstream out(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
                if (!out.good())
                    return false;
            }
            return SDL_RenamePath(temporary.c_str(), target.c_str());
        }

        std::vector<uint8_t> ReadAll(const Disc& disc, const std::string& path)
        {
            uint64_t size = disc.size(path);
            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            if (size == 0 || size > (512ull << 20) || !disc.read(path, 0, bytes.size(), bytes.data()))
                return {};
            return bytes;
        }

        std::optional<alchemy::IgaArchive> OpenArchive(const Disc& disc, const std::string& path)
        {
            uint64_t size = disc.size(path);
            if (size == 0)
                return std::nullopt;
            return alchemy::IgaArchive::Open([&disc, path](uint64_t offset, size_t count, uint8_t* out) { return disc.read(path, offset, count, out); }, size);
        }

        // The IGZ file of archive entry `part` (a name fragment), parsed.
        std::optional<alchemy::IgzFile> OpenScene(const alchemy::IgaArchive& archive, const std::string& part)
        {
            std::optional<size_t> index = archive.Find(part);
            if (!index)
                return std::nullopt;
            return alchemy::IgzFile::Parse(archive.Read(*index));
        }

        // The rows and columns of `image` whose alpha is above `threshold`, in bands of consecutive rows.
        struct Band
        {
            uint32_t top, bottom, left, right; // [top, bottom) x [left, right)
        };

        std::vector<Band> OpaqueBands(const Image& image, uint8_t threshold = 16)
        {
            std::vector<Band> bands;
            bool inside = false;
            for (uint32_t y = 0; y <= image.height; y++)
            {
                uint32_t left = image.width, right = 0;
                if (y < image.height)
                    for (uint32_t x = 0; x < image.width; x++)
                        if ((image.rgba[size_t(y) * image.width + x] >> 24) > threshold)
                            left = std::min(left, x), right = std::max(right, x + 1);
                bool row = left < right;
                if (row && !inside)
                    bands.push_back({ y, y, left, right });
                if (row)
                {
                    bands.back().bottom = y + 1;
                    bands.back().left = std::min(bands.back().left, left);
                    bands.back().right = std::max(bands.back().right, right);
                }
                inside = row;
            }
            return bands;
        }

        // Draws `band` of `from` at (x, y) of `to`; `under`: behind what is there (straight alpha, "over" blending).
        void Blit(const Image& from, const Band& band, Image& to, int x, int y, bool under)
        {
            for (uint32_t sy = band.top; sy < band.bottom; sy++)
                for (uint32_t sx = band.left; sx < band.right; sx++)
                {
                    int dx = x + int(sx - band.left), dy = y + int(sy - band.top);
                    if (dx < 0 || dy < 0 || dx >= int(to.width) || dy >= int(to.height))
                        continue;
                    uint32_t source = from.rgba[size_t(sy) * from.width + sx], &target = to.rgba[size_t(dy) * to.width + dx];
                    uint32_t top = under ? target : source, bottom = under ? source : target;
                    float ta = float(top >> 24) / 255, ba = float(bottom >> 24) / 255, alpha = ta + ba * (1 - ta);
                    if (alpha <= 0)
                        continue;
                    uint32_t out = uint32_t(alpha * 255 + 0.5f) << 24;
                    for (int shift = 0; shift < 24; shift += 8)
                    {
                        float c = (float((top >> shift) & 0xFF) * ta + float((bottom >> shift) & 0xFF) * ba * (1 - ta)) / alpha;
                        out |= uint32_t(std::clamp(c + 0.5f, 0.0f, 255.0f)) << shift;
                    }
                    target = out;
                }
        }

        // [Giants] The title's logo is a sprite sheet (spritelogo.png, 1024x1024): "SKYLANDERS" (the arch), "GIANTS"
        // and the row of element symbols, apart. The title screen draws "GIANTS" under the arch's middle, behind it
        // (as in the title's own flat picture, title_giants_3d.png); the symbols are left out, like the box art.
        bool ComposeGiantsLogo(const Image& sheet, Image& logo)
        {
            std::vector<Band> bands = OpaqueBands(sheet);
            // Top to bottom (once the right way up): the arch, the word, the symbols.
            if (bands.size() < 2)
                return false;
            const Band& arch = bands[0];
            const Band& word = bands[1];
            uint32_t archH = arch.bottom - arch.top, archW = arch.right - arch.left, wordH = word.bottom - word.top;
            // "GIANTS" starts 0.507 of the arch's height down (measured on title_giants_3d.png: 215 of 424 px).
            int wordTop = int(float(archH) * 0.507f + 0.5f);
            logo.width = archW;
            logo.height = std::max<uint32_t>(archH, uint32_t(wordTop) + wordH);
            logo.rgba.assign(size_t(logo.width) * logo.height, 0);
            Blit(sheet, arch, logo, 0, 0, false);
            Blit(sheet, word, logo, int(word.left) - int(arch.left), wordTop, true); // as aligned on the sheet: centred
            return true;
        }

        // --- What each game's pictures are ---------------------------------------------------------------------------

        struct Art
        {
            Image logo, background;
        };

        // [SSA] Both in uiimage.str: the painted Skylands sky behind the menus and the logo of the start menu.
        void SsaPictures(const Disc& disc, Art& out)
        {
            std::vector<uint8_t> stream;
            if (!lux::UnpackStream(ReadAll(disc, "uiimage.str"), stream))
                return;
            lux::DecodeImages(stream, [](const std::string& name) { return name == "skylanderslogo" || name == "fullscreen_000"; },
                [&](const std::string& name, const Image& image) { (name == "fullscreen_000" ? out.background : out.logo) = image; });
        }

        // [SSA] The HUD portraits ("toy_<name>_hud", 128 px) and the "versus" pictures ("<name>_vs", 512 px) of the toys
        // the catalogue names, and the other pictures it lists (items), from uigameimage.str.
        int SsaPortraits(const Disc& disc, const std::filesystem::path& folder)
        {
            std::set<std::string> wanted;
            if (const collection::Game* game = collection::Catalogue::Get().FindGame("ssa"))
            {
                for (const auto& [type, name] : game->names)
                {
                    wanted.insert("toy_" + Lower(name) + "_hud");
                    wanted.insert(Lower(name) + "_vs");
                }
                for (const auto& [type, name] : game->images)
                    wanted.insert(name);
            }
            int present = 0;
            for (auto it = wanted.begin(); it != wanted.end();)
                if (Exists(folder / (*it + ".png")))
                    present++, it = wanted.erase(it);
                else
                    ++it;
            if (wanted.empty())
                return present;
            std::vector<uint8_t> stream;
            if (!lux::UnpackStream(ReadAll(disc, "uigameimage.str"), stream))
            {
                fprintf(stderr, "[art] cannot read the UI images (uigameimage.str) of the disc\n");
                return present;
            }
            lux::DecodeImages(stream, [&](const std::string& name) { return wanted.erase(name) > 0; },
                [&](const std::string& name, const Image& image) { present += SavePng(image, folder / (name + ".png")); });
            return present;
        }

        // [Giants] The logo from the title's sprite sheet (level/Title.bld), the main menu's painting (permanent/global.arc,
        // ui_levelfullscreen/mainmenu.png, 1600x960 DXT1).
        void GiantsPictures(const Disc& disc, Art& out)
        {
            if (auto archive = OpenArchive(disc, "level/Title.bld"))
                if (auto scene = OpenScene(*archive, "level.bld"))
                    for (const alchemy::IgzFile::Picture& picture : scene->Pictures())
                    {
                        Image sheet;
                        if (picture.name.ends_with("giantslogo/rig/textures/spritelogo.png") && alchemy::DecodePicture(*scene, picture, sheet))
                            ComposeGiantsLogo(sheet, out.logo);
                    }
            if (auto archive = OpenArchive(disc, "permanent/global.arc"))
                if (auto file = OpenScene(*archive, "ui_levelfullscreen/mainmenu.png"))
                    for (const alchemy::IgzFile::Picture& picture : file->Pictures())
                        if (out.background.rgba.empty())
                            alchemy::DecodePicture(*file, picture, out.background);
        }

        // [Giants] Every portrait of the collection screen (misc/UI_Collection_Champions.bld: all Skylanders and their
        // Series 2, LightCore, Legendary and alternate decos, the sidekicks and the magic items; 256x256 DXT5), named
        // as on the disc ("eruptor_lightcore.png"). The catalogue's "portraits" map says which toy each one is.
        int GiantsPortraits(const Disc& disc, const std::filesystem::path& folder)
        {
            constexpr std::string_view Folder = "/collections/sky2/portraits/";
            // Done when every portrait the catalogue names is there.
            const collection::Game* game = collection::Catalogue::Get().FindGame("giants");
            bool complete = game != nullptr && !game->portraits.empty();
            if (game != nullptr)
                for (const auto& [key, name] : game->portraits)
                    complete = complete && Exists(folder / (name + ".png"));
            int present = 0;
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(folder, error))
                present += entry.path().extension() == ".png";
            if (complete)
                return present;
            auto archive = OpenArchive(disc, "misc/UI_Collection_Champions.bld");
            std::optional<alchemy::IgzFile> scene = archive ? OpenScene(*archive, "level.bld") : std::nullopt;
            if (!scene)
            {
                fprintf(stderr, "[art] cannot read the collection screen (misc/UI_Collection_Champions.bld) of the disc\n");
                return present;
            }
            for (const alchemy::IgzFile::Picture& picture : scene->Pictures())
            {
                std::string name = Lower(picture.name);
                size_t at = name.find(Folder);
                if (at == std::string::npos || picture.width != picture.height || !name.ends_with(".png"))
                    continue;
                name = name.substr(at + Folder.size(), name.size() - at - Folder.size() - 4);
                std::filesystem::path path = folder / (name + ".png");
                Image image;
                if (!Exists(path) && alchemy::DecodePicture(*scene, picture, image) && SavePng(image, path))
                    present++;
            }
            return present;
        }

        struct Recipe
        {
            const char* game;
            void (*pictures)(const Disc&, Art&);
            int (*portraits)(const Disc&, const std::filesystem::path&);
        };

        constexpr Recipe Recipes[] = {
            { "ssa", SsaPictures, SsaPortraits },
            { "giants", GiantsPictures, GiantsPortraits },
        };

        const Recipe* FindRecipe(std::string_view game)
        {
            for (const Recipe& recipe : Recipes)
                if (game == recipe.game)
                    return &recipe;
            return nullptr;
        }

        int ReadVersion(const std::filesystem::path& manifest)
        {
            std::ifstream in(manifest);
            std::string line;
            while (std::getline(in, line))
                if (line.starts_with("version="))
                    return atoi(line.c_str() + 8);
            return 0;
        }
    }

    const char* RoleFile(Role role)
    {
        switch (role)
        {
        case Role::Logo: return "logo.png";
        case Role::Background: return "background.png";
        case Role::TitleIcon: return "title_icon.png";
        case Role::Count: break;
        }
        return "";
    }

    std::filesystem::path CacheFolder(std::string_view game)
    {
        std::filesystem::path data = datadir::Game(game);
        return data.empty() ? std::filesystem::path() : data / "launcher-cache";
    }

    std::filesystem::path PortraitFolder(std::string_view game)
    {
        if (const char* folder = getenv("SSA_PORTRAITS"))
            return datadir::FromText(folder);
        std::filesystem::path data = datadir::Game(game);
        return data.empty() ? std::filesystem::path() : data / "portraits";
    }

    std::string CachedPath(std::string_view game, Role role)
    {
        std::filesystem::path folder = CacheFolder(game);
        if (folder.empty() || !Exists(folder / RoleFile(role)))
            return {};
        return Text(folder / RoleFile(role));
    }

    bool Known(std::string_view game)
    {
        return FindRecipe(game) != nullptr;
    }

    Extraction Extract(std::string_view game, const Disc& disc, const std::vector<uint8_t>& titleIconPng, int parts)
    {
        Extraction result;
        const Recipe* recipe = FindRecipe(game);
        std::filesystem::path cache = CacheFolder(game), portraits = PortraitFolder(game);
        std::error_code error;
        if ((parts & Pictures) && !cache.empty())
        {
            std::filesystem::create_directories(cache, error);
            // Before 0.4: the cache was <data>/SSA/launcher-cache/<title id>/ for every game.
            for (const auto& entry : std::filesystem::directory_iterator(cache, error))
            {
                std::string name = entry.path().filename().string();
                if (entry.is_directory() && name.size() == 8 && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isxdigit(c); }))
                    std::filesystem::remove_all(entry.path(), error);
            }
            std::filesystem::path manifest = cache / "manifest.ini";
            bool current = ReadVersion(manifest) == CacheVersion;
            if (!titleIconPng.empty())
                SaveBytes(titleIconPng, cache / RoleFile(Role::TitleIcon));
            if (recipe != nullptr && (!current || !Exists(cache / RoleFile(Role::Logo)) || !Exists(cache / RoleFile(Role::Background))))
            {
                Art pictures;
                recipe->pictures(disc, pictures);
                if (!pictures.logo.rgba.empty())
                    SavePng(pictures.logo, cache / RoleFile(Role::Logo));
                if (!pictures.background.rgba.empty())
                    SavePng(pictures.background, cache / RoleFile(Role::Background));
                std::ofstream out(manifest, std::ios::trunc);
                out << "# Pictures read from the player's own disc (docs/runtime/host/game_art.md). Local only; deleting\n"
                       "# this folder is always safe: the launcher or the game reads them again.\n"
                    << "version=" << CacheVersion << "\ngame=" << game << "\nlogo=" << (pictures.logo.rgba.empty() ? "missing" : "ok")
                    << "\nbackground=" << (pictures.background.rgba.empty() ? "missing" : "ok") << "\n";
            }
            result.logo = Exists(cache / RoleFile(Role::Logo));
            result.background = Exists(cache / RoleFile(Role::Background));
            result.titleIcon = Exists(cache / RoleFile(Role::TitleIcon));
        }
        if ((parts & Portraits) && recipe != nullptr && !portraits.empty())
        {
            std::filesystem::create_directories(portraits, error);
            result.portraits = recipe->portraits(disc, portraits);
        }
        return result;
    }
}

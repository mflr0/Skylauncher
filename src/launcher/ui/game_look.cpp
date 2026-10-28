// The game page in each game's own look (docs/launcher_ui.md, "Each game's look"): the disc's pictures as textures, the
// logo's arrival, and the action button (ui::MainAction) in the game's colours.
#include "launcher/ui/launcher_app.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fmt/format.h>
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace LauncherTheme
{
    const GameStyle& StyleOf(const char* game)
    {
        static constexpr GameStyle Launcher = { AccentHover, AccentDark, AccentInk, Accent };
        if (game != nullptr && strcmp(game, "ssa") == 0)
            return SpyrosAdventureStyle;
        if (game != nullptr && strcmp(game, "giants") == 0)
            return GiantsStyle;
        if (game != nullptr && strcmp(game, "swapforce") == 0)
            return SwapForceStyle;
        return Launcher;
    }
}

namespace launcher_ui
{
    namespace
    {
        // Straight-alpha RGBA averaged down by whole boxes, weighted by alpha (so transparent texels don't darken edges).
        std::vector<uint8_t> Shrink(const uint8_t* rgba, int pitch, int width, int height, int outWidth, int outHeight)
        {
            std::vector<uint8_t> out(size_t(outWidth) * outHeight * 4);
            for (int y = 0; y < outHeight; y++)
                for (int x = 0; x < outWidth; x++)
                {
                    int x0 = x * width / outWidth, x1 = std::max(x0 + 1, (x + 1) * width / outWidth);
                    int y0 = y * height / outHeight, y1 = std::max(y0 + 1, (y + 1) * height / outHeight);
                    double sum[4] = {};
                    for (int sy = y0; sy < y1; sy++)
                        for (int sx = x0; sx < x1; sx++)
                        {
                            const uint8_t* p = rgba + size_t(sy) * pitch + size_t(sx) * 4;
                            double a = p[3] / 255.0;
                            sum[0] += p[0] * a, sum[1] += p[1] * a, sum[2] += p[2] * a, sum[3] += a;
                        }
                    uint8_t* o = &out[(size_t(y) * outWidth + x) * 4];
                    double count = double(x1 - x0) * (y1 - y0);
                    for (int c = 0; c < 3; c++)
                        o[c] = uint8_t(sum[3] > 0 ? std::clamp(sum[c] / sum[3] + 0.5, 0.0, 255.0) : 0);
                    o[3] = uint8_t(std::clamp(sum[3] / count * 255 + 0.5, 0.0, 255.0));
                }
            return out;
        }

        // Eases out past the end and back (a short landing): 0 -> 1.
        float EaseOutBack(float t)
        {
            constexpr float C1 = 1.2f, C3 = C1 + 1;
            float u = t - 1;
            return 1 + C3 * u * u * u + C1 * u * u;
        }
    }

    ArtTexture LoadArt(SDL_Renderer* renderer, const std::string& path, bool crop, int maxWidth)
    {
        ArtTexture art;
        SDL_Surface* loaded = path.empty() ? nullptr : SDL_LoadPNG(path.c_str());
        if (loaded == nullptr)
            return art;
        SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loaded);
        if (rgba == nullptr)
            return art;
        const uint8_t* pixels = static_cast<const uint8_t*>(rgba->pixels);
        int left = 0, top = 0, right = rgba->w, bottom = rgba->h;
        if (crop)
        {
            left = rgba->w, top = rgba->h, right = 0, bottom = 0;
            for (int y = 0; y < rgba->h; y++)
                for (int x = 0; x < rgba->w; x++)
                    if (pixels[size_t(y) * rgba->pitch + size_t(x) * 4 + 3] > 8)
                        left = std::min(left, x), right = std::max(right, x + 1), top = std::min(top, y), bottom = std::max(bottom, y + 1);
            if (left >= right || top >= bottom)
                left = 0, top = 0, right = rgba->w, bottom = rgba->h;
        }
        int width = right - left, height = bottom - top;
        int outWidth = maxWidth > 0 && width > maxWidth ? maxWidth : width;
        int outHeight = std::max(1, int(std::lround(double(height) * outWidth / width)));
        std::vector<uint8_t> out = Shrink(pixels + size_t(top) * rgba->pitch + size_t(left) * 4, rgba->pitch, width, height, outWidth, outHeight);
        SDL_DestroySurface(rgba);
        art.texture = LoadRgba(renderer, out, outWidth, outHeight);
        art.aspect = float(width) / float(height);
        return art;
    }

    bool SystemReducesMotion()
    {
#if defined(__APPLE__)
        // System Settings > Accessibility > Display > Reduce motion.
        bool reduce = false;
        if (CFPropertyListRef value = CFPreferencesCopyAppValue(CFSTR("reduceMotion"), CFSTR("com.apple.universalaccess")))
        {
            reduce = CFGetTypeID(value) == CFBooleanGetTypeID() && CFBooleanGetValue(static_cast<CFBooleanRef>(value));
            CFRelease(value);
        }
        return reduce;
#elif defined(_WIN32)
        // Settings > Accessibility > Visual effects > Animation effects.
        BOOL animations = TRUE;
        return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) && !animations;
#else
        return false;
#endif
    }

    bool App::ReduceMotion() const
    {
        std::string value = SettingGet(m_config, "launcher", "reduce_motion", "auto");
        if (value == "auto" || value.empty())
            return SystemReducesMotion();
        return ParseBool(value);
    }

    const ArtTexture* App::CachedArt(const char* game, LauncherAssets::Role role, int maxWidth)
    {
        std::string key = fmt::format("{} {} {}", game, int(role), maxWidth);
        auto found = m_cachedArt.find(key);
        if (found == m_cachedArt.end())
        {
            LauncherAssets::Asset asset = LauncherAssets::Cached(game, role);
            found = m_cachedArt.emplace(key, asset.valid ? LoadArt(m_renderer, asset.path, role == LauncherAssets::Role::Logo, maxWidth) : ArtTexture{}).first;
        }
        return found->second ? &found->second : nullptr;
    }

    void App::ForgetCachedArt(const char* game)
    {
        std::string prefix = std::string(game) + " ";
        for (auto it = m_cachedArt.begin(); it != m_cachedArt.end();)
            if (it->first.starts_with(prefix))
            {
                it->second.Destroy();
                it = m_cachedArt.erase(it);
            }
            else
                ++it;
    }

    float App::LogoDrop()
    {
        // A new arrival on the game page (from another page, or another game shown), or the logo appearing while there
        // (the disc was being read): the logo falls in, once.
        uint64_t now = SDL_GetTicks();
        bool arrived = m_lastPage != Page::Play || m_logoDropSlot != m_s;
        bool appeared = m_s->logo && !m_logoDropHad;
        if (arrived || appeared)
            m_logoDropAt = ReduceMotion() ? 0 : now;
        m_logoDropSlot = m_s;
        m_logoDropHad = bool(m_s->logo);
        if (m_logoDropAt == 0)
            return 1;
        return std::clamp(float(now - m_logoDropAt) / LogoDropMs, 0.0f, 1.0f);
    }

    void App::DrawLogo(ImDrawList* draw, const ArtTexture& logo, ImVec2 at, ImVec2 size, float alpha, bool shadow)
    {
        if (!logo)
            return;
        float width = std::min(size.x, size.y * logo.aspect);
        // One limit for every game, so the logos have the same size on every page: the smallest logo on the discs (Swap
        // Force's, 872 px wide) at one texel per device pixel. Larger only blurs.
        constexpr float LogoMaxPixels = 872;
        width = std::min(width, LogoMaxPixels / std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x));
        float height = width / logo.aspect;
        ImVec2 min(at.x + (size.x - width) / 2, at.y + (size.y - height) / 2), max(min.x + width, min.y + height);
        if (shadow)
        {
            // A soft shadow under it: a few darkened copies, further and fainter.
            float step = std::max(1.5f, height * 0.012f);
            for (int i = 3; i >= 1; i--)
                draw->AddImage(Ref(logo.texture), ImVec2(min.x, min.y + step * i * 1.6f), ImVec2(max.x, max.y + step * i * 1.6f), logo.uv0, logo.uv1,
                    IM_COL32(4, 8, 30, int(alpha * (110 - 28 * i))));
        }
        draw->AddImage(Ref(logo.texture), min, max, logo.uv0, logo.uv1, IM_COL32(255, 255, 255, int(alpha * 255)));
    }

    void App::DrawHeroLogo(ImDrawList* draw, const ArtTexture& logo, ImVec2 at, ImVec2 size)
    {
        // It falls from LogoDropDistance above, quickly, lands with a short give, and fades in on the way.
        float t = LogoDrop(), fall = (1 - EaseOutBack(t)) * LogoDropDistance;
        DrawLogo(draw, logo, ImVec2(at.x, at.y - fall), size, std::clamp(t * 2.5f, 0.0f, 1.0f));
    }

    App::MainActionHit App::DrawMainAction(ImVec2 at, const MainAction& action)
    {
        // ui::MainAction (components/buttons.h) in the shown game's colours; it takes the page's focus when asked.
        ui::MainActionOptions options;
        options.label = action.label;
        options.enabled = action.enabled;
        options.progress = action.progress;
        options.style = &StyleOf(m_s->game.id);
        const std::string tooltip = Tf("{} settings", m_s->game.name);
        options.gearTooltip = tooltip.c_str();
        options.takeFocus = m_focusTo == FocusTo::Page;
        options.showFocus = m_focusShow;
        if (options.takeFocus)
            m_focusTo = FocusTo::None;
        ImGui::SetCursorScreenPos(at);
        ui::MainActionResult result = ui::MainAction(options);
        m_testMainAt = result.mainCentre;
        m_testGearAt = result.gearCentre;
        return result.hit == ui::MainActionHit::Main ? MainActionHit::Main
            : result.hit == ui::MainActionHit::Settings ? MainActionHit::Settings : MainActionHit::None;
    }
}

// The game page in each game's own look (docs/launcher_ui.md, "Each game's look"): the disc's pictures as textures, the
// logo's arrival, and the widgets drawn in the game's style (the main button, the game's settings button).
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
        static constexpr GameStyle Launcher = { AccentHover, AccentDark, 0x0A1B33, 0xFFFFFF, 0x0A1B33, Accent, false };
        if (game != nullptr && strcmp(game, "ssa") == 0)
            return SpyrosAdventureStyle;
        if (game != nullptr && strcmp(game, "giants") == 0)
            return GiantsStyle;
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

        ImU32 WithAlpha(unsigned rgb, float alpha) { return Rgb(rgb, unsigned(std::clamp(alpha, 0.0f, 1.0f) * 255)); }

        ImU32 Lighter(unsigned rgb, float amount, float alpha = 1)
        {
            auto mix = [&](unsigned c) { return unsigned(c + (255 - c) * amount); };
            return Rgb(mix(rgb >> 16 & 0xFF) << 16 | mix(rgb >> 8 & 0xFF) << 8 | mix(rgb & 0xFF), unsigned(alpha * 255));
        }

        ImU32 Darker(unsigned rgb, float amount, float alpha = 1)
        {
            auto mix = [&](unsigned c) { return unsigned(c * (1 - amount)); };
            return Rgb(mix(rgb >> 16 & 0xFF) << 16 | mix(rgb >> 8 & 0xFF) << 8 | mix(rgb & 0xFF), unsigned(alpha * 255));
        }

        // The face of a game-styled button inside its rim: a soft stone bevel (SSA) or a glossy gel (Giants).
        void DrawFace(ImDrawList* draw, ImVec2 min, ImVec2 max, const GameStyle& style, float rounding, bool hovered, bool held, float alpha)
        {
            float height = max.y - min.y;
            unsigned top = style.top, bottom = style.bottom;
            ImU32 topColor = hovered && !held ? Lighter(top, 0.12f, alpha) : held ? Darker(top, 0.1f, alpha) : WithAlpha(top, alpha);
            ImU32 bottomColor = hovered && !held ? Lighter(bottom, 0.1f, alpha) : held ? Darker(bottom, 0.12f, alpha) : WithAlpha(bottom, alpha);
            if (style.gel)
            {
                // Giants: the bottom half darker, a hard split at the middle, the top half bright and shiny.
                float split = min.y + height * 0.5f;
                GradientRect(draw, min, max, Darker(bottom, 0.05f, alpha), bottomColor, rounding);
                GradientRect(draw, min, ImVec2(max.x, split), Lighter(top, 0.45f, alpha), topColor, rounding, ImDrawFlags_RoundCornersTop);
                draw->AddLine(ImVec2(min.x + rounding * 0.5f, split), ImVec2(max.x - rounding * 0.5f, split), Lighter(top, 0.2f, alpha * 0.6f), 1.0f);
                // A glint along the top edge, and the light the gel throws on its own bottom.
                draw->AddRectFilled(ImVec2(min.x + rounding, min.y + 2), ImVec2(max.x - rounding, min.y + 4), Rgb(0xFFFFFF, unsigned(alpha * 150)), 2);
                GradientRect(draw, ImVec2(min.x + 6, max.y - height * 0.22f), ImVec2(max.x - 6, max.y - 3), Rgb(0xFFFFFF, 0),
                    Lighter(top, 0.3f, alpha * 0.45f), rounding * 0.6f, ImDrawFlags_RoundCornersBottom);
            }
            else
            {
                // SSA: the logo's sky gradient, a soft light on the upper part and a darker lip at the bottom.
                GradientRect(draw, min, max, topColor, bottomColor, rounding);
                GradientRect(draw, ImVec2(min.x + 3, min.y + 2), ImVec2(max.x - 3, min.y + height * 0.48f), Rgb(0xFFFFFF, unsigned(alpha * 95)),
                    Rgb(0xFFFFFF, 0), rounding - 2, ImDrawFlags_RoundCornersTop);
                draw->AddRectFilled(ImVec2(min.x + 2, max.y - 5), ImVec2(max.x - 2, max.y), Darker(bottom, 0.35f, alpha * 0.8f), rounding,
                    ImDrawFlags_RoundCornersBottom);
            }
        }

        // Text with a rim, as the games letter their buttons: the rim drawn around, then the letters.
        void RimmedText(ImDrawList* draw, ImVec2 at, ImU32 ink, ImU32 rim, const char* text, const char* end = nullptr, float width = 1.6f)
        {
            for (int i = 0; i < 12; i++)
            {
                float angle = 6.2831853f * i / 12;
                draw->AddText(ImVec2(at.x + std::cos(angle) * width, at.y + std::sin(angle) * width), rim, text, end);
            }
            draw->AddText(ImVec2(at.x, at.y + width * 0.9f), rim, text, end); // a little weight under the letters
            draw->AddText(at, ink, text, end);
        }
    }

    void GradientRect(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom, float rounding, ImDrawFlags flags)
    {
        // ImGui draws the shape; its vertices are then coloured by height, alpha included.
        int first = draw->VtxBuffer.Size;
        draw->AddRectFilled(min, max, IM_COL32_WHITE, rounding, flags);
        ImVec4 a = ImGui::ColorConvertU32ToFloat4(top), b = ImGui::ColorConvertU32ToFloat4(bottom);
        float height = std::max(1.0f, max.y - min.y);
        for (int i = first; i < draw->VtxBuffer.Size; i++)
        {
            ImDrawVert& vertex = draw->VtxBuffer[i];
            float t = std::clamp((vertex.pos.y - min.y) / height, 0.0f, 1.0f);
            vertex.col = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
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
        float width = std::min(size.x, size.y * logo.aspect), height = width / logo.aspect;
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

    bool App::GameButton(const char* label, ImVec2 size, const GameStyle& style, bool enabled)
    {
        ImVec2 position = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(!enabled);
        bool pressed = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
        bool hovered = ImGui::IsItemHovered() && enabled, held = ImGui::IsItemActive() && enabled;
        ImGui::EndDisabled();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float alpha = enabled ? 1.0f : 0.45f, rounding = style.gel ? Radius - 2 : Radius, sink = held ? 2.0f : 0.0f;
        ImVec2 min(position.x, position.y + sink), max(position.x + size.x, position.y + size.y + sink);

        if (hovered)
            draw->AddRectFilled(ImVec2(min.x - 5, min.y - 5), ImVec2(max.x + 5, max.y + 5), Rgb(style.glow, 70), rounding + 5);
        draw->AddRectFilled(ImVec2(position.x + 1, position.y + 5), ImVec2(position.x + size.x - 1, position.y + size.y + 5), IM_COL32(0, 0, 0, int(alpha * 80)), rounding);
        draw->AddRectFilled(min, max, WithAlpha(style.outline, alpha), rounding);
        constexpr float Rim = 3;
        DrawFace(draw, ImVec2(min.x + Rim, min.y + Rim), ImVec2(max.x - Rim, max.y - Rim), style, rounding - Rim, hovered, held, alpha);

        const char* labelEnd = std::strstr(label, "##");
        if (labelEnd != label)
        {
            ImGui::PushFont(m_titleFont, size.y * 0.40f);
            ImVec2 text = ImGui::CalcTextSize(label, labelEnd);
            RimmedText(draw, ImVec2(min.x + (size.x - text.x) / 2, min.y + (size.y - text.y) / 2 - 1), WithAlpha(style.ink, alpha),
                WithAlpha(style.inkRim, alpha), label, labelEnd, 2.0f);
            ImGui::PopFont();
        }
        FocusRing(rounding);
        return pressed && enabled;
    }

    bool App::GameSettingsButton(const char* id, float size)
    {
        const GameStyle& style = StyleOf(m_s->game.id);
        ImVec2 at = ImGui::GetCursorScreenPos();
        bool pressed = GameButton(id, ImVec2(size, size), style);
        bool held = ImGui::IsItemActive();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 center(at.x + size / 2, at.y + size / 2 + (held ? 2.0f : 0.0f));
        float radius = size * 0.27f;
        // The gear in the game's lettering: a dark rim, then the white gear, its hole showing the face.
        DrawGear(draw, center, radius + 2.2f, Rgb(style.inkRim), Rgb(style.inkRim));
        DrawGear(draw, center, radius, Rgb(style.ink), Rgb(style.gel ? style.top : style.bottom));
        draw->AddCircle(center, radius * 0.36f, Rgb(style.inkRim), 24, 1.6f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s settings", m_s->game.name);
        return pressed;
    }
}

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

        // ---- The action area's styles (DrawMainAction) ----------------------------------------------------------

        unsigned Mix(unsigned a, unsigned b, float t)
        {
            auto channel = [&](int shift) {
                float x = float(a >> shift & 0xFF), y = float(b >> shift & 0xFF);
                return unsigned(std::lround(x + (y - x) * t)) << shift;
            };
            return channel(16) | channel(8) | channel(0);
        }

        // Towards white (> 0) or black (< 0).
        unsigned Lift(unsigned rgb, float amount) { return amount >= 0 ? Mix(rgb, 0xFFFFFF, amount) : Mix(rgb, 0x000000, -amount); }

        float Luminance(unsigned rgb)
        {
            auto linear = [](unsigned c) { float v = c / 255.0f; return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
            return 0.2126f * linear(rgb >> 16 & 0xFF) + 0.7152f * linear(rgb >> 8 & 0xFF) + 0.0722f * linear(rgb & 0xFF);
        }

        // Large bold text needs 3:1: white when it gets that on the fill, else the game's dark outline colour.
        unsigned InkOn(unsigned fill, unsigned dark) { return 1.05f / (Luminance(fill) + 0.05f) >= 3.0f ? 0xFFFFFF : dark; }

        // On whole output pixels: text placed there stays sharp.
        float Snap(float v)
        {
            float scale = ImGui::GetIO().DisplayFramebufferScale.y;
            return std::round(v * scale) / scale;
        }

        // A soft shadow (or glow) around a rounded shape: a few wider, fainter copies.
        void SoftShadow(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, float offsetY, float spread, unsigned rgb, float alpha)
        {
            constexpr int Layers = 5;
            for (int i = Layers; i >= 1; i--)
            {
                float grow = spread * i / Layers;
                draw->AddRectFilled(ImVec2(min.x - grow, min.y - grow + offsetY), ImVec2(max.x + grow, max.y + grow + offsetY),
                    Rgb(rgb, unsigned(alpha * 255 / Layers)), rounding + grow);
            }
        }

        // A vertical gradient through three colours, `mid` (0..1) the height of the middle one.
        void GradientRect3(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 top, ImU32 middle, ImU32 bottom, float mid, float rounding,
            ImDrawFlags flags = 0)
        {
            int first = draw->VtxBuffer.Size;
            draw->AddRectFilled(min, max, IM_COL32_WHITE, rounding, flags);
            ImVec4 a = ImGui::ColorConvertU32ToFloat4(top), b = ImGui::ColorConvertU32ToFloat4(middle), c = ImGui::ColorConvertU32ToFloat4(bottom);
            float height = std::max(1.0f, max.y - min.y);
            for (int i = first; i < draw->VtxBuffer.Size; i++)
            {
                ImDrawVert& vertex = draw->VtxBuffer[i];
                float t = std::clamp((vertex.pos.y - min.y) / height, 0.0f, 1.0f);
                ImVec4 from = t < mid ? a : b, to = t < mid ? b : c;
                float u = t < mid ? t / mid : (t - mid) / (1 - mid);
                vertex.col = ImGui::ColorConvertFloat4ToU32(ImVec4(from.x + (to.x - from.x) * u, from.y + (to.y - from.y) * u,
                    from.z + (to.z - from.z) * u, from.w + (to.w - from.w) * u));
            }
        }

        // One button's state as drawn.
        struct Look
        {
            bool hovered = false, held = false, enabled = true;
            float progress = -1;
            bool Busy() const { return progress >= 0; }
        };

        // A fill in its state: lighter under the pointer, darker pressed, greyed when disabled, dark and tinted when busy.
        unsigned Shade(unsigned rgb, const Look& look)
        {
            if (look.Busy())
                return Mix(rgb, BottomBarRaised, 0.78f);
            if (!look.enabled)
                return Mix(rgb, 0x4A4A54, 0.7f);
            return look.held ? Lift(rgb, -0.1f) : look.hovered ? Lift(rgb, 0.1f) : rgb;
        }

        // A busy button's progress on its face (min..max, `rounding`, `flags`): the done part tinted in the game's colour,
        // and a bar along the bottom edge, both following the face's corners.
        void DrawProgress(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImDrawFlags flags, unsigned color, float progress)
        {
            float x = Snap(min.x + (max.x - min.x) * std::clamp(progress, 0.0f, 1.0f));
            constexpr float BarH = 5;
            draw->PushClipRect(ImVec2(min.x, max.y - BarH), max, true);
            draw->AddRectFilled(min, max, Rgb(0xFFFFFF, 28), rounding, flags);
            draw->PopClipRect();
            draw->PushClipRect(min, ImVec2(x, max.y), true);
            draw->AddRectFilled(min, max, Rgb(color, 60), rounding, flags);
            draw->PushClipRect(ImVec2(min.x, max.y - BarH), ImVec2(x, max.y), true);
            draw->AddRectFilled(min, max, Rgb(color), rounding, flags);
            draw->PopClipRect();
            draw->PopClipRect();
        }

        // How a label is lettered: its ink, an optional rim and drop shadow, extra space between letters, extra passes
        // (each 0.5 point to the right) to make it heavier.
        struct Lettering
        {
            ImU32 ink;
            ImU32 rim = 0;
            float rimWidth = 0;
            ImU32 shadow = 0;
            float shadowY = 0;
            float spacing = 0;
            int passes = 1;
        };

        float LabelWidth(const char* text, float spacing)
        {
            size_t count = std::strlen(text);
            return ImGui::CalcTextSize(text).x + spacing * float(count > 0 ? count - 1 : 0);
        }

        void DrawLetters(ImDrawList* draw, ImVec2 at, ImU32 color, const char* text, float spacing, int passes)
        {
            for (const char* c = text; *c != 0; c++)
            {
                for (int i = 0; i < passes; i++)
                    draw->AddText(ImVec2(at.x + 0.5f * i, at.y), color, c, c + 1);
                at.x += ImGui::CalcTextSize(c, c + 1).x + spacing;
            }
        }

        // The label (ASCII) with the current font, its left at `x`, its capitals centred on `middle`, on whole pixels.
        void DrawLabel(ImDrawList* draw, float x, float middle, const char* text, const Lettering& letters)
        {
            const ImFontGlyph* cap = ImGui::GetFontBaked()->FindGlyph('H');
            ImVec2 at(Snap(x), Snap(middle - (cap->Y0 + cap->Y1) / 2));
            if (letters.shadow != 0)
                DrawLetters(draw, ImVec2(at.x, at.y + letters.shadowY), letters.shadow, text, letters.spacing, letters.passes);
            if (letters.rim != 0)
                for (int i = 0; i < 8; i++)
                {
                    float angle = 6.2831853f * i / 8;
                    DrawLetters(draw, ImVec2(at.x + std::cos(angle) * letters.rimWidth, at.y + std::sin(angle) * letters.rimWidth), letters.rim,
                        text, letters.spacing, letters.passes);
                }
            DrawLetters(draw, at, letters.ink, text, letters.spacing, letters.passes);
        }

        // The size at which `text` fits in `width` with `font`, at most `size`.
        float FitSize(ImFont* font, float size, const char* text, float width, float spacing)
        {
            ImGui::PushFont(font, size);
            float textW = LabelWidth(text, spacing);
            ImGui::PopFont();
            return textW > width ? std::floor(size * width / textW) : size;
        }

        // The big launcher style's icons, `size` tall around `center`: PLAY's triangle, INSTALL's arrow into a tray.
        void DrawPlayIcon(ImDrawList* draw, ImVec2 center, float size, ImU32 ink)
        {
            float h = size / 2, w = size * 0.48f;
            draw->AddTriangleFilled(ImVec2(center.x - w * 0.45f, center.y - h), ImVec2(center.x + w * 0.55f + 2, center.y),
                ImVec2(center.x - w * 0.45f, center.y + h), ink);
        }

        void DrawInstallIcon(ImDrawList* draw, ImVec2 center, float size, ImU32 ink)
        {
            float h = size / 2, stroke = std::max(2.0f, size * 0.13f), head = size * 0.36f;
            draw->AddRectFilled(ImVec2(center.x - stroke / 2, center.y - h), ImVec2(center.x + stroke / 2, center.y + h * 0.3f), ink);
            draw->AddTriangleFilled(ImVec2(center.x - head, center.y + h * 0.3f - head * 0.55f), ImVec2(center.x + head, center.y + h * 0.3f - head * 0.55f),
                ImVec2(center.x, center.y + h * 0.3f + head * 0.45f), ink);
            draw->AddRectFilled(ImVec2(center.x - h, center.y + h - stroke), ImVec2(center.x + h, center.y + h), ink, stroke / 2);
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
    App::MainActionLayout App::MainActionLayoutOf(int style) const
    {
        switch (style)
        {
        case 1: return { 300, 64, 0, 64 };   // one split button
        case 2: return { 300, 72, 12, 72 };  // two buttons in the game's finish
        case 3: return { 340, 60, 10, 60 };  // a wide button, a small one
        default: return { PlayButtonW, PlayButtonH, 12, PlayButtonH };
        }
    }

    App::MainActionHit App::DrawMainAction(ImVec2 at, const MainAction& action)
    {
        const GameStyle& style = StyleOf(m_s->game.id);
        MainActionLayout layout = MainActionLayoutOf(m_buttonStyle);
        MainActionHit hit = MainActionHit::None;
        ImVec2 mainSize(layout.mainW, layout.height), gearAt(at.x + layout.mainW + layout.gap, at.y);
        if (m_buttonStyle == 0)
        {
            ImGui::SetCursorScreenPos(at);
            if (GameButton(action.label, mainSize, style, action.enabled))
                hit = MainActionHit::Main;
            TakeFocus(FocusTo::Page);
            ImGui::SetCursorScreenPos(gearAt);
            if (GameSettingsButton("##gamesettings", layout.settingsW))
                hit = MainActionHit::Settings;
            m_testMainAt = ImVec2(at.x + mainSize.x / 2, at.y + mainSize.y / 2);
            m_testGearAt = ImVec2(gearAt.x + layout.settingsW / 2, at.y + layout.height / 2);
            return hit;
        }

        // The label: busy, its percentage instead of the dots.
        std::string label = action.label;
        if (action.progress >= 0)
        {
            if (label.ends_with("..."))
                label.resize(label.size() - 3);
            label += fmt::format(" {}%", int(std::floor(std::clamp(action.progress, 0.0f, 1.0f) * 100)));
        }

        // The two items first (ids that keep the focus when the label changes), each drawn right after it.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 mainMin = at, mainMax(at.x + mainSize.x, at.y + mainSize.y);
        ImVec2 gearMin = gearAt, gearMax(gearAt.x + layout.settingsW, at.y + layout.height);
        m_testMainAt = ImVec2((mainMin.x + mainMax.x) / 2, (mainMin.y + mainMax.y) / 2);
        m_testGearAt = ImVec2((gearMin.x + gearMax.x) / 2, (gearMin.y + gearMax.y) / 2);

        ImGui::PushStyleColor(ImGuiCol_NavCursor, 0); // the ring below instead of ImGui's
        ImGui::SetCursorScreenPos(mainMin);
        ImGui::BeginDisabled(!action.enabled);
        if (ImGui::InvisibleButton("##main", mainSize, ImGuiButtonFlags_EnableNav) && action.enabled)
            hit = MainActionHit::Main;
        ImGui::EndDisabled();
        Look main;
        main.enabled = action.enabled;
        main.hovered = action.enabled && ImGui::IsItemHovered();
        main.held = action.enabled && ImGui::IsItemActive();
        main.progress = action.progress;
        bool mainFocused = ImGui::IsItemFocused() && io.NavVisible;
        if (m_buttonStyle == 1)
            main.hovered = main.hovered || (mainFocused && main.enabled);
        TakeFocus(FocusTo::Page);

        // The keyboard and pad focus: the launcher's ring, 4 points outside.
        auto ring = [&](ImVec2 min, ImVec2 max, float rounding) {
            constexpr float Gap = 4;
            draw->AddRect(ImVec2(min.x - Gap, min.y - Gap), ImVec2(max.x + Gap, max.y + Gap), Rgb(Accent), rounding + Gap, 0, 2.0f);
        };

        if (m_buttonStyle == 1)
        {
            // Flat and crisp: one rounded button in the game's colour, a light gradient, the gear in its own segment.
            const float rounding = Radius;
            unsigned top = Mix(style.top, style.bottom, 0.2f), bottom = Mix(style.top, style.bottom, 0.58f);
            ImVec2 groupMax(gearMax.x, mainMax.y);
            SoftShadow(draw, ImVec2(mainMin.x, mainMin.y + 2), groupMax, rounding, 2, 6, 0x000000, 0.35f);
            GradientRect(draw, mainMin, mainMax, Rgb(Shade(top, main)), Rgb(Shade(bottom, main)), rounding, ImDrawFlags_RoundCornersLeft);
            if (main.Busy())
                DrawProgress(draw, mainMin, mainMax, rounding, ImDrawFlags_RoundCornersLeft, Mix(top, bottom, 0.5f), main.progress);
            draw->AddLine(ImVec2(mainMin.x + rounding, mainMin.y + 0.5f), ImVec2(mainMax.x, mainMin.y + 0.5f), Rgb(0xFFFFFF, main.enabled ? 70 : 25));
            // The ink of the idle face in every state (it never flips under the pointer); light on the busy one.
            unsigned ink = main.Busy() ? Text : InkOn(Mix(top, bottom, 0.5f), style.outline);
            ImGui::PushFont(m_titleFont, FitSize(m_titleFont, std::round(layout.height * 0.4f), label.c_str(), mainSize.x - 40, 0));
            float textW = LabelWidth(label.c_str(), 0);
            DrawLabel(draw, mainMin.x + (mainSize.x - textW) / 2, (mainMin.y + mainMax.y) / 2, label.c_str(),
                { Rgb(ink, main.enabled || main.Busy() ? 255 : 150) });
            ImGui::PopFont();

            ImGui::SetCursorScreenPos(gearMin);
            if (ImGui::InvisibleButton("##gamesettings", ImVec2(layout.settingsW, layout.height), ImGuiButtonFlags_EnableNav))
                hit = MainActionHit::Settings;
            Look gear;
            bool gearFocused = ImGui::IsItemFocused() && io.NavVisible;
            gear.hovered = ImGui::IsItemHovered() || gearFocused;
            gear.held = ImGui::IsItemActive();
            GradientRect(draw, gearMin, gearMax, Rgb(Shade(top, gear)), Rgb(Shade(bottom, gear)), rounding, ImDrawFlags_RoundCornersRight);
            draw->AddLine(ImVec2(gearMin.x, gearMin.y + 0.5f), ImVec2(gearMax.x - rounding, gearMin.y + 0.5f), Rgb(0xFFFFFF, 70));
            // The split: a dark hairline and a light one beside it, short of the edges.
            draw->AddLine(ImVec2(gearMin.x - 0.5f, gearMin.y + 14), ImVec2(gearMin.x - 0.5f, gearMax.y - 14), Rgb(style.outline, 110));
            draw->AddLine(ImVec2(gearMin.x + 0.5f, gearMin.y + 14), ImVec2(gearMin.x + 0.5f, gearMax.y - 14), Rgb(0xFFFFFF, 45));
            unsigned gearFace = Shade(Mix(top, bottom, 0.5f), gear);
            DrawGear(draw, m_testGearAt, layout.height * 0.22f, Rgb(InkOn(Mix(top, bottom, 0.5f), style.outline)), Rgb(gearFace));
            // One ring around the whole split button; the focused half is lit as under the pointer.
            if (mainFocused || gearFocused)
                ring(mainMin, gearMax, rounding);
        }
        else if (m_buttonStyle == 2)
        {
            // The game's finish, refined: a thin dark rim, SSA's stone bevel or Giants' gold, clean lettering.
            const float rounding = Radius;
            constexpr float Rim = 2;
            auto button = [&](ImVec2 min, ImVec2 max, const Look& look) {
                float sink = look.held ? 1.0f : 0.0f;
                min.y += sink, max.y += sink;
                SoftShadow(draw, ImVec2(min.x, min.y + 3 - sink), ImVec2(max.x, max.y - sink), rounding, 3 - sink, 6, 0x000000, 0.4f);
                if (look.hovered && !look.held)
                    SoftShadow(draw, min, max, rounding, 0, 9, style.glow, 0.32f);
                float alpha = look.enabled || look.Busy() ? 1.0f : 0.8f;
                draw->AddRectFilled(min, max, WithAlpha(look.Busy() ? Mix(style.outline, BottomBarRaised, 0.5f) : style.outline, alpha), rounding);
                ImVec2 faceMin(min.x + Rim, min.y + Rim), faceMax(max.x - Rim, max.y - Rim);
                float faceR = rounding - Rim, height = faceMax.y - faceMin.y;
                unsigned top = Shade(style.top, look), bottom = Shade(style.bottom, look);
                if (style.gel)
                    GradientRect3(draw, faceMin, faceMax, Rgb(Lift(top, 0.4f)), Rgb(top), Rgb(bottom), 0.42f, faceR);
                else
                    GradientRect(draw, faceMin, faceMax, Rgb(Lift(top, 0.06f)), Rgb(bottom), faceR);
                if (look.Busy())
                    DrawProgress(draw, faceMin, faceMax, faceR, 0, style.top, look.progress);
                unsigned light = look.enabled ? 255 : 120;
                // A soft sheen on the upper half, a crisp light line just inside the top, a darker lip at the bottom.
                GradientRect(draw, faceMin, ImVec2(faceMax.x, faceMin.y + height * 0.5f), Rgb(0xFFFFFF, light * (style.gel ? 50 : 70) / 255),
                    Rgb(0xFFFFFF, 0), faceR, ImDrawFlags_RoundCornersTop);
                draw->AddLine(ImVec2(faceMin.x + faceR * 0.7f, faceMin.y + 1), ImVec2(faceMax.x - faceR * 0.7f, faceMin.y + 1),
                    Rgb(0xFFFFFF, light * (style.gel ? 190 : 140) / 255), 1.0f);
                GradientRect(draw, ImVec2(faceMin.x, faceMax.y - height * 0.3f), faceMax, Rgb(Lift(bottom, -0.3f), 0),
                    Rgb(Lift(bottom, -0.3f), look.enabled ? 170 : 90), faceR, ImDrawFlags_RoundCornersBottom);
                return sink;
            };
            Lettering letters{ Rgb(0xFFFFFF, main.enabled || main.Busy() ? 255 : 170), Rgb(style.inkRim, main.enabled || main.Busy() ? 235 : 120), 1.25f,
                Rgb(style.inkRim, main.enabled ? 150 : 60), 2.0f };
            float sink = button(mainMin, mainMax, main);
            ImGui::PushFont(m_titleFont, FitSize(m_titleFont, std::round(layout.height * 0.38f), label.c_str(), mainSize.x - 44, 0));
            float textW = LabelWidth(label.c_str(), 0);
            DrawLabel(draw, mainMin.x + (mainSize.x - textW) / 2, (mainMin.y + mainMax.y) / 2 + sink - 1, label.c_str(), letters);
            ImGui::PopFont();
            if (mainFocused)
                ring(mainMin, mainMax, rounding);

            ImGui::SetCursorScreenPos(gearMin);
            if (ImGui::InvisibleButton("##gamesettings", ImVec2(layout.settingsW, layout.height), ImGuiButtonFlags_EnableNav))
                hit = MainActionHit::Settings;
            Look gear;
            gear.hovered = ImGui::IsItemHovered();
            gear.held = ImGui::IsItemActive();
            float gearSink = button(gearMin, gearMax, gear);
            ImVec2 center(m_testGearAt.x, m_testGearAt.y + gearSink);
            float radius = layout.height * 0.28f;
            DrawGear(draw, ImVec2(center.x, center.y + 1.5f), radius + 0.5f, Rgb(style.inkRim, 150), Rgb(style.inkRim, 0));
            DrawGear(draw, center, radius + 1.25f, Rgb(style.inkRim), Rgb(style.inkRim));
            DrawGear(draw, center, radius, Rgb(0xFFFFFF), Rgb(Mix(Shade(style.top, gear), Shade(style.bottom, gear), 0.5f)));
            if (ImGui::IsItemFocused() && io.NavVisible)
                ring(gearMin, gearMax, rounding);
        }
        else
        {
            // A big launcher's button: one wide button in the game's colour, PLAY's icon and a spaced label; the settings a
            // quiet icon button of the band beside it.
            const float rounding = RadiusSmall;
            unsigned top = Mix(style.top, style.bottom, 0.5f), bottom = style.bottom;
            unsigned faceTop = Shade(top, main), faceBottom = Shade(bottom, main);
            SoftShadow(draw, ImVec2(mainMin.x, mainMin.y + 3), mainMax, rounding, 3, 8, 0x000000, 0.4f);
            if (main.hovered && !main.held)
                SoftShadow(draw, mainMin, mainMax, rounding, 0, 12, style.glow, 0.35f);
            GradientRect(draw, mainMin, mainMax, Rgb(faceTop), Rgb(faceBottom), rounding);
            if (main.Busy())
                DrawProgress(draw, mainMin, mainMax, rounding, 0, Mix(style.top, style.bottom, 0.3f), main.progress);
            draw->AddLine(ImVec2(mainMin.x + rounding, mainMin.y + 0.5f), ImVec2(mainMax.x - rounding, mainMin.y + 0.5f),
                Rgb(0xFFFFFF, main.enabled ? 80 : 25));
            unsigned ink = main.Busy() ? Text : InkOn(Mix(top, bottom, 0.5f), style.outline); // the idle face's, in every state
            ImU32 inkColor = Rgb(ink, main.enabled || main.Busy() ? 255 : 150);
            // The label in the text font's semibold, spaced, with PLAY's or INSTALL's icon before it.
            bool play = label == "PLAY", install = label == "INSTALL";
            constexpr float Spacing = 2.5f, IconGap = 14;
            ImGui::PushFont(m_boldFont, FitSize(m_boldFont, std::round(layout.height * 0.42f), label.c_str(), mainSize.x - 80, Spacing));
            const ImFontGlyph* cap = ImGui::GetFontBaked()->FindGlyph('H');
            float capH = cap->Y1 - cap->Y0, iconW = play || install ? capH * 1.05f + IconGap : 0;
            float textW = LabelWidth(label.c_str(), Spacing), left = mainMin.x + (mainSize.x - textW - iconW) / 2;
            float middle = (mainMin.y + mainMax.y) / 2;
            if (play)
                DrawPlayIcon(draw, ImVec2(left + capH * 0.45f, middle), capH * 1.05f, inkColor);
            else if (install)
                DrawInstallIcon(draw, ImVec2(left + capH * 0.5f, middle), capH * 1.1f, inkColor);
            DrawLabel(draw, left + iconW, middle, label.c_str(), { inkColor, 0, 0, 0, 0, Spacing, 2 });
            ImGui::PopFont();
            if (mainFocused)
                ring(mainMin, mainMax, rounding);

            ImGui::SetCursorScreenPos(gearMin);
            if (ImGui::InvisibleButton("##gamesettings", ImVec2(layout.settingsW, layout.height), ImGuiButtonFlags_EnableNav))
                hit = MainActionHit::Settings;
            bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
            unsigned fill = held ? BottomBarRaisedActive : hovered ? BottomBarRaisedHover : BottomBarRaised;
            draw->AddRectFilled(gearMin, gearMax, Rgb(fill), rounding);
            draw->AddRect(gearMin, gearMax, Rgb(hovered ? BorderBright : BottomBarBorder), rounding, 0, 1.0f);
            DrawGear(draw, m_testGearAt, layout.height * 0.2f, Rgb(BottomBarInk), Rgb(fill));
            if (ImGui::IsItemFocused() && io.NavVisible)
                ring(gearMin, gearMax, rounding);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s settings", m_s->game.name);
        return hit;
    }
}

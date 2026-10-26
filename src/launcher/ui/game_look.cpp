// The game page in each game's own look (docs/launcher_ui.md, "Each game's look"): the disc's pictures as textures, the
// logo's arrival, and the action button in the game's colours (the main action and the game's settings).
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

        // On whole output pixels: edges and text placed there stay sharp.
        float Snap(float v)
        {
            float scale = ImGui::GetIO().DisplayFramebufferScale.y;
            return std::round(v * scale) / scale;
        }

        // A soft shadow around a rounded shape: a few wider, fainter copies.
        void SoftShadow(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, float offsetY, float spread, float alpha)
        {
            constexpr int Layers = 5;
            for (int i = Layers; i >= 1; i--)
            {
                float grow = spread * i / Layers;
                draw->AddRectFilled(ImVec2(min.x - grow, min.y - grow + offsetY), ImVec2(max.x + grow, max.y + grow + offsetY),
                    IM_COL32(0, 0, 0, int(alpha * 255 / Layers)), rounding + grow);
            }
        }

        // One half of the action button as drawn.
        struct Look
        {
            bool hovered = false, held = false, enabled = true;
            float progress = -1; // 0..1 while busy
            bool Busy() const { return progress >= 0; }
        };

        // A fill in its state: lighter under the pointer (or the focus), darker pressed, greyed when disabled, dark and
        // tinted when busy.
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

        // The label with the current font, centred in `min..max` on its capitals, on whole pixels.
        void DrawLabel(ImDrawList* draw, ImVec2 min, ImVec2 max, const char* text, ImU32 ink)
        {
            const ImFontGlyph* cap = ImGui::GetFontBaked()->FindGlyph('H');
            float width = ImGui::CalcTextSize(text).x;
            draw->AddText(ImVec2(Snap((min.x + max.x - width) / 2), Snap((min.y + max.y) / 2 - (cap->Y0 + cap->Y1) / 2)), ink, text);
        }

        // The size at which `text` fits in `width` with `font`, at most `size`.
        float FitSize(ImFont* font, float size, const char* text, float width)
        {
            ImGui::PushFont(font, size);
            float textW = ImGui::CalcTextSize(text).x;
            ImGui::PopFont();
            return textW > width ? std::floor(size * width / textW) : size;
        }
    }


    void GradientRect(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom, float rounding, ImDrawFlags flags)
    {
        // ImGui draws the shape; its vertices are then coloured by height, alpha included. The anti-aliased fringe's
        // vertices keep their transparency (else the edges come out hard and half a pixel wider).
        int first = draw->VtxBuffer.Size;
        draw->AddRectFilled(min, max, IM_COL32_WHITE, rounding, flags);
        ImVec4 a = ImGui::ColorConvertU32ToFloat4(top), b = ImGui::ColorConvertU32ToFloat4(bottom);
        float height = std::max(1.0f, max.y - min.y);
        for (int i = first; i < draw->VtxBuffer.Size; i++)
        {
            ImDrawVert& vertex = draw->VtxBuffer[i];
            float t = std::clamp((vertex.pos.y - min.y) / height, 0.0f, 1.0f), coverage = float(vertex.col >> IM_COL32_A_SHIFT & 0xFF) / 255;
            vertex.col = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                (a.w + (b.w - a.w) * t) * coverage));
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

    App::MainActionHit App::DrawMainAction(ImVec2 at, const MainAction& action)
    {
        // One split button in the game's colour: a light gradient, the UI's corners, no outline; the main action on the
        // left, the game's settings (a gear) in a segment on the right behind a hairline.
        const GameStyle& style = StyleOf(m_s->game.id);
        const float rounding = Radius;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        MainActionHit hit = MainActionHit::None;
        ImVec2 mainMin = at, mainMax(at.x + PlayButtonW, at.y + PlayButtonH);
        ImVec2 gearMin(mainMax.x, at.y), gearMax(mainMax.x + PlayGearW, mainMax.y);
        m_testMainAt = ImVec2((mainMin.x + mainMax.x) / 2, (mainMin.y + mainMax.y) / 2);
        m_testGearAt = ImVec2((gearMin.x + gearMax.x) / 2, (gearMin.y + gearMax.y) / 2);
        unsigned top = Mix(style.top, style.bottom, 0.2f), bottom = Mix(style.top, style.bottom, 0.58f), middle = Mix(top, bottom, 0.5f);

        // The label: busy, its percentage instead of the dots.
        std::string label = action.label;
        if (action.progress >= 0)
        {
            if (label.ends_with("..."))
                label.resize(label.size() - 3);
            label += fmt::format(" {}%", int(std::floor(std::clamp(action.progress, 0.0f, 1.0f) * 100)));
        }

        // The main half (an id that keeps the focus when the label changes); the focused half is lit as under the pointer.
        ImGui::PushStyleColor(ImGuiCol_NavCursor, 0); // the ring below instead of ImGui's
        ImGui::SetCursorScreenPos(mainMin);
        ImGui::BeginDisabled(!action.enabled);
        if (ImGui::InvisibleButton("##main", ImVec2(PlayButtonW, PlayButtonH), ImGuiButtonFlags_EnableNav) && action.enabled)
            hit = MainActionHit::Main;
        ImGui::EndDisabled();
        bool mainFocused = ImGui::IsItemFocused() && io.NavVisible;
        Look main;
        main.enabled = action.enabled;
        main.hovered = action.enabled && (ImGui::IsItemHovered() || mainFocused);
        main.held = action.enabled && ImGui::IsItemActive();
        main.progress = action.progress;
        TakeFocus(FocusTo::Page);

        SoftShadow(draw, ImVec2(mainMin.x, mainMin.y + 2), gearMax, rounding, 2, 6, 0.35f);
        GradientRect(draw, mainMin, mainMax, Rgb(Shade(top, main)), Rgb(Shade(bottom, main)), rounding, ImDrawFlags_RoundCornersLeft);
        if (main.Busy())
            DrawProgress(draw, mainMin, mainMax, rounding, ImDrawFlags_RoundCornersLeft, middle, main.progress);
        draw->AddLine(ImVec2(mainMin.x + rounding, mainMin.y + 0.5f), ImVec2(mainMax.x, mainMin.y + 0.5f), Rgb(0xFFFFFF, main.enabled ? 70 : 25));
        // The idle face's ink under the pointer and pressed (it never flips); the greyed or busy face's own otherwise.
        unsigned ink = main.Busy() || !main.enabled ? InkOn(Shade(middle, main), style.outline) : InkOn(middle, style.outline);
        ImGui::PushFont(m_titleFont, FitSize(m_titleFont, std::round(PlayButtonH * 0.4f), label.c_str(), PlayButtonW - 40));
        DrawLabel(draw, mainMin, mainMax, label.c_str(), Rgb(ink, main.enabled || main.Busy() ? 255 : 150));
        ImGui::PopFont();

        // The settings half.
        ImGui::SetCursorScreenPos(gearMin);
        if (ImGui::InvisibleButton("##gamesettings", ImVec2(PlayGearW, PlayButtonH), ImGuiButtonFlags_EnableNav))
            hit = MainActionHit::Settings;
        bool gearFocused = ImGui::IsItemFocused() && io.NavVisible;
        Look gear;
        gear.hovered = ImGui::IsItemHovered() || gearFocused;
        gear.held = ImGui::IsItemActive();
        GradientRect(draw, gearMin, gearMax, Rgb(Shade(top, gear)), Rgb(Shade(bottom, gear)), rounding, ImDrawFlags_RoundCornersRight);
        draw->AddLine(ImVec2(gearMin.x, gearMin.y + 0.5f), ImVec2(gearMax.x - rounding, gearMin.y + 0.5f), Rgb(0xFFFFFF, 70));
        // The split: a dark hairline and a light one beside it, short of the edges.
        draw->AddLine(ImVec2(gearMin.x - 0.5f, gearMin.y + 14), ImVec2(gearMin.x - 0.5f, gearMax.y - 14), Rgb(style.outline, 110));
        draw->AddLine(ImVec2(gearMin.x + 0.5f, gearMin.y + 14), ImVec2(gearMin.x + 0.5f, gearMax.y - 14), Rgb(0xFFFFFF, 45));
        DrawGear(draw, m_testGearAt, PlayButtonH * 0.22f, Rgb(InkOn(middle, style.outline)), Rgb(Shade(middle, gear)));
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tf("{} settings", m_s->game.name).c_str());

        // One ring around the whole button for the keyboard and pad focus, 4 points outside.
        if (mainFocused || gearFocused)
            draw->AddRect(ImVec2(mainMin.x - 4, mainMin.y - 4), ImVec2(gearMax.x + 4, gearMax.y + 4), Rgb(Accent), rounding + 4, 0, 2.0f);
        return hit;
    }
}

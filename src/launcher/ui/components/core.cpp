// The components' shared ground (core.h): context, state, text, colour, focus ring, icons.
#include "launcher/ui/components/core.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui_internal.h> // ImGui::FocusItem

namespace ui
{
    namespace
    {
        Context g_context;
        State g_preview;
        bool g_previewOn = false;
        int g_bubbles = 0; // bubbles drawn this frame: each has its own window
        constexpr float Tau = 6.2831853f;
    }

    Context& Ctx() { return g_context; }

    void NewFrame(uint64_t now)
    {
        g_context.now = now;
        g_context.animating = false;
        g_bubbles = 0;
    }

    void PreviewState(const State& state)
    {
        g_preview = state;
        g_previewOn = true;
    }

    State ItemState(bool disabled, bool loading)
    {
        State state;
        state.disabled = disabled;
        state.loading = loading;
        if (!disabled && !loading)
        {
            state.hovered = ImGui::IsItemHovered();
            // Pressed: held by the keyboard or the pad, or by the mouse while still over it (leaving it cancels a click).
            state.pressed = ImGui::IsItemActive() &&
                (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
        }
        state.focused = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
        if (g_previewOn)
        {
            state.hovered = state.hovered || g_preview.hovered;
            state.focused = state.focused || g_preview.focused;
            state.pressed = state.pressed || g_preview.pressed;
            state.holdFill = g_preview.holdFill;
            state.preview = true;
            g_previewOn = false;
        }
        return state;
    }

    // ---- Text ------------------------------------------------------------------------------------------------------

    ImFont* FontOf(TextStyle style, bool bold)
    {
        const Fonts& fonts = g_context.fonts;
        switch (style)
        {
        case TextStyle::Title:
        case TextStyle::Display: return fonts.title;
        case TextStyle::Label: return fonts.bold;
        default: return bold ? fonts.bold : fonts.text;
        }
    }

    float SizeOf(TextStyle style)
    {
        switch (style)
        {
        case TextStyle::Caption: return type::Caption;
        case TextStyle::Label: return type::Label;
        case TextStyle::Title: return type::Title;
        case TextStyle::Display: return type::Display;
        default: return type::Body;
        }
    }

    void PushText(TextStyle style, bool bold) { ImGui::PushFont(FontOf(style, bold), SizeOf(style)); }
    void PopText() { ImGui::PopFont(); }

    ImVec2 TextSize(TextStyle style, const char* text, const char* end, bool bold, float wrap)
    {
        PushText(style, bold);
        ImVec2 size = ImGui::CalcTextSize(text, end, false, wrap);
        PopText();
        return size;
    }

    void Text(TextStyle style, const char* text, unsigned rgb, float wrap, bool bold)
    {
        PushText(style, bold);
        ImGui::PushStyleColor(ImGuiCol_Text, Col(rgb));
        if (wrap >= 0)
            ImGui::PushTextWrapPos(wrap > 0 ? ImGui::GetCursorPosX() + wrap : 0.0f);
        ImGui::TextUnformatted(text, LabelEnd(text));
        if (wrap >= 0)
            ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        PopText();
    }

    void DrawText(ImDrawList* draw, ImVec2 at, ImU32 color, const char* text, const char* end)
    {
        draw->AddText(ImVec2(Snap(at.x), Snap(at.y)), color, text, end != nullptr ? end : LabelEnd(text));
    }

    const char* LabelEnd(const char* label)
    {
        const char* end = std::strstr(label, "##");
        return end != nullptr ? end : label + std::strlen(label);
    }

    std::string Ellipsize(const char* text, float width)
    {
        const char* end = LabelEnd(text);
        if (ImGui::CalcTextSize(text, end).x <= width)
            return std::string(text, end);
        const float dots = ImGui::CalcTextSize("...").x;
        std::string shown(text, end);
        while (!shown.empty() && ImGui::CalcTextSize(shown.c_str()).x + dots > width)
        {
            shown.pop_back();
            while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80) // a whole UTF-8 character
                shown.pop_back();
            if (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0xC0)
                shown.pop_back();
        }
        while (!shown.empty() && shown.back() == ' ')
            shown.pop_back();
        return shown + "...";
    }

    float FitSize(ImFont* font, float size, const char* text, float width)
    {
        ImGui::PushFont(font, size);
        float textW = ImGui::CalcTextSize(text, LabelEnd(text)).x;
        ImGui::PopFont();
        return textW > width ? std::floor(size * width / textW) : size;
    }

    // ---- Colour ----------------------------------------------------------------------------------------------------

    unsigned Mix(unsigned a, unsigned b, float t)
    {
        auto channel = [&](int shift) {
            float x = float(a >> shift & 0xFF), y = float(b >> shift & 0xFF);
            return unsigned(std::lround(x + (y - x) * t)) << shift;
        };
        return channel(16) | channel(8) | channel(0);
    }

    unsigned Lift(unsigned rgb, float amount) { return amount >= 0 ? Mix(rgb, 0xFFFFFF, amount) : Mix(rgb, 0x000000, -amount); }

    float Luminance(unsigned rgb)
    {
        auto linear = [](unsigned c) { float v = float(c) / 255.0f; return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
        return 0.2126f * linear(rgb >> 16 & 0xFF) + 0.7152f * linear(rgb >> 8 & 0xFF) + 0.0722f * linear(rgb & 0xFF);
    }

    float Contrast(unsigned a, unsigned b)
    {
        float la = Luminance(a), lb = Luminance(b);
        return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
    }

    unsigned InkOn(unsigned fill, unsigned dark) { return Contrast(0xFFFFFF, fill) >= 3.0f ? 0xFFFFFF : dark; }

    ImU32 Col(unsigned rgb, float alpha)
    {
        return ImGui::GetColorU32(LauncherTheme::Rgb(rgb, unsigned(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f)));
    }

    float Snap(float v)
    {
        float scale = ImGui::GetIO().DisplayFramebufferScale.y;
        return scale > 0 ? std::round(v * scale) / scale : v;
    }

    // ---- Drawing ---------------------------------------------------------------------------------------------------

    void FocusRing(ImVec2 min, ImVec2 max, float rounding, ImDrawFlags flags)
    {
        // The ring's centre line sits half its width outside the gap, so the gap stays clear of it.
        const float out = focus::Offset + focus::Width / 2;
        ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - out, min.y - out), ImVec2(max.x + out, max.y + out),
            LauncherTheme::Rgb(g_context.accent), std::max(0.0f, rounding + out), flags, focus::Width);
    }

    void FocusRingIf(const State& state, float rounding, ImDrawFlags flags)
    {
        if (state.focused)
            FocusRing(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), rounding, flags);
    }

    void SoftShadow(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, float offsetY, float spread, float alpha)
    {
        constexpr int Layers = 5;
        for (int i = Layers; i >= 1; i--)
        {
            float grow = spread * float(i) / Layers;
            draw->AddRectFilled(ImVec2(min.x - grow, min.y - grow + offsetY), ImVec2(max.x + grow, max.y + grow + offsetY),
                ImGui::GetColorU32(IM_COL32(0, 0, 0, int(alpha * 255 / Layers))), rounding + grow);
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

    bool Hit(const char* id, ImVec2 size)
    {
        ImGui::PushStyleColor(ImGuiCol_NavCursor, 0);
        bool pressed = ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
        ImGui::PopStyleColor();
        return pressed;
    }

    void FocusLastItem(bool show)
    {
        ImGui::SetWindowFocus();
        ImGui::FocusItem();
        if (show)
            ImGui::SetNavCursorVisible(true);
    }

    // ---- Icons -----------------------------------------------------------------------------------------------------

    float Phase(int periodMs)
    {
        if (g_context.reduceMotion || periodMs <= 0)
            return 0;
        g_context.animating = true;
        return float(g_context.now % uint64_t(periodMs)) / float(periodMs);
    }

    void DrawSpinner(ImDrawList* draw, ImVec2 centre, float radius, ImU32 color)
    {
        float start = Phase(motion::Spin) * Tau - Tau / 4;
        draw->PathArcTo(centre, radius, start, start + Tau * 0.72f, 24);
        draw->PathStroke(color, ImDrawFlags_None, std::max(2.0f, radius / 3.5f));
    }

    void DrawIcon(ImDrawList* draw, Icon icon, ImVec2 c, float size, ImU32 color, ImU32 hole)
    {
        const float h = size / 2, w = std::max(1.5f, size / 9);
        auto p = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };
        auto line = [&](float x0, float y0, float x1, float y1) { draw->AddLine(p(x0, y0), p(x1, y1), color, w); };
        auto poly = [&](std::initializer_list<ImVec2> points, bool closed = false) {
            draw->AddPolyline(points.begin(), int(points.size()), color, closed ? ImDrawFlags_Closed : ImDrawFlags_None, w);
        };
        switch (icon)
        {
        case Icon::None: break;
        case Icon::Check: poly({ p(-0.62f, 0.02f), p(-0.2f, 0.45f), p(0.62f, -0.42f) }); break;
        case Icon::Cross: line(-0.5f, -0.5f, 0.5f, 0.5f); line(-0.5f, 0.5f, 0.5f, -0.5f); break;
        case Icon::Plus: line(-0.6f, 0, 0.6f, 0); line(0, -0.6f, 0, 0.6f); break;
        case Icon::Minus: line(-0.6f, 0, 0.6f, 0); break;
        case Icon::ChevronDown: poly({ p(-0.55f, -0.25f), p(0, 0.3f), p(0.55f, -0.25f) }); break;
        case Icon::ChevronUp: poly({ p(-0.55f, 0.25f), p(0, -0.3f), p(0.55f, 0.25f) }); break;
        case Icon::ChevronLeft: poly({ p(0.25f, -0.55f), p(-0.3f, 0), p(0.25f, 0.55f) }); break;
        case Icon::ChevronRight: poly({ p(-0.25f, -0.55f), p(0.3f, 0), p(-0.25f, 0.55f) }); break;
        case Icon::ArrowUp: line(0, 0.7f, 0, -0.6f); poly({ p(-0.45f, -0.15f), p(0, -0.65f), p(0.45f, -0.15f) }); break;
        case Icon::ArrowDown: line(0, -0.7f, 0, 0.6f); poly({ p(-0.45f, 0.15f), p(0, 0.65f), p(0.45f, 0.15f) }); break;
        case Icon::Search:
            draw->AddCircle(p(-0.15f, -0.15f), h * 0.5f, color, 20, w);
            line(0.22f, 0.22f, 0.72f, 0.72f);
            break;
        case Icon::Gear:
        {
            constexpr int Teeth = 8;
            float radius = h * 0.95f, half = radius * 0.2f, inner = radius * 0.7f;
            for (int i = 0; i < Teeth; i++)
            {
                float angle = float(i) * Tau / Teeth, cs = std::cos(angle), sn = std::sin(angle);
                auto at = [&](float along, float across) { return ImVec2(c.x + cs * along - sn * across, c.y + sn * along + cs * across); };
                draw->AddQuadFilled(at(inner, -half), at(radius, -half), at(radius, half), at(inner, half), color);
            }
            if ((hole >> IM_COL32_A_SHIFT & 0xFF) == 0) // nothing known behind it: a ring, the hole left open
                draw->AddCircle(c, radius * 0.55f, color, 32, radius * 0.46f);
            else
            {
                draw->AddCircleFilled(c, radius * 0.78f, color, 32);
                draw->AddCircleFilled(c, radius * 0.32f, hole, 24);
            }
            break;
        }
        case Icon::Info:
            draw->AddCircle(c, h * 0.85f, color, 24, w);
            line(0, -0.05f, 0, 0.42f);
            draw->AddCircleFilled(p(0, -0.36f), w * 0.75f, color);
            break;
        case Icon::Warning:
            poly({ p(0, -0.82f), p(0.86f, 0.7f), p(-0.86f, 0.7f) }, true);
            line(0, -0.28f, 0, 0.2f);
            draw->AddCircleFilled(p(0, 0.44f), w * 0.75f, color);
            break;
        case Icon::Error:
            draw->AddCircle(c, h * 0.85f, color, 24, w);
            line(-0.3f, -0.3f, 0.3f, 0.3f);
            line(-0.3f, 0.3f, 0.3f, -0.3f);
            break;
        case Icon::Download:
            line(0, -0.75f, 0, 0.3f);
            poly({ p(-0.42f, -0.1f), p(0, 0.32f), p(0.42f, -0.1f) });
            line(-0.7f, 0.72f, 0.7f, 0.72f);
            break;
        case Icon::Trash:
            line(-0.72f, -0.5f, 0.72f, -0.5f);
            poly({ p(-0.25f, -0.5f), p(-0.25f, -0.75f), p(0.25f, -0.75f), p(0.25f, -0.5f) });
            poly({ p(-0.55f, -0.5f), p(-0.45f, 0.78f), p(0.45f, 0.78f), p(0.55f, -0.5f) });
            line(-0.15f, -0.2f, -0.15f, 0.48f);
            line(0.15f, -0.2f, 0.15f, 0.48f);
            break;
        case Icon::Folder:
            poly({ p(-0.8f, 0.65f), p(-0.8f, -0.65f), p(-0.25f, -0.65f), p(-0.1f, -0.45f), p(0.8f, -0.45f), p(0.8f, 0.65f) }, true);
            break;
        case Icon::Disc:
            draw->AddCircle(c, h * 0.85f, color, 28, w);
            draw->AddCircle(c, h * 0.22f, color, 16, w);
            break;
        case Icon::Play: draw->AddTriangleFilled(p(-0.5f, -0.68f), p(0.7f, 0), p(-0.5f, 0.68f), color); break;
        case Icon::Hold: // a timer: held for a moment
            draw->AddCircle(p(0, 0.1f), h * 0.72f, color, 24, w);
            line(0, 0.1f, 0, -0.3f);
            line(0, 0.1f, 0.3f, 0.1f);
            line(-0.25f, -0.88f, 0.25f, -0.88f);
            break;
        case Icon::More:
            for (int i = -1; i <= 1; i++)
                draw->AddCircleFilled(p(0.55f * float(i), 0), w * 1.1f, color);
            break;
        case Icon::Star:
        {
            ImVec2 points[10];
            for (int i = 0; i < 10; i++)
            {
                float angle = -Tau / 4 + float(i) * Tau / 10, r = (i % 2 == 0 ? 0.9f : 0.4f) * h;
                points[i] = ImVec2(c.x + std::cos(angle) * r, c.y + std::sin(angle) * r + h * 0.06f);
            }
            draw->AddConcavePolyFilled(points, 10, color);
            break;
        }
        }
    }
}

namespace ui
{
    void Bubble(const char* text, ImVec2 min, ImVec2 max)
    {
        constexpr float MaxWidth = 360;
        const ImVec2 pad(space::S, space::XS);
        ImVec2 size = TextSize(TextStyle::Caption, text, nullptr, false, MaxWidth);
        size = ImVec2(size.x + 2 * pad.x, size.y + 2 * pad.y);
        ImVec2 display = ImGui::GetIO().DisplaySize;
        const float margin = space::XS;
        ImVec2 at(std::clamp(min.x, margin, std::max(margin, display.x - size.x - margin)), max.y + space::XS);
        if (at.y + size.y > display.y - margin)
            at.y = min.y - space::XS - size.y;
        ImGui::SetNextWindowPos(ImVec2(Snap(at.x), Snap(at.y)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, radius::Control);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, elevation::Raised.border);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, LauncherTheme::Rgb(elevation::Raised.fill, elevation::Raised.fillAlpha));
        ImGui::PushStyleColor(ImGuiCol_Border, LauncherTheme::Rgb(color::Hairline));
        char name[24];
        std::snprintf(name, sizeof name, "##bubble%d", g_bubbles++);
        ImGui::Begin(name, nullptr, ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
        PushText(TextStyle::Caption);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + MaxWidth);
        ImGui::PushStyleColor(ImGuiCol_Text, LauncherTheme::Rgb(color::Ink));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        PopText();
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    bool WantsTooltip(const State& state)
    {
        return state.focused || (state.hovered && (state.preview || ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)));
    }
}

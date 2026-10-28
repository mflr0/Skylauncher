// What tells the player something (feedback.h): badges, chips, status, progress, toasts, key caps and the pad legend.
#include "launcher/ui/components/feedback.h"
#include "launcher/i18n.h"
#include "launcher/ui/components/format.h"
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace ui
{
    namespace
    {
        // Light or dark ink, whichever reads better on `fill`.
        unsigned BestInk(unsigned fill) { return Contrast(0xFFFFFF, fill) >= Contrast(color::OnAccent, fill) ? 0xFFFFFF : color::OnAccent; }
        bool VeryDark(unsigned rgb) { return Luminance(rgb) < 0.03f; }
    }

    // ---- Badge -----------------------------------------------------------------------------------------------------

    ImVec2 BadgeSize(const char* text)
    {
        ImVec2 size = TextSize(TextStyle::Caption, text, nullptr, true);
        return ImVec2(std::ceil(size.x + 2 * space::XS), std::ceil(size.y + space::XXS));
    }

    ImVec2 DrawBadge(ImDrawList* draw, ImVec2 at, const char* text, unsigned fill, unsigned ink)
    {
        const ImVec2 size = BadgeSize(text), end(at.x + size.x, at.y + size.y);
        draw->AddRectFilled(at, end, Col(fill), size.y / 2);
        if (VeryDark(fill)) // a very dark badge (Dark) gets a light edge on the dark cards
            draw->AddRect(at, end, Col(color::Dim), size.y / 2);
        PushText(TextStyle::Caption, true);
        DrawText(draw, ImVec2(at.x + space::XS, at.y + space::XXS / 2), Col(ink != 0 ? ink : BestInk(fill)), text);
        PopText();
        return size;
    }

    void Badge(const char* text, unsigned fill, unsigned ink)
    {
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(DrawBadge(ImGui::GetWindowDrawList(), at, text, fill, ink));
    }

    // ---- Chip ------------------------------------------------------------------------------------------------------

    bool Chip(const char* label, const ChipOptions& options)
    {
        const bool sort = options.kind == ChipKind::Sort, on = !sort && options.selected;
        const char* end = LabelEnd(label);
        const float h = height::Small, icon = icon::Small;
        const bool lead = on || sort;
        const float textW = TextSize(TextStyle::Body, label, end, on).x;
        const float w = std::ceil(textW + 2 * space::S + (lead ? icon + space::XS : 0));
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + w, min.y + h);
        ImGui::BeginDisabled(options.disabled);
        const bool pressed = Hit(label, ImVec2(w, h));
        State state = ItemState(options.disabled);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float rounding = sort ? radius::Control : h / 2; // a filter is round, the sort is square-cornered
        const bool lit = state.hovered || state.pressed;
        unsigned ink = color::Ink;
        if (sort)
        {
            draw->AddRectFilled(min, max, Col(lit ? color::InsetHover : color::Inset), rounding);
            draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), Col(color::Edge), rounding, 0, stroke::Hairline);
        }
        else if (on)
        {
            unsigned fill = options.color != 0 ? options.color : Ctx().accent;
            draw->AddRectFilled(min, max, Col(lit ? Lift(fill, 0.12f) : fill), rounding);
            ink = BestInk(fill);
        }
        else
        {
            draw->AddRectFilled(min, max, Col(lit ? color::RaisedHover : color::Raised, 0.92f), rounding);
            draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), Col(color::Hairline), rounding, 0, stroke::Hairline);
        }
        float x = min.x + space::S;
        const float mid = std::round((min.y + max.y) / 2) + (state.pressed ? 1.0f : 0.0f);
        if (lead)
        {
            DrawIcon(draw, sort ? (options.direction < 0 ? Icon::ArrowDown : Icon::ArrowUp) : Icon::Check, ImVec2(x + icon / 2, mid), icon, Col(ink));
            x += icon + space::XS;
        }
        PushText(TextStyle::Body, on);
        DrawText(draw, ImVec2(x, std::round(mid - CapMiddle(TextStyle::Body, on))), Col(ink), label, end);
        PopText();
        ImGui::EndDisabled();
        FocusRingIf(state, rounding);
        return pressed && !options.disabled;
    }

    // ---- Status ----------------------------------------------------------------------------------------------------

    unsigned ColorOf(Status status)
    {
        switch (status)
        {
        case Status::Info:
        case Status::Busy: return color::Info;
        case Status::Success: return color::Success;
        case Status::Warning: return color::Warning;
        case Status::Error: return color::Danger;
        default: return color::Dim;
        }
    }

    Icon IconOf(Status status)
    {
        switch (status)
        {
        case Status::Info: return Icon::Info;
        case Status::Success: return Icon::Success;
        case Status::Warning: return Icon::Warning;
        case Status::Error: return Icon::Error;
        case Status::Busy: return Icon::Loader;
        default: return Icon::Unknown;
        }
    }

    void DrawStatusIcon(ImDrawList* draw, Status status, ImVec2 centre, float size)
    {
        DrawStatusIcon(draw, status, IconOf(status), centre, size);
    }

    void DrawStatusIcon(ImDrawList* draw, Status status, Icon icon, ImVec2 centre, float size)
    {
        if (status == Status::Busy)
            DrawSpinner(draw, centre, size, Col(ColorOf(status)));
        else
            DrawIcon(draw, icon, centre, size, Col(ColorOf(status)));
    }

    void StatusPill(Status status, const char* text, Icon icon)
    {
        // [XS] icon [XXS] text [XS + 2]: the icon's own margin (Lucide's 2 of 24) evens the ends.
        const float iconSize = icon::Small, h = PillHeight;
        const float textW = TextSize(TextStyle::Caption, text, nullptr, true).x;
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + std::ceil(space::XS + iconSize + space::XXS + textW + space::XS + 2), min.y + h);
        ImGui::Dummy(ImVec2(max.x - min.x, h));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const unsigned tone = ColorOf(status);
        const float mid = min.y + h / 2;
        draw->AddRectFilled(min, max, Col(Mix(color::Surface, tone, 0.16f)), h / 2);
        draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), Col(tone, 0.5f), h / 2, 0, stroke::Hairline);
        DrawStatusIcon(draw, status, icon != Icon::None ? icon : IconOf(status), ImVec2(min.x + space::XS + iconSize / 2, mid), iconSize);
        PushText(TextStyle::Caption, true);
        DrawText(draw, ImVec2(min.x + space::XS + iconSize + space::XXS, std::round(mid - CapMiddle(TextStyle::Caption, true))), Col(color::Ink), text);
        PopText();
    }

    // ---- Progress --------------------------------------------------------------------------------------------------

    namespace
    {
        constexpr float BarH = space::XS;

        // The track; returns its rectangle.
        void Track(ImVec2 min, ImVec2 max)
        {
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, Col(color::Dim, 0.35f), BarH / 2); // light: the bar's end shows on dark art
        }
    }

    void ProgressBar(float fraction, float width, bool showPercent)
    {
        const std::string percent = FormatPercent(fraction);
        const float label = showPercent ? TextSize(TextStyle::Caption, FormatPercent(1.0f).c_str()).x + space::S : 0;
        const float w = (width < 0 ? ImGui::GetContentRegionAvail().x : width) - label;
        const float h = showPercent ? type::Caption * 1.3f : BarH;
        const ImVec2 at = ImGui::GetCursorScreenPos(), min(at.x, std::round(at.y + (h - BarH) / 2)), max(at.x + w, min.y + BarH);
        ImGui::Dummy(ImVec2(w + label, h));
        Track(min, max);
        if (fraction > 0) // a whole cap at both ends, even when nearly empty
            ImGui::GetWindowDrawList()->AddRectFilled(min, ImVec2(min.x + std::max(w * std::min(fraction, 1.0f), BarH), max.y), Col(color::AccentFill), BarH / 2);
        if (showPercent)
        {
            PushText(TextStyle::Caption);
            DrawText(ImGui::GetWindowDrawList(), ImVec2(at.x + w + space::S, at.y + (h - ImGui::GetFontSize()) / 2), Col(color::InkSoft), percent.c_str());
            PopText();
        }
    }

    void IndeterminateBar(float width)
    {
        const float w = width < 0 ? ImGui::GetContentRegionAvail().x : width;
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + w, min.y + BarH);
        ImGui::Dummy(ImVec2(w, BarH));
        Track(min, max);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (Ctx().reduceMotion)
        {
            // Still: short dashes along the whole track, which no one reads as a percentage.
            for (float x = min.x; x + BarH < max.x; x += 3 * BarH)
                draw->AddRectFilled(ImVec2(x, min.y), ImVec2(x + 1.5f * BarH, max.y), Col(color::AccentFill, 0.7f), BarH / 2);
            return;
        }
        const float band = w * 0.3f, from = min.x - band + (w + band) * Phase(motion::Spin * 2);
        draw->PushClipRect(min, max, true);
        draw->AddRectFilled(ImVec2(from, min.y), ImVec2(from + band, max.y), Col(color::AccentFill), BarH / 2);
        draw->PopClipRect();
    }

    void Spinner(float size)
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(size, size));
        DrawSpinner(ImGui::GetWindowDrawList(), ImVec2(at.x + size / 2, at.y + size / 2), size, Col(color::Info));
    }

    // ---- Toasts ----------------------------------------------------------------------------------------------------

    void Toasts::Push(std::string text, ToastKind kind)
    {
        // Long enough to read: 2.5 s, more for a long message, up to 6 s. An error stays until it is closed.
        const uint64_t now = SDL_GetTicks();
        const uint64_t readMs = std::clamp<uint64_t>(2500 + 45 * (text.size() > 40 ? text.size() - 40 : 0), 2500, 6000);
        const uint64_t until = kind == ToastKind::Error ? 0 : now + readMs;
        for (Item& item : m_items)
            if (item.text == text) // the same words again: the one already shown, kept longer
            {
                if (kind == ToastKind::Error || item.kind == ToastKind::Error)
                {
                    item.kind = ToastKind::Error;
                    item.until = 0;
                }
                else
                    item.until = until;
                return;
            }
        m_items.push_back({ std::move(text), kind, until, ++m_serial });
        if (m_items.size() > Max)
            m_items.erase(m_items.begin());
    }

    int Toasts::NextWakeMs(uint64_t now) const
    {
        int wait = -1;
        for (const Item& item : m_items)
            if (item.until > now)
                wait = wait < 0 ? int(item.until - now) + 1 : std::min(wait, int(item.until - now) + 1);
        return wait;
    }

    bool Toasts::Draw(float bottomInset, float maxTextWidth)
    {
        const uint64_t now = SDL_GetTicks();
        std::erase_if(m_items, [&](const Item& item) { return item.until != 0 && now > item.until; });
        m_focused = false;
        if (m_items.empty())
            return false;
        // Bottom right, stacked upwards, newest at the bottom; each at most `maxTextWidth` of text (a long one wraps).
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const bool navigating = ImGui::GetIO().NavVisible;
        const float icon = icon::Normal, padX = space::M, padY = space::S, cross = height::Small;
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        float bottom = display.y - space::L - bottomInset;
        int closed = -1;
        bool closedByNav = false;
        // The newest error takes the keyboard and pad focus once, when they are in use.
        const Item* newestError = nullptr;
        for (const Item& item : m_items)
            if (item.kind == ToastKind::Error)
                newestError = &item;
        PushText(TextStyle::Body);
        for (size_t i = m_items.size(); i-- > 0;)
        {
            const Item& item = m_items[i];
            const bool error = item.kind == ToastKind::Error;
            const unsigned tone = error ? color::Danger : item.kind == ToastKind::Success ? color::Success : color::Info;
            const ImVec2 text = ImGui::CalcTextSize(item.text.c_str(), nullptr, false, maxTextWidth);
            const float width = padX + icon + space::S + text.x + padX + (error ? cross : 0.0f);
            const float heightNeeded = std::max(text.y, error ? cross : 0.0f) + 2 * padY;
            // Whole points, so the ring covers whole pixels at 1x and 2x.
            const ImVec2 min(std::round(display.x - space::L - width), std::round(bottom - heightNeeded)), max(std::round(display.x - space::L), std::round(bottom));
            // A window of its own, so a click closes the message and does not reach what is under it.
            ImGui::SetNextWindowPos(min);
            ImGui::SetNextWindowSize(ImVec2(max.x - min.x, max.y - min.y));
            if (error && navigating && &item == newestError && m_focusedSerial != item.serial)
            {
                ImGui::SetNextWindowFocus();
                m_focusedSerial = item.serial;
            }
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
            ImGui::PushStyleColor(ImGuiCol_NavCursor, 0);
            const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoScrollbar | (error ? 0 : ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
            ImGui::Begin(fmt::format("##toast{}", i).c_str(), nullptr, flags);
            if (Hit("##close", ImVec2(max.x - min.x, max.y - min.y)))
            {
                closed = int(i);
                closedByNav = navigating && !ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            }
            const bool hovered = ImGui::IsItemHovered();
            const bool focused = error && ((ImGui::IsWindowFocused() && navigating) || (m_previewFocus && &item == newestError));
            if (focused)
            {
                m_focused = true;
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
                {
                    closed = int(i);
                    closedByNav = true;
                }
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);

            draw->AddRectFilled(min, max, LauncherTheme::Rgb(elevation::Raised.fill, elevation::Raised.fillAlpha), radius::Card);
            // AddRect puts the line's centre half a point in: a 2-point ring then runs between whole points only from 1 point in.
            draw->AddRect(ImVec2(min.x + 1, min.y + 1), ImVec2(max.x - 1, max.y - 1), LauncherTheme::Rgb(tone, 200), radius::Card - 1, 0, 2.0f);
            const float mid = (min.y + max.y) / 2, textTop = std::round(mid - text.y / 2);
            DrawIcon(draw, error ? Icon::Error : item.kind == ToastKind::Success ? Icon::Check : Icon::Info,
                ImVec2(min.x + padX + icon / 2, std::round(textTop + ImGui::GetFontSize() / 2)), icon, LauncherTheme::Rgb(tone));
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + padX + icon + space::S, textTop),
                LauncherTheme::Rgb(color::Ink), item.text.c_str(), nullptr, maxTextWidth);
            if (error) // the cross, or with the keyboard / pad focus the key that closes it
            {
                const ImVec2 centre(max.x - padX / 2 - cross / 2, mid);
                if (focused)
                    DrawKeyCap(draw, ImVec2(centre.x - 12, centre.y), PadButton::South, Ctx().glyphs == Glyphs::Keyboard ? Glyphs::Xbox : Ctx().glyphs);
                else
                    DrawIcon(draw, Icon::Cross, centre, icon::Small, LauncherTheme::Rgb(hovered ? color::Ink : color::Dim));
            }
            if (focused)
            {
                const float out = focus::Offset + focus::Width / 2;
                draw->AddRect(ImVec2(min.x - out, min.y - out), ImVec2(max.x + out, max.y + out), LauncherTheme::Rgb(Ctx().accent),
                    radius::Card + out, 0, focus::Width);
            }
            bottom = min.y - space::XS;
        }
        PopText();
        if (closed >= 0)
            m_items.erase(m_items.begin() + closed);
        return closedByNav;
    }

    // ---- Key caps and the pad legend -------------------------------------------------------------------------------

    const char* PadButtonLabel(PadButton button, Glyphs glyphs)
    {
        static const char* const Xbox[] = { "A", "B", "X", "Y", "LB", "RB", "Menu", "View" };
        static const char* const PlayStation[] = { "Cross", "Circle", "Square", "Triangle", "L1", "R1", "Options", "Share" };
        static const char* const Nintendo[] = { "B", "A", "Y", "X", "L", "R", "+", "-" };
        const int i = int(button);
        switch (glyphs)
        {
        case Glyphs::PlayStation: return PlayStation[i];
        case Glyphs::Nintendo: return Nintendo[i];
        case Glyphs::Xbox: return Xbox[i];
        default: return "";
        }
    }

    namespace
    {
        constexpr float CapH = type::Caption + space::XS + 2; // a key cap's height

        float CapWidth(const char* text, bool round)
        {
            if (round)
                return CapH;
            return std::max(CapH, std::ceil(TextSize(TextStyle::Caption, text, nullptr, true).x + 2 * space::XS));
        }

        ImVec2 DrawCapBox(ImDrawList* draw, ImVec2 at, const char* text, bool round)
        {
            PushText(TextStyle::Caption, true);
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            const float w = CapWidth(text, round);
            const ImVec2 min(at.x, std::round(at.y - CapH / 2)), max(min.x + w, min.y + CapH);
            const float rounding = round ? CapH / 2 : radius::Control * 0.6f;
            draw->AddRectFilled(ImVec2(min.x, min.y + 2), ImVec2(max.x, max.y + 2), Col(0x000000, 0.35f), rounding); // its depth
            draw->AddRectFilled(min, max, Col(color::Raised), rounding);
            draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), Col(color::Edge), rounding, 0, stroke::Hairline);
            if (text[0] != 0)
                DrawText(draw, ImVec2(std::round((min.x + max.x - textSize.x) / 2), std::round((min.y + max.y - textSize.y) / 2)), Col(color::Ink), text);
            PopText();
            return ImVec2(w, CapH);
        }
    }

    ImVec2 DrawKeyCap(ImDrawList* draw, ImVec2 at, PadButton button, Glyphs glyphs)
    {
        const bool face = button <= PadButton::North;
        if (!face || glyphs != Glyphs::PlayStation)
            return DrawCapBox(draw, at, PadButtonLabel(button, glyphs), face);
        // PlayStation's face buttons are shapes: Lucide's, in the cap.
        ImVec2 size = DrawCapBox(draw, at, "", true);
        static const Icon Shapes[] = { Icon::Cross, Icon::Circle, Icon::Square, Icon::Triangle };
        DrawIcon(draw, Shapes[int(button)], ImVec2(at.x + CapH / 2, std::round(at.y - CapH / 2) + CapH / 2), icon::Small, Col(color::Ink));
        return size;
    }

    ImVec2 DrawKeyCap(ImDrawList* draw, ImVec2 at, const char* key) { return DrawCapBox(draw, at, key, false); }

    float KeyCapWidth(PadButton button, Glyphs glyphs) { return CapWidth(PadButtonLabel(button, glyphs), button <= PadButton::North); }
    float KeyCapWidth(const char* key) { return CapWidth(key, false); }
    float KeyCapHeight() { return CapH; }

    void KeyCap(PadButton button, Glyphs glyphs)
    {
        SettingControl(KeyCapWidth(button, glyphs));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(DrawKeyCap(ImGui::GetWindowDrawList(), ImVec2(at.x, at.y + CapH / 2), button, glyphs));
    }

    void KeyCap(const char* key)
    {
        SettingControl(KeyCapWidth(key));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(DrawKeyCap(ImGui::GetWindowDrawList(), ImVec2(at.x, at.y + CapH / 2), key));
    }

    void PadLegend(std::span<const PadHint> hints, Glyphs glyphs, float width)
    {
        const float h = height::Normal, w = width < 0 ? ImGui::GetContentRegionAvail().x : width;
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + w, min.y + h);
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(min, max, Col(color::Surface, 0.92f));
        draw->AddLine(ImVec2(min.x, min.y + 0.5f), ImVec2(max.x, min.y + 0.5f), Col(color::Hairline), stroke::Hairline);
        // Right-aligned, in the order given: measured first.
        PushText(TextStyle::Body);
        const bool keyboard = glyphs == Glyphs::Keyboard;
        float total = 0;
        for (const PadHint& hint : hints)
        {
            if (keyboard && hint.key == nullptr)
                continue;
            float cap = keyboard ? KeyCapWidth(hint.key) : KeyCapWidth(hint.button, glyphs);
            total += cap + space::XS + ImGui::CalcTextSize(hint.label).x + space::L;
        }
        float x = max.x - space::M - std::max(0.0f, total - space::L);
        const float mid = std::round((min.y + max.y) / 2);
        for (const PadHint& hint : hints)
        {
            if (keyboard && hint.key == nullptr)
                continue;
            float cap = keyboard ? DrawKeyCap(draw, ImVec2(x, mid), hint.key).x : DrawKeyCap(draw, ImVec2(x, mid), hint.button, glyphs).x;
            x += cap + space::XS;
            DrawText(draw, ImVec2(x, mid - ImGui::GetFontSize() / 2), Col(color::InkSoft), hint.label);
            x += ImGui::CalcTextSize(hint.label).x + space::L;
        }
        PopText();
    }
}

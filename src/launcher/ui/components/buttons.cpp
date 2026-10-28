// Buttons (buttons.h): Button, Pill, IconButton, MainAction.
#include "launcher/ui/components/buttons.h"
#include "launcher/i18n.h"
#include "launcher/ui/components/format.h"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <string>

namespace ui
{
    namespace
    {
        // How a button of `kind` looks in `state`: its fill (alpha 0: none), its edge and its ink.
        struct Paint
        {
            unsigned fill = 0;
            float fillAlpha = 1;
            unsigned edge = 0;
            float edgeWidth = 0;
            unsigned ink = color::Ink;
        };

        Paint PaintFor(ButtonKind kind, const State& state, bool overArt, bool selected = false)
        {
            Paint paint;
            const bool down = state.pressed, over = state.hovered;
            if (selected || kind == ButtonKind::Primary)
            {
                paint.fill = down ? color::AccentFillPressed : over ? color::AccentFillHover : color::AccentFill;
                paint.ink = color::OnAccent;
                return paint;
            }
            switch (kind)
            {
            case ButtonKind::Danger:
                paint.fill = down ? color::DangerFillPressed : over ? color::DangerFillHover : color::DangerFill;
                paint.edge = color::Danger;
                paint.edgeWidth = 1.5f;
                break;
            case ButtonKind::Ghost:
                paint.fill = overArt ? (down ? color::OverArtPressed : color::OverArtHover) : (down ? color::RaisedPressed : color::RaisedHover);
                paint.fillAlpha = down || over ? 1.0f : 0.0f;
                paint.ink = down || over ? color::Ink : color::InkSoft;
                break;
            default:
                if (overArt)
                    paint.fill = down ? color::OverArtPressed : over ? color::OverArtHover : color::OverArt;
                else
                    paint.fill = down ? color::RaisedPressed : over ? color::RaisedHover : color::Raised;
                paint.edge = overArt ? color::OverArtEdge : color::Hairline;
                paint.edgeWidth = 1;
                break;
            }
            return paint;
        }

        void DrawFace(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, const Paint& paint)
        {
            if (paint.fillAlpha > 0)
                draw->AddRectFilled(min, max, Col(paint.fill, paint.fillAlpha), rounding);
            if (paint.edgeWidth > 0) // inside the face: the half-point offset keeps a 1-point line on whole pixels
                draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), Col(paint.edge), rounding, 0, paint.edgeWidth);
        }

        constexpr float IconSize = space::M;   // an icon beside a label
        constexpr float TrailingSize = 10;     // the chevron after a label

        // The content of a button: [icon] label [trailing], centred in min..max, one point lower when pressed.
        void DrawContent(ImDrawList* draw, ImVec2 min, ImVec2 max, const char* label, TextStyle style, Icon icon, bool spinner,
            Icon trailing, unsigned ink, bool pressed)
        {
            const char* end = LabelEnd(label);
            PushText(style, true);
            ImVec2 text = ImGui::CalcTextSize(label, end);
            bool hasText = end != label, lead = icon != Icon::None || spinner;
            float width = text.x + (lead ? IconSize + (hasText ? space::XS : 0) : 0) + (trailing != Icon::None ? TrailingSize + space::XS : 0);
            float x = std::round((min.x + max.x - width) / 2), mid = std::round((min.y + max.y) / 2) + (pressed ? 1.0f : 0.0f);
            if (spinner)
                DrawSpinner(draw, ImVec2(x + IconSize / 2, mid), IconSize * 0.42f, Col(ink));
            else if (icon != Icon::None)
                DrawIcon(draw, icon, ImVec2(x + IconSize / 2, mid), IconSize, Col(ink));
            if (lead)
                x += IconSize + (hasText ? space::XS : 0);
            if (hasText)
                DrawText(draw, ImVec2(x, mid - text.y / 2), Col(ink), label, end);
            if (trailing != Icon::None)
                DrawIcon(draw, trailing, ImVec2(x + text.x + space::XS + TrailingSize / 2, mid), TrailingSize, Col(ink));
            PopText();
        }

        TextStyle LabelStyle(Size size) { return size == Size::Large ? TextStyle::Label : TextStyle::Body; }
        float SidePad(Size size) { return size == Size::Small ? space::M : space::L; }
    }

    float HeightOf(Size size) { return size == Size::Small ? height::Small : size == Size::Large ? height::Large : height::Normal; }

    float ButtonWidth(const char* label, const ButtonOptions& options)
    {
        if (options.width < 0)
            return ImGui::GetContentRegionAvail().x;
        const char* end = LabelEnd(label);
        float text = TextSize(LabelStyle(options.size), label, end, true).x;
        bool lead = options.icon != Icon::None || options.loading || options.hold;
        float content = text + (lead ? IconSize + (end != label ? space::XS : 0) : 0) + (options.trailing != Icon::None ? TrailingSize + space::XS : 0);
        return std::max(options.width, std::ceil(content + 2 * SidePad(options.size)));
    }

    bool Button(const char* label, const ButtonOptions& options)
    {
        Context& context = Ctx();
        const float h = HeightOf(options.size), w = ButtonWidth(label, options), rounding = radius::Control;
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + w, min.y + h);
        ImGui::BeginDisabled(options.disabled);
        bool clicked = Hit(label, ImVec2(w, h));
        State state = ItemState(options.disabled, options.loading);
        const ImGuiID id = ImGui::GetItemID();

        // Hold to confirm: held (pressed and, for the mouse, still over it) for HoldConfirm::HoldMs. Space, Enter and the
        // pad's A hold it too; leaving or releasing early cancels and shows the hint.
        float fill = 0;
        bool done = false;
        if (options.hold && !options.disabled && !options.loading)
        {
            bool mouse = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            bool held = ImGui::IsItemActive() && (!mouse || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
            bool inputDown = mouse || ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_Enter) ||
                ImGui::IsKeyDown(ImGuiKey_KeypadEnter) || ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown);
            if (context.testHeld != 0 && id == context.testHeld)
                held = inputDown = true;
            fill = context.hold.Update(id, held, inputDown, context.now, done);
            ImGuiStorage* storage = ImGui::GetStateStorage();
            const ImGuiID firedKey = id ^ 0x484F4C44; // "HOLD"
            if (done)
                storage->SetBool(firedKey, true);
            if (held)
                context.animating = true;
            if (ImGui::IsItemDeactivated())
            {
                if (!storage->GetBool(firedKey)) // let go too soon: say how it works
                {
                    context.holdHintId = id;
                    context.holdHintUntil = context.now + motion::HoldHint;
                }
                storage->SetBool(firedKey, false);
            }
            state.pressed = state.pressed || held;
        }
        if (state.holdFill >= 0)
            fill = state.holdFill;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        Paint paint = PaintFor(options.kind, state, options.overArt);
        DrawFace(draw, min, max, rounding, paint);
        if (fill > 0) // the hold's sweep, rounded at its leading end like the face (at least two radii wide)
        {
            unsigned sweep = options.kind == ButtonKind::Danger ? color::Danger : options.kind == ButtonKind::Primary ? color::AccentFillPressed : context.accent;
            draw->AddRectFilled(min, ImVec2(min.x + std::max(w * std::min(fill, 1.0f), 2 * rounding), max.y), Col(sweep, 0.55f), rounding);
        }
        Icon icon = options.hold ? Icon::Hold : options.icon;
        DrawContent(draw, min, max, label, LabelStyle(options.size), icon, options.loading, options.trailing, paint.ink, state.pressed);
        ImGui::EndDisabled();
        FocusRingIf(state, rounding);

        const char* hint = options.tooltip;
        if (options.hold && hint == nullptr)
            hint = Tr("Hold to confirm");
        bool tapped = options.hold && context.holdHintId == id && context.now < context.holdHintUntil;
        if (hint != nullptr && (WantsTooltip(state) || tapped) && fill <= 0)
            Bubble(hint, min, max);
        if (options.disabled || options.loading)
            return false;
        return options.hold ? done : clicked;
    }

    bool Pill(const char* label, const PillOptions& options)
    {
        const float h = height::Small;
        const char* end = LabelEnd(label);
        float text = TextSize(TextStyle::Body, label, end, true).x;
        float content = text + (options.icon != Icon::None ? IconSize + space::XS : 0) + (options.trailing != Icon::None ? TrailingSize + space::XS : 0);
        float w = std::max(options.width, std::ceil(content + 2 * space::M));
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + w, min.y + h);
        ImGui::BeginDisabled(options.disabled);
        bool clicked = Hit(label, ImVec2(w, h));
        State state = ItemState(options.disabled);
        Paint paint = PaintFor(ButtonKind::Secondary, state, options.overArt, options.selected);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        DrawFace(draw, min, max, h / 2, paint);
        DrawContent(draw, min, max, label, TextStyle::Body, options.icon, false, options.trailing, paint.ink, state.pressed);
        ImGui::EndDisabled();
        FocusRingIf(state, h / 2);
        if (options.tooltip != nullptr && WantsTooltip(state))
            Bubble(options.tooltip, min, max);
        return clicked && !options.disabled;
    }

    bool IconButton(const char* id, Icon icon, const char* tooltip, const IconButtonOptions& options)
    {
        const float h = HeightOf(options.size);
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + h, min.y + h);
        ImGui::BeginDisabled(options.disabled);
        bool clicked = Hit(id, ImVec2(h, h));
        State state = ItemState(options.disabled);
        Paint paint = PaintFor(options.kind, state, options.overArt, options.selected);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        DrawFace(draw, min, max, h / 2, paint);
        ImVec2 centre(std::round((min.x + max.x) / 2), std::round((min.y + max.y) / 2) + (state.pressed ? 1.0f : 0.0f));
        const float mark = std::round(h * 0.45f);
        if (options.image.GetTexID() != ImTextureID_Invalid)
            draw->AddImage(options.image, ImVec2(centre.x - mark / 2, centre.y - mark / 2), ImVec2(centre.x + mark / 2, centre.y + mark / 2),
                ImVec2(0, 0), ImVec2(1, 1), Col(paint.ink));
        else
            DrawIcon(draw, icon, centre, mark, Col(paint.ink), Col(paint.fill, paint.fillAlpha > 0 ? 1.0f : 0.0f));
        ImGui::EndDisabled();
        FocusRingIf(state, h / 2);
        if (tooltip != nullptr && WantsTooltip(state))
            Bubble(tooltip, min, max);
        return clicked && !options.disabled;
    }

    // ---- MainAction ------------------------------------------------------------------------------------------------

    namespace
    {
        // One half of the main action as drawn.
        struct Look
        {
            bool hovered = false, held = false, enabled = true, busy = false;
        };

        // A fill in its state: lighter under the pointer (or the focus), darker pressed, greyed when disabled, dark and
        // tinted when busy.
        unsigned Shade(unsigned rgb, const Look& look)
        {
            if (look.busy)
                return Mix(rgb, color::OverArt, 0.78f);
            if (!look.enabled)
                return Mix(rgb, 0x4A4A54, 0.7f);
            return look.held ? Lift(rgb, -0.1f) : look.hovered ? Lift(rgb, 0.1f) : rgb;
        }

        // A busy button's progress on its face: the done part tinted in the game's colour, and a bar along the bottom edge,
        // both following the face's corners. Indeterminate: a band sweeping along that bar.
        void DrawProgress(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImDrawFlags flags, unsigned rgb, float progress, bool indeterminate)
        {
            constexpr float BarH = 5;
            draw->PushClipRect(ImVec2(min.x, max.y - BarH), max, true);
            draw->AddRectFilled(min, max, Col(0xFFFFFF, 0.11f), rounding, flags);
            draw->PopClipRect();
            float from = min.x, to = Snap(min.x + (max.x - min.x) * std::clamp(progress, 0.0f, 1.0f));
            if (indeterminate)
            {
                float t = Phase(motion::Spin * 2), band = (max.x - min.x) * 0.3f;
                from = min.x - band + (max.x - min.x + band) * t;
                to = from + band;
            }
            else
            {
                draw->PushClipRect(min, ImVec2(to, max.y), true);
                draw->AddRectFilled(min, max, Col(rgb, 0.24f), rounding, flags);
                draw->PopClipRect();
            }
            draw->PushClipRect(ImVec2(std::max(from, min.x), max.y - BarH), ImVec2(std::min(to, max.x), max.y), true);
            draw->AddRectFilled(min, max, Col(rgb), rounding, flags);
            draw->PopClipRect();
        }

        // The label in the current font, centred in min..max on its capitals, on whole pixels.
        void DrawCapsLabel(ImDrawList* draw, ImVec2 min, ImVec2 max, const char* text, ImU32 ink)
        {
            const ImFontGlyph* cap = ImGui::GetFontBaked()->FindGlyph('H');
            float width = ImGui::CalcTextSize(text).x;
            draw->AddText(ImVec2(Snap((min.x + max.x - width) / 2), Snap((min.y + max.y) / 2 - (cap->Y0 + cap->Y1) / 2)), ink, text);
        }
    }

    MainActionResult MainAction(const MainActionOptions& options)
    {
        static constexpr LauncherTheme::GameStyle Launcher = { LauncherTheme::AccentHover, LauncherTheme::AccentDark, LauncherTheme::AccentInk, LauncherTheme::Accent };
        const LauncherTheme::GameStyle& style = options.style != nullptr ? *options.style : Launcher;
        const float rounding = radius::Card;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        MainActionResult result;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 mainMin = at, mainMax(at.x + height::MainWidth, at.y + height::Main);
        const ImVec2 gearMin(mainMax.x, at.y), gearMax(mainMax.x + height::MainGear, mainMax.y);
        result.mainCentre = ImVec2((mainMin.x + mainMax.x) / 2, (mainMin.y + mainMax.y) / 2);
        result.gearCentre = ImVec2((gearMin.x + gearMax.x) / 2, (gearMin.y + gearMax.y) / 2);
        const unsigned top = Mix(style.top, style.bottom, 0.2f), bottom = Mix(style.top, style.bottom, 0.58f), middle = Mix(top, bottom, 0.5f);
        const bool busy = options.progress >= 0 || options.indeterminate;

        // The label: busy, its percentage instead of the dots.
        std::string label = options.label;
        if (options.progress >= 0 && !options.indeterminate)
        {
            if (label.ends_with("..."))
                label.resize(label.size() - 3);
            label += " " + FormatPercent(options.progress);
        }

        // The main half (an id that keeps the focus when the label changes); the focused half is lit as under the pointer.
        ImGui::PushStyleColor(ImGuiCol_NavCursor, 0); // the ring below instead of ImGui's
        ImGui::BeginDisabled(!options.enabled);
        if (Hit("##main", ImVec2(height::MainWidth, height::Main)) && options.enabled)
            result.hit = MainActionHit::Main;
        ImGui::EndDisabled();
        State mainState = ItemState(!options.enabled);
        if (options.takeFocus)
        {
            FocusLastItem(options.showFocus);
            mainState.focused = mainState.focused || options.showFocus;
        }
        Look main;
        main.enabled = options.enabled;
        main.busy = busy;
        main.hovered = options.enabled && (mainState.hovered || mainState.focused);
        main.held = options.enabled && mainState.pressed;

        SoftShadow(draw, ImVec2(mainMin.x, mainMin.y + 2), gearMax, rounding, 2, 6, 0.35f);
        GradientRect(draw, mainMin, mainMax, Col(Shade(top, main)), Col(Shade(bottom, main)), rounding, ImDrawFlags_RoundCornersLeft);
        if (busy)
            DrawProgress(draw, mainMin, mainMax, rounding, ImDrawFlags_RoundCornersLeft, middle, options.progress, options.indeterminate);
        draw->AddLine(ImVec2(mainMin.x + rounding, mainMin.y + 0.5f), ImVec2(mainMax.x, mainMin.y + 0.5f), Col(0xFFFFFF, main.enabled ? 0.27f : 0.1f));
        // The idle face's ink under the pointer and pressed (it never flips); the greyed or busy face's own otherwise.
        unsigned ink = busy || !main.enabled ? InkOn(Shade(middle, main), style.outline) : InkOn(middle, style.outline);
        const float labelW = height::MainWidth - 2 * (space::M + space::XXS);
        ImGui::PushFont(Ctx().fonts.title, FitSize(Ctx().fonts.title, type::Title, label.c_str(), labelW));
        DrawCapsLabel(draw, mainMin, mainMax, label.c_str(), Col(ink, main.enabled || busy ? 1.0f : 0.6f));
        ImGui::PopFont();

        // The settings half: greyed with the main half, lit again when it is the one pointed at.
        ImGui::SetCursorScreenPos(gearMin);
        if (Hit("##gamesettings", ImVec2(height::MainGear, height::Main)))
            result.hit = MainActionHit::Settings;
        State gearState = ItemState();
        Look gear;
        gear.hovered = gearState.hovered || gearState.focused;
        gear.held = gearState.pressed;
        gear.enabled = options.enabled || gear.hovered || gear.held;
        GradientRect(draw, gearMin, gearMax, Col(Shade(top, gear)), Col(Shade(bottom, gear)), rounding, ImDrawFlags_RoundCornersRight);
        draw->AddLine(ImVec2(gearMin.x, gearMin.y + 0.5f), ImVec2(gearMax.x - rounding, gearMin.y + 0.5f), Col(0xFFFFFF, 0.27f));
        // The split: a dark hairline and a light one beside it, from edge to edge.
        draw->AddLine(ImVec2(gearMin.x - 0.5f, gearMin.y + 1), ImVec2(gearMin.x - 0.5f, gearMax.y - 1), Col(style.outline, 0.43f));
        draw->AddLine(ImVec2(gearMin.x + 0.5f, gearMin.y + 1), ImVec2(gearMin.x + 0.5f, gearMax.y - 1), Col(0xFFFFFF, 0.18f));
        DrawIcon(draw, Icon::Gear, result.gearCentre, height::Main * 0.46f, Col(InkOn(middle, style.outline), gear.enabled ? 1.0f : 0.6f),
            Col(Shade(middle, gear)));
        ImGui::PopStyleColor();
        if (options.gearTooltip != nullptr && WantsTooltip(gearState))
            Bubble(options.gearTooltip, gearMin, gearMax);

        // One ring around the whole button for the keyboard and pad focus, in the game's colour.
        if (mainState.focused || gearState.focused)
            FocusRing(mainMin, gearMax, rounding);
        return result;
    }
}

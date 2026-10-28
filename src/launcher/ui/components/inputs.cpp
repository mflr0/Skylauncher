// Controls that choose or enter a value (inputs.h).
#include "launcher/ui/components/inputs.h"
#include "launcher/ui/components/buttons.h"
#include "launcher/ui/components/feedback.h"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace ui
{
    namespace
    {
        // A value easing towards `target` (0 or 1) over motion::Fast, kept per item; at once with reduced motion.
        float Ease(ImGuiID id, float target)
        {
            ImGuiStorage* storage = ImGui::GetStateStorage();
            float* value = storage->GetFloatRef(id, target);
            if (Ctx().reduceMotion)
                return *value = target;
            float step = ImGui::GetIO().DeltaTime * 1000.0f / motion::Fast;
            *value = *value < target ? std::min(target, *value + step) : std::max(target, *value - step);
            if (*value != target)
                Ctx().animating = true;
            return *value;
        }

        // The frame of a field or a select: height::Normal, the inset grey, its edge; ImGui's own focus cursor off (ours).
        constexpr int FieldVars = 4, FieldColors = 6;
        void PushField(float padLeft = space::M)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padLeft, (height::Normal - type::Body) / 2));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, radius::Control);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, stroke::Hairline);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, radius::Control);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, LauncherTheme::Rgb(color::Inset));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, LauncherTheme::Rgb(color::InsetHover));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, LauncherTheme::Rgb(color::InsetActive));
            ImGui::PushStyleColor(ImGuiCol_Border, LauncherTheme::Rgb(color::Hairline));
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, LauncherTheme::Rgb(color::Dim)); // the hint
            ImGui::PushStyleColor(ImGuiCol_NavCursor, 0);
            PushText(TextStyle::Body);
        }
        void PopField()
        {
            PopText();
            ImGui::PopStyleColor(FieldColors);
            ImGui::PopStyleVar(FieldVars);
        }

        // A mark (checkbox, radio) and its label after it, as one item height::Small high.
        struct Marked
        {
            ImVec2 min, max, mark; // the item, the mark's top left
            bool pressed = false;
            State state;
        };
        Marked MarkItem(const char* label, bool disabled)
        {
            Marked item;
            const char* end = LabelEnd(label);
            float text = end != label ? TextSize(TextStyle::Body, label, end).x + space::S : 0;
            item.min = ImGui::GetCursorScreenPos();
            item.max = ImVec2(item.min.x + height::Mark + text, item.min.y + height::Small);
            item.mark = ImVec2(item.min.x, std::round(item.min.y + (height::Small - height::Mark) / 2));
            item.pressed = Hit(label, ImVec2(item.max.x - item.min.x, height::Small));
            item.state = ItemState(disabled);
            if (end != label)
            {
                PushText(TextStyle::Body);
                DrawText(ImGui::GetWindowDrawList(), ImVec2(item.min.x + height::Mark + space::S, item.min.y + (height::Small - ImGui::GetFontSize()) / 2),
                    Col(color::Ink), label, end);
                PopText();
            }
            return item;
        }
    }

    TabChange Tabs(const char* id, std::span<const char* const> labels, int* current, const TabsOptions& options)
    {
        TabChange change = TabChange::None;
        const int count = int(labels.size());
        if (options.bumpers && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) && !ImGui::IsAnyItemActive())
        {
            int step = ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) ? -1 : ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) ? 1 : 0;
            if (step != 0 && std::clamp(*current + step, 0, count - 1) != *current)
            {
                *current = std::clamp(*current + step, 0, count - 1);
                change = TabChange::Bumper;
            }
        }
        ImGui::PushID(id);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const bool caps = options.bumpers && Ctx().glyphs != Glyphs::Keyboard;
        const float rowH = height::Normal;
        ImVec2 start = ImGui::GetCursorScreenPos();
        if (caps)
        {
            ImVec2 cap = DrawKeyCap(draw, ImVec2(start.x, start.y + (rowH - stroke::Selection) / 2), PadButton::LeftBumper, Ctx().glyphs);
            ImGui::SetCursorScreenPos(ImVec2(start.x + cap.x + space::M, start.y));
        }
        PushText(TextStyle::Label);
        for (int i = 0; i < count; i++)
        {
            const char* label = labels[i];
            const bool active = *current == i;
            ImVec2 at = ImGui::GetCursorScreenPos(), text = ImGui::CalcTextSize(label, LabelEnd(label));
            if (Hit(label, ImVec2(text.x, rowH)) && *current != i)
            {
                *current = i;
                change = TabChange::Click;
            }
            State state = ItemState();
            if (active && options.focusSelected)
            {
                FocusLastItem(options.showFocus);
                state.focused = state.focused || options.showFocus;
            }
            unsigned ink = active || state.hovered || state.pressed ? color::Ink : color::Dim;
            ImVec2 textAt(at.x, at.y + (rowH - stroke::Selection - text.y) / 2);
            DrawText(draw, textAt, Col(ink), label);
            if (active)
                draw->AddRectFilled(ImVec2(at.x, at.y + rowH - stroke::Selection), ImVec2(at.x + text.x, at.y + rowH), Col(color::AccentFillPressed));
            else if (state.hovered)
                draw->AddRectFilled(ImVec2(at.x, at.y + rowH - 1), ImVec2(at.x + text.x, at.y + rowH), Col(color::Hairline));
            if (state.focused) // the label's row with S of air at its sides: the ring is not drawn on the letters
                FocusRing(ImVec2(at.x - space::S, at.y), ImVec2(at.x + text.x + space::S, at.y + rowH), radius::Control);
            ImGui::SameLine(0, space::XL);
        }
        PopText();
        if (caps)
        {
            ImVec2 at = ImGui::GetCursorScreenPos();
            DrawKeyCap(draw, ImVec2(at.x - space::XL + space::M, at.y + (rowH - stroke::Selection) / 2), PadButton::RightBumper, Ctx().glyphs);
        }
        ImGui::NewLine();
        ImGui::PopID();
        return change;
    }

    bool Segmented(const char* id, std::span<const char* const> labels, int* current, float width, bool disabled)
    {
        const int count = int(labels.size());
        if (count == 0)
            return false;
        float widest = 0;
        for (const char* label : labels)
            widest = std::max(widest, TextSize(TextStyle::Body, label, nullptr, true).x);
        const float h = height::Normal, inset = space::XXS;
        float segment = width > 0 ? std::floor((width - 2 * inset) / float(count)) : std::ceil(widest + 2 * space::M);
        ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + segment * float(count) + 2 * inset, min.y + h);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImGui::PushID(id);
        ImGui::BeginDisabled(disabled);
        draw->AddRectFilled(min, max, Col(color::Inset), radius::Control);
        draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), Col(color::Hairline), radius::Control);
        bool changed = false;
        PushText(TextStyle::Body, true);
        for (int i = 0; i < count; i++)
        {
            ImVec2 a(min.x + inset + segment * float(i), min.y + inset), b(a.x + segment, max.y - inset);
            ImGui::SetCursorScreenPos(a);
            if (Hit(labels[i], ImVec2(segment, b.y - a.y)) && *current != i)
            {
                *current = i;
                changed = true;
            }
            State state = ItemState(disabled);
            const bool on = *current == i;
            if (on)
                draw->AddRectFilled(a, b, Col(state.pressed ? color::AccentFillPressed : state.hovered ? color::AccentFillHover : color::AccentFill), radius::Control - inset);
            else if (state.hovered || state.pressed)
                draw->AddRectFilled(a, b, Col(state.pressed ? color::InsetActive : color::InsetHover), radius::Control - inset);
            const std::string shown = Ellipsize(labels[i], segment - 2 * space::XS);
            ImVec2 text = ImGui::CalcTextSize(shown.c_str());
            DrawText(draw, ImVec2(std::round((a.x + b.x - text.x) / 2), std::round((a.y + b.y - text.y) / 2)), Col(on ? color::OnAccent : color::InkSoft), shown.c_str());
            FocusRingIf(state, radius::Control - inset);
        }
        PopText();
        ImGui::EndDisabled();
        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(min.x, max.y));
        ImGui::Dummy(ImVec2(max.x - min.x, 0));
        return changed;
    }

    // ---- Select ----------------------------------------------------------------------------------------------------

    namespace
    {
        constexpr int SelectVars = 3, SelectColors = 2; // pushed for the list while it is open
    }

    bool BeginSelect(const char* id, const char* preview, const SelectOptions& options)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList(); // the box's window, not the list's
        const float width = options.width < 0 ? ImGui::GetContentRegionAvail().x : options.width;
        PushField();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(space::XS, space::XS));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, elevation::Raised.border);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, LauncherTheme::Rgb(elevation::Raised.fill, elevation::Raised.fillAlpha));
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + width, min.y + height::Normal);
        ImGui::SetNextItemWidth(width);
        if (options.disabled)
            ImGui::BeginDisabled();
        const bool open = ImGui::BeginCombo(id, preview, ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLarge);
        if (options.disabled)
            ImGui::EndDisabled();
        if (!open)
        {
            State state = ItemState(options.disabled);
            if (options.disabled)
                ImGui::BeginDisabled();
            DrawIcon(draw, Icon::ChevronDown, ImVec2(max.x - space::S - icon::Small / 2, (min.y + max.y) / 2), icon::Small, Col(color::InkSoft));
            if (options.disabled)
                ImGui::EndDisabled();
            FocusRingIf(state, radius::Control);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            PopField();
            return false;
        }
        draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), Col(Ctx().accent), radius::Control, 0, stroke::Edge);
        DrawIcon(draw, Icon::ChevronUp, ImVec2(max.x - space::S - icon::Small / 2, (min.y + max.y) / 2), icon::Small, Col(color::Ink));
        // The list's rows: height::Small, text centred on them, a little air between them.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, space::XXS / 2));
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(space::S, 0));
        ImGui::PushStyleColor(ImGuiCol_NavCursor, LauncherTheme::Rgb(Ctx().accent));
        ImGui::PushStyleColor(ImGuiCol_Text, LauncherTheme::Rgb(color::Ink));
        return true;
    }

    bool SelectOption(const char* label, bool selected, bool disabled)
    {
        // ImGui's highlight is square: it is left out and drawn rounded under the row's text, with a check on the chosen row.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->ChannelsSplit(2);
        draw->ChannelsSetCurrent(1);
        ImGui::PushStyleColor(ImGuiCol_Header, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0);
        const std::string id = std::string("##") + label; // the text is drawn below, inside the highlight's margin
        const bool pressed = ImGui::Selectable(id.c_str(), selected, disabled ? ImGuiSelectableFlags_Disabled : 0, ImVec2(0, height::Small));
        ImGui::PopStyleColor(3);
        if (selected && ImGui::IsWindowAppearing())
            ImGui::SetItemDefaultFocus();
        State state = ItemState(disabled);
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        const float mid = std::round((min.y + max.y) / 2);
        DrawText(draw, ImVec2(min.x + space::S, std::round(mid - CapMiddle(TextStyle::Body))), Col(disabled ? color::Dim : color::Ink), label, LabelEnd(label));
        if (selected)
            DrawIcon(draw, Icon::Check, ImVec2(max.x - space::S - icon::Small / 2, mid), icon::Small, Col(Ctx().accent));
        draw->ChannelsSetCurrent(0);
        if (selected || state.hovered || state.pressed)
            draw->AddRectFilled(min, max, Col(state.pressed ? color::RaisedPressed : state.hovered ? color::RaisedHover : color::InsetActive), radius::Control);
        draw->ChannelsMerge();
        return pressed;
    }

    void EndSelect()
    {
        ImGui::PopStyleColor(SelectColors);
        ImGui::PopStyleVar(SelectVars);
        ImGui::EndCombo();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        PopField();
    }

    // ---- Toggle, Checkbox, Radio, Slider ---------------------------------------------------------------------------

    bool Toggle(const char* label, bool* value, bool disabled)
    {
        const float h = height::Small, w = std::round(h * 1.75f);
        const char* end = LabelEnd(label);
        const float text = end != label ? TextSize(TextStyle::Body, label, end).x + space::S : 0;
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + w, min.y + h);
        ImGui::BeginDisabled(disabled);
        const bool pressed = Hit(label, ImVec2(w + text, h));
        if (pressed)
            *value = !*value;
        State state = ItemState(disabled);
        const float t = Ease(ImGui::GetItemID(), *value ? 1.0f : 0.0f);
        const bool on = *value, lit = state.hovered || state.pressed;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(min, max, Col(on ? (lit ? color::AccentFillHover : color::AccentFill) : lit ? color::InsetHover : color::Inset), h / 2);
        draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), Col(on ? color::AccentFillPressed : color::Edge), h / 2, 0, stroke::Edge);
        const float knobR = h / 2 - space::XXS - 1, left = min.x + h / 2, right = max.x - h / 2;
        draw->AddCircleFilled(ImVec2(left + (right - left) * t, min.y + h / 2), knobR * (state.pressed ? 1.12f : 1.0f), Col(on ? color::OnAccent : color::InkSoft), 24);
        if (end != label)
        {
            PushText(TextStyle::Body);
            DrawText(draw, ImVec2(max.x + space::S, min.y + (h - ImGui::GetFontSize()) / 2), Col(color::Ink), label, end);
            PopText();
        }
        ImGui::EndDisabled();
        if (state.focused)
            FocusRing(min, max, h / 2);
        return pressed;
    }

    bool Checkbox(const char* label, bool* value, bool disabled)
    {
        ImGui::BeginDisabled(disabled);
        Marked item = MarkItem(label, disabled);
        if (item.pressed)
            *value = !*value;
        const ImVec2 a = item.mark, b(a.x + height::Mark, a.y + height::Mark);
        const float r = radius::Control * 0.6f;
        const bool lit = item.state.hovered || item.state.pressed;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (*value)
        {
            draw->AddRectFilled(a, b, Col(lit ? color::AccentFillHover : color::AccentFill), r);
            DrawIcon(draw, Icon::Check, ImVec2((a.x + b.x) / 2, (a.y + b.y) / 2), icon::Small, Col(color::OnAccent));
        }
        else
        {
            draw->AddRectFilled(a, b, Col(lit ? color::InsetHover : color::Inset), r);
            draw->AddRect(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f), Col(lit ? color::Ink : color::Edge), r, 0, stroke::Edge);
        }
        ImGui::EndDisabled();
        if (item.state.focused)
            FocusRing(a, b, r);
        return item.pressed;
    }

    bool Radio(const char* label, bool selected, bool disabled)
    {
        ImGui::BeginDisabled(disabled);
        Marked item = MarkItem(label, disabled);
        const float r = height::Mark / 2;
        const ImVec2 centre(item.mark.x + r, item.mark.y + r);
        const bool lit = item.state.hovered || item.state.pressed;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddCircleFilled(centre, r, Col(lit ? color::InsetHover : color::Inset), 24);
        draw->AddCircle(centre, r - 0.75f, Col(selected ? (lit ? color::AccentFillHover : color::AccentFill) : lit ? color::Ink : color::Edge), 24,
            selected ? 2 * stroke::Edge : stroke::Edge);
        if (selected)
            draw->AddCircleFilled(centre, r * 0.45f, Col(lit ? color::AccentFillHover : color::AccentFill), 20);
        ImGui::EndDisabled();
        if (item.state.focused)
            FocusRing(item.mark, ImVec2(item.mark.x + height::Mark, item.mark.y + height::Mark), r);
        return item.pressed && !disabled;
    }

    bool Slider(const char* id, int* value, int min, int max, const SliderOptions& options)
    {
        PushText(TextStyle::Body);
        const std::string widest = fmt::format("{}", std::abs(max) > std::abs(min) ? max : min);
        const float valueW = options.showValue ? ImGui::CalcTextSize(widest.c_str()).x + space::S : 0;
        const float width = (options.width < 0 ? ImGui::GetContentRegionAvail().x : options.width) - valueW;
        const float h = height::Normal, knob = height::Mark / 2, track = space::XS;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(options.disabled);
        Hit(id, ImVec2(width, h));
        State state = ItemState(options.disabled);
        bool changed = false;
        const float left = at.x + knob + 2, right = at.x + width - knob - 2;
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            int next = min + int(std::lround(std::clamp((ImGui::GetIO().MousePos.x - left) / (right - left), 0.0f, 1.0f) * float(max - min)));
            changed = next != *value;
            *value = next;
        }
        else if (ImGui::IsItemFocused() && !options.disabled)
        {
            int step = (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) ? 1 : 0) -
                (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) ? 1 : 0);
            if (step != 0 && std::clamp(*value + step, min, max) != *value)
            {
                *value = std::clamp(*value + step, min, max);
                changed = true;
            }
        }
        const float t = max > min ? float(*value - min) / float(max - min) : 0.0f, x = left + (right - left) * t, mid = at.y + h / 2;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(ImVec2(at.x, mid - track / 2), ImVec2(at.x + width, mid + track / 2), Col(color::Inset), track / 2);
        draw->AddRect(ImVec2(at.x, mid - track / 2), ImVec2(at.x + width, mid + track / 2), Col(color::Edge), track / 2);
        draw->AddRectFilled(ImVec2(at.x, mid - track / 2), ImVec2(x, mid + track / 2), Col(color::AccentFill), track / 2);
        const bool lit = state.hovered || state.pressed;
        draw->AddCircleFilled(ImVec2(x, mid), knob * (state.pressed ? 1.1f : 1.0f), Col(lit ? color::AccentFillHover : color::AccentFill), 24);
        draw->AddCircle(ImVec2(x, mid), knob, Col(color::AccentFillPressed), 24, stroke::Hairline);
        if (options.showValue)
        {
            const std::string shown = fmt::format("{}", *value);
            DrawText(draw, ImVec2(at.x + width + space::S, mid - ImGui::GetFontSize() / 2), Col(color::InkSoft), shown.c_str());
        }
        ImGui::EndDisabled();
        PopText();
        FocusRingIf(state, h / 2);
        if (options.showValue)
        {
            ImGui::SameLine(0, 0);
            ImGui::Dummy(ImVec2(valueW, h));
        }
        return changed;
    }

    // ---- Text fields -----------------------------------------------------------------------------------------------

    namespace
    {
        // The field's look around ImGui's input: its edge in the accent while it is being edited, the ring with the focus.
        bool Field(const char* id, char* buffer, size_t size, const TextFieldOptions& options, float padLeft, ImVec2& min, ImVec2& max)
        {
            const float width = options.width < 0 ? ImGui::GetContentRegionAvail().x : options.width;
            PushField(padLeft);
            min = ImGui::GetCursorScreenPos();
            max = ImVec2(min.x + width, min.y + height::Normal);
            ImGui::SetNextItemWidth(width);
            ImGui::BeginDisabled(options.disabled);
            bool changed = options.hint != nullptr
                ? ImGui::InputTextWithHint(id, options.hint, buffer, size, options.flags, options.callback)
                : ImGui::InputText(id, buffer, size, options.flags, options.callback);
            State state = ItemState(options.disabled);
            if (ImGui::IsItemActive() || state.pressed) // being edited (the gallery shows it as pressed)
                ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), Col(Ctx().accent),
                    radius::Control, 0, stroke::Edge);
            ImGui::EndDisabled();
            PopField();
            if (!ImGui::IsItemActive() && !state.pressed)
                FocusRingIf(state, radius::Control);
            return changed;
        }
    }

    bool TextField(const char* id, char* buffer, size_t size, const TextFieldOptions& options)
    {
        ImVec2 min, max;
        return Field(id, buffer, size, options, space::M, min, max);
    }

    bool SearchField(const char* id, char* buffer, size_t size, const char* hint, float width)
    {
        constexpr float Glass = icon::Small;
        TextFieldOptions options;
        options.hint = hint;
        options.width = width;
        options.flags = ImGuiInputTextFlags_EscapeClearsAll;
        const bool clearable = buffer[0] != 0;
        if (clearable)
            ImGui::SetNextItemAllowOverlap(); // the cross over it takes the pointer
        ImVec2 min, max;
        bool changed = Field(id, buffer, size, options, space::M + Glass + space::XS, min, max);
        ImVec2 after = ImGui::GetCursorScreenPos();
        DrawIcon(ImGui::GetWindowDrawList(), Icon::Search, ImVec2(min.x + space::M + Glass / 2, (min.y + max.y) / 2), Glass, Col(color::Dim));
        if (clearable)
        {
            ImGui::SetCursorScreenPos(ImVec2(max.x - height::Small - space::XXS, min.y + (height::Normal - height::Small) / 2));
            ImGui::PushID(id);
            if (IconButton("##clear", Icon::Cross, nullptr, { ButtonKind::Ghost, Size::Small }))
            {
                buffer[0] = 0;
                changed = true;
            }
            ImGui::PopID();
            ImGui::SetCursorScreenPos(after);
        }
        return changed;
    }
}

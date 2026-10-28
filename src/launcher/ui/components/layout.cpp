// What holds and arranges (layout.h): Card, ListRow, SectionHeader, Modal, Menu, Tooltip, EmptyState.
#include "launcher/ui/components/layout.h"
#include "launcher/i18n.h"
#include "launcher/ui/components/buttons.h"
#include <algorithm>
#include <cmath>

namespace ui
{
    // ---- Card ------------------------------------------------------------------------------------------------------

    void BeginCard(const char* id, ImVec2 size, const CardOptions& options)
    {
        const Elevation& level = elevation::Card;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, options.inset ? LauncherTheme::Rgb(color::Inset) : LauncherTheme::Rgb(level.fill, level.fillAlpha));
        ImGui::PushStyleColor(ImGuiCol_Border, LauncherTheme::Rgb(color::Hairline));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, options.panel ? radius::Panel : radius::Card);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, options.inset ? 0.0f : level.border);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(options.padding, options.padding));
        ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened | options.flags);
        ImGui::SetScrollX(0.0f);
    }

    void EndCard()
    {
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }

    // ---- ListRow ---------------------------------------------------------------------------------------------------

    bool ListRow(const char* title, const ListRowOptions& options)
    {
        const float h = options.subtitle != nullptr ? height::Large : height::Normal;
        const float w = options.width < 0 ? ImGui::GetContentRegionAvail().x : options.width;
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + w, min.y + h);
        ImGui::BeginDisabled(options.disabled);
        const bool pressed = Hit(title, ImVec2(w, h));
        State state = ItemState(options.disabled);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (options.selected || state.hovered || state.pressed)
            draw->AddRectFilled(min, max, Col(state.pressed ? color::RaisedPressed : state.hovered ? color::RaisedHover : color::Raised), radius::Control);
        if (options.selected)
            draw->AddRectFilled(min, ImVec2(min.x + stroke::Selection + 2, max.y), Col(color::Selected), radius::Control, ImDrawFlags_RoundCornersLeft);

        // The leading column (an icon, or the caller's image), centred in `leadWidth` when the list gives one: the titles of a
        // list's rows then start on one edge.
        float x = min.x + space::S, right = max.x - space::S;
        const float mid = std::round((min.y + max.y) / 2);
        if (options.icon != Icon::None)
        {
            const float slot = std::max(icon::Normal, options.leadWidth);
            DrawIcon(draw, options.icon, ImVec2(x + slot / 2, mid), icon::Normal, Col(color::InkSoft));
            x += slot + space::S;
        }
        else if (options.image)
        {
            const float slot = std::max(options.imageWidth, options.leadWidth);
            options.image(draw, ImVec2(x + std::round((slot - options.imageWidth) / 2), min.y + space::XXS), ImVec2(options.imageWidth, h - 2 * space::XXS));
            x += slot + space::S;
        }
        else if (options.leadWidth > 0)
            x += options.leadWidth + space::S;
        if (options.chevron)
        {
            DrawIcon(draw, Icon::ChevronRight, ImVec2(right - icon::Small / 2, mid), icon::Small, Col(color::Dim));
            right -= icon::Small + space::S;
        }
        // One line: the title's capitals centred on the row. Two: the title and the subtitle as one block centred on it.
        const TextStyle titleStyle = TextStyle::Body;
        const bool titleBold = options.subtitle != nullptr;
        const float gap = space::XXS;
        const float titleTop = options.subtitle != nullptr
            ? std::round(mid - (CapTop(titleStyle, titleBold) + SizeOf(titleStyle) + gap + Baseline(TextStyle::Caption)) / 2)
            : std::round(mid - CapMiddle(titleStyle, titleBold));
        const float baseline = titleTop + Baseline(titleStyle, titleBold);
        if (options.trailing != nullptr)
        {
            // On the title's baseline on a row of one line, else centred on the row.
            PushText(TextStyle::Caption);
            ImVec2 text = ImGui::CalcTextSize(options.trailing);
            const float y = options.subtitle != nullptr ? std::round(mid - CapMiddle(TextStyle::Caption)) : baseline - Baseline(TextStyle::Caption);
            DrawText(draw, ImVec2(right - text.x, y), Col(color::InkSoft), options.trailing);
            right -= text.x + space::S;
            PopText();
        }
        // The title (and subtitle) cut with "..." before the trailing part, never under it.
        const float room = right - x;
        const unsigned titleInk = options.dim ? color::Dim : color::Ink;
        float noteW = 0;
        if (options.note != nullptr)
        {
            PushText(TextStyle::Caption);
            noteW = ImGui::CalcTextSize(options.note).x + space::XS;
            PopText();
            PushText(titleStyle, titleBold);
            if (ImGui::CalcTextSize(title).x + noteW > room) // the name comes first: a note that would cut it is left out
                noteW = 0;
            PopText();
        }
        PushText(titleStyle, titleBold);
        const std::string shown = Ellipsize(title, room - noteW);
        DrawText(draw, ImVec2(x, titleTop), Col(titleInk), shown.c_str());
        const float titleEnd = x + ImGui::CalcTextSize(shown.c_str()).x;
        PopText();
        if (options.note != nullptr && noteW > 0) // on the title's baseline
        {
            PushText(TextStyle::Caption);
            DrawText(draw, ImVec2(titleEnd + space::XS, baseline - Baseline(TextStyle::Caption)), Col(color::InkSoft), options.note);
            PopText();
        }
        if (options.subtitle != nullptr)
        {
            // The state's icon on the subtitle's line, centred on its capitals, at the title's left edge.
            const float subTop = titleTop + SizeOf(TextStyle::Body) + gap;
            float subX = x;
            if (options.status != nullptr)
            {
                DrawStatusIcon(draw, *options.status, options.statusIcon != Icon::None ? options.statusIcon : IconOf(*options.status),
                    ImVec2(x + icon::Small / 2, std::round(subTop + CapMiddle(TextStyle::Caption))), icon::Small);
                subX += icon::Small + space::XXS;
            }
            PushText(TextStyle::Caption);
            DrawText(draw, ImVec2(subX, subTop), Col(color::Dim), Ellipsize(options.subtitle, room - (subX - x)).c_str());
            PopText();
        }
        if (options.progress >= 0)
        {
            const float barLeft = x, barRight = max.x - space::S, y = max.y - space::XS + 1;
            draw->AddRectFilled(ImVec2(barLeft, y), ImVec2(barRight, y + stroke::Selection), Col(color::Hairline), stroke::Selection / 2);
            const float fill = std::floor(barLeft + (barRight - barLeft) * std::min(options.progress, 1.0f));
            if (fill > barLeft)
                draw->AddRectFilled(ImVec2(barLeft, y), ImVec2(std::max(fill, barLeft + stroke::Selection), y + stroke::Selection),
                    Col(options.progressColor != 0 ? options.progressColor : color::AccentFill), stroke::Selection / 2);
        }
        ImGui::EndDisabled();
        FocusRingIf(state, radius::Control);
        return pressed && !options.disabled;
    }

    // ---- Settings rows ---------------------------------------------------------------------------------------------

    bool BeginSettings(const char* id)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(space::XXS, space::S));
        ImGui::PushStyleColor(ImGuiCol_TableBorderLight, LauncherTheme::Rgb(color::Hairline, 110));
        const bool open = ImGui::BeginTable(id, 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp);
        if (!open)
        {
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            return false;
        }
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthFixed, height::SettingControl);
        return true;
    }

    namespace
    {
        // The row being drawn: its label waits until its control is drawn, to be centred on it.
        struct PendingLabel
        {
            bool open = false;
            std::string label, hint, note;
            float controlTop = 0;
        };
        PendingLabel g_row;

        float LabelBlockHeight(const char* hint, float width)
        {
            return SizeOf(TextStyle::Body) + (hint != nullptr ? space::XXS + TextSize(TextStyle::Caption, hint, nullptr, false, width).y : 0);
        }

        // From the label's capitals to the hint's last baseline: what is centred.
        float LabelInk(const char* hint, float width)
        {
            return LabelBlockHeight(hint, width) - CapTop(TextStyle::Body) - (SizeOf(TextStyle::Caption) - Baseline(TextStyle::Caption));
        }

        // The label (and its note, its hint) in the first column, centred on what the second column holds: one line's
        // capitals on its middle, a label and its hint as a block.
        void FlushSettingRow()
        {
            if (!g_row.open)
                return;
            g_row.open = false;
            const float controlBottom = std::max(g_row.controlTop, ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y);
            const float centre = (g_row.controlTop + controlBottom) / 2;
            ImGui::TableSetColumnIndex(0);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const char* hint = g_row.hint.empty() ? nullptr : g_row.hint.c_str();
            const float top = hint != nullptr ? std::round(centre - LabelInk(hint, width) / 2 - CapTop(TextStyle::Body)) : std::round(centre - CapMiddle(TextStyle::Body));
            ImDrawList* draw = ImGui::GetWindowDrawList();
            PushText(TextStyle::Body);
            const std::string label = Ellipsize(g_row.label.c_str(), width);
            DrawText(draw, ImVec2(at.x, top), Col(color::Ink), label.c_str());
            const float labelEnd = at.x + ImGui::CalcTextSize(label.c_str()).x;
            PopText();
            if (!g_row.note.empty()) // on the label's baseline
            {
                PushText(TextStyle::Caption);
                DrawText(draw, ImVec2(labelEnd + space::XS, top + Baseline(TextStyle::Body) - Baseline(TextStyle::Caption)), Col(color::Dim), g_row.note.c_str());
                PopText();
            }
            float bottom = top + SizeOf(TextStyle::Body);
            if (hint != nullptr)
            {
                ImGui::SetCursorScreenPos(ImVec2(at.x, bottom + space::XXS));
                Text(TextStyle::Caption, hint, color::Dim, width);
                bottom = ImGui::GetItemRectMax().y;
            }
            ImGui::SetCursorScreenPos(at);
            ImGui::Dummy(ImVec2(0, std::max(0.0f, bottom - at.y))); // the row is as tall as the label needs
        }
    }

    void SettingRow(const char* label, const char* hint, const char* note)
    {
        FlushSettingRow();
        ImGui::TableNextRow(ImGuiTableRowFlags_None, height::Large);
        ImGui::TableSetColumnIndex(0);
        const float block = LabelBlockHeight(hint, ImGui::GetContentRegionAvail().x);
        ImGui::TableSetColumnIndex(1);
        // A label taller than a control (a hint on two lines) moves the control down to its middle.
        const float lower = std::max(0.0f, std::round((block - height::Normal) / 2));
        if (lower > 0)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + lower);
        g_row = { true, label, hint != nullptr ? hint : "", note != nullptr ? note : "", ImGui::GetCursorScreenPos().y };
    }

    void EndSettings()
    {
        FlushSettingRow();
        ImGui::EndTable();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // ---- SectionHeader ---------------------------------------------------------------------------------------------

    void SectionHeader(const char* title, const char* subtitle, bool divider)
    {
        if (divider)
        {
            ImGui::Dummy(ImVec2(0, space::XS));
            ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, at.y + 0.5f), ImVec2(at.x + ImGui::GetContentRegionAvail().x, at.y + 0.5f),
                Col(color::Hairline), stroke::Hairline);
            ImGui::Dummy(ImVec2(0, space::M - ImGui::GetStyle().ItemSpacing.y));
        }
        Text(TextStyle::Title, title, color::Ink, -1);
        if (subtitle != nullptr)
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + space::XXS);
            Text(TextStyle::Caption, subtitle, color::Dim);
        }
    }

    // ---- Modal -----------------------------------------------------------------------------------------------------

    namespace
    {
        float ModalWidth(ModalSize size)
        {
            float wanted = size == ModalSize::Small ? 480.0f : size == ModalSize::Large ? 880.0f : 680.0f;
            return std::min(wanted, ImGui::GetIO().DisplaySize.x - 2 * space::XL);
        }
        constexpr int ModalVars = 4, ModalColors = 3;
    }

    bool BeginModal(const char* id, const char* title, bool* open, ModalSize size)
    {
        if (!*open)
        {
            if (ImGui::IsPopupOpen(id))
            {
                // Closed from outside: the popup goes with it.
                if (ImGui::BeginPopupModal(id))
                {
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
            return false;
        }
        if (!ImGui::IsPopupOpen(id))
            ImGui::OpenPopup(id);
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(ImVec2(ModalWidth(size), 0));
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(display.x, display.y - 2 * space::XL));
        ImGui::SetNextWindowPos(ImVec2(display.x / 2, display.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(space::L, space::L));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, radius::Panel);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, elevation::Overlay.border);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(space::S, space::S));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, LauncherTheme::Rgb(elevation::Overlay.fill, elevation::Overlay.fillAlpha));
        ImGui::PushStyleColor(ImGuiCol_Border, LauncherTheme::Rgb(color::Hairline));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, LauncherTheme::Rgb(color::Scrim, elevation::ScrimAlpha));
        const bool shown = ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleColor(ModalColors);
        ImGui::PopStyleVar(ModalVars);
        if (!shown)
        {
            *open = false;
            return false;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(space::S, space::S));
        // The title, and the cross at the top right.
        const float cross = height::Normal, top = ImGui::GetCursorPosY();
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - cross - space::S + ImGui::GetCursorPosX());
        Text(TextStyle::Title, title, color::Ink, -1);
        ImGui::PopTextWrapPos();
        const float below = ImGui::GetCursorPosY();
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - space::L - cross, top - (cross - type::Title) / 2));
        if (IconButton("##close", Icon::Cross, Tr("Close")))
        {
            *open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetCursorPosY(std::max(below, ImGui::GetCursorPosY()));
        // B / Escape close it, unless a field or a list inside it answers first.
        if (!ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))
        {
            *open = false;
            ImGui::CloseCurrentPopup();
        }
        return true;
    }

    ModalResult ModalActions(bool* open, const char* confirm, const char* cancel, ModalKind kind)
    {
        ModalResult result = ModalResult::None;
        ImGui::Dummy(ImVec2(0, space::XS));
        ButtonOptions cancelOptions;
        ButtonOptions confirmOptions;
        confirmOptions.kind = kind == ModalKind::Danger ? ButtonKind::Danger : ButtonKind::Primary;
        float width = ButtonWidth(cancel, cancelOptions) + (confirm != nullptr ? space::S + ButtonWidth(confirm, confirmOptions) : 0);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
        if (Button(cancel, cancelOptions))
            result = ModalResult::Cancel;
        if ((kind == ModalKind::Danger || confirm == nullptr) && ImGui::IsWindowAppearing())
            ImGui::SetItemDefaultFocus();
        if (confirm != nullptr)
        {
            ImGui::SameLine(0, space::S);
            if (Button(confirm, confirmOptions))
                result = ModalResult::Confirm;
            if (kind != ModalKind::Danger && ImGui::IsWindowAppearing())
                ImGui::SetItemDefaultFocus();
        }
        if (result != ModalResult::None)
        {
            *open = false;
            ImGui::CloseCurrentPopup();
        }
        return result;
    }

    void EndModal()
    {
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }

    ModalResult ConfirmDialog(const char* id, const char* title, const char* body, const char* confirm, const char* cancel, bool* open, ModalKind kind)
    {
        const bool wasOpen = *open;
        if (!BeginModal(id, title, open, ModalSize::Small))
            return wasOpen ? ModalResult::Cancel : ModalResult::None;
        Text(TextStyle::Body, body, color::InkSoft);
        ModalResult result = ModalActions(open, confirm, cancel, kind);
        if (!*open && result == ModalResult::None)
            result = ModalResult::Cancel; // the cross, B or Escape
        EndModal();
        return result;
    }

    // ---- Menu ------------------------------------------------------------------------------------------------------

    namespace
    {
        constexpr ImGuiID AnchorX = 0x4D454E58, AnchorY = 0x4D454E59, AnchorPivot = 0x4D454E5A; // "MENX", "MENY", "MENZ": where a menu opens, by its id
        constexpr float IconSlot = space::L; // a menu row's room for its icon or check, kept when it has none (the labels align)
    }

    void OpenMenu(const char* id, MenuAnchor anchor)
    {
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID key = ImGui::GetID(id);
        storage->SetFloat(key ^ AnchorX, anchor.alignRight ? ImGui::GetItemRectMax().x : ImGui::GetItemRectMin().x);
        storage->SetFloat(key ^ AnchorY, anchor.above ? ImGui::GetItemRectMin().y - space::XS : ImGui::GetItemRectMax().y + space::XS);
        storage->SetInt(key ^ AnchorPivot, (anchor.alignRight ? 1 : 0) | (anchor.above ? 2 : 0));
        ImGui::OpenPopup(id);
    }

    bool BeginMenu(const char* id)
    {
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID key = ImGui::GetID(id);
        if (ImGui::IsPopupOpen(id))
            ImGui::SetNextWindowPos(ImVec2(storage->GetFloat(key ^ AnchorX), storage->GetFloat(key ^ AnchorY)), ImGuiCond_Appearing,
                ImVec2(storage->GetInt(key ^ AnchorPivot) & 1 ? 1.0f : 0.0f, storage->GetInt(key ^ AnchorPivot) & 2 ? 1.0f : 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(space::XS, space::XS));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, radius::Control);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, elevation::Raised.border);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, LauncherTheme::Rgb(elevation::Raised.fill, elevation::Raised.fillAlpha));
        ImGui::PushStyleColor(ImGuiCol_Border, LauncherTheme::Rgb(color::Hairline));
        const bool open = ImGui::BeginPopup(id);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        if (open)
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, space::XXS / 2));
        return open;
    }

    bool MenuItem(const char* label, const MenuItemOptions& options)
    {
        const char* end = LabelEnd(label);
        PushText(TextStyle::Body);
        const float textW = ImGui::CalcTextSize(label, end).x;
        const float lead = space::S + IconSlot, w = std::max(ImGui::GetContentRegionAvail().x, std::ceil(lead + textW + space::XL));
        PopText();
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + w, min.y + height::Small);
        ImGui::BeginDisabled(options.disabled);
        const bool pressed = Hit(label, ImVec2(w, height::Small));
        State state = ItemState(options.disabled);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (state.hovered || state.pressed || state.focused)
            draw->AddRectFilled(min, max, Col(state.pressed ? color::RaisedPressed : color::RaisedHover), radius::Control);
        const unsigned ink = options.danger ? color::Danger : color::Ink;
        const float mid = std::round((min.y + max.y) / 2);
        Icon icon = options.checked ? Icon::Check : options.icon;
        if (icon != Icon::None)
            DrawIcon(draw, icon, ImVec2(min.x + space::S + IconSlot / 2 - space::XXS, mid), icon::Small, Col(options.checked ? Ctx().accent : ink));
        PushText(TextStyle::Body);
        DrawText(draw, ImVec2(min.x + lead, std::round(mid - CapMiddle(TextStyle::Body))), Col(ink), label, end);
        PopText();
        ImGui::EndDisabled();
        if (pressed && !options.disabled)
            ImGui::CloseCurrentPopup();
        return pressed && !options.disabled;
    }

    void MenuSeparator()
    {
        ImVec2 at = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(w, space::XS));
        ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x + space::XS, at.y + space::XS / 2), ImVec2(at.x + w - space::XS, at.y + space::XS / 2),
            Col(color::Hairline), stroke::Hairline);
    }

    void EndMenu()
    {
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }

    // ---- Tooltip, EmptyState ---------------------------------------------------------------------------------------

    void Tooltip(const char* text)
    {
        if (WantsTooltip(ItemState()))
            Bubble(text, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    }

    bool EmptyState(Icon icon, const char* line, const char* action)
    {
        const float width = ImGui::GetContentRegionAvail().x, x = ImGui::GetCursorPosX();
        ImGui::Dummy(ImVec2(0, space::XL));
        ImVec2 at = ImGui::GetCursorScreenPos();
        DrawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(at.x + width / 2, at.y + space::XXL / 2), icon::Large, Col(color::Dim));
        ImGui::Dummy(ImVec2(0, space::XXL + space::XS));
        const float wrap = std::min(width, 420.0f);
        ImVec2 text = TextSize(TextStyle::Body, line, nullptr, false, wrap);
        ImGui::SetCursorPosX(x + (width - text.x) / 2);
        Text(TextStyle::Body, line, color::InkSoft, wrap);
        bool pressed = false;
        if (action != nullptr)
        {
            ImGui::Dummy(ImVec2(0, space::XS));
            ButtonOptions options;
            options.kind = ButtonKind::Primary;
            ImGui::SetCursorPosX(x + (width - ButtonWidth(action, options)) / 2);
            pressed = Button(action, options);
        }
        ImGui::Dummy(ImVec2(0, space::XL));
        return pressed;
    }
}

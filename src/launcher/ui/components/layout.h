#pragma once

// What holds and arranges: Card, ListRow, SectionHeader, Modal (one dialog shell: title, body, actions), Menu (a popover),
// Tooltip and EmptyState.
#include "launcher/ui/components/core.h"
#include "launcher/ui/components/feedback.h"
#include <functional>

namespace ui
{
    // ---- Card: a rounded surface with the tokens' padding; its content is drawn between BeginCard and EndCard. ----
    struct CardOptions
    {
        bool panel = false;      // a page's big card (radius::Panel), else a tile (radius::Card)
        bool inset = false;      // a sunken area inside a card (the inset grey, no edge)
        float padding = space::L;
        ImGuiChildFlags flags = 0; // ImGuiChildFlags_AutoResizeY for a card as tall as its content
    };
    void BeginCard(const char* id, ImVec2 size, const CardOptions& options = {});
    void EndCard();

    // ---- ListRow: one row of a list, the whole row pressable. Its title, a subtitle under it (the row is then
    // height::Large), a trailing text, a chevron when it opens something; selected: the raised grey and the orange bar. ----
    struct ListRowOptions
    {
        const char* subtitle = nullptr;
        const char* trailing = nullptr; // a value or a size at the right ("6.1 GB")
        Icon icon = Icon::None;
        bool chevron = false;
        bool selected = false;
        bool disabled = false;
        float width = -1;
        // An image at the row's left (a game's logo, an initial): drawn by the caller in `imageWidth` points of the row's
        // height, called with the box's top left and size. The sidebar's rows have one.
        std::function<void(ImDrawList*, ImVec2 at, ImVec2 size)> image;
        float imageWidth = 0;
        const char* note = nullptr;     // a small word after the title (a version)
        const Status* status = nullptr; // the shape of the subtitle's state, before it
        float progress = -1;            // 0..1: a thin bar along the row's foot (an install running)
        unsigned progressColor = 0;     // its fill; 0: the accent
        bool dim = false;               // the title in the dim ink (a game not out yet)
    };
    bool ListRow(const char* title, const ListRowOptions& options = {});

    // ---- Settings rows: the one settings idiom. The label (and a one-line hint under it) on the left, the control in a
    // column on the right (height::SettingControl wide), rows at least height::Large high with a hairline between them.
    // Between BeginSettings (false: draw nothing) and EndSettings, one SettingRow per setting, then its control. ----
    bool BeginSettings(const char* id);
    void SettingRow(const char* label, const char* hint = nullptr);
    void EndSettings();

    // ---- SectionHeader: a section's title in a card or a page, a divider above it unless it is the first. ----
    void SectionHeader(const char* title, const char* subtitle = nullptr, bool divider = true);

    // ---- Modal: the one dialog shell. A scrim, a centred panel (Small 480, Medium 680, Large 880 points wide, at most the
    // window), the title and a close cross on top, the body, then the actions at the bottom right. B / Escape closes it. ----
    enum class ModalSize { Small, Medium, Large };
    enum class ModalKind { Normal, Danger }; // Danger: the confirm button is red and the focus starts on Cancel
    enum class ModalResult { None, Confirm, Cancel };
    // True while it is open (`*open`); then draw the body, ModalActions, and EndModal.
    bool BeginModal(const char* id, const char* title, bool* open, ModalSize size = ModalSize::Medium);
    // The footer: Cancel (secondary) then the confirm button (primary, or danger), right-aligned. `confirm` null: only Cancel
    // (then labelled as given, "Close"). Pressing either closes the dialog.
    ModalResult ModalActions(bool* open, const char* confirm, const char* cancel, ModalKind kind = ModalKind::Normal);
    void EndModal();
    // A whole confirmation: title, one sentence, the two buttons.
    ModalResult ConfirmDialog(const char* id, const char* title, const char* body, const char* confirm, const char* cancel, bool* open,
        ModalKind kind = ModalKind::Normal);

    // ---- Menu: a popover under the item that opens it, with its rows. ----
    // After the item that opens it: the menu opens under that item, or above it (`above`: an item at the window's foot, a band).
    void OpenMenu(const char* id, bool above = false);
    bool BeginMenu(const char* id);
    struct MenuItemOptions
    {
        Icon icon = Icon::None;
        bool checked = false;
        bool danger = false;
        bool disabled = false;
    };
    bool MenuItem(const char* label, const MenuItemOptions& options = {});
    void MenuSeparator();
    void EndMenu();

    // ---- Tooltip: the last item's explanation, under it and inside the window, with the pointer or the keyboard / pad focus. ----
    void Tooltip(const char* text);

    // ---- EmptyState: nothing to show yet. An icon, one line, and the action that fills it (null: none). ----
    bool EmptyState(Icon icon, const char* line, const char* action = nullptr);
}

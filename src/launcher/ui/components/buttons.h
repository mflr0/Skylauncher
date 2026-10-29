#pragma once

// Buttons: Button (primary, secondary, danger, ghost; an icon; loading; hold to confirm), Pill, IconButton and the game
// page's MainAction. docs/launcher_ui.md, "Components", says which one to use where.
#include "launcher/ui/components/core.h"

namespace ui
{
    enum class ButtonKind
    {
        Primary,   // the one action the page or dialog is for (at most one in view)
        Secondary, // every other action
        Danger,    // deletes, resets or overwrites: always with hold, or inside a danger dialog
        Ghost,     // a quiet action in a toolbar or a row: no fill until pointed at
    };
    enum class Size { Small, Normal, Large }; // height::Small, height::Normal, height::Large

    struct ButtonOptions
    {
        ButtonKind kind = ButtonKind::Secondary;
        Size size = Size::Normal;
        Icon icon = Icon::None;     // before the label
        Icon trailing = Icon::None; // after it: ChevronDown for a button that opens a menu or a list
        float width = 0;            // 0: the label's; > 0: at least this; < 0: the available width
        bool disabled = false;
        bool loading = false;       // a spinner in place of the icon; not pressable meanwhile
        bool hold = false;          // acts once held for a second (HoldConfirm); a timer icon and "Hold to confirm" say so
        bool overArt = false;       // on the game's art or the band: the band's darker greys (Secondary and Ghost)
        const char* tooltip = nullptr; // also the hint of a hold button (default "Hold to confirm")
    };

    // True on the frame it acts: released over it (a click, Enter, Space, the pad's A), or held to the end for `hold`.
    bool Button(const char* label, const ButtonOptions& options = {});
    float ButtonWidth(const char* label, const ButtonOptions& options = {});
    float HeightOf(Size size);

    // A small rounded action, fully round (height::Small): toolbars, over the art ("Change disc..."), a menu's opener.
    struct PillOptions
    {
        Icon icon = Icon::None;
        Icon trailing = Icon::None;
        const char* prefix = nullptr; // words before the icon, a dot between ("12 h 34", then "· trophy 6/38")
        unsigned iconColor = 0;       // the icon in a state's colour (ColorOf); 0: the label's ink
        bool selected = false; // the accent fill: a pill that stands for a mode that is on
        bool disabled = false;
        bool overArt = false;
        float width = 0;
        const char* tooltip = nullptr;
    };
    bool Pill(const char* label, const PillOptions& options = {});
    float PillWidth(const char* label, const PillOptions& options = {});

    // An icon alone, square: always with a tooltip (it is the icon's only words).
    struct IconButtonOptions
    {
        ButtonKind kind = ButtonKind::Ghost;
        Size size = Size::Normal;
        bool selected = false;
        bool disabled = false;
        bool overArt = false;
        ImTextureRef image;       // a picture in place of the icon (a brand's mark), tinted with the ink
    };
    bool IconButton(const char* id, Icon icon, const char* tooltip, const IconButtonOptions& options = {});

    // A key bound to an action, drawn as a key cap: height::Normal high, at least as wide, as wide as its name needs.
    // Pressed, it waits for the new key (`capturing`: the accent and "..."); `conflict`: another action has the same key
    // (a warning icon and edge; the tooltip says so).
    struct KeyButtonOptions
    {
        bool capturing = false;
        bool conflict = false;
        const char* tooltip = nullptr;
    };
    bool KeyButton(const char* id, const char* key, const KeyButtonOptions& options = {});
    float KeyButtonWidth(const char* key, const KeyButtonOptions& options = {});

    // The game page's big split button: the main action (PLAY, INSTALL, UPDATE, CHOOSE DISC) in the game's colours, and the
    // game's settings in a gear segment on its right. height::MainWidth + height::MainGear by height::Main, at the cursor.
    struct MainActionOptions
    {
        const char* label = "";
        bool enabled = true;
        float progress = -1;         // 0..1 while busy (checking the disc, installing): the percentage replaces "..."
        bool indeterminate = false;  // busy with no known end: a sweep instead of the percentage
        const LauncherTheme::GameStyle* style = nullptr; // the game's colours (StyleOf); the launcher's blue when null
        const char* gearTooltip = nullptr; // "Spyro's Adventure settings"
        bool takeFocus = false;      // the main half takes the keyboard and pad focus this frame
        bool showFocus = false;      // and shows its ring
    };
    enum class MainActionHit { None, Main, Settings };
    struct MainActionResult
    {
        MainActionHit hit = MainActionHit::None;
        ImVec2 mainCentre, gearCentre; // where the screenshot tests point
    };
    MainActionResult MainAction(const MainActionOptions& options);
}

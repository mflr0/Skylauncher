#pragma once

// The design tokens: the only sizes, colours, radii and timings the components (ui/components) use, and through them the
// screens. A screen never writes a number of points or a colour of its own (docs/launcher_ui.md, "Tokens").
// The palette is launcher_theme.h's (dorydev's): here each colour is named by its role. Contrast is measured in
// docs/launcher_ui.md ("Contrast"): text 4.5:1 or more, large text and the parts of a control that show its state 3:1 or more.
#include "launcher/ui/theme/launcher_theme.h"

namespace ui
{
    // ---- Type: five styles. Caption, Body and Label are Outfit (Label in SemiBold); Title and Display are Bungee. ----
    enum class TextStyle { Caption, Body, Label, Title, Display };
    namespace type
    {
        inline constexpr float Caption = 14; // hints, status lines, badges, a card's meta: the smallest text there is
        inline constexpr float Body = 17;    // sentences, button labels, fields, menus: the default
        inline constexpr float Label = 20;   // what leads a row: a game's name in the sidebar, tabs, a list row's title
        inline constexpr float Title = 24;   // section and dialog titles, the main action's label
        inline constexpr float Display = 34; // page titles
    }

    // ---- Spacing: 4-point steps. ----
    namespace space
    {
        inline constexpr float XXS = 4;  // an icon and its text, a badge's inside
        inline constexpr float XS = 8;   // inside a group: a label and its control, two chips
        inline constexpr float S = 12;   // between controls of a row or a list
        inline constexpr float M = 16;   // between groups inside a card
        inline constexpr float L = 24;   // a card's padding, between cards
        inline constexpr float XL = 32;  // a page's margins, before a section
        inline constexpr float XXL = 48; // an empty state's air
    }

    // ---- Radii: three, by role. Chips, badges, toggles, bars and the focus of round things have round ends (height / 2). ----
    namespace radius
    {
        inline constexpr float Control = 10; // buttons, fields, selects, menus, list rows
        inline constexpr float Card = 14;    // tiles and small cards, the main action, toasts
        inline constexpr float Panel = 22;   // a page's big card (Settings), dialogs
    }

    // ---- Control heights. ----
    namespace height
    {
        inline constexpr float Small = 32;  // chips, pills, compact buttons, the toggle, a select's rows
        inline constexpr float Normal = 40; // buttons, fields, selects, tabs, segmented controls, list rows of one line
        inline constexpr float Large = 56;  // list rows with a subtitle, the sidebar's rows, big buttons
        inline constexpr float Main = 64;   // the main action (PLAY, INSTALL, UPDATE)
        inline constexpr float MainWidth = 300, MainGear = 64; // its main half's width and its settings segment's
        inline constexpr float Mark = 22;   // a checkbox's box, a radio's circle, a slider's knob
        inline constexpr float SettingControl = 320; // a settings row's control column: one width for its select, toggle or button
    }

    // ---- Lines. ----
    namespace width
    {
        inline constexpr float Page = 940; // a settings page's column: settings read badly stretched across a wide window
    }

    // Game hero: the large fallback name and the compact portal beside the main action.
    namespace hero
    {
        inline constexpr float Title = 96, TitleWidth = 560;
        inline constexpr float LogoHeight = 144;
        inline constexpr float PanelWidth = 400, PanelMinWidth = 264;
        inline constexpr float PanelHeight = 280, CompactPanelHeight = 320;
    }

    namespace stroke
    {
        inline constexpr float Hairline = 1;  // card edges, dividers, a secondary button's edge
        inline constexpr float Edge = 1.5f;   // what shows a state: a toggle's track, a danger button, a field being edited
        inline constexpr float Selection = 3; // the line under the selected tab, the bar of a selected row
    }

    // ---- Icons: Lucide's, drawn in a square of one of these sizes (its stroke is 1/12 of it). ----
    namespace icon
    {
        inline constexpr float Small = 16;  // on a line of Caption or Body text: a status, a pill, a chip, a select's chevron
        inline constexpr float Normal = 20; // a button, a list row, a menu row, a toast
        inline constexpr float Large = 32;  // the picture of an empty state or a drop zone, the main action's gear
    }

    // ---- Keyboard and pad focus: one ring, around every focusable control. Its colour is the shown game's accent
    // (Context::accent: the launcher's blue, Giants' gold, Swap Force's green). ----
    namespace focus
    {
        inline constexpr float Width = 3;  // points
        inline constexpr float Offset = 3; // the gap between the control and the ring
        inline constexpr float Reach = Offset + Width; // how far the ring goes past its control: what a clip rect must leave
    }

    // ---- Colours by role (values from launcher_theme.h). ----
    namespace color
    {
        using namespace LauncherTheme;
        // Surfaces, darkest to lightest: inset (fields) < surface (cards) < raised (buttons) < raised hover / pressed.
        inline constexpr unsigned Inset = Field, InsetHover = FieldHover, InsetActive = FieldActive;
        inline constexpr unsigned Surface = Panel;
        inline constexpr unsigned Raised = PanelRaised, RaisedHover = PanelRaisedHover, RaisedPressed = PanelRaisedActive;
        // On the game's art and the band under it: the band's darker greys.
        inline constexpr unsigned OverArt = BottomBarRaised, OverArtHover = BottomBarRaisedHover, OverArtPressed = BottomBarRaisedActive;
        inline constexpr unsigned OverArtEdge = BottomBarBorder;
        inline constexpr unsigned Hairline = Border;           // card edges, dividers (decoration only)
        inline constexpr unsigned Edge = BorderBright;         // what shows a control's state: a toggle's track, a box (3:1 on surfaces)
        // Text.
        inline constexpr unsigned Ink = LauncherTheme::Text;   // body text: 10:1 or more
        inline constexpr unsigned InkSoft = TextSecondary;     // secondary text: 7:1 or more
        inline constexpr unsigned Dim = TextDim;               // hints, captions, disabled labels: 4.7:1 or more
        // The accent, and its ink.
        inline constexpr unsigned AccentFill = Accent, AccentFillHover = AccentHover, AccentFillPressed = AccentDark;
        inline constexpr unsigned OnAccent = AccentInk;        // 8.3:1 on the accent
        // Meanings: as text and icons on the dark surfaces (4.5:1 or more on surface, inset and page).
        inline constexpr unsigned Info = Accent;
        inline constexpr unsigned Success = Ok;
        inline constexpr unsigned Warning = 0xFFC65C;          // the launcher catalogue's Giants gold
        inline constexpr unsigned Danger = LauncherTheme::Danger;
        inline constexpr unsigned DangerFill = 0x6B4345;       // a danger button: Danger mixed 32 % into the surface, white ink 7.6:1
        inline constexpr unsigned DangerFillHover = 0x7D4A4B, DangerFillPressed = 0x5C3D40;
        inline constexpr unsigned Selected = SidebarBar;       // the bar of a selected row (the sidebar's orange)
        inline constexpr unsigned Focus = Accent;              // the default focus ring (Context::accent replaces it per game)
        inline constexpr unsigned Scrim = 0x0B0F2E;            // behind a dialog, at Elevation::Overlay's alpha
    }

    // ---- Elevation: how far a surface stands out. ----
    struct Elevation
    {
        unsigned fill;
        unsigned fillAlpha;
        float border;       // hairline width, 0 for none
        float shadowAlpha;  // a soft shadow under it, 0 for none
    };
    namespace elevation
    {
        inline constexpr Elevation Flat = { color::Surface, 0, 0, 0 };          // on the page, no box
        inline constexpr Elevation Card = { color::Surface, 235, 1.5f, 0 };     // a card on the page
        inline constexpr Elevation Raised = { color::Raised, 245, 1, 0.25f };   // a menu, a tooltip, a toast
        inline constexpr Elevation Overlay = { color::Surface, 250, 1.5f, 0.45f }; // a dialog, over a scrim
        inline constexpr unsigned ScrimAlpha = 150;
    }

    // ---- Motion, in milliseconds. Every animation stops when the system asks for less motion (Context::reduceMotion). ----
    namespace motion
    {
        inline constexpr int Fast = 120;    // hover and press
        inline constexpr int Normal = 200;  // a toast arriving, a menu opening
        inline constexpr int Slow = 300;    // the logo's drop
        inline constexpr int Spin = 900;    // one turn of the spinner, one sweep of the indeterminate bar
        inline constexpr int HoldHint = 2500; // how long "Hold to confirm" stays after a tap too short
    }

    // The state every component is drawn in; the gallery forces it (PreviewState).
    inline constexpr float DisabledAlpha = 0.45f; // a disabled control: everything at this alpha, nothing else changes
}

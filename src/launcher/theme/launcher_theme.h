#pragma once

// The launcher's look in one place: colours, sizes and fonts. Change these first; the screens only use names from here.
// Art direction: the game's sky behind (a drawn night sky before a disc is verified), neutral dark greys for the surfaces,
// one blue accent, an orange bar for the sidebar's selection, the eight element colours, chunky rounded buttons. There is no publisher artwork: the backdrop is drawn, game art comes from the player's own disc.
#include <imgui.h>

namespace LauncherTheme
{
    constexpr ImU32 Rgb(unsigned rgb, unsigned alpha = 255)
    {
        return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
    }

    // Backdrop gradient, top to bottom, and the glow behind the portal ring.
    inline constexpr unsigned SkyTop = 0x0B0F2E;
    inline constexpr unsigned SkyBottom = 0x2A1A5E;
    inline constexpr unsigned PortalGlow = 0x5B3FD0;
    // Every page but the game's (Home, Collection, Settings): a plain gradient in the sidebar's grey, top to bottom.
    inline constexpr unsigned PageTop = 0x32323C, PageBottom = 0x1E1E24;

    // The sidebar: a cool dark grey (a touch of blue), light ink. The bottom band and its buttons are shades of the same grey.
    inline constexpr unsigned Sidebar = 0x26262C; // Sidebar background color
    inline constexpr unsigned SidebarHover = 0x47474F;    // a game row under the mouse
    inline constexpr unsigned SidebarSelected = 0x55555E; // the selected row, with an orange bar on its left
    inline constexpr unsigned SidebarInk = 0xF2F2F2;   // light text on the dark sidebar
    inline constexpr unsigned SidebarInkDim = 0xFFFFFF;
    inline constexpr unsigned Ready = 0x2EEA8F;     // green dot: the disc is verified
    // The band under the game art, one step lighter than the sidebar; its buttons sit a step darker than the band, lighten
    // under the mouse and darken when pressed. Every shade keeps the sidebar's hue (blue a little above red and green).
    inline constexpr unsigned BottomBar = 0x3A3A44, BottomBarTop = 0x42424D; // a light gradient, top to bottom
    inline constexpr unsigned BottomBarRaised = 0x2C2C34, BottomBarRaisedHover = 0x34343E, BottomBarRaisedActive = 0x26262C;
    inline constexpr unsigned BottomBarBorder = 0x50505E;
    inline constexpr unsigned BottomBarInk = 0xF2F2F2;
    inline constexpr unsigned BadgeInk = 0x15171C;     // initials on the coloured game badges
    // Surfaces (settings cards, fields, buttons, menus): the sidebar's neutral greys, each with its hover and pressed shade.
    inline constexpr unsigned Panel = 0x26262C;
    inline constexpr unsigned PanelRaised = 0x3A3A42, PanelRaisedHover = 0x47474F, PanelRaisedActive = 0x55555E;
    inline constexpr unsigned Field = 0x1C1C21, FieldHover = 0x2A2A31, FieldActive = 0x34343C;
    inline constexpr unsigned Border = 0x4A4A55;
    inline constexpr unsigned BorderBright = 0x7A7A88;

    inline constexpr unsigned Text = 0xF4F4F6;
    inline constexpr unsigned TextSecondary = 0xD0D0D8;
    inline constexpr unsigned TextDim = 0xB8B8C4; // was 0x9C9CA8: 4.1:1 on the raised greys, now 4.5:1 or more (docs/launcher_ui.md, Legibility)

    // One accent everywhere, the blue of the main button (close to the Skylanders logo's sky blue): selections,
    // toggles, sliders, focus. The only other accent is the orange bar marking the selected row of the sidebar.
    inline constexpr unsigned Accent = 0x6BBAFF;
    inline constexpr unsigned AccentHover = 0x8ECBFF;
    inline constexpr unsigned AccentDark = 0x2F86E0; // bevel under accent buttons, lines marking a selection
    inline constexpr unsigned AccentInk = 0x0A1B33;  // text on the accent
    inline constexpr unsigned Play = Accent;         // the main button (CHOOSE ISO / PLAY)
    inline constexpr unsigned PlayDark = AccentDark;
    inline constexpr unsigned PlayInk = AccentInk;
    inline constexpr unsigned SidebarBar = 0xF08A12; // orange, the sidebar's selected row only
    inline constexpr unsigned Danger = 0xFF817A; // was 0xF85149: 3.4:1 on the raised greys
    inline constexpr unsigned Ok = 0x48D17A;

    // Element colours: air, earth, fire, water, magic, tech, life, undead.
    inline constexpr unsigned Elements[8] = { 0x3E9FD8, 0x9A6233, 0xE0531F, 0x1F74D1, 0x8E44C9, 0xD08A00, 0x3E9B3F, 0x5E5A86 };

    // Sizes in points (the window scales them to the display's pixel density).
    inline constexpr float WindowW = 1080, WindowH = 720, WindowMinW = 1024, WindowMinH = 680;
    inline constexpr float MinUiScale = 0.75f, MaxUiScale = 3.0f; // [launcher] ui_scale and the Ctrl +/- zoom
    inline constexpr float SidebarW = 270;
    inline constexpr float Pad = 24;
    inline constexpr float Radius = 14, RadiusSmall = 10, RadiusLarge = 22;
    inline constexpr float PlayButtonW = 300, PlayButtonH = 64, PlayGearW = 64; // the action button: its main half, height, gear half
    inline constexpr float BottomBarH = 150; // the band with the main button, under the game art
    inline constexpr float GameRowH = 56, SidebarBarW = 5; // sidebar rows and the bar marking the selected one
    inline constexpr float TabUnderlineH = 3, TabGap = 34; // settings tabs: the line under the selected one, the space between them
    inline constexpr float ControllerW = 190, ControllerGap = 40; // the controller picker, and its space before the main button
    inline constexpr float GameBadgeR = 17;     // the game's icon (or initial) in a sidebar row
    inline constexpr float GearButton = 30;  // the settings icon at the bottom of the sidebar

    // Fonts: Outfit for text, Bungee for titles and buttons (both SIL OFL 1.1, resources/fonts).
    inline constexpr float TextSize = 17, SmallSize = 14, TitleSize = 22, HeadlineSize = 34, HeroSize = 48;
    inline constexpr float SidebarTextSize = 20, SidebarSmallSize = 16; // game names and their status in the sidebar
    inline constexpr float CaptionSize = 12;  // the smallest text there is, at 1x
    inline constexpr float BoldOffset = 0.6f; // extra copies of SemiBold text, this far apart, for what must read heavier (the chosen controller)
    inline constexpr int BolderPasses = 2;

    // Each game's own colours, for what belongs to the game on its page: the action button (docs/launcher_ui.md, "Each
    // game's look"), and the Home cards' glow. Taken from the game's logo: Spyro's Adventure's sky blue to deep blue,
    // Giants' gold to orange, Swap Force's lime to green.
    struct GameStyle
    {
        unsigned top, bottom;  // the logo's colours, top to bottom (the button's gradient is drawn between them)
        unsigned outline;      // its dark ink: the label and the gear when white wouldn't read on the fill
        unsigned glow;         // the Home card under the pointer
    };
    inline constexpr GameStyle SpyrosAdventureStyle = { 0x7CC8FF, 0x2C7FD8, 0x0C2A5C, 0x6BBAFF };
    inline constexpr GameStyle GiantsStyle = { 0xFFD23C, 0xF06A10, 0x3A1404, 0xFFB43A };
    inline constexpr GameStyle SwapForceStyle = { 0xD9EC45, 0x6FB42E, 0x0F2E22, 0xBFE04A };
    // The game's style by catalogue id; the launcher's own blue for a game without one.
    const GameStyle& StyleOf(const char* game);

    // The game page: how the logo arrives (once per visit, docs/launcher_ui.md), its shadow, the backdrop's shading.
    inline constexpr float LogoDropMs = 300, LogoDropDistance = 44;
    inline constexpr unsigned BackdropTopShade = 120, BackdropBottomShade = 235; // alpha of SkyTop over the game's painting
    inline constexpr int BackdropArtW = 1024; // the painting's width as a texture (Giants' is 1600 px)
    // The sidebar's game rows: the logo's box on the left, the name and status beside it.
    inline constexpr float GameLogoW = 72, GameLogoH = 38, GameTextX = 98;
    inline constexpr float VersionSize = 13; // "v0.4.2" beside the SKYLAUNCHER title

    // ImGui widget colours and shapes for the whole window.
    void Apply(ImGuiStyle& style);
}

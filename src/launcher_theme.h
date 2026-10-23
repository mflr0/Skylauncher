#pragma once

// The launcher's look in one place: colours, sizes and fonts. Change these first; the screens only use names from here.
// Art direction: a Skylanders-like night sky (deep blue to purple), gold accents, the eight element colours, chunky
// rounded buttons. There is no publisher artwork: the backdrop is drawn, game art comes from the player's own disc.
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

    inline constexpr unsigned Sidebar = 0x0A0D2A;
    inline constexpr unsigned Panel = 0x161B4D;
    inline constexpr unsigned PanelRaised = 0x222866;
    inline constexpr unsigned Field = 0x0E1240;
    inline constexpr unsigned Border = 0x3A3F8C;
    inline constexpr unsigned BorderBright = 0x6F78E8;

    inline constexpr unsigned Text = 0xF4F1FF;
    inline constexpr unsigned TextSecondary = 0xC9C5F0;
    inline constexpr unsigned TextDim = 0x9C97D6;

    inline constexpr unsigned Gold = 0xFFC233;      // titles, selection, sliders
    inline constexpr unsigned GoldDark = 0xD98A00;  // bevel under gold buttons
    inline constexpr unsigned GoldInk = 0x3A1C00;   // text on gold
    inline constexpr unsigned Play = 0x3FCB5A;      // the PLAY button
    inline constexpr unsigned PlayDark = 0x1F8F3A;
    inline constexpr unsigned PlayInk = 0x06240F;
    inline constexpr unsigned Danger = 0xF85149;
    inline constexpr unsigned Ok = 0x48D17A;

    // Element colours: air, earth, fire, water, magic, tech, life, undead.
    inline constexpr unsigned Elements[8] = { 0x3E9FD8, 0x9A6233, 0xE0531F, 0x1F74D1, 0x8E44C9, 0xD08A00, 0x3E9B3F, 0x5E5A86 };

    // Sizes in points (the window scales them to the display's pixel density).
    inline constexpr float WindowW = 1280, WindowH = 800, WindowMinW = 1024, WindowMinH = 680;
    inline constexpr float SidebarW = 270;
    inline constexpr float Pad = 24;
    inline constexpr float Radius = 14, RadiusSmall = 10, RadiusLarge = 22;
    inline constexpr float PlayButtonW = 300, PlayButtonH = 72;

    // Fonts: Outfit for text, Bungee for titles and buttons (both SIL OFL 1.1, resources/fonts).
    inline constexpr float TextSize = 17, SmallSize = 14, TitleSize = 22, HeadlineSize = 34, HeroSize = 48;

    // ImGui widget colours and shapes for the whole window.
    void Apply(ImGuiStyle& style);
}

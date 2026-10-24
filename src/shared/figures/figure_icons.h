#pragma once

// An icon for every toy, for the figures the player's disc has no picture of (magic items, adventure packs, and any toy
// before a disc is verified): a shape for the category, the element's colour, a badge for the variant. The disc's own
// portraits are preferred when they exist (see LauncherAssets).
//
// The look is all in IconStyle, so it can follow another visual identity without touching the drawing.
#include "shared/figures/figure_catalog.h"
#include <cstdint>
#include <vector>

namespace figures
{
    struct IconStyle
    {
        uint32_t outline = 0xFF201C1C;     // ABGR, like ElementColor
        uint32_t neutral = 0xFF7A6E66;     // the fill of toys without an element (items, packs)
        float outlineWidth = 0.035f;       // fraction of the icon size
        float shade = 0.35f;               // how much darker the bottom of the fill is than its top
        uint32_t variantColors[size_t(Variant::Count)] = {
            0x00000000, // Normal: no badge
            0xFF4AD5FF, // Legendary: gold
            0xFF40242E, // Dark: deep purple
            0xC0F5E8B0, // Crystal: translucent ice
            0xFF6BFF9B, // Glow: glow-in-the-dark green
            0xFFD6CDC8, // Metallic: silver
            0xFFA8F4FF, // LightCore: pale yellow
        };
    };

    // RGBA pixels (R first), `size` x `size`, transparent around the shape.
    std::vector<uint8_t> GenerateIcon(const ToyInfo& toy, int size, const IconStyle& style = {});
}

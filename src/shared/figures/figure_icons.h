#pragma once

// An icon for every toy, for the figures the player's disc has no picture of (magic items, adventure packs, the later
// games' toys, and any toy before a disc is verified): a shape for the category, the element's colour, a badge for the
// variant. The disc's own portraits are preferred when they exist (see LauncherAssets). The toys are the shared
// catalogue's (collection/catalogue.h); the look is the launcher's own.
//
// The look is all in IconStyle, so it can follow another visual identity without touching the drawing.
#include "shared/collection/catalogue.h"
#include "shared/figures/figure_catalog.h"
#include <cstdint>
#include <string>
#include <vector>

namespace figures
{
    struct IconStyle
    {
        uint32_t outline = 0xFF201C1C;     // ABGR, like ElementColor
        uint32_t neutral = 0xFF7A6E66;     // the fill of toys without an element (items, packs)
        uint32_t light = 0xFF9EF2FF;       // Light and Dark (Trap Team on): Spyro's Adventure's palette has neither
        uint32_t dark = 0xFF5A2E3E;
        float outlineWidth = 0.035f;       // fraction of the icon size
        float shade = 0.35f;               // how much darker the bottom of the fill is than its top
        // Badge colours of the variant kinds that have their own; the others take the catalogue's (VariantKindRgb).
        uint32_t legendary = 0xFF4AD5FF;   // gold
        uint32_t darkEdition = 0xFF40242E; // deep purple
        uint32_t crystal = 0xC0F5E8B0;     // translucent ice
        uint32_t glow = 0xFF6BFF9B;        // glow-in-the-dark green
        uint32_t metallic = 0xFFD6CDC8;    // silver
        uint32_t lightCore = 0xFFA8F4FF;   // pale yellow
    };

    // The badge colour of a variant kind (ABGR), 0 for the plain toy.
    uint32_t VariantBadgeColor(collection::VariantKind kind, const IconStyle& style = {});

    // RGBA pixels (R first), `size` x `size`, transparent around the shape. `toy` null: an unknown toy.
    std::vector<uint8_t> GenerateIcon(const collection::Toy* toy, collection::VariantKind kind, int size, const IconStyle& style = {});

    // Spyro's Adventure's pictures of a toy on its disc: its HUD portrait (PortraitImageName) and its 512 px "versus"
    // picture ("<portrait>_vs"), empty if it has none.
    std::string PortraitLargeImageName(const ToyInfo& toy);
}

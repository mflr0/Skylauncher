#pragma once

// The launcher's fonts (Outfit Regular and SemiBold, and Bungee, SIL Open Font License 1.1), built into the executable by CMake from
// resources/fonts so that a single executable works without its fonts folder.
#include <cstddef>

extern const unsigned char g_embeddedOutfit[];
extern const size_t g_embeddedOutfitSize;
extern const unsigned char g_embeddedOutfitBold[];
extern const size_t g_embeddedOutfitBoldSize;
extern const unsigned char g_embeddedIcon[]; // the window's icon, a 128x128 PNG (resources/icons)
extern const size_t g_embeddedIconSize;
extern const unsigned char g_embeddedBungee[];
extern const size_t g_embeddedBungeeSize;

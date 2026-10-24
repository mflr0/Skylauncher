#pragma once

// A picture for any toy: the one the player's own disc has (cached as PNG by the game, docs/figures.md), else one drawn
// here: a shape per category in the element's colour. Variants get a badge (Legendary star, Dark crescent...).
// Nothing is shipped or downloaded: the shapes are code.
#include "catalogue.h"
#include <cstdint>
#include <string>
#include <vector>

namespace collection
{
    struct Image
    {
        int width = 0, height = 0;
        std::vector<uint32_t> rgba; // R in the low byte, straight (not premultiplied) alpha

        bool Empty() const { return rgba.empty(); }
    };

    // The category's shape on a transparent square (on an opaque rounded tile with `tile`).
    Image GeneratedIcon(const Toy* toy, int size, bool tile = false);
    // A round badge for the variant kind in the bottom right corner; nothing for Base.
    void AddBadge(Image& image, VariantKind kind);

    // The cached disc picture of the toy (from a game whose catalogue entry names one), empty if there is none yet.
    // Folders: SSA_PORTRAITS, else "portraits" in the game runtime's data folder.
    std::string DiscImagePath(const Toy& toy);

    // The disc picture if cached, else the generated one, `size` pixels square, with the badge of `kind`.
    Image Icon(const Toy* toy, VariantKind kind, int size, bool* fromDisc = nullptr);

    Image LoadPng(const std::string& path);
    bool SavePng(const Image& image, const std::string& path);
    Image Resize(const Image& image, int width, int height);
}

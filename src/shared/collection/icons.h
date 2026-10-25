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

    // A toy's picture from one game's disc, as cached by art::Extract (art/game_art.h): the picture of its variant
    // (`variant`: the variant id, 0 for the plain toy) if the game has one, else the toy's. `large`: the game's biggest
    // (SSA: the 512 px "versus" picture; Giants has one size, 256 px). Empty when that game has none cached.
    // Folders: SSA_PORTRAITS, else "portraits" in the game's data folder.
    std::string GamePortraitPath(const std::string& gameId, const Toy& toy, uint16_t variant = 0, bool large = false);

    // The rule for every picture of a toy (docs/runtime/host/game_art.md): the art of the game being viewed (`gameId`),
    // else another game's (catalogue order), else empty, and the caller draws the generated icon.
    std::string PortraitPath(const std::string& gameId, const Toy& toy, uint16_t variant = 0, bool large = false);
    // When only the kind is known (the Toy Box, the collection's cards): the toy's one variant of that kind, else 0.
    uint16_t VariantFor(const Toy& toy, VariantKind kind);
    // PortraitPath with no game being viewed: the first game, in catalogue order, that has one.
    std::string DiscImagePath(const Toy& toy);

    // The picture PortraitPath finds, else the generated one, `size` pixels square, with the badge of `kind`.
    Image Portrait(const std::string& gameId, const Toy* toy, VariantKind kind, int size, uint16_t variant = 0, bool* fromDisc = nullptr);
    // Portrait with no game being viewed.
    Image Icon(const Toy* toy, VariantKind kind, int size, bool* fromDisc = nullptr);

    Image LoadPng(const std::string& path);
    bool SavePng(const Image& image, const std::string& path);
    Image Resize(const Image& image, int width, int height);
}

#include "shared/figures/figure_icons.h"
#include <algorithm>
#include <cmath>

namespace figures
{
    namespace
    {
        struct Rgba
        {
            float r, g, b, a;
        };

        Rgba Unpack(uint32_t abgr)
        {
            return { float(abgr & 0xFF), float(abgr >> 8 & 0xFF), float(abgr >> 16 & 0xFF), float(abgr >> 24) / 255.0f };
        }

        Rgba Scale(Rgba color, float factor)
        {
            auto channel = [&](float v) { return std::clamp(v * factor, 0.0f, 255.0f); };
            return { channel(color.r), channel(color.g), channel(color.b), color.a };
        }

        // Signed distances in a unit square centred on 0 (negative inside).
        float Circle(float x, float y, float r) { return std::hypot(x, y) - r; }

        float Box(float x, float y, float hx, float hy, float round)
        {
            float qx = std::fabs(x) - hx + round, qy = std::fabs(y) - hy + round;
            return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - round;
        }

        float Diamond(float x, float y, float r) { return (std::fabs(x) + std::fabs(y) - r) * 0.70710678f; }

        // A regular polygon with `n` sides and circumradius `r`, a vertex pointing up.
        float Polygon(float x, float y, int n, float r)
        {
            const float pi = 3.14159265f, half = pi / n;
            float angle = std::fmod(std::atan2(x, y) + 2 * pi, 2 * half) - half;
            float length = std::hypot(x, y);
            float px = length * std::cos(angle) - r * std::cos(half), py = std::fabs(length * std::sin(angle)) - r * std::sin(half);
            py += std::clamp(-py, 0.0f, r * std::sin(half));
            return std::hypot(px, py) * (px < 0 ? -1.0f : 1.0f);
        }

        float Shape(Category category, float x, float y)
        {
            switch (category)
            {
            case Category::Giant: return Polygon(x, y, 8, 0.47f);
            case Category::TrapMaster: return Polygon(x, y, 6, 0.47f);
            case Category::Mini: return Circle(x, y, 0.34f);
            case Category::Trap:
            case Category::CreationCrystal: return Diamond(x, y, 0.46f);
            case Category::Vehicle: return Box(x, y, 0.46f, 0.30f, 0.12f);
            case Category::MagicItem: return Box(x, y, 0.38f, 0.38f, 0.10f);
            case Category::AdventurePack: return Polygon(x, y, 6, 0.46f);
            default: return Circle(x, y, 0.44f);
            }
        }
    }

    std::vector<uint8_t> GenerateIcon(const ToyInfo& toy, int size, const IconStyle& style)
    {
        constexpr int Samples = 4;
        std::vector<uint8_t> rgba(size_t(size) * size * 4, 0);
        Rgba base = Unpack(toy.element == Element::None ? style.neutral : ElementColor(toy.element));
        Rgba top = Scale(base, 1.15f), bottom = Scale(base, 1.0f - style.shade);
        Rgba outline = Unpack(style.outline);
        Rgba badge = Unpack(style.variantColors[size_t(toy.variant)]);
        bool hasBadge = toy.variant != Variant::Normal;
        float fillAlpha = toy.variant == Variant::Crystal ? 0.65f : 1.0f;
        const float badgeX = 0.30f, badgeY = 0.30f, badgeR = 0.13f;

        for (int py = 0; py < size; py++)
            for (int px = 0; px < size; px++)
            {
                float r = 0, g = 0, b = 0, a = 0;
                for (int sy = 0; sy < Samples; sy++)
                    for (int sx = 0; sx < Samples; sx++)
                    {
                        float x = (px + (sx + 0.5f) / Samples) / size - 0.5f, y = (py + (sy + 0.5f) / Samples) / size - 0.5f;
                        Rgba color{};
                        float shape = Shape(toy.category, x, y);
                        if (shape < 0)
                        {
                            float t = std::clamp(y + 0.5f, 0.0f, 1.0f);
                            color = { top.r + (bottom.r - top.r) * t, top.g + (bottom.g - top.g) * t, top.b + (bottom.b - top.b) * t, fillAlpha };
                            // Swap Force: the two halves in two tones.
                            if (toy.category == Category::SwapForce && x + y > 0)
                                color = Scale(color, 0.8f);
                            if (shape > -style.outlineWidth)
                                color = outline;
                        }
                        if (hasBadge)
                        {
                            float d = Circle(x - badgeX, y - badgeY, badgeR);
                            if (d < 0)
                                color = d > -style.outlineWidth * 0.8f ? outline : badge;
                        }
                        // Premultiplied accumulation, so edges blend cleanly.
                        r += color.r * color.a;
                        g += color.g * color.a;
                        b += color.b * color.a;
                        a += color.a;
                    }
                uint8_t* out = &rgba[(size_t(py) * size + px) * 4];
                if (a > 0)
                {
                    out[0] = uint8_t(r / a);
                    out[1] = uint8_t(g / a);
                    out[2] = uint8_t(b / a);
                }
                out[3] = uint8_t(std::lround(a / (Samples * Samples) * 255));
            }
        return rgba;
    }
}

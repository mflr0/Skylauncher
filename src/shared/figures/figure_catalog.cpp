#include "figure_catalog.h"
#include "../collection/catalogue.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <vector>

namespace figures
{
    namespace
    {
        // The catalogue's toys that the title accepts, in its order; built once, never changed afterwards.
        const std::vector<ToyInfo>& Toys()
        {
            static const std::vector<ToyInfo> toys = [] {
                static std::deque<std::string> strings; // the ToyInfo fields point into these
                auto keep = [](const std::string& text) { return text.empty() ? nullptr : strings.emplace_back(text).c_str(); };
                const collection::Catalogue& catalogue = collection::Catalogue::Get();
                const collection::Game* game = catalogue.FindGame("ssa");
                std::vector<ToyInfo> list;
                for (const collection::Toy* toy : catalogue.Accepted("ssa"))
                {
                    ToyInfo info{ toy->type, keep(toy->name), nullptr, Element::None, ToyKind::Character, nullptr };
                    if (auto name = game->names.find(toy->type); name != game->names.end())
                        info.portrait = keep(name->second);
                    if (auto image = game->images.find(toy->type); image != game->images.end())
                        info.image = keep(image->second);
                    // The title's eight elements; light, dark and none are None here.
                    info.element = toy->element <= collection::Element::Undead ? Element(toy->element) : Element::None;
                    info.kind = toy->category == collection::Category::MagicItem ? ToyKind::MagicItem
                        : toy->category == collection::Category::Adventure       ? ToyKind::Adventure
                        : toy->category == collection::Category::Mini            ? ToyKind::Sidekick
                        : toy->edition == collection::VariantKind::Legendary     ? ToyKind::Legendary
                                                                                 : ToyKind::Character;
                    list.push_back(info);
                }
                return list;
            }();
            return toys;
        }
    }

    std::span<const ToyInfo> Catalog()
    {
        return Toys();
    }

    const ToyInfo* FindToy(uint16_t toyType)
    {
        for (const ToyInfo& toy : Toys())
        {
            if (toy.toyType == toyType)
                return &toy;
        }
        return nullptr;
    }

    const char* ElementName(Element element)
    {
        static const char* const Names[] = { "Air", "Earth", "Fire", "Water", "Magic", "Tech", "Life", "Undead", "" };
        return Names[size_t(element)];
    }

    uint32_t ElementColor(Element element)
    {
        // 0xAABBGGRR
        static const uint32_t Colors[] = { 0xFFE8C46A, 0xFF2E6C9A, 0xFF2A4FE0, 0xFFD08A28, 0xFFC04AA8, 0xFF20B0E8,
            0xFF3CB04C, 0xFF7A5A6A, 0xFF707070 };
        return Colors[size_t(element)];
    }

    std::string PortraitImageName(const ToyInfo& toy)
    {
        if (toy.portrait == nullptr)
            return toy.image != nullptr ? toy.image : std::string();
        std::string name = std::string("toy_") + toy.portrait + "_hud";
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(tolower(c)); });
        return name;
    }

    std::string StarterFileName(const ToyInfo& toy)
    {
        char prefix[8];
        snprintf(prefix, sizeof(prefix), "%03u ", toy.toyType);
        return prefix + std::string(toy.name) + ".sky";
    }
}

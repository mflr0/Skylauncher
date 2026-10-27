// Add a figure: the catalogue of the six games, one card per character, the versions in a side panel, "owned x2" marks.
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/launcher_ui.h"
#include <algorithm>
#include <fmt/format.h>

namespace launcher_ui
{
    using collection::Category;
    using collection::Toy;

    namespace
    {
        // A swapper is added whole: the catalogue lists its tops only, named without the "(top)", and creating one makes both halves.
        bool IsSwapper(const Toy& toy) { return toy.category == Category::SwapTop; }

        // A swapper's plain version is its "Standard": its own entry in the list would say the same twice.
        bool PlainDuplicate(const Toy& toy, const collection::Variant& variant) { return IsSwapper(toy) && variant.kind == collection::VariantKind::Base; }

        size_t CountVersions(const Toy& toy)
        {
            return size_t(std::count_if(toy.variants.begin(), toy.variants.end(), [&](const collection::Variant& v) { return !PlainDuplicate(toy, v); }));
        }
    }

    void CollectionPage::DrawAdd(ImVec2 size)
    {
        const collection::Catalogue& catalogue = collection::Catalogue::Get();
        m_app.Title(Tr("ADD A FIGURE"), HeadlineSize - 6, Text);
        ImGui::Dummy(ImVec2(0, 2));

        // Game, search, category.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 8));
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("##addgame", Tf("For {}", ShortGameName(m_addGame)).c_str()))
        {
            for (const collection::Game& game : catalogue.Games())
                if (ImGui::Selectable(ShortGameName(game.id).c_str(), game.id == m_addGame))
                {
                    m_addGame = game.id;
                    m_addToy = 0xFFFF;
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine(0, 10);
        ImGui::SetNextItemWidth(260);
        ImGui::InputTextWithHint("##addsearch", Tr("Search the catalogue"), m_addSearch, sizeof(m_addSearch));
        // Type: the same menu as the shelf's bar.
        ImGui::SameLine(0, 10);
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("##addtype", m_addCategory < 0 ? Tr("All types") : CategoryLabel(Category(m_addCategory)).c_str()))
        {
            if (ImGui::Selectable(Tr("All types"), m_addCategory < 0))
                m_addCategory = -1;
            for (int category = 0; category < int(Category::Count); category++)
                if (std::any_of(catalogue.Toys().begin(), catalogue.Toys().end(), [&](const Toy& toy) { return int(toy.category) == category; }))
                    if (ImGui::Selectable(CategoryLabel(Category(category)).c_str(), m_addCategory == category))
                        m_addCategory = category;
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0, 4));

        std::string search = Lower(m_addSearch);
        // The first version (1-based, 0 = none) of a toy whose own name holds the search: "polar" finds Polar Whirlwind.
        auto firstMatchingVersion = [&](const Toy& toy) {
            if (search.empty())
                return 0;
            for (size_t i = 0; i < toy.variants.size(); i++)
                if (!PlainDuplicate(toy, toy.variants[i]) && Lower(toy.variants[i].name).find(search) != std::string::npos)
                    return int(i) + 1;
            return 0;
        };
        auto matches = [&](const Toy& toy) {
            if (toy.category == Category::SwapBottom)
                return false; // the bottom comes with its top
            if (m_addCategory >= 0 && int(toy.category) != m_addCategory)
                return false;
            return search.empty() || Lower(toy.name).find(search) != std::string::npos || firstMatchingVersion(toy) > 0;
        };

        // Owned copies of a toy (not counting the starters the game made on its own).
        auto owned = [&](uint16_t type, int variant, const std::string& edition) {
            int count = 0;
            for (const collection::Figure& figure : m_model.figures)
                if (figure.type == type && (variant < 0 || figure.variant == variant) && (edition.empty() || figure.edition == edition) &&
                    !collection::UntouchedStarter(figure))
                    count++;
            return count;
        };

        constexpr float PanelW = 270;
        float listH = size.y - ImGui::GetCursorPosY() - Pad - 56;
        ImGui::BeginChild("##addtoys", ImVec2(size.x - 2 * Pad - PanelW - 14, listH), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        constexpr float Cell = 104, Gap = 10;
        int columns = std::max(1, int((ImGui::GetContentRegionAvail().x + Gap) / (Cell + Gap)));
        const Toy* chosen = nullptr;
        for (int pass = 0; pass < 2; pass++) // the game's own toys, then the later games'
        {
            std::vector<const Toy*> toys;
            for (const Toy& toy : catalogue.Toys())
                if (matches(toy) && catalogue.Accepts(m_addGame, toy.type) == (pass == 0))
                    toys.push_back(&toy);
            if (toys.empty())
                continue;
            m_app.BoldText(pass == 0 ? Tf("Works in {}  {}", ShortGameName(m_addGame), toys.size()).c_str()
                                     : Tf("Later games  {}", toys.size()).c_str(), TextSecondary);
            ImGui::Dummy(ImVec2(0, 2));
            for (size_t i = 0; i < toys.size(); i++)
            {
                const Toy& toy = *toys[i];
                if (i % columns != 0)
                    ImGui::SameLine(0, Gap);
                ImVec2 at = ImGui::GetCursorScreenPos();
                ImGui::PushID(toy.type);
                if (ImGui::InvisibleButton("##toy", ImVec2(Cell, Cell + 38), ImGuiButtonFlags_EnableNav))
                {
                    m_addToy = toy.type;
                    m_addVersion = firstMatchingVersion(toy);
                    m_created.clear();
                    m_addLabel[0] = 0;
                }
                bool hovered = ImGui::IsItemHovered();
                m_app.FocusRing(Radius);
                ImGui::PopID();
                bool selected = m_addToy == toy.type;
                if (selected)
                    chosen = &toy;
                ImDrawList* draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled(at, ImVec2(at.x + Cell, at.y + Cell + 38), Rgb(selected ? PanelRaisedActive : hovered ? PanelRaised : Field), Radius);
                if (selected)
                    draw->AddRect(at, ImVec2(at.x + Cell, at.y + Cell + 38), Rgb(Accent), Radius, 0, 2.5f);
                DrawIcon(draw, &toy, collection::VariantKind::Base, ImVec2(at.x + 16, at.y + 6), Cell - 32, false);
                ImGui::PushFont(nullptr, SmallSize);
                std::string name = LauncherMiddleEllipsis(IsSwapper(toy) ? WithoutHalf(toy.name) : toy.name, 14);
                ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
                draw->AddText(ImVec2(at.x + (Cell - nameSize.x) / 2, at.y + Cell - 18), Rgb(Text), name.c_str());
                int count = owned(toy.type, -1, {});
                std::string note = pass == 1 ? Tf("needs {}", ShortGameName(toy.game)) : count > 0 ? Tf("owned x{}", count) : std::string();
                ImVec2 noteSize = ImGui::CalcTextSize(note.c_str());
                draw->AddText(ImVec2(at.x + (Cell - noteSize.x) / 2, at.y + Cell), Rgb(pass == 1 ? TextDim : Ok), note.c_str());
                ImGui::PopFont();
                if (CountVersions(toy) > 0) // it has versions: a small badge, the count
                {
                    std::string more = "+" + std::to_string(CountVersions(toy));
                    ImGui::PushFont(nullptr, SmallSize - 1);
                    float moreW = ImGui::CalcTextSize(more.c_str()).x + 12;
                    ImGui::PopFont();
                    m_app.Badge(draw, ImVec2(at.x + Cell - moreW - 6, at.y + 6), more.c_str(), Accent, AccentInk);
                }
            }
            ImGui::Dummy(ImVec2(0, 10));
        }
        ImGui::EndChild();

        // The versions of the chosen toy, then Create.
        ImGui::SameLine(0, 14);
        ImGui::BeginChild("##addversions", ImVec2(PanelW, listH), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        if (chosen == nullptr)
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", Tr("Pick a figure."));
        else
        {
            m_app.BoldText((IsSwapper(*chosen) ? WithoutHalf(chosen->name) : chosen->name).c_str(), Text);
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s · %s", ElementName(chosen->element).c_str(),
                (IsSwapper(*chosen) ? std::string(Tr("Swapper")) : CategoryLabel(chosen->category)).c_str());
            ImGui::Dummy(ImVec2(0, 4));
            std::vector<std::pair<std::string, collection::VariantKind>> versions{ { chosen->edition != collection::VariantKind::Base ? chosen->name : std::string(Tr("Standard")),
                chosen->edition } };
            for (const collection::Variant& variant : chosen->variants)
                versions.push_back({ variant.name, variant.kind });
            m_addVersion = std::clamp(m_addVersion, 0, int(versions.size()) - 1);
            m_app.BoldText(Tr("Versions"), TextSecondary);
            for (size_t i = 0; i < versions.size(); i++)
            {
                if (i > 0 && PlainDuplicate(*chosen, chosen->variants[i - 1]))
                    continue;
                int count = i == 0 ? owned(chosen->type, 0, {})
                    : chosen->variants[i - 1].sameTag ? owned(chosen->type, -1, chosen->variants[i - 1].name) : owned(chosen->type, chosen->variants[i - 1].id, {});
                // A row: the version's picture (the toy's own when it has none), its name, its kind and how many are owned.
                constexpr float Row = 50;
                ImVec2 at = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable((std::string("##v") + std::to_string(i)).c_str(), m_addVersion == int(i), 0, ImVec2(0, Row)))
                    m_addVersion = int(i);
                ImDrawList* draw = ImGui::GetWindowDrawList();
                DrawPicture(draw, chosen, versions[i].second, i == 0 ? uint16_t(0) : chosen->variants[i - 1].id, ImVec2(at.x + 4, at.y + 3), Row - 6);
                float textX = at.x + Row + 6;
                ImGui::PushFont(m_app.m_boldFont, SmallSize);
                draw->AddText(ImVec2(textX, at.y + 5), Rgb(Text), LauncherMiddleEllipsis(IsSwapper(*chosen) ? WithoutHalf(versions[i].first) : versions[i].first, 26).c_str());
                ImGui::PopFont();
                ImGui::PushFont(nullptr, SmallSize - 1);
                std::string kind = versions[i].second == collection::VariantKind::Base ? std::string() : KindLabel(versions[i].second);
                if (count > 0)
                    kind += (kind.empty() ? "" : "  ·  ") + Tf("owned x{}", count);
                draw->AddText(ImVec2(textX, at.y + 27), Rgb(count > 0 ? Ok : TextDim), kind.c_str());
                ImGui::PopFont();
            }
        }
        ImGui::EndChild();

        // The bar: a name (optional), Create, and putting what was just made on a seat.
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + Pad, ImGui::GetWindowPos().y + size.y - Pad - 40));
        if (chosen != nullptr)
        {
            std::string versionName = m_addVersion > 0 ? chosen->variants[std::min<size_t>(m_addVersion - 1, chosen->variants.size() - 1)].name : std::string(chosen->name);
            if (IsSwapper(*chosen))
                versionName = WithoutHalf(versionName);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
            ImGui::SetNextItemWidth(220);
            ImGui::InputTextWithHint("##addlabel", Tr("Name (optional)"), m_addLabel, sizeof(m_addLabel));
            ImGui::PopStyleVar();
            ImGui::SameLine(0, 8);
            if (m_app.PillButton(Tf("Create {}", LauncherMiddleEllipsis(versionName, 20)).c_str(), true, 300, true))
            {
                const collection::Variant* variant = m_addVersion > 0 ? &chosen->variants[m_addVersion - 1] : nullptr;
                std::string path;
                if (IsSwapper(*chosen))
                {
                    // Both halves, linked: the bottom of the same version when it has one.
                    uint16_t bottomType = collection::MatchingHalf(chosen->type), bottomVariant = 0;
                    if (const Toy* bottom = collection::Catalogue::Get().Find(bottomType); bottom != nullptr && variant != nullptr && !variant->sameTag)
                        for (const collection::Variant& candidate : bottom->variants)
                            if (candidate.id == variant->id)
                                bottomVariant = candidate.id;
                    path = collection::CreateSwapper(chosen->type, bottomType, variant != nullptr && !variant->sameTag ? variant->id : 0, bottomVariant).top;
                }
                else
                    path = collection::Create(chosen->type, variant != nullptr && !variant->sameTag ? variant->id : 0);
                if (path.empty())
                    Toast(Tr("Couldn't create the figure"));
                else
                {
                    if (variant != nullptr && variant->sameTag)
                        collection::SetEdition(path, variant->name, variant->kind);
                    if (std::string label = Trimmed(m_addLabel); !label.empty())
                        collection::SetLabel(path, label);
                    m_created = path;
                    m_addLabel[0] = 0;
                    Reload();
                    Toast(Tf("Created {}", versionName));
                }
            }
            if (!m_created.empty())
            {
                const collection::Game* game = m_model.Game();
                const collection::Figure* made = m_model.Find(m_created);
                for (size_t seat = 0; made != nullptr && game != nullptr && seat < game->seats.size(); seat++)
                {
                    if (!SeatAccepts(game->seats[seat], *made) || !collection::Catalogue::Get().Accepts(m_model.gameId, made->type))
                        continue;
                    ImGui::SameLine(0, 8);
                    if (m_app.PillButton(Tf("Put it on {}", SeatName(seat)).c_str(), false, 190, true))
                    {
                        PutOn(seat, m_created);
                        m_created.clear();
                    }
                    break;
                }
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + size.x - Pad - 110, ImGui::GetWindowPos().y + size.y - Pad - 40));
        if (m_app.PillButton(Tr("Close"), false, 110, true))
            CloseOverlay();
    }
}

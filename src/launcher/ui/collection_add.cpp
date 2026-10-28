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

        // The bottom that goes with a swapper's top (the same version's when it has one), for its joined picture.
        const Toy* BottomOf(const Toy& top, uint16_t variant, uint16_t& bottomVariant)
        {
            bottomVariant = 0;
            const Toy* bottom = collection::Catalogue::Get().Find(collection::MatchingHalf(top.type));
            if (bottom != nullptr && variant != 0)
                for (const collection::Variant& candidate : bottom->variants)
                    if (candidate.id == variant)
                        bottomVariant = candidate.id;
            return bottom;
        }

        // The game a toy's versions are told apart in: the chosen game when it reads the toy, else the game the toy came with.
        std::string ViewGame(const std::string& chosen, const Toy& toy)
        {
            return collection::Catalogue::Get().Accepts(chosen, toy.type) ? chosen : toy.game;
        }

        // A row of its own only for the plain toy (made under the id it is sold with: its "base" entry is never a second row)
        // and the versions the game tells apart; the others play as the plain toy in it.
        bool PlainDuplicate(const std::string& game, const Toy& toy, const collection::Variant& variant)
        {
            if (variant.kind == collection::VariantKind::Base)
                return true;
            return !variant.sameTag && !collection::Catalogue::Get().Read(game, &toy, variant.id).exact;
        }

        size_t CountVersions(const std::string& game, const Toy& toy)
        {
            return size_t(std::count_if(toy.variants.begin(), toy.variants.end(), [&](const collection::Variant& v) { return !PlainDuplicate(game, toy, v); }));
        }
    }

    void CollectionPage::DrawAdd()
    {
        const collection::Catalogue& catalogue = collection::Catalogue::Get();

        // Game, search, category.
        const float room = ImGui::GetContentRegionAvail().x;
        const float selectW = (room - 2 * ui::space::S) / 4;
        ui::SelectOptions gameOptions;
        gameOptions.width = selectW;
        if (ui::BeginSelect("##addgame", Tf("For {}", ShortGameName(m_addGame)).c_str(), gameOptions))
        {
            for (const collection::Game& game : catalogue.Games())
                if (ui::SelectOption(ShortGameName(game.id).c_str(), game.id == m_addGame))
                {
                    m_addGame = game.id;
                    m_addToy = 0xFFFF;
                }
            ui::EndSelect();
        }
        ImGui::SameLine(0, ui::space::S);
        ui::SearchField("##addsearch", m_addSearch, sizeof(m_addSearch), Tr("Search the catalogue"), room - 2 * selectW - 2 * ui::space::S);
        // Type: the same menu as the shelf's bar.
        ImGui::SameLine(0, ui::space::S);
        ui::SelectOptions typeOptions;
        typeOptions.width = selectW;
        if (ui::BeginSelect("##addtype", m_addCategory < 0 ? Tr("All types") : CategoryLabel(Category(m_addCategory)).c_str(), typeOptions))
        {
            if (ui::SelectOption(Tr("All types"), m_addCategory < 0))
                m_addCategory = -1;
            for (int category = 0; category < int(Category::Count); category++)
                if (std::any_of(catalogue.Toys().begin(), catalogue.Toys().end(), [&](const Toy& toy) { return int(toy.category) == category; }))
                    if (ui::SelectOption(CategoryLabel(Category(category)).c_str(), m_addCategory == category))
                        m_addCategory = category;
            ui::EndSelect();
        }

        std::string search = Lower(m_addSearch);
        // The first version (1-based, 0 = none) of a toy whose own name holds the search: "polar" finds Polar Whirlwind.
        auto firstMatchingVersion = [&](const Toy& toy) {
            if (search.empty())
                return 0;
            for (size_t i = 0; i < toy.variants.size(); i++)
                if (!PlainDuplicate(ViewGame(m_addGame, toy), toy, toy.variants[i]) && Lower(toy.variants[i].name).find(search) != std::string::npos)
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

        constexpr float PanelW = ui::height::SettingControl - ui::space::XL;
        // The grid ends above the footer; its scrollbar (always there, so nothing shifts) sits against the last column, and the
        // cards share the width left, so the grid fills its column.
        const float listH = OverlayBody(4 * ui::height::Main + ui::height::Normal, 4 * ui::height::Main, 8 * ui::height::Main);
        const float gridW = ImGui::GetContentRegionAvail().x - PanelW - ui::space::S;
        const bool found = std::any_of(catalogue.Toys().begin(), catalogue.Toys().end(), matches);
        ImGui::BeginChild("##addtoys", ImVec2(gridW, listH), ImGuiChildFlags_NavFlattened,
            ImGuiWindowFlags_NoBackground | (found ? ImGuiWindowFlags_AlwaysVerticalScrollbar : 0));
        ImGui::SetScrollX(0.0f);
        constexpr float MinCell = ui::height::Main + ui::height::Normal, Gap = ui::space::S, Art = ui::height::Main + ui::space::XS;
        constexpr float CardH = ui::space::XS + Art + ui::space::XS + ui::type::Caption + ui::space::XXS + ui::height::Small;
        const float usable = ImGui::GetContentRegionAvail().x - ui::space::XS;
        const int columns = std::max(1, int((usable + Gap) / (MinCell + Gap)));
        const float Cell = std::floor((usable + Gap) / columns - Gap);
        const Toy* chosen = nullptr;
        if (!found)
            ui::EmptyState(ui::Icon::Search, Tr("No figure matches"));
        for (int pass = 0; pass < 2; pass++) // the game's own toys, then the later games'
        {
            std::vector<const Toy*> toys;
            for (const Toy& toy : catalogue.Toys())
                if (matches(toy) && catalogue.Accepts(m_addGame, toy.type) == (pass == 0))
                    toys.push_back(&toy);
            if (toys.empty())
                continue;
            const std::string count = collection::FigureCount(int(toys.size()), French());
            ui::SectionHeader(pass == 0 ? Tf("Works in {}", ShortGameName(m_addGame)).c_str() : Tr("Later games"), count.c_str(), pass > 0);
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            for (size_t i = 0; i < toys.size(); i++)
            {
                const Toy& toy = *toys[i];
                if (i % columns != 0)
                    ImGui::SameLine(0, Gap);
                ImVec2 at = ImGui::GetCursorScreenPos();
                ImGui::PushID(toy.type);
                if (ui::Hit("##toy", ImVec2(Cell, CardH)))
                {
                    m_addToy = toy.type;
                    m_addVersion = firstMatchingVersion(toy);
                    m_created.clear();
                    m_addLabel[0] = 0;
                }
                const ui::State state = ui::ItemState();
                ui::FocusRingIf(state, ui::radius::Card);
                ImGui::PopID();
                const bool hovered = state.hovered, selected = m_addToy == toy.type;
                if (selected)
                    chosen = &toy;
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const ImVec2 end(at.x + Cell, at.y + CardH);
                draw->AddRectFilled(at, end, ui::Col(selected ? ui::color::RaisedPressed : hovered ? ui::color::Raised : ui::color::Inset), ui::radius::Card);
                if (selected)
                    draw->AddRect(at, end, ui::Col(ui::color::AccentFill), ui::radius::Card, 0, ui::stroke::Edge);
                uint16_t bottomVariant = 0;
                const ImVec2 art(at.x + (Cell - Art) / 2, at.y + ui::space::XS);
                if (!IsSwapper(toy) || !DrawSwapper(draw, &toy, 0, BottomOf(toy, 0, bottomVariant), bottomVariant, art, Art))
                    DrawIcon(draw, &toy, collection::VariantKind::Base, art, Art, false);
                const std::string name = IsSwapper(toy) ? WithoutHalf(toy.name) : toy.name;
                ui::PushText(ui::TextStyle::Caption, true);
                const std::string cut = ui::Ellipsize(name.c_str(), Cell - 2 * ui::space::XS);
                ui::DrawText(draw, ImVec2(at.x + (Cell - ImGui::CalcTextSize(cut.c_str()).x) / 2, art.y + Art + ui::space::XS), ui::Col(ui::color::Ink), cut.c_str());
                ui::PopText();
                const int owns = owned(toy.type, -1, {});
                const float noteY = art.y + Art + ui::space::XS + ui::type::Caption + ui::space::XXS;
                if (pass == 1)
                {
                    ui::PushText(ui::TextStyle::Caption);
                    const std::string note = Tf("needs {}", ShortGameName(toy.game));
                    ui::DrawText(draw, ImVec2(at.x + (Cell - ImGui::CalcTextSize(note.c_str()).x) / 2, noteY), ui::Col(ui::color::Dim), note.c_str());
                    ui::PopText();
                }
                else if (owns > 0)
                {
                    const std::string note = Tf("owned x{}", owns);
                    ui::DrawBadge(draw, ImVec2(at.x + (Cell - ui::BadgeSize(note.c_str()).x) / 2, noteY), note.c_str(), ui::color::Success);
                }
                if (CountVersions(ViewGame(m_addGame, toy), toy) > 0) // it has versions: a small badge, the count
                {
                    const std::string more = "+" + std::to_string(CountVersions(ViewGame(m_addGame, toy), toy));
                    ui::DrawBadge(draw, ImVec2(end.x - ui::BadgeSize(more.c_str()).x - ui::space::XS, at.y + ui::space::XS), more.c_str(), ui::color::AccentFill, ui::color::OnAccent);
                }
            }
            ImGui::Dummy(ImVec2(0, ui::space::XS));
        }
        ImGui::EndChild();

        // The versions of the chosen toy.
        ImGui::SameLine(0, ui::space::S);
        ui::BeginCard("##addversions", ImVec2(PanelW, listH), { .padding = ui::space::XS });
        if (chosen == nullptr)
            ui::Text(ui::TextStyle::Body, Tr("Pick a figure."), ui::color::Dim);
        else
        {
            ui::Text(ui::TextStyle::Body, (IsSwapper(*chosen) ? WithoutHalf(chosen->name) : chosen->name).c_str(), ui::color::Ink, 0, true);
            ui::Text(ui::TextStyle::Caption, fmt::format("{} · {}", ElementName(chosen->element), IsSwapper(*chosen) ? std::string(Tr("Swapper")) : CategoryLabel(chosen->category)).c_str(),
                ui::color::Dim);
            std::vector<std::pair<std::string, collection::VariantKind>> versions{ { IsSwapper(*chosen) ? WithoutHalf(chosen->name) : chosen->name, chosen->edition } };
            for (const collection::Variant& variant : chosen->variants)
                versions.push_back({ variant.name, variant.kind });
            m_addVersion = std::clamp(m_addVersion, 0, int(versions.size()) - 1);
            ui::Text(ui::TextStyle::Caption, Tr("Versions"), ui::color::InkSoft, 0, true);
            constexpr float VersionArt = ui::height::Large - 2 * ui::space::XXS;
            for (size_t i = 0; i < versions.size(); i++)
            {
                if (i > 0 && PlainDuplicate(ViewGame(m_addGame, *chosen), *chosen, chosen->variants[i - 1]))
                    continue;
                int count = i == 0 ? owned(chosen->type, 0, {}) + (collection::StandardVariant(*chosen) != 0 ? owned(chosen->type, collection::StandardVariant(*chosen), {}) : 0)
                    : chosen->variants[i - 1].sameTag ? owned(chosen->type, -1, chosen->variants[i - 1].name) : owned(chosen->type, chosen->variants[i - 1].id, {});
                // A row: the version's picture (the toy's own when it has none), its name, its kind and how many are owned.
                const std::string title = IsSwapper(*chosen) ? WithoutHalf(versions[i].first) : versions[i].first;
                const std::string kind = versions[i].second == collection::VariantKind::Base ? std::string(Tr("Standard")) : KindLabel(versions[i].second);
                const std::string owns = count > 0 ? Tf("owned x{}", count) : std::string();
                ui::ListRowOptions options;
                options.subtitle = kind.c_str();
                options.trailing = owns.empty() ? nullptr : owns.c_str();
                options.selected = m_addVersion == int(i);
                options.imageWidth = VersionArt;
                options.image = [&](ImDrawList* draw, ImVec2 art, ImVec2) {
                    const uint16_t versionId = i == 0 || chosen->variants[i - 1].sameTag ? uint16_t(0) : chosen->variants[i - 1].id;
                    uint16_t bottomVariant = 0;
                    if (!IsSwapper(*chosen) || !DrawSwapper(draw, chosen, versionId, BottomOf(*chosen, versionId, bottomVariant), bottomVariant, art, VersionArt))
                        DrawPicture(draw, chosen, versions[i].second, i == 0 ? uint16_t(0) : chosen->variants[i - 1].id, art, VersionArt);
                };
                ImGui::PushID(int(i));
                if (ui::ListRow(title.c_str(), options))
                    m_addVersion = int(i);
                ImGui::PopID();
            }
        }
        ui::EndCard();

        // The footer: a name (optional), what was just made on a seat, then Close and Create.
        ImGui::Dummy(ImVec2(0, ui::space::XXS));
        if (chosen == nullptr)
        {
            if (OverlayActions(Tr("Close")) == ui::ModalResult::Cancel)
                CloseOverlay();
            return;
        }
        std::string versionName = m_addVersion > 0 ? chosen->variants[std::min<size_t>(m_addVersion - 1, chosen->variants.size() - 1)].name : std::string(chosen->name);
        if (IsSwapper(*chosen))
            versionName = WithoutHalf(versionName);
        ui::TextFieldOptions labelOptions;
        labelOptions.hint = Tr("Name (optional)");
        labelOptions.width = ui::height::SettingControl - ui::space::XL;
        ui::TextField("##addlabel", m_addLabel, sizeof(m_addLabel), labelOptions);
        if (!m_created.empty())
        {
            const collection::Game* game = m_model.Game();
            const collection::Figure* made = m_model.Find(m_created);
            for (size_t seat = 0; made != nullptr && game != nullptr && seat < game->seats.size(); seat++)
            {
                if (!SeatAccepts(game->seats[seat], *made) || !collection::Catalogue::Get().Accepts(m_model.gameId, made->type))
                    continue;
                ImGui::SameLine(0, ui::space::S);
                if (ui::Button(Tf("Put it on {}", SeatName(seat)).c_str()))
                {
                    PutOn(seat, m_created);
                    m_created.clear();
                }
                break;
            }
        }
        ImGui::SameLine(0, ui::space::S);
        const std::string create = Tf("Create {}", ui::Ellipsize(versionName.c_str(), ui::height::SettingControl / 2));
        const ui::ModalResult result = OverlayActions(Tr("Close"), create.c_str());
        if (result == ui::ModalResult::Cancel)
            CloseOverlay();
        else if (result == ui::ModalResult::Confirm)
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
                path = collection::Create(chosen->type, variant != nullptr ? (variant->sameTag ? collection::StandardVariant(*chosen) : variant->id) : collection::StandardVariant(*chosen));
            if (path.empty())
                Toast(Tr("Couldn't create the figure"), ui::ToastKind::Error);
            else
            {
                if (variant != nullptr && variant->sameTag)
                    collection::SetEdition(path, variant->name, variant->kind);
                if (std::string label = Trimmed(m_addLabel); !label.empty())
                    collection::SetLabel(path, label);
                m_created = path;
                m_addLabel[0] = 0;
                Reload();
                Toast(Tf("Created {}", versionName), ui::ToastKind::Success);
            }
        }
    }
}

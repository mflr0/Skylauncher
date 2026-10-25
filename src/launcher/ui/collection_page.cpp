// The collection page: the seats of the next session on the left, My Skylanders in sections on the right, the View menu.
// The figures and their folder are the shared library's (src/shared/collection); the model is collection_model.cpp; the
// Figure page, Add a figure and Manage are in collection_figure.cpp, collection_add.cpp and collection_manage.cpp.
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/launcher_ui.h"
#include "shared/collection/icons.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fmt/format.h>
#include <imgui_internal.h> // ImGui::FocusItem

namespace launcher_ui
{
    namespace fs = std::filesystem;
    using collection::Category;
    using collection::Figure;
    using collection::Section;

    // ---- Helpers --------------------------------------------------------------------------------------------------

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(tolower(c)); });
        return text;
    }

    // A five-pointed star (favourites): a pentagon and its five spikes, all convex.
    void DrawStar(ImDrawList* draw, ImVec2 center, float radius, ImU32 color)
    {
        ImVec2 outer[5], inner[5];
        for (int i = 0; i < 5; i++)
        {
            float a = -1.5707963f + i * 1.2566371f, b = a + 0.6283185f;
            outer[i] = ImVec2(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius);
            inner[i] = ImVec2(center.x + std::cos(b) * radius * 0.42f, center.y + std::sin(b) * radius * 0.42f);
        }
        draw->AddConvexPolyFilled(inner, 5, color);
        for (int i = 0; i < 5; i++)
            draw->AddTriangleFilled(outer[i], inner[i], inner[(i + 4) % 5], color);
    }

    std::string PlayTime(uint32_t seconds)
    {
        return seconds >= 3600 ? fmt::format("{} h {:02} min", seconds / 3600, seconds / 60 % 60) : fmt::format("{} min", seconds / 60);
    }

    std::string ShortDate(const std::string& iso)
    {
        if (iso.size() < 16)
            return iso;
        return iso.substr(0, 10) + " " + iso.substr(11, 5) + " UTC";
    }

    std::string ElementName(collection::Element element)
    {
        static const char* const Names[] = { "Air", "Earth", "Fire", "Water", "Magic", "Tech", "Life", "Undead", "Light", "Dark", "No element" };
        return Names[std::min<size_t>(size_t(element), 10)];
    }

    unsigned ElementInk(collection::Element element)
    {
        if (size_t(element) < 8)
            return Elements[size_t(element)];
        return element == collection::Element::Light ? 0xE9D96A : element == collection::Element::Dark ? 0x7A6AA8 : 0x8A8A96;
    }

    bool HasProgress(const Figure& figure)
    {
        return figure.toy != nullptr && figure.toy->category <= Category::Mini;
    }

    namespace
    {
        std::vector<std::string> SplitList(const std::string& list)
        {
            std::vector<std::string> items;
            for (size_t begin = 0; begin < list.size();)
            {
                size_t end = std::min(list.find(',', begin), list.size());
                if (end > begin)
                    items.push_back(list.substr(begin, end - begin));
                begin = end + 1;
            }
            return items;
        }

        // "AB" for "Anvil Rain", "G" for "Gill"...: the letters written over a generated icon.
        std::string Initials(const std::string& name)
        {
            std::string out;
            bool start = true;
            for (char c : name)
            {
                if (out.size() >= 2)
                    break;
                if (start && isalpha(static_cast<unsigned char>(c)))
                    out += char(toupper(static_cast<unsigned char>(c)));
                start = c == ' ' || c == '-';
            }
            return out;
        }

        const char* GroupLabel(const std::string& id)
        {
            static const std::pair<const char*, const char*> Labels[] = { { "smart", "Smart" }, { "element", "Element" }, { "game", "Game" },
                { "category", "Category" }, { "series", "Series and variants" }, { "level", "Level" }, { "name", "Name" } };
            for (auto& [key, label] : Labels)
                if (id == key)
                    return label;
            return "Smart";
        }
    }

    // ---- Widgets (App's, here since the collection was their first user) --------------------------------------------

    bool App::Chip(const char* label, bool selected, unsigned color)
    {
        ImVec2 at = ImGui::GetCursorScreenPos();
        const char* end = strstr(label, "##");
        ImVec2 text = ImGui::CalcTextSize(label, end);
        ImVec2 size(text.x + 26, text.y + 14);
        bool pressed = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
        bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        unsigned fill = selected ? color : hovered ? PanelRaisedHover : PanelRaised;
        draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), Rgb(fill, selected ? 255 : 235), size.y / 2);
        if (!selected)
            draw->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), Rgb(Border), size.y / 2);
        ImU32 ink = Rgb(selected ? AccentInk : Text);
        if (selected)
            AddBoldText(draw, ImVec2(at.x + 13, at.y + 7), ink, std::string(label, end != nullptr ? end : label + strlen(label)).c_str());
        else
            draw->AddText(ImVec2(at.x + 13, at.y + 7), ink, label, end);
        FocusRing(size.y / 2);
        return pressed;
    }

    void App::Badge(ImDrawList* draw, ImVec2 at, const char* text, unsigned fill, unsigned ink)
    {
        ImGui::PushFont(nullptr, SmallSize - 1);
        ImVec2 size = ImGui::CalcTextSize(text);
        draw->AddRectFilled(at, ImVec2(at.x + size.x + 12, at.y + size.y + 4), Rgb(fill), (size.y + 4) / 2);
        AddBoldText(draw, ImVec2(at.x + 6, at.y + 2), Rgb(ink), text);
        ImGui::PopFont();
    }

    // ---- Icons ----------------------------------------------------------------------------------------------------

    void CollectionPage::ClearIcons()
    {
        for (auto& [name, icon] : m_icons)
            if (icon.texture != nullptr)
                SDL_DestroyTexture(icon.texture);
        m_icons.clear();
    }

    const FigureIconTexture& CollectionPage::Icon(const collection::Toy* toy, collection::VariantKind kind, bool large)
    {
        const figures::ToyInfo* ssa = toy != nullptr ? figures::FindToy(toy->type) : nullptr;
        std::string image = ssa == nullptr ? std::string() : large ? figures::PortraitLargeImageName(*ssa) : figures::PortraitImageName(*ssa);
        if (image.empty() && ssa != nullptr)
            image = figures::PortraitImageName(*ssa);
        std::string key = image.empty() ? fmt::format("gen:{}:{}", toy != nullptr ? toy->type : 0xFFFF, int(kind)) : image;
        auto found = m_icons.find(key);
        if (found != m_icons.end())
            return found->second;

        // The launcher's cache of the disc's pictures, else the game's (docs/collection.md), else a generated icon.
        FigureIconTexture icon;
        if (std::string path = m_app.m_s->assets.FigurePortraitPath(image); !path.empty())
            icon.texture = LoadPngFile(m_app.m_renderer, path);
        if (icon.texture == nullptr && toy != nullptr)
            if (std::string path = collection::DiscImagePath(*toy); !path.empty())
                icon.texture = LoadPngFile(m_app.m_renderer, path);
        if (icon.texture == nullptr)
        {
            constexpr int Size = 128;
            icon.texture = LoadRgba(m_app.m_renderer, figures::GenerateIcon(toy, kind, Size), Size, Size);
            icon.generated = true;
        }
        return m_icons[key] = icon;
    }

    void CollectionPage::DrawIcon(ImDrawList* draw, const collection::Toy* toy, collection::VariantKind kind, ImVec2 at, float size, bool large)
    {
        const FigureIconTexture& icon = Icon(toy, kind, large);
        if (icon.texture != nullptr)
            draw->AddImage(Ref(icon.texture), at, ImVec2(at.x + size, at.y + size));
        if (!icon.generated)
            return;
        std::string letters = Initials(toy != nullptr ? toy->name : "?");
        ImGui::PushFont(m_app.m_titleFont, size * 0.3f);
        ImVec2 text = ImGui::CalcTextSize(letters.c_str());
        ImVec2 position(at.x + (size - text.x) / 2, at.y + (size - text.y) / 2);
        draw->AddText(ImVec2(position.x + 1, position.y + 2), IM_COL32(0, 0, 0, 120), letters.c_str());
        draw->AddText(position, IM_COL32_WHITE, letters.c_str());
        ImGui::PopFont();
    }

    // ---- Data -----------------------------------------------------------------------------------------------------

    std::string CollectionPage::Folder() const
    {
        return collection::Folder();
    }

    void CollectionPage::Toast(const std::string& text)
    {
        m_app.Toast(text);
    }

    void CollectionPage::Open()
    {
        m_opened = true;
        collection::ClearStaleLocks();
        // One folder with the game: the old per-game folder is copied in (never moved), once per file.
        if (std::string legacy = collection::LegacyFolder(); !legacy.empty())
        {
            collection::Migration migration = collection::Migrate(legacy);
            if (migration.copied > 0)
                Toast(fmt::format("{} figure{} copied into the shared collection", migration.copied, migration.copied == 1 ? "" : "s"));
        }
        // The launcher's own favourites and portal list, from before the shared collection, move once.
        if (SettingGet(m_app.m_config, "collection", "moved") != "1")
        {
            if (const collection::Game* game = m_model.Game())
            {
                std::vector<std::string> seats = MoveLauncherSettings(*game, collection::LegacyFolder(),
                    SplitList(SettingGet(m_app.m_config, "collection", "favourites")), SplitList(SettingGet(m_app.m_config, "portal", "place")));
                if (!seats.empty())
                    WriteSeats(SessionFile(*game), seats);
            }
            m_app.Set("collection", "moved", "1");
            // The seats are the session file now: a [portal] place would override it at the game's start.
            m_app.Set("portal", "place", "");
        }
        collection::PurgeTrash(30);
        m_view = collection::GetView(m_model.gameId, "launcher");
        Reload();
    }

    void CollectionPage::Reload()
    {
        m_model.Load();
        m_generation++;
    }

    // The figures of the focused seat's family, filtered by the View and the search, then cut into sections.
    void CollectionPage::Rebuild()
    {
        const collection::Game* game = m_model.Game();
        std::string key = fmt::format("{}|{}|{}|{}|{}|{}|{}|{}|{}", m_generation, m_search, m_seat, m_view.group, m_view.order, m_view.show,
            m_view.foldForeign, m_foreignOpen, m_model.gameId);
        if (key == m_listKey)
            return;
        m_listKey = key;
        std::string search = Lower(m_search);
        std::vector<std::string> onPortal = m_model.OnPortal();
        m_list.clear();
        m_hiddenStarters = 0;
        for (const Figure& figure : m_model.figures)
        {
            bool seated = std::find(onPortal.begin(), onPortal.end(), figure.path) != onPortal.end();
            if (!seated && collection::UntouchedStarter(figure))
            {
                m_hiddenStarters++; // the game made it on its own: not the player's (Manage > Tidy)
                continue;
            }
            if (game != nullptr && m_seat < game->seats.size() && !SeatAccepts(game->seats[m_seat], figure))
                continue;
            if ((m_view.show == "favourites" && !figure.favourite) || (m_view.show == "played" && figure.state == figures::AreaState::Blank) ||
                (m_view.show == "new" && figure.state != figures::AreaState::Blank))
                continue;
            if (!search.empty() && Lower(Name(figure) + "\n" + figure.variantInfo.name + "\n" + figure.file).find(search) == std::string::npos)
                continue;
            m_list.push_back(figure);
        }
        m_sections = collection::Group(m_list, collection::ParseGroupBy(m_view.group), collection::ParseOrderBy(m_view.order), m_model.gameId, onPortal);
        if (!m_view.foldForeign)
            m_sections.erase(std::remove_if(m_sections.begin(), m_sections.end(), [](const Section& s) { return s.kind == Section::Kind::Foreign; }),
                m_sections.end());
    }

    std::string CollectionPage::Name(const Figure& figure) const
    {
        if (!figure.label.empty())
            return figure.label;
        if (std::string nickname = figures::NicknameUtf8(figure.stats); !nickname.empty())
            return nickname;
        return figure.DisplayName();
    }

    std::string CollectionPage::Detail(const Figure& figure) const
    {
        if (figure.toy == nullptr)
            return fmt::format("Toy type {}", figure.type);
        if (!HasProgress(figure))
            return collection::CategoryName(figure.toy->category);
        if (figure.state == figures::AreaState::Valid)
            return fmt::format("Level {} · {} gold", figure.level, figure.stats.gold);
        return figure.state == figures::AreaState::Blank ? "New" : "Progress unreadable";
    }

    std::string CollectionPage::SeatName(size_t seat) const
    {
        const collection::Game* game = m_model.Game();
        if (game == nullptr || seat >= game->seats.size())
            return {};
        const std::string& id = game->seats[seat].id;
        if (id == "p1" || id == "p2")
            return "Player " + id.substr(1);
        if (id == "item")
            return "Magic item";
        if (id == "adventure")
            return "Adventure";
        if (id == "trap")
            return "Trap";
        if (id.rfind("vehicle", 0) == 0)
            return "Vehicle " + id.substr(7);
        return id;
    }

    std::string CollectionPage::SectionName(const Section& section) const
    {
        switch (section.kind)
        {
        case Section::Kind::Portal: return "On the portal";
        case Section::Kind::Recent: return "Recent";
        case Section::Kind::Favourites: return "Favourites";
        case Section::Kind::Element: return ElementName(collection::Element(section.value));
        case Section::Kind::Game:
            if (const collection::Game* game = collection::Catalogue::Get().FindGame(section.game))
                return game->name;
            return section.game;
        case Section::Kind::Category: return collection::CategoryName(Category(section.value));
        case Section::Kind::Series: return section.value == 0 ? "Standard" : collection::VariantKindName(collection::VariantKind(section.value));
        case Section::Kind::Level: return fmt::format("Level {}", section.value);
        case Section::Kind::Letter: return std::string(1, char(section.value));
        case Section::Kind::Unknown: return "Unknown toys";
        case Section::Kind::Foreign: return "Not for " + ShortGameName(m_model.gameId);
        }
        return {};
    }

    std::string CollectionPage::LiveGame(const Figure& figure) const
    {
        return figure.lock.Busy() ? ShortGameName(figure.lock.game) : std::string();
    }

    bool CollectionPage::Writable(const Figure& figure) const
    {
        return !figure.lock.Busy();
    }

    // ---- Actions --------------------------------------------------------------------------------------------------

    void CollectionPage::OpenFigure(const std::string& path)
    {
        m_figure = path;
        m_overlay = Overlay::Figure;
        m_editing = m_renaming = false;
        m_confirm.clear();
        if (const Figure* figure = m_model.Find(path))
            snprintf(m_label, sizeof(m_label), "%s", figure->label.c_str());
    }

    void CollectionPage::PutOn(size_t seat, const std::string& path)
    {
        const collection::Game* game = m_model.Game();
        const Figure* figure = m_model.Find(path);
        if (game == nullptr || figure == nullptr || seat >= game->seats.size())
            return;
        if (!m_model.CanEditSeats())
        {
            Toast(SessionFile(*game).empty() ? "This game has no runtime yet: its seats can't be prepared"
                                             : "The game is running: the seats change from its Toy Box");
            return;
        }
        if (figure->toy != nullptr && !collection::Catalogue::Get().Accepts(m_model.gameId, figure->type))
            Toast(fmt::format("{} is not for {}", Name(*figure), ShortGameName(m_model.gameId)));
        else if (!SeatAccepts(game->seats[seat], *figure))
            Toast(fmt::format("{} doesn't go on the {} seat", Name(*figure), SeatName(seat)));
        else if (figure->lock.Busy())
            Toast(fmt::format("{} is in use by {}", Name(*figure), LiveGame(*figure)));
        else if (m_model.SetSeat(seat, path))
        {
            m_generation++;
            Toast(fmt::format("{}: {}", SeatName(seat), Name(*figure)));
        }
        else
            Toast("Couldn't write the next session's seats");
    }

    bool CollectionPage::Drop(const std::string& path)
    {
        std::error_code error;
        if (fs::is_directory(path, error))
        {
            Import({ path });
            return true;
        }
        if (!collection::IsDumpName(fs::path(path).filename().string()))
            return false;
        Import({ path });
        return true;
    }

    void CollectionPage::Import(const std::vector<std::string>& paths)
    {
        if (!m_opened)
            Open();
        collection::ImportReport report = collection::Import(paths);
        Reload();
        std::string text = fmt::format("Imported {}", report.added);
        if (report.duplicates > 0)
            text += fmt::format(" · {} already there", report.duplicates);
        if (report.invalid > 0)
            text += fmt::format(" · {} not a figure", report.invalid);
        if (!report.unknown.empty())
            text += fmt::format(" · {} unknown toy{} (Manage)", report.unknown.size(), report.unknown.size() == 1 ? "" : "s");
        Toast(text);
    }

    void CollectionPage::CloseOverlay()
    {
        m_overlay = Overlay::None;
        m_editing = m_renaming = false;
        ImGui::CloseCurrentPopup();
    }

    // ---- The page -------------------------------------------------------------------------------------------------

    void CollectionPage::Draw()
    {
        if (!m_opened)
            Open();
        if (m_model.Poll(SDL_GetTicks()))
            m_generation++; // a game wrote a dump, took or released a figure, or quit
        Rebuild();

        float available = ImGui::GetContentRegionAvail().x;
        float width = std::min(available, 1180.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) / 2);
        ImGui::BeginChild("##collectioncolumn", ImVec2(width, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        DrawHeader(width);

        constexpr float SeatsW = 236, Gap = 16;
        ImGui::BeginChild("##seats", ImVec2(SeatsW, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        DrawSeats();
        ImGui::EndChild();
        ImGui::SameLine(0, Gap);
        ImGui::BeginChild("##mylist", ImVec2(0, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        DrawList();
        ImGui::EndChild();
        // A figure dragged off a seat and dropped on the list leaves the seat.
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SKYSEAT"))
                TakeOff(*static_cast<const size_t*>(payload->Data));
            ImGui::EndDragDropTarget();
        }
        ImGui::EndChild();

        HandleShortcuts();
        DrawOverlay();
    }

    void CollectionPage::DrawHeader(float width)
    {
        ImVec2 top = ImGui::GetCursorScreenPos();
        m_app.Title("COLLECTION", HeadlineSize, Text);
        size_t favourites = std::count_if(m_model.figures.begin(), m_model.figures.end(), [](const Figure& f) { return f.favourite; });
        std::string summary = fmt::format("{} figure{} · {} favourite{}", m_model.figures.size() - m_hiddenStarters, m_model.figures.size() - m_hiddenStarters == 1 ? "" : "s",
            favourites, favourites == 1 ? "" : "s");
        if (m_hiddenStarters > 0)
            summary += fmt::format(" · {} untouched starters hidden (Manage)", m_hiddenStarters);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", summary.c_str());
        ImGui::SameLine(0, 16);
        m_app.DrawPortalStatus();
        ImVec2 after = ImGui::GetCursorScreenPos();

        float buttonsW = 190 + 120 + 110 + 2 * 10;
        ImGui::SetCursorScreenPos(ImVec2(top.x + width - buttonsW, top.y + 4));
        if (m_app.PillButton("+  Add a figure", true, 190, true))
        {
            m_overlay = Overlay::Add;
            m_addToy = 0xFFFF;
            m_created.clear();
            m_addGame = m_model.gameId;
        }
        m_app.TakeFocus(FocusTo::Page);
        ImGui::SameLine(0, 10);
        if (m_app.PillButton("Manage", false, 120, true))
            m_overlay = Overlay::Manage;
        ImGui::SameLine(0, 10);
        ImVec2 viewAt = ImGui::GetCursorScreenPos();
        if (m_app.PillButton("View", m_viewMenu, 110, true))
        {
            m_viewMenu = !m_viewMenu;
            m_viewFocus = true;
        }
        if (m_viewFocus && !m_viewMenu)
        {
            // The menu closed: the focus comes back to its button.
            ImGui::SetWindowFocus();
            ImGui::FocusItem();
            m_viewFocus = false;
        }
        ImGui::SetCursorScreenPos(ImVec2(after.x, after.y + 8));
        if (m_viewMenu)
            DrawViewMenu(ImVec2(viewAt.x + 110, viewAt.y + 46));
    }

    void CollectionPage::DrawViewMenu(ImVec2 anchor)
    {
        ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(1, 0));
        ImGui::SetNextWindowSize(ImVec2(470, 0));
        if (m_viewFocus)
        {
            ImGui::SetNextWindowFocus(); // the pad can use it as soon as it opens (Y)
            m_viewFocus = false;
        }
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Rgb(Panel, 252));
        ImGui::PushStyleColor(ImGuiCol_Border, Rgb(BorderBright));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Radius);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
        ImGui::Begin("##viewmenu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        bool changed = false;
        auto row = [&](const char* label, auto&& choices) {
            ImGui::PushFont(nullptr, SmallSize);
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", label);
            ImGui::PopFont();
            float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            bool first = true;
            for (auto& [id, name] : choices)
            {
                std::string text = fmt::format("{}##{}{}", name, label, id);
                float w = ImGui::CalcTextSize(name).x + 26;
                if (!first)
                {
                    ImGui::SameLine(0, 6);
                    if (ImGui::GetCursorScreenPos().x + w > right)
                        ImGui::NewLine();
                }
                first = false;
                if (m_app.Chip(text.c_str(), choices.selected(id)))
                {
                    choices.set(id);
                    changed = true;
                }
            }
            ImGui::Dummy(ImVec2(0, 6));
        };
        struct Choices
        {
            std::vector<std::pair<std::string, const char*>> items;
            std::string* value;
            auto begin() const { return items.begin(); }
            auto end() const { return items.end(); }
            bool selected(const std::string& id) const { return *value == id; }
            void set(const std::string& id) const { *value = id; }
        };
        row("Group by", Choices{ { { "smart", "Smart" }, { "element", "Element" }, { "game", "Game" }, { "category", "Category" },
            { "series", "Series and variants" }, { "level", "Level" }, { "name", "Name" } }, &m_view.group });
        row("Order inside a group", Choices{ { { "recent", "Recent" }, { "level", "Level" }, { "name", "Name" }, { "toy", "Toy number" } }, &m_view.order });
        row("Show", Choices{ { { "all", "Everything" }, { "favourites", "Favourites" }, { "played", "Played" }, { "new", "Never played" } }, &m_view.show });
        ImGui::PushFont(nullptr, SmallSize);
        ImGui::TextColored(ImColor(Rgb(TextDim)), "Not for %s", ShortGameName(m_model.gameId).c_str());
        ImGui::PopFont();
        if (m_app.Chip("Folded at the end##fold", m_view.foldForeign))
        {
            m_view.foldForeign = true;
            changed = true;
        }
        ImGui::SameLine(0, 6);
        if (m_app.Chip("Hidden##hide", !m_view.foldForeign))
        {
            m_view.foldForeign = false;
            changed = true;
        }
        ImGui::Dummy(ImVec2(0, 6));
        if (m_app.PillButton("Back to the default", false, 200))
        {
            m_view = collection::View{};
            changed = true;
        }
        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImColor(Rgb(TextDim)), "Saved for %s", ShortGameName(m_model.gameId).c_str());
        if (changed)
        {
            collection::SetView(m_model.gameId, "launcher", m_view);
            m_generation++;
        }
        // A click outside, or Escape, closes it.
        bool inside = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) ||
            (ImGui::IsMouseClicked(0) && !inside && !ImGui::IsMouseHoveringRect(ImVec2(anchor.x - 110, anchor.y - 46), ImVec2(anchor.x, anchor.y))))
        {
            m_viewMenu = false;
            m_viewFocus = true;
        }
        ImGui::End();
    }

    // Empties a seat: the figure leaves the next session's portal (and stays in the collection).
    void CollectionPage::TakeOff(size_t seat)
    {
        const collection::Game* game = m_model.Game();
        if (game == nullptr || seat >= game->seats.size() || m_model.seats[seat].empty())
            return;
        std::string what = m_model.seats[seat];
        const Figure* figure = m_model.Find(what);
        if (!m_model.CanEditSeats())
            Toast("The game is running: the seats change from its Toy Box");
        else if (m_model.SetSeat(seat, ""))
        {
            m_generation++;
            Toast(fmt::format("{}: {} taken off", SeatName(seat), figure != nullptr ? Name(*figure) : std::string("figure")));
        }
        else
            Toast("Couldn't write the next session's seats");
    }

    void CollectionPage::DrawSeatMenu()
    {
        if (!ImGui::BeginPopup("##seatmenu"))
            return;
        size_t seat = size_t(m_seatMenu);
        const Figure* figure = seat < m_model.seats.size() && !m_model.seats[seat].empty() ? m_model.Find(m_model.seats[seat]) : nullptr;
        if (seat >= m_model.seats.size() || m_model.seats[seat].empty())
            ImGui::CloseCurrentPopup();
        else
        {
            if (ImGui::MenuItem("Take it off the seat", "Delete", false, m_model.CanEditSeats()))
                TakeOff(seat);
            if (ImGui::MenuItem("Figure page", nullptr, false, figure != nullptr))
                OpenFigure(figure->path);
        }
        ImGui::EndPopup();
    }

    void CollectionPage::DrawSeats()
    {
        const collection::Game* game = m_model.Game();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        m_app.BoldText("PORTAL FOR THE NEXT SESSION", TextDim);

        // The game whose seats are prepared; only games with a runtime can be (the others: seats shown, not saved).
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 6));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##seatgame", ShortGameName(m_model.gameId).c_str()))
        {
            for (const collection::Game& g : collection::Catalogue::Get().Games())
                if (ImGui::Selectable(ShortGameName(g.id).c_str(), g.id == m_model.gameId))
                {
                    m_model.gameId = g.id;
                    m_seat = 0;
                    m_view = collection::GetView(g.id, "launcher");
                    Reload();
                }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0, 4));
        if (game == nullptr)
            return;

        std::string live = m_model.RunningGame();
        bool editable = m_model.CanEditSeats();
        m_navSeat = m_hoverSeat = -1;
        for (size_t i = 0; i < game->seats.size(); i++)
        {
            constexpr float H = 78;
            ImVec2 at = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            ImGui::PushID(int(i));
            if (ImGui::InvisibleButton("##seat", ImVec2(w, H), ImGuiButtonFlags_EnableNav))
            {
                m_seat = i;
                m_generation++;
            }
            if (m_focusSeat == int(i))
            {
                ImGui::FocusItem();
                ImGui::SetNavCursorVisible(true);
                m_focusSeat = -1;
            }
            bool hovered = ImGui::IsItemHovered();
            bool nav = ImGui::IsItemFocused();
            m_app.FocusRing(Radius);
            // Dropping a card on a seat puts the figure there.
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SKYFIG"))
                    PutOn(i, std::string(static_cast<const char*>(payload->Data)));
                ImGui::EndDragDropTarget();
            }
            const Figure* figure = m_model.seats[i].empty() ? nullptr : m_model.Find(m_model.seats[i]);
            bool occupied = !m_model.seats[i].empty();
            if (nav)
                m_navSeat = int(i);
            if (hovered)
                m_hoverSeat = int(i);
            // Dragging a figure off its seat, onto the list, takes it off.
            if (occupied && editable && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                size_t index = i;
                ImGui::SetDragDropPayload("SKYSEAT", &index, sizeof(index));
                ImGui::TextUnformatted(figure != nullptr ? Name(*figure).c_str() : "Take it off the seat");
                ImGui::EndDragDropSource();
            }
            bool menu = ImGui::IsItemClicked(ImGuiMouseButton_Right) && occupied;
            ImGui::PopID();
            if (menu) // outside the seat's ID scope: the popup is drawn by DrawSeatMenu at the page's level
            {
                m_seatMenu = int(i);
                ImGui::OpenPopup("##seatmenu");
            }
            bool focused = m_seat == i;
            draw->AddRectFilled(at, ImVec2(at.x + w, at.y + H), Rgb(hovered || nav ? PanelRaised : Panel, 240), Radius);
            draw->AddRect(at, ImVec2(at.x + w, at.y + H), Rgb(focused ? Accent : hovered ? BorderBright : Border), Radius, 0, focused ? 2.5f : 1.0f);
            ImGui::PushFont(nullptr, SmallSize - 1);
            draw->AddText(ImVec2(at.x + 76, at.y + 9), Rgb(focused ? Accent : TextDim), Lower(SeatName(i)).c_str());
            ImGui::PopFont();
            if (occupied)
            {
                if (figure != nullptr)
                {
                    DrawIcon(draw, figure->toy, figure->Kind(), ImVec2(at.x + 10, at.y + 10), 58, false);
                    AddBoldText(draw, ImVec2(at.x + 76, at.y + 28), Rgb(Text), LauncherMiddleEllipsis(Name(*figure), 15).c_str());
                    ImGui::PushFont(nullptr, SmallSize);
                    draw->AddText(ImVec2(at.x + 76, at.y + 52), Rgb(TextDim), Detail(*figure).c_str());
                    ImGui::PopFont();
                }
                else
                {
                    // A figure the collection no longer has (deleted, moved): the seat can still be emptied.
                    draw->AddRectFilled(ImVec2(at.x + 10, at.y + 10), ImVec2(at.x + 68, at.y + 68), Rgb(Field), Radius);
                    AddBoldText(draw, ImVec2(at.x + 76, at.y + 28), Rgb(Text), "Missing figure");
                    ImGui::PushFont(nullptr, SmallSize);
                    draw->AddText(ImVec2(at.x + 76, at.y + 52), Rgb(TextDim), LauncherMiddleEllipsis(m_model.seats[i].substr(m_model.seats[i].find_last_of("/\\") + 1), 18).c_str());
                    ImGui::PopFont();
                }
                if (figure != nullptr && figure->lock.Busy())
                    m_app.Badge(draw, ImVec2(at.x + w - 62, at.y + 6), "LIVE", Ok, AccentInk);
                else if (editable)
                {
                    ImVec2 cross(at.x + w - 20, at.y + 18);
                    ImGui::SetCursorScreenPos(ImVec2(cross.x - 12, cross.y - 12));
                    ImGui::PushID(int(i) + 100);
                    bool off = ImGui::InvisibleButton("##off", ImVec2(24, 24)); // the pad takes it off with X on the seat
                    bool over = ImGui::IsItemHovered();
                    ImGui::PopID();
                    ImU32 ink = Rgb(over ? Text : TextDim);
                    draw->AddLine(ImVec2(cross.x - 5, cross.y - 5), ImVec2(cross.x + 5, cross.y + 5), ink, 2);
                    draw->AddLine(ImVec2(cross.x - 5, cross.y + 5), ImVec2(cross.x + 5, cross.y - 5), ink, 2);
                    if (over)
                        ImGui::SetTooltip("Take it off the seat");
                    if (off)
                        TakeOff(i);
                }
            }
            else
            {
                draw->AddRectFilled(ImVec2(at.x + 10, at.y + 10), ImVec2(at.x + 68, at.y + 68), Rgb(Field), Radius);
                draw->AddRect(ImVec2(at.x + 10, at.y + 10), ImVec2(at.x + 68, at.y + 68), Rgb(Border), Radius);
                ImGui::PushFont(nullptr, SmallSize);
                draw->AddText(ImVec2(at.x + 76, at.y + 32), Rgb(TextSecondary), focused ? "Pick a figure below" : "Empty");
                ImGui::PopFont();
            }
            ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + H + 8));
        }

        DrawSeatMenu();

        // The portal at a glance: every occupied seat with its figure.
        std::vector<std::string> names(m_model.seats.size());
        for (size_t i = 0; i < names.size(); i++)
            if (const Figure* f = m_model.seats[i].empty() ? nullptr : m_model.Find(m_model.seats[i]))
                names[i] = Name(*f);
        std::string overview = collection::PortalOverviewLine(collection::PortalOverview(*game, m_model.seats, names), "Nothing on the portal");
        ImGui::PushFont(nullptr, SmallSize);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
        ImGui::TextWrapped("%s", overview.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4));

        ImGui::PushFont(nullptr, SmallSize);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(live.empty() ? TextDim : Ok));
        if (!live.empty())
            ImGui::TextWrapped("On the portal in %s. Read-only while the game runs; use its Toy Box to change a seat.", ShortGameName(live).c_str());
        else if (SessionFile(*game).empty())
            ImGui::TextWrapped("%s has no runtime yet: its seats can't be prepared.", ShortGameName(game->id).c_str());
        else
            ImGui::TextWrapped("Drag a figure onto a seat, or double-click it. Take one off with its cross, Delete, X, or by dragging it out. The game starts with these.");
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    bool CollectionPage::DrawCard(const Figure& figure, float width, bool dim)
    {
        ImVec2 at = ImGui::GetCursorScreenPos();
        float height = width + 44;
        ImGui::PushID(figure.path.c_str());
        bool pressed = ImGui::InvisibleButton("##figure", ImVec2(width, height), ImGuiButtonFlags_EnableNav);
        if (m_focusCard)
        {
            ImGui::FocusItem();
            ImGui::SetNavCursorVisible(true);
            m_focusCard = false;
        }
        bool hovered = ImGui::IsItemHovered(), focused = ImGui::IsItemFocused();
        m_app.FocusRing(Radius);
        bool doubleClick = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        bool rightClick = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        if (hovered || focused)
            m_hovered = figure.path;
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            ImGui::SetDragDropPayload("SKYFIG", figure.path.c_str(), figure.path.size() + 1);
            ImGui::Text("%s", Name(figure).c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
        if (pressed)
        {
            m_selected = figure.path;
            // Enter or A on a focused card (no mouse release this frame) puts it on the seat.
            if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !dim)
                PutOn(m_seat, figure.path);
        }
        if (doubleClick && !dim)
            PutOn(m_seat, figure.path);
        if (rightClick)
            OpenFigure(figure.path);
        if (!ImGui::IsItemVisible()) // scrolled out of view: a big collection draws only what shows
            return pressed;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 end(at.x + width, at.y + height);
        int seat = m_model.SeatOf(figure.path);
        bool selected = m_selected == figure.path || focused;
        draw->AddRectFilled(at, end, Rgb(hovered ? PanelRaised : Panel, 240), Radius);
        draw->AddRect(at, end, Rgb(seat >= 0 ? Accent : selected ? BorderBright : hovered ? BorderBright : Border), Radius, 0, seat >= 0 || selected ? 2.5f : 1.0f);
        float icon = width - 28;
        DrawIcon(draw, figure.toy, figure.Kind(), ImVec2(at.x + 14, at.y + 10), icon, false);

        std::string name = LauncherMiddleEllipsis(Name(figure), 13);
        ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
        AddBoldText(draw, ImVec2(at.x + std::max(6.0f, (width - nameSize.x) / 2), at.y + icon + 14), Rgb(Text), name.c_str());
        std::string detail = Detail(figure);
        ImGui::PushFont(nullptr, SmallSize);
        ImVec2 detailSize = ImGui::CalcTextSize(detail.c_str());
        draw->AddText(ImVec2(at.x + std::max(6.0f, (width - detailSize.x) / 2), at.y + icon + 34), Rgb(TextDim), detail.c_str());
        ImGui::PopFont();

        // Marks: the variant, favourite, seat, lock.
        collection::VariantKind kind = figure.Kind();
        if (kind != collection::VariantKind::Base)
        {
            uint32_t rgb = collection::VariantKindRgb(kind);
            bool light = ((rgb >> 16 & 0xFF) * 3 + (rgb >> 8 & 0xFF) * 6 + (rgb & 0xFF)) / 10 > 120;
            m_app.Badge(draw, ImVec2(at.x + 6, at.y + 6), collection::VariantKindName(kind), rgb, light ? AccentInk : Text);
        }
        if (figure.favourite)
            DrawStar(draw, ImVec2(end.x - 15, at.y + 16), 9, Rgb(0xFFD54A));
        if (seat >= 0)
            m_app.Badge(draw, ImVec2(at.x + 6, at.y + icon - 8), SeatName(size_t(seat)).substr(0, 8) == "Player 1" ? "P1" : SeatName(size_t(seat)) == "Player 2" ? "P2" : "SEAT", Accent, AccentInk);
        if (figure.lock.Busy())
        {
            draw->AddRectFilled(at, end, IM_COL32(0, 0, 0, 110), Radius);
            m_app.Badge(draw, ImVec2(at.x + (width - 66) / 2, at.y + icon / 2), figure.lock.game == m_model.gameId ? "ON PORTAL" : "IN USE", Danger, Text);
        }
        else if (dim)
            draw->AddRectFilled(at, end, IM_COL32(20, 20, 26, 150), Radius);
        else if (figure.state == figures::AreaState::Blank && HasProgress(figure))
            m_app.Badge(draw, ImVec2(end.x - 46, at.y + icon - 8), "new", PanelRaised, Text);
        if (hovered)
        {
            std::string tip = Name(figure) + "\n" + figure.variantInfo.name + "\n" + figure.file;
            if (figure.lock.Busy())
                tip += "\nOn the portal in " + LiveGame(figure) + ": read-only";
            else if (dim)
                tip += "\nNot for " + ShortGameName(m_model.gameId);
            ImGui::SetTooltip("%s", tip.c_str());
        }
        return pressed;
    }

    void CollectionPage::DrawList()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 8));
        ImGui::SetNextItemWidth(260);
        ImGui::InputTextWithHint("##search", "Search figures, nicknames, files", m_search, sizeof(m_search));
        ImGui::PopStyleVar();
        ImGui::SameLine(0, 12);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
        ImGui::TextColored(ImColor(Rgb(TextDim)), "For: %s · view: %s", SeatName(m_seat).c_str(), GroupLabel(m_view.group));

        // The sections as a strip: click to jump (LB / RB too).
        if (m_sections.size() > 1)
        {
            ImGui::Dummy(ImVec2(0, 2));
            float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            for (size_t i = 0; i < m_sections.size(); i++)
            {
                std::string label = fmt::format("{}##strip{}", SectionName(m_sections[i]), i);
                float w = ImGui::CalcTextSize(SectionName(m_sections[i]).c_str()).x + 26;
                if (i > 0)
                {
                    ImGui::SameLine(0, 6);
                    if (ImGui::GetCursorScreenPos().x + w > right)
                        ImGui::NewLine();
                }
                if (m_app.Chip(label.c_str(), int(i) == m_currentSection))
                    m_jumpTo = int(i);
            }
        }
        ImGui::Dummy(ImVec2(0, 4));

        ImGui::BeginChild("##sections", ImVec2(0, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        m_hovered.clear();
        m_navEdgeCard = false;
        if (m_list.empty())
        {
            ImGui::Dummy(ImVec2(0, 30));
            bool none = m_model.figures.size() == size_t(m_hiddenStarters);
            m_app.Title(none ? "My Skylanders is empty" : "No figure here", TitleSize, Text);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
            ImGui::TextWrapped("%s", none ? "Add a figure from the catalogue, import your own dumps (.sky, .bin, .dmp: drop them on the window), or start with the three Skylanders of the starter pack."
                                          : "Nothing matches this seat, the view or the search.");
            ImGui::PopStyleColor();
            if (none)
            {
                ImGui::Dummy(ImVec2(0, 6));
                if (m_app.PillButton("Start with Spyro, Gill Grunt, Trigger Happy", true, 380, true))
                {
                    for (uint16_t type : { 16, 14, 19 })
                        collection::Create(type);
                    Reload();
                }
            }
            ImGui::EndChild();
            return;
        }

        constexpr float Cell = 116, Gap = 10;
        int columns = std::max(1, int((ImGui::GetContentRegionAvail().x + Gap) / (Cell + Gap)));
        float scroll = ImGui::GetScrollY();
        std::vector<float> lastY = m_sectionY; // where the sections were last frame: a jump scrolls to them
        m_sectionY.assign(m_sections.size(), 0);
        for (size_t s = 0; s < m_sections.size(); s++)
        {
            const Section& section = m_sections[s];
            m_sectionY[s] = ImGui::GetCursorPosY();
            if (m_jumpTo == int(s) && s < lastY.size())
            {
                ImGui::SetScrollY(lastY[s]);
                m_currentSection = int(s);
                m_jumpTo = -1;
            }
            bool foreign = section.kind == Section::Kind::Foreign;
            std::string title = fmt::format("{}  {}", SectionName(section), section.items.size());
            if (foreign)
            {
                if (m_app.Chip(fmt::format("{} {}##foreign", m_foreignOpen ? "v" : ">", title).c_str(), m_foreignOpen))
                {
                    m_foreignOpen = !m_foreignOpen;
                    m_generation++;
                }
                ImGui::SameLine(0, 10);
                ImGui::TextColored(ImColor(Rgb(TextDim)), "greyed: this game can't read them");
                if (!m_foreignOpen)
                    continue;
            }
            else
                m_app.BoldText(title.c_str(), section.kind == Section::Kind::Portal ? Accent : TextSecondary);
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::PushID(int(s)); // a figure can be in two sections (On the portal, Recent): one ID per card
            for (size_t i = 0; i < section.items.size(); i++)
            {
                if (i % columns != 0)
                    ImGui::SameLine(0, Gap);
                DrawCard(m_list[section.items[i]], Cell, foreign);
                if (i % columns == 0 && ImGui::IsItemFocused())
                    m_navEdgeCard = true;
            }
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, 12));
        }
        // The section the list is scrolled to.
        for (size_t s = 0; s < m_sectionY.size(); s++)
            if (m_sectionY[s] <= scroll + 12 && m_jumpTo < 0)
                m_currentSection = int(s);
        ImGui::EndChild();
    }

    void CollectionPage::HandleShortcuts()
    {
        if (m_overlay != Overlay::None || ImGui::IsAnyItemActive())
            return;
        auto pressed = [](ImGuiKey a, ImGuiKey b) { return ImGui::IsKeyPressed(a, false) || ImGui::IsKeyPressed(b, false); };
        // A seat with the focus (or under the mouse): Delete, and the pad's X, take its figure off.
        if (ImGui::IsPopupOpen("##seatmenu"))
            return;
        if (m_navSeat >= 0 && pressed(ImGuiKey_GamepadFaceLeft, ImGuiKey_Delete))
        {
            TakeOff(size_t(m_navSeat));
            return;
        }
        if (m_hoverSeat >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            TakeOff(size_t(m_hoverSeat));
            return;
        }
        // Left and right cross between the seats and the list (ImGui's nav, by distance, misses the gap between them).
        if (m_navSeat >= 0 && pressed(ImGuiKey_GamepadDpadRight, ImGuiKey_RightArrow) && !m_list.empty())
            m_focusCard = true;
        if (m_navEdgeCard && pressed(ImGuiKey_GamepadDpadLeft, ImGuiKey_LeftArrow))
            m_focusSeat = int(m_seat);
        if (m_sections.empty())
            return;
        int count = int(m_sections.size());
        if (pressed(ImGuiKey_GamepadL1, ImGuiKey_PageUp))
            m_jumpTo = std::max(0, m_currentSection - 1);
        if (pressed(ImGuiKey_GamepadR1, ImGuiKey_PageDown))
            m_jumpTo = std::min(count - 1, m_currentSection + 1);
        if (pressed(ImGuiKey_GamepadFaceUp, ImGuiKey_F3))
        {
            m_viewMenu = !m_viewMenu;
            m_viewFocus = true;
        }
        if (pressed(ImGuiKey_GamepadFaceLeft, ImGuiKey_F2))
        {
            const std::string& path = !m_hovered.empty() ? m_hovered : m_selected;
            if (!path.empty() && m_model.Find(path) != nullptr)
                OpenFigure(path);
        }
        if (pressed(ImGuiKey_Escape, ImGuiKey_GamepadFaceRight) && m_search[0] != 0)
            m_search[0] = 0;
    }
}

// The collection page: the portal at the next start along the top (the chosen game's seats), the bar (search, Element,
// Game, Type), the shelf below (every figure once, favourites first). The figures and their folder are the shared
// library's (src/shared/collection); the model is collection_model.cpp; the figure sheet, Add a figure and Manage are
// in collection_figure.cpp, collection_add.cpp and collection_manage.cpp.
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/launcher_ui.h"
#include "launcher/ui/text_fit.h"
#include "shared/collection/icons.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
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
        return seconds >= 3600 ? Tf("{} h {:02} min", seconds / 3600, seconds / 60 % 60) : Tf("{} min", seconds / 60);
    }

    std::string ShortDate(const std::string& iso)
    {
        if (iso.size() < 16)
            return iso;
        return iso.substr(0, 10) + " " + iso.substr(11, 5) + " UTC";
    }

    std::string ElementName(collection::Element element)
    {
        static const char* const Names[] = { N_("Air"), N_("Earth"), N_("Fire"), N_("Water"), N_("Magic"), N_("Tech"), N_("Life"), N_("Undead"),
            N_("Light"), N_("Dark"), N_("No element") };
        return Tr(Names[std::min<size_t>(size_t(element), 10)]);
    }

    std::string CategoryLabel(collection::Category category)
    {
        static const char* const Names[] = { N_("Skylanders"), N_("Giants"), N_("Swap Force tops"), N_("Swap Force bottoms"), N_("Trap Masters"),
            N_("SuperChargers"), N_("Senseis"), N_("Minis and sidekicks"), N_("Traps"), N_("Villains"), N_("Vehicles"), N_("Creation Crystals"),
            N_("Magic Items"), N_("Adventure Packs"), N_("Trophies") };
        static_assert(std::size(Names) == size_t(collection::Category::Count));
        return category < collection::Category::Count ? Tr(Names[size_t(category)]) : std::string();
    }

    std::string KindLabel(collection::VariantKind kind)
    {
        static const char* const Names[] = { N_("Standard"), N_("Legendary"), N_("Dark"), N_("Crystal Clear"), N_("Glow in the Dark"),
            N_("Metallic"), N_("Gold"), N_("Silver"), N_("Chrome"), N_("Flocked"), N_("Series"), N_("LightCore"), N_("Eon's Elite"),
            N_("Alternate deco"), N_("Event and exclusive"), N_("Trap shape"), N_("Other") };
        static_assert(std::size(Names) == size_t(collection::VariantKind::Count));
        return kind < collection::VariantKind::Count ? Tr(Names[size_t(kind)]) : std::string();
    }

    bool French() { return i18n::Current() == "fr"; }

    unsigned ElementInk(collection::Element element)
    {
        if (size_t(element) < 8)
            return Elements[size_t(element)];
        return element == collection::Element::Light ? 0xE9D96A : element == collection::Element::Dark ? 0x7A6AA8 : 0x8A8A96;
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

        // The elements in the order the games' menus show them (the Element menu).
        constexpr collection::Element MenuElements[] = { collection::Element::Magic, collection::Element::Earth, collection::Element::Water,
            collection::Element::Air, collection::Element::Fire, collection::Element::Life, collection::Element::Undead, collection::Element::Tech,
            collection::Element::Light, collection::Element::Dark };
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
        // The picture of the game being shown, else the other game's, else a generated icon (shared/collection/icons.h).
        std::string path = toy != nullptr ? collection::PortraitPath(m_app.m_s->game.id, *toy, collection::VariantFor(*toy, kind), large) : std::string();
        std::string key = !path.empty() ? path : fmt::format("gen:{}:{}", toy != nullptr ? toy->type : 0xFFFF, int(kind));
        auto found = m_icons.find(key);
        if (found != m_icons.end())
            return found->second;

        FigureIconTexture icon;
        if (!path.empty())
            icon.texture = LoadPngFile(m_app.m_renderer, path);
        if (icon.texture != nullptr)
            SDL_GetTextureSize(icon.texture, &icon.width, &icon.height);
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
        // A drawn icon gets the toy's initials, unless its family has a mark of its own (an item's potion...).
        if (!icon.generated || (toy != nullptr && collection::HasKindGlyph(toy->category)))
            return;
        std::string letters = Initials(toy != nullptr ? toy->name : "?");
        ImGui::PushFont(m_app.m_titleFont, size * 0.3f);
        ImVec2 text = ImGui::CalcTextSize(letters.c_str());
        ImVec2 position(at.x + (size - text.x) / 2, at.y + (size - text.y) / 2);
        draw->AddText(ImVec2(position.x + 1, position.y + 2), IM_COL32(0, 0, 0, 120), letters.c_str());
        draw->AddText(position, IM_COL32_WHITE, letters.c_str());
        ImGui::PopFont();
    }

    void CollectionPage::DrawPicture(ImDrawList* draw, const collection::Toy* toy, collection::VariantKind kind, uint16_t variant, ImVec2 at, float box)
    {
        std::string path = toy != nullptr ? collection::BestPicturePath(m_app.m_s->game.id, *toy, variant) : std::string();
        if (path.empty())
        {
            DrawIcon(draw, toy, kind, at, box, true);
            return;
        }
        auto found = m_icons.find(path);
        if (found == m_icons.end())
        {
            FigureIconTexture picture;
            picture.texture = LoadPngFile(m_app.m_renderer, path);
            if (picture.texture != nullptr)
                SDL_GetTextureSize(picture.texture, &picture.width, &picture.height);
            found = m_icons.emplace(path, picture).first;
        }
        const FigureIconTexture& picture = found->second;
        if (picture.texture == nullptr || picture.width <= 0 || picture.height <= 0)
        {
            DrawIcon(draw, toy, kind, at, box, true);
            return;
        }
        float scale = std::min(box / picture.width, box / picture.height), w = picture.width * scale, h = picture.height * scale;
        ImVec2 corner(at.x + (box - w) / 2, at.y + (box - h) / 2);
        draw->AddImage(Ref(picture.texture), corner, ImVec2(corner.x + w, corner.y + h));
    }

    bool CollectionPage::DrawSwapper(ImDrawList* draw, const collection::Toy* top, uint16_t topVariant, const collection::Toy* bottom, uint16_t bottomVariant, ImVec2 at, float box)
    {
        if (top == nullptr || bottom == nullptr)
            return false;
        std::string key = fmt::format("swapper:{}:{}:{}:{}", top->type, topVariant, bottom->type, bottomVariant);
        auto found = m_icons.find(key);
        if (found == m_icons.end())
        {
            FigureIconTexture picture;
            if (collection::Image image = collection::SwapperPicture(*top, topVariant, *bottom, bottomVariant); !image.Empty())
            {
                std::vector<uint8_t> bytes(image.rgba.size() * 4);
                std::memcpy(bytes.data(), image.rgba.data(), bytes.size()); // R, G, B, A in memory on the little-endian systems we run on
                picture.texture = LoadRgba(m_app.m_renderer, bytes, image.width, image.height);
                picture.width = float(image.width), picture.height = float(image.height);
            }
            found = m_icons.emplace(key, picture).first; // no picture is remembered too
        }
        const FigureIconTexture& picture = found->second;
        if (picture.texture == nullptr)
            return false;
        float scale = std::min(box / picture.width, box / picture.height), w = picture.width * scale, h = picture.height * scale;
        ImVec2 corner(at.x + (box - w) / 2, at.y + (box - h) / 2);
        draw->AddImage(Ref(picture.texture), corner, ImVec2(corner.x + w, corner.y + h));
        return true;
    }

    bool CollectionPage::DrawSwapper(ImDrawList* draw, const collection::Card& card, ImVec2 at, float box)
    {
        return card.Swapper() && DrawSwapper(draw, card.first->toy, card.first->variant, card.second->toy, card.second->variant, at, box);
    }

    // ---- Data -----------------------------------------------------------------------------------------------------

    std::string CollectionPage::Folder() const
    {
        return collection::Folder();
    }

    void CollectionPage::Toast(const std::string& text, bool error)
    {
        m_app.Toast(text, error);
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
                Toast(migration.copied == 1 ? Tf("{} figure copied", migration.copied)
                                            : Tf("{} figures copied", migration.copied));
        }
        // The launcher's own favourites and portal list, from before the shared collection, move once.
        if (SettingGet(m_app.m_config, "collection", "moved") != "1")
        {
            if (const collection::Game* game = m_model.Game())
            {
                std::vector<std::string> seats = MoveLauncherSettings(*game, collection::LegacyFolder(),
                    SplitList(SettingGet(m_app.m_config, "collection", "favourites")), SplitList(SettingGet(m_app.m_config, "portal", "place")));
                if (!seats.empty())
                    WriteSeats(*game, SessionFile(*game), seats);
            }
            m_app.Set("collection", "moved", "1");
            // The seats are the session file now: a [portal] place would override it at the game's start.
            m_app.Set("portal", "place", "");
        }
        collection::PurgeTrash(30);
        // Only a game that can start with prepared seats has a portal here (one with a runtime: SSA, Giants).
        if (const collection::Game* game = m_model.Game(); game == nullptr || SessionFile(*game).empty())
            m_model.gameId = "ssa";
        Reload();
    }

    void CollectionPage::Reload()
    {
        m_model.Load();
        m_generation++;
    }

    void CollectionPage::SelectPortalGame(const std::string& id)
    {
        m_model.gameId = id;
        m_choosing = -1;
        Reload();
    }

    // The shelf: the filters, the search and the seat being chosen for, cut into the shelf's sections.
    void CollectionPage::Rebuild()
    {
        std::string key = fmt::format("{}|{}|{}|{}|{}|{}|{}|{}", m_generation, m_search, m_choosing, m_filter.element, m_filter.game,
            m_filter.category, int(m_filter.sort), m_model.gameId);
        if (key == m_listKey)
            return;
        m_listKey = key;
        ShelfFilter filter = m_filter;
        filter.text = m_search;
        filter.seat = m_choosing;
        m_sections = m_model.Shelf(filter, m_list, &m_hiddenStarters);
    }

    std::string WithoutHalf(std::string name)
    {
        for (const char* suffix : { " (top)", " (bottom)" })
        {
            std::string tail = suffix;
            if (name.size() > tail.size() && name.compare(name.size() - tail.size(), tail.size(), tail) == 0)
                return name.substr(0, name.size() - tail.size());
        }
        return name;
    }

    collection::Card CollectionPage::CardOf(const Figure& figure) const
    {
        if (!figure.partner.empty())
            for (const Figure& other : m_model.figures)
                if (other.file == figure.partner)
                    return figure.IsSwapBottom() ? collection::Card{ &other, &figure } : collection::Card{ &figure, &other };
        return { &figure, nullptr };
    }

    // The bottom half of a swapper, small over the corner of the card's picture.
    void CollectionPage::DrawBottom(ImDrawList* draw, const Figure& bottom, ImVec2 at, float size)
    {
        draw->AddRectFilled(ImVec2(at.x - 2, at.y - 2), ImVec2(at.x + size + 2, at.y + size + 2), Rgb(Panel), Radius);
        DrawIcon(draw, bottom.toy, bottom.Kind(), at, size, false);
        draw->AddRect(ImVec2(at.x - 2, at.y - 2), ImVec2(at.x + size + 2, at.y + size + 2), Rgb(BorderBright), Radius);
    }

    // The game's words: the player's name for it, else the toy's name with the nickname the game shows.
    std::string CollectionPage::Name(const Figure& figure) const
    {
        if (collection::Card card = CardOf(figure); card.Swapper())
            return collection::CardName(card);
        if (!figure.label.empty())
            return figure.label;
        if (figure.toy == nullptr)
            return Tf("Unknown toy {}", figure.type);
        std::string name = figure.DisplayName(), nickname = figures::NicknameUtf8(figure.stats);
        if (!nickname.empty() && Lower(nickname) != Lower(name))
            name += " (" + nickname + ")";
        return name;
    }

    // The line under a card's name, as in the Toy Box: the level for the portal's game ("Level 10 (15)"), or that a game
    // has it on its portal (the tooltip and the sheet say which).
    std::string CollectionPage::Detail(const Figure& figure) const
    {
        collection::Card card = CardOf(figure);
        if (card.Busy())
            return Tr("Playing now");
        return card.Swapper() ? collection::CardLevelText(card, m_model.gameId, French()) : collection::LevelText(figure, m_model.gameId, French());
    }

    std::string CollectionPage::SeatName(size_t seat) const
    {
        const collection::Game* game = m_model.Game();
        if (game == nullptr || seat >= game->seats.size())
            return {};
        const std::string& id = game->seats[seat].id;
        if (id == "p1" || id == "p2")
            return Tf("Player {}", id.substr(1));
        if (id == "item")
        {
            // Giants has one seat for a magic item or an adventure pack.
            const std::vector<Category>& accepts = game->seats[seat].accepts;
            return std::find(accepts.begin(), accepts.end(), Category::Adventure) != accepts.end() ? Tr("Item or adventure") : Tr("Magic item");
        }
        if (id == "adventure")
            return Tr("Adventure");
        if (id == "trap")
            return Tr("Trap");
        if (id.rfind("vehicle", 0) == 0)
            return Tf("Vehicle {}", id.substr(7));
        return id;
    }

    std::string CollectionPage::SeatTag(size_t seat) const
    {
        const collection::Game* game = m_model.Game();
        if (game == nullptr || seat >= game->seats.size())
            return {};
        return game->seats[seat].player > 0 ? collection::SeatTag(game->seats[seat], French()) : Tr("On");
    }

    std::string CollectionPage::SectionName(const Section& section) const
    {
        switch (section.kind)
        {
        case Section::Kind::Shelf: return Tr("Your figures");
        case Section::Kind::Element: return ElementName(collection::Element(section.value));
        case Section::Kind::Game:
            if (const collection::Game* game = collection::Catalogue::Get().FindGame(section.game))
                return game->name;
            return section.game;
        case Section::Kind::Category: return CategoryLabel(Category(section.value));
        case Section::Kind::Series: return KindLabel(collection::VariantKind(section.value));
        case Section::Kind::Level: return Tf("Level {}", section.value);
        case Section::Kind::Letter: return std::string(1, char(section.value));
        case Section::Kind::Unknown: return Tr("Unknown toys");
        case Section::Kind::Foreign: return Tf("Not for {}", ShortGameName(m_model.gameId));
        }
        return {};
    }

    std::string CollectionPage::LiveGame(const Figure& figure) const
    {
        collection::Card card = CardOf(figure);
        for (const Figure* half : card.Halves())
            if (half->lock.Busy())
                return ShortGameName(half->lock.game);
        return {};
    }

    bool CollectionPage::Writable(const Figure& figure) const
    {
        return !Busy(figure);
    }

    // ---- Actions --------------------------------------------------------------------------------------------------

    void CollectionPage::OpenFigure(const std::string& path)
    {
        m_figure = path;
        m_changeKeep.clear();
        // A swapper's sheet is its top's.
        if (const Figure* figure = m_model.Find(path); figure != nullptr && figure->IsSwapBottom())
            if (collection::Card card = CardOf(*figure); card.Swapper())
                m_figure = card.first->path;
        m_overlay = Overlay::Figure;
        m_editing = false;
        m_confirm.clear();
    }

    void CollectionPage::Choose(int seat)
    {
        const collection::Game* game = m_model.Game();
        if (seat >= 0 && (game == nullptr || size_t(seat) >= game->seats.size()))
            seat = -1;
        if (seat >= 0 && !m_model.CanEditSeats())
        {
            Toast(SessionFile(*game).empty() ? Tf("Not available for {} yet", ShortGameName(m_model.gameId))
                                             : Tr("The game is running: use its Toy Box"));
            seat = -1;
        }
        if (seat != m_choosing)
            m_generation++;
        m_choosing = seat;
    }

    void CollectionPage::PutOn(size_t seat, const std::string& path)
    {
        const collection::Game* game = m_model.Game();
        const Figure* figure = m_model.Find(path);
        if (game == nullptr || figure == nullptr || seat >= game->seats.size())
            return;
        if (!m_model.CanEditSeats())
        {
            Toast(SessionFile(*game).empty() ? Tr("The game isn't installed yet")
                                             : Tr("The game is running: use its Toy Box"));
            return;
        }
        if (figure->toy != nullptr && !collection::Catalogue::Get().Accepts(m_model.gameId, figure->type))
            Toast(Tf("{} is not for {}", Name(*figure), ShortGameName(m_model.gameId)));
        else if (!SeatAccepts(game->seats[seat], *figure))
            Toast(Tf("{} doesn't go on {}", Name(*figure), SeatName(seat)));
        else if (Busy(*figure))
            Toast(Tf("{} is in use by {}", Name(*figure), LiveGame(*figure)));
        else if (m_model.SetSeat(seat, path))
        {
            m_generation++;
            m_choosing = -1;
            Toast(fmt::format("{}: {}", SeatName(seat), Name(*figure)));
        }
        else
            Toast(Tr("Couldn't write the seats for the next start"));
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
        std::string text = Tf("Imported {}", report.added);
        if (report.duplicates > 0)
            text += Tf(" · {} already there", report.duplicates);
        if (report.invalid > 0)
            text += Tf(" · {} not a figure", report.invalid);
        if (!report.unknown.empty())
            text += report.unknown.size() == 1 ? Tf(" · {} unknown toy (Manage)", report.unknown.size())
                                               : Tf(" · {} unknown toys (Manage)", report.unknown.size());
        Toast(text);
    }

    void CollectionPage::CloseOverlay()
    {
        m_overlay = Overlay::None;
        m_editing = false;
        m_confirm.clear();
        ImGui::CloseCurrentPopup();
    }

    // Empties a seat: the figure leaves the portal of the next start (and stays in the collection).
    void CollectionPage::TakeOff(size_t seat)
    {
        const collection::Game* game = m_model.Game();
        if (game == nullptr || seat >= game->seats.size() || m_model.seats[seat].empty())
            return;
        std::string what = m_model.seats[seat];
        const Figure* figure = m_model.Find(what);
        if (!m_model.CanEditSeats())
            Toast(Tr("The game is running: use its Toy Box"));
        else if (m_model.SetSeat(seat, ""))
        {
            m_generation++;
            Toast(Tf("{}: {} taken off", SeatName(seat), figure != nullptr ? Name(*figure) : std::string(Tr("figure"))));
        }
        else
            Toast(Tr("Couldn't write the seats for the next start"));
    }

    void CollectionPage::TestSetSeats(const std::vector<std::string>& seats)
    {
        for (size_t i = 0; i < seats.size(); i++)
            m_model.SetSeat(i, seats[i]);
        m_generation++;
    }

    // ---- The page -------------------------------------------------------------------------------------------------

    void CollectionPage::Draw()
    {
        if (!m_opened)
            Open();
        if (m_model.Poll(SDL_GetTicks()))
            m_generation++; // a game wrote a dump, took or released a figure, or quit
        if (m_choosing >= 0 && !m_model.CanEditSeats())
            m_choosing = -1; // a game started meanwhile
        Rebuild();

        float available = ImGui::GetContentRegionAvail().x;
        float width = std::min(available, 1180.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) / 2);
        m_app.BeginPageColumn("##collectioncolumn", width);
        DrawHeader(width);
        DrawSeats(width);
        DrawBar();
        ImGui::BeginChild("##shelf", ImVec2(0, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        ImGui::SetScrollX(0.0f);
        DrawList();
        ImGui::EndChild();
        // A figure dragged off a seat and dropped on the shelf leaves the seat.
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
        m_app.Title(Tr("COLLECTION"), HeadlineSize, Text);
        int shown = int(m_model.figures.size()) - m_hiddenStarters;
        size_t favourites = std::count_if(m_model.figures.begin(), m_model.figures.end(), [](const Figure& f) { return f.favourite; });
        std::string summary = shown == 1 ? Tf("{} figure", shown) : Tf("{} figures", shown);
        if (favourites > 0)
            summary += favourites == 1 ? Tf(" · {} favourite", favourites) : Tf(" · {} favourites", favourites);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", summary.c_str());
        ImGui::SameLine(0, 16);
        m_app.DrawPortalStatus();
        ImVec2 after = ImGui::GetCursorScreenPos();

        // Add a figure, and the rest of the collection's work behind "...".
        constexpr float AddW = 190, MoreW = 52;
        ImGui::SetCursorScreenPos(ImVec2(top.x + width - m_app.PillWidth(Tr("+  Add a figure"), AddW, true) - 10 - MoreW, top.y + 4));
        if (m_app.PillButton(Tr("+  Add a figure"), true, AddW, true))
        {
            m_overlay = Overlay::Add;
            m_addToy = 0xFFFF;
            m_created.clear();
            m_addGame = m_model.gameId;
        }
        m_app.TakeFocus(FocusTo::Page);
        ImGui::SameLine(0, 10);
        ImVec2 moreAt = ImGui::GetCursorScreenPos();
        if (m_app.PillButton((std::string("...") + "##collectionmore").c_str(), false, MoreW, true))
            ImGui::OpenPopup("##collectionmenu");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("Manage"));
        ImGui::SetNextWindowPos(ImVec2(moreAt.x + MoreW, moreAt.y + 46), ImGuiCond_Always, ImVec2(1, 0));
        if (ImGui::BeginPopup("##collectionmenu"))
        {
            auto manage = [&](const char* label, int tab) {
                if (ImGui::MenuItem(label))
                {
                    m_overlay = Overlay::Manage;
                    m_manageTab = tab;
                }
            };
            manage(Tr("Trash"), 0);
            manage(Tr("Backups"), 1);
            manage(Tr("Import and export"), 2);
            manage(Tr("Unknown toys"), 3);
            manage(Tr("Tidy the starters"), 4);
            ImGui::Separator();
            if (ImGui::MenuItem(Tr("Show the collection folder")))
            {
                std::string folder = collection::Folder();
                OpenFolder(std::filesystem::path(std::u8string(folder.begin(), folder.end())));
            }
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(ImVec2(after.x, after.y + 10));
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
            if (ImGui::MenuItem(Tr("Take it off"), Tr("Delete"), false, m_model.CanEditSeats()))
                TakeOff(seat);
            if (ImGui::MenuItem(Tr("Choose another"), nullptr, false, m_model.CanEditSeats()))
                Choose(int(seat));
            if (ImGui::MenuItem(Tr("Open its sheet"), nullptr, false, figure != nullptr))
                OpenFigure(figure->path);
        }
        ImGui::EndPopup();
    }

    // The portal at the next start: the chosen game's seats in a row, what each holds, and how to change it.
    void CollectionPage::DrawSeats(float width)
    {
        const collection::Game* game = m_model.Game();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        std::string live = m_model.RunningGame() == m_model.gameId ? m_model.gameId : std::string();
        bool editable = m_model.CanEditSeats();

        // "PORTAL AT THE NEXT START", the game it is for (the games that can start with prepared seats), what it does.
        ImVec2 line = ImGui::GetCursorScreenPos();
        m_app.BoldText(live.empty() ? Tr("PORTAL AT THE NEXT START") : Tr("ON THE PORTAL NOW"), TextDim);
        ImGui::SameLine(0, 12);
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, line.y - 6));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 5));
        ImGui::SetNextItemWidth(210);
        if (ImGui::BeginCombo("##portalgame", ShortGameName(m_model.gameId).c_str()))
        {
            for (const collection::Game& g : collection::Catalogue::Get().Games())
                if (!SessionFile(g).empty() && ImGui::Selectable(ShortGameName(g.id).c_str(), g.id == m_model.gameId))
                    SelectPortalGame(g.id);
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
        std::string status = !live.empty() ? Tf("{} is running: use its Toy Box", ShortGameName(live))
            : m_choosing >= 0 ? std::string(Tr("Pick a figure below"))
            : std::string(Tr("Click a seat, then a figure"));
        ImGui::PushFont(nullptr, SmallSize);
        float statusW = ImGui::CalcTextSize(status.c_str()).x;
        draw->AddText(ImVec2(line.x + width - statusW, line.y + 2), Rgb(!live.empty() ? Ok : TextDim), status.c_str());
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(line.x, line.y + 32));
        if (game == nullptr)
            return;

        size_t count = game->seats.size();
        constexpr float H = 76, Gap = 10;
        float w = (width - Gap * float(count - 1)) / float(count);
        ImVec2 row = ImGui::GetCursorScreenPos();
        m_navSeat = m_hoverSeat = -1;
        m_crossAt.assign(count, ImVec2(-1, -1));
        for (size_t i = 0; i < count; i++)
        {
            ImVec2 at(row.x + float(i) * (w + Gap), row.y);
            ImGui::SetCursorScreenPos(at);
            ImGui::PushID(int(i));
            // The X is submitted over the seat: without this the seat keeps the hover and the X never sees the pointer.
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::InvisibleButton("##seat", ImVec2(w, H), ImGuiButtonFlags_EnableNav))
                Choose(m_choosing == int(i) ? -1 : int(i));
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
            // Dragging a figure off its seat, onto the shelf, takes it off.
            if (occupied && editable && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                size_t index = i;
                ImGui::SetDragDropPayload("SKYSEAT", &index, sizeof(index));
                ImGui::TextUnformatted(figure != nullptr ? Name(*figure).c_str() : Tr("Take it off"));
                ImGui::EndDragDropSource();
            }
            bool menu = ImGui::IsItemClicked(ImGuiMouseButton_Right) && occupied;
            ImGui::PopID();
            if (menu) // outside the seat's ID scope: the popup is drawn by DrawSeatMenu at the page's level
            {
                m_seatMenu = int(i);
                ImGui::OpenPopup("##seatmenu");
            }
            bool choosing = m_choosing == int(i);
            ImVec2 end(at.x + w, at.y + H);
            draw->AddRectFilled(at, end, Rgb(choosing ? PanelRaised : hovered || nav ? PanelRaised : Panel, 240), Radius);
            draw->AddRect(at, end, Rgb(choosing ? Accent : hovered ? BorderBright : Border), Radius, 0, choosing ? 2.5f : 1.0f);
            constexpr float Icon = 52;
            ImVec2 icon(at.x + 12, at.y + (H - Icon) / 2);
            // The seat's name leaves room for the X; the figure's name and level run under it, to the edge.
            float tx = icon.x + Icon + 12, tw = end.x - tx - 10;
            ImGui::PushFont(nullptr, SmallSize - 1);
            draw->AddText(ImVec2(tx, at.y + 11), Rgb(choosing ? Accent : TextDim), SeatName(i).c_str());
            ImGui::PopFont();
            auto fit = [&](const std::string& text, float size, ImFont* font = nullptr) {
                ImGui::PushFont(font, size);
                std::string out = FitText(text, tw, [](const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x; });
                ImGui::PopFont();
                return out;
            };
            if (occupied && figure != nullptr)
            {
                if (collection::Card card = CardOf(*figure); !DrawSwapper(draw, card, icon, Icon))
                {
                    DrawIcon(draw, figure->toy, figure->Kind(), icon, Icon, false);
                    if (card.Swapper())
                        DrawBottom(draw, *card.second, ImVec2(icon.x + Icon - 22, icon.y + Icon - 22), 22);
                }
                ImGui::PushFont(m_app.m_boldFont, TextSize - 1);
                draw->AddText(ImVec2(tx, at.y + 30), Rgb(Text), fit(Name(*figure), TextSize - 1, m_app.m_boldFont).c_str());
                ImGui::PopFont();
                if (std::string detail = Detail(*figure); !detail.empty())
                {
                    ImGui::PushFont(nullptr, SmallSize - 1);
                    draw->AddText(ImVec2(tx, at.y + 52), Rgb(Busy(*figure) ? Ok : TextDim), fit(detail, SmallSize - 1).c_str());
                    ImGui::PopFont();
                }
            }
            else
            {
                // Empty, or a figure the collection no longer has (deleted, moved): the seat can still be emptied.
                draw->AddRectFilled(icon, ImVec2(icon.x + Icon, icon.y + Icon), Rgb(Field), Radius);
                draw->AddRect(icon, ImVec2(icon.x + Icon, icon.y + Icon), Rgb(choosing ? Accent : Border), Radius, 0, 1.5f);
                ImU32 plus = Rgb(choosing ? Accent : TextDim);
                ImVec2 c(icon.x + Icon / 2, icon.y + Icon / 2);
                draw->AddLine(ImVec2(c.x - 9, c.y), ImVec2(c.x + 9, c.y), plus, 2.5f);
                draw->AddLine(ImVec2(c.x, c.y - 9), ImVec2(c.x, c.y + 9), plus, 2.5f);
                ImGui::PushFont(m_app.m_boldFont, TextSize - 1);
                draw->AddText(ImVec2(tx, at.y + 30), Rgb(occupied ? Danger : TextSecondary), occupied ? Tr("Missing figure") : Tr("Empty"));
                ImGui::PopFont();
                ImGui::PushFont(nullptr, SmallSize - 1);
                draw->AddText(ImVec2(tx, at.y + 52), Rgb(TextDim), choosing ? Tr("Pick one below") : editable ? Tr("Choose") : "");
                ImGui::PopFont();
            }
            if (occupied && editable && !(figure != nullptr && Busy(*figure)))
            {
                ImVec2 cross(end.x - 18, at.y + 18);
                m_crossAt[i] = cross;
                ImGui::SetCursorScreenPos(ImVec2(cross.x - 12, cross.y - 12));
                ImGui::PushID(int(i) + 100);
                bool off = ImGui::InvisibleButton("##off", ImVec2(24, 24)); // the pad takes it off with X on the seat
                bool over = ImGui::IsItemHovered();
                ImGui::PopID();
                ImU32 ink = Rgb(over ? Text : TextDim);
                draw->AddLine(ImVec2(cross.x - 5, cross.y - 5), ImVec2(cross.x + 5, cross.y + 5), ink, 2);
                draw->AddLine(ImVec2(cross.x - 5, cross.y + 5), ImVec2(cross.x + 5, cross.y - 5), ink, 2);
                if (over)
                    ImGui::SetTooltip("%s", Tr("Take it off"));
                if (off)
                    TakeOff(i);
            }
        }
        DrawSeatMenu();
        ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + H + 16));
    }

    // The bar: what the shelf is for (a seat being chosen for), the search and Filters.
    void CollectionPage::DrawBar()
    {
        const collection::Game* game = m_model.Game();
        if (m_choosing >= 0 && game != nullptr)
        {
            // Choosing for a seat: the shelf shows only what goes there.
            ImVec2 at = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(at, ImVec2(at.x + w, at.y + 40), Rgb(AccentDark, 70), Radius);
            draw->AddRect(at, ImVec2(at.x + w, at.y + 40), Rgb(Accent), Radius);
            std::string text = Tf("Choose a figure for {}", SeatName(size_t(m_choosing)));
            ImGui::PushFont(m_app.m_boldFont, TextSize);
            draw->AddText(ImVec2(at.x + 16, at.y + 10), Rgb(Text), text.c_str());
            float textW = ImGui::CalcTextSize(text.c_str()).x;
            ImGui::PopFont();
            ImGui::PushFont(nullptr, SmallSize);
            draw->AddText(ImVec2(at.x + 16 + textW + 12, at.y + 13), Rgb(TextSecondary),
                Tf("only what {} reads", ShortGameName(m_model.gameId)).c_str());
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(at.x + w - 110, at.y + 4));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 4));
            if (ImGui::Button((std::string(Tr("Cancel")) + "##choosing").c_str(), ImVec2(100, 32)))
                Choose(-1);
            ImGui::PopStyleVar();
            ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + 50));
        }

        // The search first and wide, then Filters (Element, Game, Type, Sort by in one small panel); when something is on, its tags
        // (an x removes one), Clear all and the count of figures found.
        constexpr float FiltersW = 130, Gap = 8;
        int active = (m_filter.element >= 0) + !m_filter.game.empty() + (m_filter.category >= 0) + (m_filter.sort != collection::Sort::Recent);
        float searchW = std::max(200.0f, ImGui::GetContentRegionAvail().x - FiltersW - Gap);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 11));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 19);
        ImGui::SetNextItemWidth(searchW);
        if (m_focusSearch)
        {
            ImGui::SetKeyboardFocusHere();
            m_focusSearch = false;
        }
        ImGui::InputTextWithHint("##search", Tr("Name, element, game, level…"), m_search, sizeof(m_search));
        m_navBar = ImGui::IsItemFocused();
        ImGui::PopStyleVar(2);
        ImGui::SameLine(0, Gap);

        std::string filters = std::string(Tr("Filters")) + (active > 0 ? fmt::format(" · {}", active) : std::string()) + "##filters";
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 19);
        bool pressed = m_app.PillButton(filters.c_str(), false, FiltersW, active > 0, active > 0 ? AccentDark : PanelRaised,
            active > 0 ? Accent : PanelRaisedHover, active > 0 ? AccentDark : PanelRaisedActive, 1);
        m_navBar = m_navBar || ImGui::IsItemFocused();
        ImGui::PopStyleVar();
        if (pressed || m_openFilters)
            ImGui::OpenPopup("##filtersmenu");
        m_openFilters = false;
        constexpr float PanelW = 400;
        ImGui::SetNextWindowPos(ImVec2(at.x + FiltersW - PanelW, at.y + 44));
        ImGui::SetNextWindowSizeConstraints(ImVec2(PanelW, 0), ImVec2(PanelW, 420));
        if (ImGui::BeginPopup("##filtersmenu"))
        {
            if (m_closeFilters)
                ImGui::CloseCurrentPopup();
            m_closeFilters = false;
            auto row = [&](const char* label, const char* id, const std::string& value, const std::function<void()>& items) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tr(label));
                ImGui::SameLine(110);
                ImGui::SetNextItemWidth(PanelW - 110 - 16);
                if (ImGui::BeginCombo(id, value.c_str()))
                {
                    items();
                    ImGui::EndCombo();
                }
            };
            row(N_("Element"), "##element", m_filter.element >= 0 ? ElementName(collection::Element(m_filter.element)) : Tr("All elements"), [&] {
                if (ImGui::Selectable(Tr("All elements"), m_filter.element < 0))
                    m_filter.element = -1;
                for (collection::Element element : MenuElements)
                    if (ImGui::Selectable(ElementName(element).c_str(), m_filter.element == int(element)))
                        m_filter.element = int(element);
            });
            row(N_("Game"), "##game", m_filter.game.empty() ? Tr("All games") : ShortGameName(m_filter.game), [&] {
                if (ImGui::Selectable(Tr("All games"), m_filter.game.empty()))
                    m_filter.game.clear();
                for (const collection::Game& g : collection::Catalogue::Get().Games())
                    if (ImGui::Selectable(ShortGameName(g.id).c_str(), m_filter.game == g.id))
                        m_filter.game = g.id;
            });
            row(N_("Type"), "##type", m_filter.category >= 0 ? CategoryLabel(Category(m_filter.category)) : Tr("All types"), [&] {
                if (ImGui::Selectable(Tr("All types"), m_filter.category < 0))
                    m_filter.category = -1;
                // The types the collection has.
                for (int category = 0; category < int(Category::Count); category++)
                    if (std::any_of(m_model.figures.begin(), m_model.figures.end(), [&](const Figure& f) { return f.toy != nullptr && int(f.toy->category) == category; }))
                        if (ImGui::Selectable(CategoryLabel(Category(category)).c_str(), m_filter.category == category))
                            m_filter.category = category;
            });
            row(N_("Sort by"), "##sort", collection::SortLabel(m_filter.sort, French()), [&] {
                for (int sort = 0; sort < int(collection::Sort::Count); sort++)
                    if (ImGui::Selectable(collection::SortLabel(collection::Sort(sort), French()).c_str(), m_filter.sort == collection::Sort(sort)))
                        m_filter.sort = collection::Sort(sort);
            });
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::BeginDisabled(active == 0 && m_search[0] == 0);
            if (ImGui::Button((std::string(Tr("Clear all")) + "##clearpanel").c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 34)))
                ClearFilters();
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
        ImGui::Dummy(ImVec2(0, 4));
        DrawTags();
    }

    void CollectionPage::ClearFilters()
    {
        m_search[0] = 0;
        m_filter = ShelfFilter{};
    }

    // What is on, as small tags with an x (the search is the field's own text), Clear all, and how many figures are found.
    void CollectionPage::DrawTags()
    {
        std::vector<std::pair<std::string, int>> tags; // the label and the filter it is
        if (m_filter.element >= 0)
            tags.push_back({ ElementName(collection::Element(m_filter.element)), 0 });
        if (!m_filter.game.empty())
            tags.push_back({ ShortGameName(m_filter.game), 1 });
        if (m_filter.category >= 0)
            tags.push_back({ CategoryLabel(Category(m_filter.category)), 2 });
        if (m_filter.sort != collection::Sort::Recent)
            tags.push_back({ Tf("Sort: {}", collection::SortLabel(m_filter.sort, French())), 3 });
        if (tags.empty() && m_search[0] == 0)
            return;

        constexpr float H = 28, Gap = 8;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        ImGui::PushFont(m_app.m_boldFont, SmallSize);
        // A pill drawn by hand: a focusable button, the label, and an x when `cross`.
        auto pill = [&](const std::string& label, const char* id, bool cross, bool accent) {
            float width = ImGui::CalcTextSize(label.c_str()).x + (cross ? 44 : 28);
            ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + width, min.y + H);
            ImGui::PushID(id);
            bool pressed = ImGui::InvisibleButton("##tag", ImVec2(width, H), ImGuiButtonFlags_EnableNav);
            bool hovered = ImGui::IsItemHovered();
            m_navBar = m_navBar || ImGui::IsItemFocused();
            m_app.FocusRing(H / 2);
            ImGui::PopID();
            draw->AddRectFilled(min, max, Rgb(accent ? AccentDark : hovered ? PanelRaisedHover : PanelRaised, accent ? 120 : 255), H / 2);
            if (accent)
                draw->AddRect(min, max, Rgb(Accent), H / 2);
            draw->AddText(ImVec2(min.x + 14, min.y + (H - ImGui::GetFontSize()) / 2), Rgb(Text), label.c_str());
            if (cross)
            {
                ImVec2 c(max.x - 17, (min.y + max.y) / 2);
                draw->AddLine(ImVec2(c.x - 4, c.y - 4), ImVec2(c.x + 4, c.y + 4), Rgb(Text), 1.8f);
                draw->AddLine(ImVec2(c.x - 4, c.y + 4), ImVec2(c.x + 4, c.y - 4), Rgb(Text), 1.8f);
            }
            return pressed;
        };
        int remove = -1;
        for (size_t i = 0; i < tags.size(); i++)
        {
            if (i > 0)
                ImGui::SameLine(0, Gap);
            if (pill(tags[i].first, tags[i].first.c_str(), true, true))
                remove = tags[i].second;
        }
        if (!tags.empty())
            ImGui::SameLine(0, Gap);
        if (pill(Tr("Clear all"), "##clear", false, false))
            ClearFilters();
        std::string count = collection::FigureCount(FoundCount(), French());
        ImGui::SameLine(0, 0);
        ImGui::SetCursorScreenPos(ImVec2(right - ImGui::CalcTextSize(count.c_str()).x, ImGui::GetCursorScreenPos().y + (H - ImGui::GetFontSize()) / 2));
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", count.c_str());
        ImGui::PopFont();
        switch (remove)
        {
        case 0: m_filter.element = -1; break;
        case 1: m_filter.game.clear(); break;
        case 2: m_filter.category = -1; break;
        case 3: m_filter.sort = collection::Sort::Recent; break;
        default: break;
        }
        ImGui::Dummy(ImVec2(0, 4));
    }

    int CollectionPage::FoundCount() const
    {
        int count = 0;
        for (const Section& section : m_sections)
            count += int(section.items.size());
        return count;
    }

    bool CollectionPage::DrawCard(const Figure& figure, float width, bool dim)
    {
        ImVec2 at = ImGui::GetCursorScreenPos();
        float height = width + 40;
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
        bool rightClick = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        if (hovered || focused)
            m_hovered = figure.path;
        if (focused)
            m_selected = figure.path;
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            ImGui::SetDragDropPayload("SKYFIG", figure.path.c_str(), figure.path.size() + 1);
            ImGui::Text("%s", Name(figure).c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
        // A click (Enter, A): onto the seat being chosen for, else the figure's sheet.
        if (pressed)
        {
            m_selected = figure.path;
            if (m_choosing >= 0 && !dim)
                PutOn(size_t(m_choosing), figure.path);
            else
                OpenFigure(figure.path);
        }
        if (rightClick)
            OpenFigure(figure.path);
        if (!ImGui::IsItemVisible()) // scrolled out of view: a big collection draws only what shows
            return pressed;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 end(at.x + width, at.y + height);
        int seat = m_model.SeatOf(figure.path);
        bool selected = m_selected == figure.path && focused;
        draw->AddRectFilled(at, end, Rgb(hovered ? PanelRaised : Panel, 240), Radius);
        draw->AddRect(at, end, Rgb(seat >= 0 ? Accent : selected || hovered ? BorderBright : Border), Radius, 0, seat >= 0 || selected ? 2.0f : 1.0f);
        float icon = width - 28;
        if (collection::Card card = CardOf(figure); !DrawSwapper(draw, card, ImVec2(at.x + 14, at.y + 10), icon))
        {
            DrawIcon(draw, figure.toy, figure.Kind(), ImVec2(at.x + 14, at.y + 10), icon, false);
            if (card.Swapper())
                DrawBottom(draw, *card.second, ImVec2(at.x + 14 + icon - icon * 0.38f, at.y + 10 + icon - icon * 0.38f), icon * 0.38f);
        }
        const bool busy = Busy(figure);

        ImGui::PushFont(m_app.m_boldFont, SmallSize + 1);
        std::string name = Name(figure);
        for (size_t keep = name.size(); keep > 4 && ImGui::CalcTextSize(name.c_str()).x > width - 12; keep--)
            name = LauncherMiddleEllipsis(Name(figure), keep);
        ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
        draw->AddText(ImVec2(at.x + std::max(6.0f, (width - nameSize.x) / 2), at.y + icon + 13), Rgb(Text), name.c_str());
        ImGui::PopFont();
        if (std::string detail = Detail(figure); !detail.empty())
        {
            ImGui::PushFont(nullptr, SmallSize - 1);
            ImVec2 detailSize = ImGui::CalcTextSize(detail.c_str());
            draw->AddText(ImVec2(at.x + std::max(6.0f, (width - detailSize.x) / 2), at.y + icon + 31), Rgb(busy ? Ok : TextDim), detail.c_str());
            ImGui::PopFont();
        }

        // Marks: the variant, favourite, seat, lock.
        collection::VariantKind kind = figure.Kind();
        if (kind != collection::VariantKind::Base)
        {
            uint32_t rgb = collection::VariantKindRgb(kind);
            bool light = ((rgb >> 16 & 0xFF) * 3 + (rgb >> 8 & 0xFF) * 6 + (rgb & 0xFF)) / 10 > 120;
            m_app.Badge(draw, ImVec2(at.x + 6, at.y + 6), KindLabel(kind).c_str(), rgb, light ? AccentInk : Text);
        }
        if (figure.favourite)
            DrawStar(draw, ImVec2(end.x - 15, at.y + 16), 9, Rgb(0xFFD54A));
        if (seat >= 0)
            m_app.Badge(draw, ImVec2(at.x + 6, at.y + icon - 8), SeatTag(size_t(seat)).c_str(), Accent, AccentInk);
        if (dim)
            draw->AddRectFilled(at, end, IM_COL32(20, 20, 26, 150), Radius);
        if (hovered)
        {
            std::string tip = Name(figure);
            if (busy)
                tip += "\n" + Tf("On the portal in {}: read-only", LiveGame(figure));
            else if (dim)
                tip += "\n" + Tf("Not for {}", ShortGameName(m_model.gameId));
            else if (m_choosing >= 0)
                tip += "\n" + Tf("Click: put it on {}", SeatName(size_t(m_choosing)));
            else
                tip += std::string("\n") + Tr("Click: sheet · drag to a seat");
            ImGui::SetTooltip("%s", tip.c_str());
        }
        return pressed;
    }

    void CollectionPage::DrawList()
    {
        m_hovered.clear();
        m_navTopCard = false;
        bool filtered = m_search[0] != 0 || m_filter.element >= 0 || m_filter.category >= 0 || !m_filter.game.empty();
        if (m_list.empty())
        {
            ImGui::Dummy(ImVec2(0, 18));
            bool none = int(m_model.figures.size()) == m_hiddenStarters;
            const char* title = none ? Tr("My Skylanders is empty") : m_choosing >= 0 && !filtered ? Tr("Nothing for this seat yet") : Tr("No figure matches");
            m_app.Title(title, TitleSize, Text);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
            if (none)
                ImGui::TextWrapped("%s", Tr("Add a figure or import yours."));
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 6));
            if (none && m_app.PillButton(Tr("Start with Spyro, Gill Grunt, Trigger Happy"), true, 380, true))
            {
                for (uint16_t type : { 16, 14, 19 })
                    collection::Create(type);
                Reload();
            }
            else if (!none && filtered && m_app.PillButton(Tr("Clear search and filters"), false, 300, true))
                ClearFilters();
            else if (!none && !filtered && m_app.PillButton((std::string(Tr("+  Add a figure")) + "##empty").c_str(), true, 200, true))
            {
                m_overlay = Overlay::Add;
                m_addToy = 0xFFFF;
                m_created.clear();
                m_addGame = m_model.gameId;
            }
            return;
        }

        constexpr float Cell = 124, Gap = 10;
        int columns = std::max(1, int((ImGui::GetContentRegionAvail().x + Gap) / (Cell + Gap)));
        float cell = (ImGui::GetContentRegionAvail().x - Gap * float(columns - 1)) / float(columns);
        for (size_t s = 0; s < m_sections.size(); s++)
        {
            const Section& section = m_sections[s];
            bool foreign = section.kind == Section::Kind::Foreign;
            // The shelf has no title (the bar says what it shows); what follows does.
            if (section.kind != Section::Kind::Shelf)
            {
                ImGui::Dummy(ImVec2(0, 4));
                m_app.BoldText(fmt::format("{}  {}", SectionName(section), section.items.size()).c_str(), TextSecondary);
                if (foreign)
                {
                    ImGui::SameLine(0, 10);
                    ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", Tr("For other games"));
                }
                ImGui::Dummy(ImVec2(0, 2));
            }
            ImGui::PushID(int(s));
            for (size_t i = 0; i < section.items.size(); i++)
            {
                if (i % columns != 0)
                    ImGui::SameLine(0, Gap);
                DrawCard(m_list[section.items[i]], cell, foreign);
                if (s == 0 && i < size_t(columns) && ImGui::IsItemFocused())
                    m_navTopCard = true;
            }
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, 10));
        }
    }

    void CollectionPage::HandleShortcuts()
    {
        if (m_overlay != Overlay::None || ImGui::IsAnyItemActive())
            return;
        auto pressed = [](ImGuiKey a, ImGuiKey b) { return ImGui::IsKeyPressed(a, false) || ImGui::IsKeyPressed(b, false); };
        if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
            return;
        // A seat with the focus (or under the mouse): Delete, and the pad's X, take its figure off.
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
        // Down from the seats goes to the shelf, up from its first row or from the bar back to the seats (ImGui's nav, by
        // distance, misses the bar in between or lands on a card).
        if (m_navSeat >= 0 && pressed(ImGuiKey_GamepadDpadDown, ImGuiKey_DownArrow) && !m_list.empty())
            m_focusCard = true;
        if ((m_navTopCard || m_navBar) && pressed(ImGuiKey_GamepadDpadUp, ImGuiKey_UpArrow))
            m_focusSeat = std::max(m_choosing, 0);
        // LB / RB: choose for the seat before or after.
        if (const collection::Game* game = m_model.Game(); game != nullptr && !game->seats.empty())
        {
            int last = int(game->seats.size()) - 1;
            if (pressed(ImGuiKey_GamepadL1, ImGuiKey_PageUp))
                Choose(std::max(0, (m_choosing < 0 ? 1 : m_choosing) - 1));
            if (pressed(ImGuiKey_GamepadR1, ImGuiKey_PageDown))
                Choose(std::min(last, m_choosing + 1));
        }
        // Y: the search.
        if (pressed(ImGuiKey_GamepadFaceUp, ImGuiKey_F3))
            m_focusSearch = true;
        // X, F2: the focused card's sheet.
        if (pressed(ImGuiKey_GamepadFaceLeft, ImGuiKey_F2))
        {
            const std::string& path = !m_hovered.empty() ? m_hovered : m_selected;
            if (!path.empty() && m_model.Find(path) != nullptr)
                OpenFigure(path);
        }
        // B, Escape: stop choosing, then clear the search.
        if (pressed(ImGuiKey_Escape, ImGuiKey_GamepadFaceRight))
        {
            if (m_choosing >= 0)
                Choose(-1);
            else if (m_search[0] != 0)
                m_search[0] = 0;
        }
    }
}

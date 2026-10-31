// The collection page: the portal at the next start along the top (the chosen game's seats), the bar (search, Element,
// Game, Type), the shelf below (every figure once, favorites first). The figures and their folder are the shared
// library's (src/shared/collection); the model is collection_model.cpp; the figure sheet, Add a figure and Manage are
// in collection_figure.cpp, collection_add.cpp and collection_manage.cpp.
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/launcher_ui.h"
#include "shared/collection/icons.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fmt/format.h>
#include <imgui_internal.h> // the nav id of the search field

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

    std::string PlayTime(uint32_t seconds)
    {
        return seconds >= 3600 ? Tf("{} h {:02} min", seconds / 3600, seconds / 60 % 60) : Tf("{} min", seconds / 60);
    }

    std::string When(const std::string& iso)
    {
        const std::time_t when = ui::ParseIsoUtc(iso);
        return when == 0 ? iso : ui::FormatWhen(when, std::time(nullptr));
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
        ImGui::PushFont(ui::Ctx().fonts.title, size * 0.3f); // the letters grow with the picture they are written over
        ImVec2 text = ImGui::CalcTextSize(letters.c_str());
        ImVec2 position(at.x + (size - text.x) / 2, at.y + (size - text.y) / 2);
        draw->AddText(ImVec2(position.x + ui::stroke::Hairline, position.y + 2 * ui::stroke::Hairline), ui::Col(ui::color::Scrim, 0.5f), letters.c_str());
        draw->AddText(position, ui::Col(ui::color::Ink), letters.c_str());
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

    void CollectionPage::Toast(const std::string& text, ui::ToastKind kind)
    {
        m_app.Toast(text, kind);
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
                                            : Tf("{} figures copied", migration.copied), ui::ToastKind::Success);
        }
        // The launcher's own favorites and portal list, from before the shared collection, move once.
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
        const float edge = ui::stroke::Edge;
        draw->AddRectFilled(ImVec2(at.x - edge, at.y - edge), ImVec2(at.x + size + edge, at.y + size + edge), ui::Col(ui::color::Surface), ui::radius::Control);
        DrawIcon(draw, bottom.toy, bottom.Kind(), at, size, false);
        draw->AddRect(ImVec2(at.x - edge, at.y - edge), ImVec2(at.x + size + edge, at.y + size + edge), ui::Col(ui::color::Edge), ui::radius::Control);
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
        case Section::Kind::Foreign: return Tf("Not for {}", FullGameName(m_model.gameId));
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
        m_details = false;
        m_sheetShown.clear(); // it opens at the top, even right after another sheet
    }

    void CollectionPage::Choose(int seat)
    {
        const collection::Game* game = m_model.Game();
        if (seat >= 0 && (game == nullptr || size_t(seat) >= game->seats.size()))
            seat = -1;
        if (seat >= 0 && !m_model.CanEditSeats())
        {
            Toast(SessionFile(*game).empty() ? Tf("Not available for {} yet", FullGameName(m_model.gameId))
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
            Toast(Tf("{} is not for {}", Name(*figure), FullGameName(m_model.gameId)), ui::ToastKind::Error);
        else if (!SeatAccepts(game->seats[seat], *figure))
            Toast(Tf("{} doesn't go on {}", Name(*figure), SeatName(seat)), ui::ToastKind::Error);
        else if (Busy(*figure))
            Toast(Tf("{} is in use by {}", Name(*figure), LiveGame(*figure)), ui::ToastKind::Error);
        else if (m_model.SetSeat(seat, path))
        {
            m_generation++;
            m_choosing = -1;
            Toast(fmt::format("{}: {}", SeatName(seat), Name(*figure)));
        }
        else
            Toast(Tr("Couldn't write the seats for the next start"), ui::ToastKind::Error);
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
            Toast(Tr("Couldn't write the seats for the next start"), ui::ToastKind::Error);
    }

    void CollectionPage::TestSetSeats(const std::vector<std::string>& seats)
    {
        for (size_t i = 0; i < seats.size(); i++)
            m_model.SetSeat(i, seats[i]);
        m_generation++;
    }

    // ---- The page -------------------------------------------------------------------------------------------------

    namespace
    {
        // The page's own parts, sized from the tokens.
        constexpr float PageMax = 1180;                    // the widest the page's column gets
        constexpr float SeatPicture = ui::height::Normal;
        constexpr float SeatPad = ui::space::M;                        // inside a seat, round its picture and its lines
        constexpr float SeatHeight = SeatPicture + 2 * SeatPad;
        constexpr float SeatGap = ui::space::M;                        // between the seats
        constexpr float ShelfCell = 2 * ui::height::Main;  // the narrowest a figure's card gets
        constexpr float ShelfGap = ui::space::XS;
        constexpr float FilterPanel = ui::height::SettingControl + 2 * ui::height::Main;

        // A card's fill: the surface (raised under the pointer or the focus) at the elevation's alpha, faded by `fade`.
        ImU32 CardFill(bool raised, float fade = 1)
        {
            return ui::Col(raised ? ui::color::Raised : ui::color::Surface, fade * float(ui::elevation::Card.fillAlpha) / 255.0f);
        }

    }

    void PutText(ImDrawList* draw, ImVec2 at, ui::TextStyle style, bool bold, unsigned color, const std::string& text, float room)
    {
        ui::PushText(style, bold);
        ui::DrawText(draw, at, ui::Col(color), ui::Ellipsize(text.c_str(), room).c_str());
        ui::PopText();
    }

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
        float width = std::min(available, PageMax);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) / 2);
        m_app.BeginPageColumn("##collectioncolumn", width);
        DrawHeader(width);
        DrawSeats(width);
        DrawBar();
        // The shelf has room round its cards (their focus rings, the choose banner's border).
        const bool pad = ui::Ctx().glyphs != ui::Glyphs::Keyboard;
        const float legend = pad ? ui::height::Normal + ImGui::GetStyle().ItemSpacing.y : 0;
        ui::BeginRoomyChild("##shelf", ImVec2(0, -legend), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        ImGui::SetScrollX(0.0f);
        DrawList();
        if (m_testScrollBottom)
            ImGui::SetScrollY(ImGui::GetScrollMaxY());
        ui::EndRoomyChild();
        // A figure dragged off a seat and dropped on the shelf leaves the seat.
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SKYSEAT"))
                TakeOff(*static_cast<const size_t*>(payload->Data));
            ImGui::EndDragDropTarget();
        }
        if (pad)
        {
            const ui::PadHint hints[] = { { ui::PadButton::South, "Enter", Tr("Select") }, { ui::PadButton::West, "F2", Tr("Figure sheet") },
                { ui::PadButton::North, "F3", Tr("Search") }, { ui::PadButton::East, "Esc", Tr("Back") } };
            ui::PadLegend(hints, ui::Ctx().glyphs, width);
        }
        ImGui::EndChild();

        HandleShortcuts();
        DrawOverlay();
    }

    void CollectionPage::DrawHeader(float width)
    {
        const ImVec2 top = ImGui::GetCursorScreenPos();
        ui::Text(ui::TextStyle::Display, Tr("Collection"), ui::color::Ink, -1);
        const ImVec2 title = ImGui::GetItemRectSize();
        // The real portal's state beside the title, centred on its capitals (a pill, as on the game page).
        if (!m_app.m_portal.Current().Line().empty())
        {
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMax().x + ui::space::M, std::round(top.y + ui::CapMiddle(ui::TextStyle::Display) - ui::PillHeight / 2)));
            m_app.DrawPortalStatus();
        }

        // Add a figure, and the rest of the collection's work under Manage.
        ui::ButtonOptions add;
        add.kind = ui::ButtonKind::Primary;
        add.icon = ui::Icon::Plus;
        ui::ButtonOptions manage;
        manage.trailing = ui::Icon::ChevronDown;
        const float addWidth = ui::ButtonWidth(Tr("Add a figure"), add), manageWidth = ui::ButtonWidth(Tr("Manage"), manage);
        // On the title's line: the buttons' middle on its capitals' middle.
        const float buttonsY = std::round(top.y + ui::CapMiddle(ui::TextStyle::Display) - ui::height::Normal / 2);
        const float row = std::max(title.y, buttonsY + ui::height::Normal - top.y);
        ImGui::SetCursorScreenPos(ImVec2(top.x + width - addWidth - ui::space::S - manageWidth, buttonsY));
        if (ui::Button(Tr("Add a figure"), add))
        {
            m_overlay = Overlay::Add;
            m_addToy = 0xFFFF;
            m_created.clear();
            m_addGame = m_model.gameId;
        }
        m_app.TakeFocus(FocusTo::Page);
        ImGui::SameLine(0, ui::space::S);
        if (ui::Button(Tr("Manage"), manage) || m_testMenu == 1)
        {
            ui::OpenMenu("##collectionmenu", {.alignRight = true});
            m_testMenu = 0;
        }
        if (ui::BeginMenu("##collectionmenu"))
        {
            auto entry = [&](const char* label, int tab, ui::Icon icon = ui::Icon::None) {
                if (ui::MenuItem(label, { icon }))
                {
                    m_overlay = Overlay::Manage;
                    m_manageTab = tab;
                }
            };
            entry(Tr("Trash"), 0, ui::Icon::Trash);
            entry(Tr("Backups"), 1);
            entry(Tr("Import and export"), 2, ui::Icon::Download);
            entry(Tr("Unknown toys"), 3);
            entry(Tr("Tidy the starters"), 4);
            ui::MenuSeparator();
            if (ui::MenuItem(Tr("Show the collection folder"), { ui::Icon::Folder }))
            {
                std::string folder = collection::Folder();
                OpenFolder(std::filesystem::path(std::u8string(folder.begin(), folder.end())));
            }
            ui::EndMenu();
        }
        // What the page holds, under the title.
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + row + ui::space::XXS));
        const int shown = int(m_model.figures.size()) - m_hiddenStarters;
        const size_t favourites = std::count_if(m_model.figures.begin(), m_model.figures.end(), [](const Figure& f) { return f.favourite; });
        std::string summary = collection::FigureCount(shown, French());
        if (favourites > 0)
            summary += favourites == 1 ? Tf(" · {} favorite", favourites) : Tf(" · {} favorites", favourites);
        ui::Text(ui::TextStyle::Caption, summary.c_str(), ui::color::InkSoft, -1);
        ImGui::SetCursorScreenPos(ImVec2(top.x, ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y + ui::space::L));
    }

    void CollectionPage::DrawSeatMenu()
    {
        if (!ui::BeginMenu("##seatmenu"))
            return;
        const size_t seat = size_t(m_seatMenu);
        const Figure* figure = seat < m_model.seats.size() && !m_model.seats[seat].empty() ? m_model.Find(m_model.seats[seat]) : nullptr;
        if (seat >= m_model.seats.size() || m_model.seats[seat].empty())
            ImGui::CloseCurrentPopup();
        else
        {
            if (ui::MenuItem(Tr("Take it off"), { ui::Icon::Cross, false, false, !m_model.CanEditSeats() }))
                TakeOff(seat);
            if (ui::MenuItem(Tr("Choose another"), { ui::Icon::None, false, false, !m_model.CanEditSeats() }))
                Choose(int(seat));
            if (ui::MenuItem(Tr("Open its sheet"), { ui::Icon::None, false, false, figure == nullptr }))
                OpenFigure(figure->path);
        }
        ui::EndMenu();
    }

    // The portal at the next start: the chosen game's seats in a row, what each holds, and how to change it.
    void CollectionPage::TestHeroPortal(bool installing, bool running)
    {
        m_heroFixture = m_heroLoaded = true;
        m_model.figures.clear();
        m_model.seats.assign(3, "");
        for (int i = 0; i < (installing ? 1 : 2); i++)
        {
            Figure figure;
            figure.type = i == 0 ? 9 : 19;
            figure.toy = collection::Catalogue::Get().Find(figure.type);
            figure.variantInfo.name = figure.toy != nullptr ? figure.toy->name : "?";
            figure.path = i == 0 ? "hero-eruptor" : "hero-trigger-happy";
            figure.level = i == 0 ? (running ? 11 : 10) : 2;
            m_model.seats[size_t(i)] = figure.path;
            m_model.figures.push_back(figure);
        }
    }

    void CollectionPage::DrawHeroPortal(ImVec2 at, ImVec2 size, bool live)
    {
        // Read Collection's seats without opening its migration or editing flows.
        if (m_model.gameId != m_app.m_s->game.id)
        {
            SelectPortalGame(m_app.m_s->game.id);
            m_heroLoaded = true;
        }
        if (!m_heroLoaded)
        {
            m_model.Load();
            m_heroLoaded = true;
        }
        if (!m_heroFixture) m_model.Poll(SDL_GetTicks());
        if (m_model.Game() == nullptr)
            return; // a game the catalogue doesn't list yet has no seats to show
        ImGui::SetCursorScreenPos(at);
        ui::BeginCard("##hero-portal", size, { .panel = true, .padding = ui::space::M, .edge = live ? ui::color::Success : 0 });
        ui::Text(ui::TextStyle::Body, Tr("On the portal"), ui::color::Dim, -1, true);
        if (live)
        {
            ImGui::SameLine();
            ui::Badge(Tr("Live"), ui::color::Success);
        }
        const ui::ButtonOptions open{ .kind = ui::ButtonKind::Ghost, .size = ui::Size::Small };
        const float linkW = ui::ButtonWidth(Tr("Open the Toy Box"), open);
        if (ImGui::GetItemRectMax().x + ui::space::S + linkW < at.x + size.x - ui::space::M)
            ImGui::SetCursorScreenPos(ImVec2(at.x + size.x - ui::space::M - linkW, at.y + ui::space::M));
        if (ui::Button(Tr("Open the Toy Box"), open))
            m_app.GoTo(Page::Collection);
        ImGui::SetCursorScreenPos(ImVec2(at.x + ui::space::M, std::max(ImGui::GetCursorScreenPos().y, at.y + ui::space::M + ui::height::Small) + ui::space::XS));
        const collection::Game* game = m_model.Game();
        if (game != nullptr)
            for (size_t i = 0; i < game->seats.size(); i++)
            {
                const std::string& seatId = game->seats[i].id;
                if (seatId != "p1" && seatId != "p2" && seatId != "item") continue;
                const Figure* figure = i < m_model.seats.size() ? m_model.Find(m_model.seats[i]) : nullptr;
                const std::string seat = SeatName(i);
                const std::string name = figure != nullptr ? Name(*figure) : Tf("{} · empty", seat);
                std::string detail;
                if (figure != nullptr)
                {
                    const collection::Card card = CardOf(*figure);
                    const std::string level = m_heroFixture ? Tf("Level {}", figure->level) : card.Swapper()
                        ? collection::CardLevelText(card, m_model.gameId, French()) : collection::LevelText(*figure, m_model.gameId, French());
                    detail = level.empty() ? seat : Tf("{} · {}", seat, level);
                }
                ImGui::PushID(int(i));
                ui::ListRowOptions row;
                row.subtitle = figure != nullptr ? detail.c_str() : nullptr;
                row.titleStyle = figure != nullptr ? ui::TextStyle::Label : ui::TextStyle::Body;
                row.subtitleStyle = ui::TextStyle::Body;
                row.minHeight = ui::height::Large;
                row.dim = figure == nullptr;
                row.imageWidth = ui::height::Normal;
                row.image = [&](ImDrawList* draw, ImVec2 picture, ImVec2 box) {
                    const float side = std::min(box.x, box.y);
                    picture.y += (box.y - side) / 2;
                    if (figure != nullptr)
                    {
                        if (!DrawSwapper(draw, CardOf(*figure), picture, side))
                            DrawPicture(draw, figure->toy, figure->Kind(), figure->variant, picture, side);
                    }
                    else
                        ui::DrawEmptySeat(draw, picture, side);
                };
                if (ui::ListRow((name + "##hero-seat").c_str(), row))
                {
                    if (!m_opened) Open();
                    Choose(live ? -1 : int(i));
                    m_app.GoTo(Page::Collection);
                }
                ImGui::PopID();
            }
        ui::EndCard();
    }

    void CollectionPage::DrawSeats(float width)
    {
        const collection::Game* game = m_model.Game();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const std::string live = m_model.RunningGame() == m_model.gameId ? m_model.gameId : std::string();
        const bool editable = m_model.CanEditSeats();

        // Over the seats: the game they are for (the games that can start with prepared seats), and the game running, if it is
        // that one (its Toy Box has the portal then).
        const ImVec2 line = ImGui::GetCursorScreenPos();
        const float textY = std::round(line.y + ui::height::Normal / 2 - ui::CapMiddle(ui::TextStyle::Body));
        ImGui::SetCursorScreenPos(ImVec2(line.x, textY));
        ui::Text(ui::TextStyle::Body, Tr("On the portal for"), ui::color::InkSoft, -1);
        ui::SelectOptions selectOptions;
        for (const collection::Game& g : collection::Catalogue::Get().Games())
            if (!SessionFile(g).empty())
                selectOptions.width = std::max(selectOptions.width, ui::SelectWidth(ShortGameName(g.id).c_str()));
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMax().x + ui::space::XS, line.y));
        if (ui::BeginSelect("##portalgame", ShortGameName(m_model.gameId).c_str(), selectOptions))
        {
            for (const collection::Game& g : collection::Catalogue::Get().Games())
                if (!SessionFile(g).empty() && ui::SelectOption(ShortGameName(g.id).c_str(), g.id == m_model.gameId))
                    SelectPortalGame(g.id);
            ui::EndSelect();
        }
        else
            ui::Tooltip(Tr("The figures on the portal when the game starts"));
        if (!live.empty())
        {
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMax().x + ui::space::M, std::round(line.y + (ui::height::Normal - ui::PillHeight) / 2)));
            ui::StatusPill(ui::Status::Info, Tf("{} is running: use its Toy Box", ShortGameName(live)).c_str());
        }
        ImGui::SetCursorScreenPos(ImVec2(line.x, line.y + ui::height::Normal + ui::space::S));
        if (game == nullptr)
            return;

        const size_t count = game->seats.size();
        const float w = std::floor((width - SeatGap * float(count - 1)) / float(count));
        const ImVec2 row = ImGui::GetCursorScreenPos();
        m_navSeat = m_hoverSeat = -1;
        m_crossAt.assign(count, ImVec2(-1, -1));
        for (size_t i = 0; i < count; i++)
        {
            const ImVec2 at(row.x + float(i) * (w + SeatGap), row.y), end(i + 1 == count ? row.x + width : at.x + w, at.y + SeatHeight);
            ImGui::SetCursorScreenPos(at);
            ImGui::PushID(int(i));
            // The X is submitted over the seat: without this the seat keeps the hover and the X never sees the pointer.
            ImGui::SetNextItemAllowOverlap();
            if (ui::Hit("##seat", ImVec2(end.x - at.x, SeatHeight)))
                Choose(m_choosing == int(i) ? -1 : int(i));
            if (m_focusSeat == int(i))
            {
                ui::FocusLastItem(true);
                m_focusSeat = -1;
            }
            const ui::State state = ui::ItemState();
            const bool hovered = state.hovered, nav = ImGui::IsItemFocused();
            ui::FocusRingIf(state, ui::radius::Card);
            // Dropping a card on a seat puts the figure there.
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SKYFIG"))
                    PutOn(i, std::string(static_cast<const char*>(payload->Data)));
                ImGui::EndDragDropTarget();
            }
            const Figure* figure = m_model.seats[i].empty() ? nullptr : m_model.Find(m_model.seats[i]);
            const bool occupied = !m_model.seats[i].empty();
            if (nav)
                m_navSeat = int(i);
            if (hovered)
                m_hoverSeat = int(i);
            // Dragging a figure off its seat, onto the shelf, takes it off.
            if (occupied && editable && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                size_t index = i;
                ImGui::SetDragDropPayload("SKYSEAT", &index, sizeof(index));
                ui::Text(ui::TextStyle::Body, figure != nullptr ? Name(*figure).c_str() : Tr("Take it off"), ui::color::Ink, -1);
                ImGui::EndDragDropSource();
            }
            const bool menu = ImGui::IsItemClicked(ImGuiMouseButton_Right) && occupied;
            ImGui::PopID();
            if (menu) // outside the seat's ID scope: the menu is drawn by DrawSeatMenu at the page's level
            {
                m_seatMenu = int(i);
                ui::OpenMenu("##seatmenu");
            }
            const bool choosing = m_choosing == int(i);
            draw->AddRectFilled(at, end, CardFill(choosing || hovered || nav), ui::radius::Card);
            draw->AddRect(at, end, ui::Col(choosing ? ui::color::AccentFill : hovered ? ui::color::Edge : ui::color::Hairline), ui::radius::Card, 0,
                choosing ? ui::stroke::Edge : ui::stroke::Hairline);
            const ImVec2 picture(at.x + SeatPad, at.y + SeatPad);
            // Three lines beside the picture: the seat (leaving room for the X at the corner), the figure's name, its level.
            const float tx = picture.x + SeatPicture + ui::space::S, room = end.x - tx - SeatPad;
            const bool cross = occupied && editable && !(figure != nullptr && Busy(*figure));
            const float crossRoom = room - (cross ? ui::space::L : 0);
            const float top = at.y + std::round((SeatHeight - (2 * ui::type::Caption + ui::type::Body + 2 * ui::space::XXS)) / 2);
            const float second = top + ui::type::Caption + ui::space::XXS, third = second + ui::type::Body + ui::space::XXS;
            PutText(draw, ImVec2(tx, top), ui::TextStyle::Caption, false, choosing ? ui::color::Info : ui::color::Dim, SeatName(i), crossRoom);
            if (occupied && figure != nullptr)
            {
                if (collection::Card card = CardOf(*figure); !DrawSwapper(draw, card, picture, SeatPicture))
                {
                    DrawIcon(draw, figure->toy, figure->Kind(), picture, SeatPicture, false);
                    if (card.Swapper())
                        DrawBottom(draw, *card.second, ImVec2(picture.x + SeatPicture * 0.58f, picture.y + SeatPicture * 0.58f), SeatPicture * 0.42f);
                }
                PutText(draw, ImVec2(tx, second), ui::TextStyle::Body, true, ui::color::Ink, Name(*figure), room);
                if (std::string detail = Detail(*figure); !detail.empty())
                    PutText(draw, ImVec2(tx, third), ui::TextStyle::Caption, false, Busy(*figure) ? ui::color::Success : ui::color::Dim, detail, room);
            }
            else
            {
                // Empty, or a figure the collection no longer has (deleted, moved): the seat can still be emptied.
                const ImVec2 box(picture.x + SeatPicture, picture.y + SeatPicture);
                draw->AddRectFilled(picture, box, ui::Col(ui::color::Inset), ui::radius::Control);
                draw->AddRect(picture, box, ui::Col(choosing ? ui::color::AccentFill : ui::color::Hairline), ui::radius::Control, 0, ui::stroke::Edge);
                ui::DrawIcon(draw, ui::Icon::Plus, ImVec2(picture.x + SeatPicture / 2, picture.y + SeatPicture / 2), ui::icon::Normal,
                    ui::Col(choosing ? ui::color::AccentFill : ui::color::Dim));
                PutText(draw, ImVec2(tx, second), ui::TextStyle::Body, true, occupied ? ui::color::Danger : ui::color::InkSoft,
                    occupied ? Tr("Missing figure") : Tr("Empty"), room);
                PutText(draw, ImVec2(tx, third), ui::TextStyle::Caption, false, ui::color::Dim, choosing ? Tr("Pick one below") : editable ? Tr("Choose") : "", room);
            }
            if (cross)
            {
                ImGui::SetCursorScreenPos(ImVec2(end.x - ui::height::Small, at.y));
                ImGui::PushID(int(i) + 100);
                const bool off = ui::IconButton("##off", ui::Icon::Cross, Tr("Take it off"), { ui::ButtonKind::Ghost, ui::Size::Small }); // the pad takes it off with X on the seat
                const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                m_crossAt[i] = ImVec2((a.x + b.x) / 2, (a.y + b.y) / 2);
                ImGui::PopID();
                if (off)
                    TakeOff(i);
            }
        }
        DrawSeatMenu();
        ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + SeatHeight + ui::space::M));
    }

    // The bar: what the shelf is for (a seat being chosen for), the search, the order and Filters.
    void CollectionPage::DrawBar()
    {
        const collection::Game* game = m_model.Game();
        if (m_choosing >= 0 && game != nullptr)
        {
            // Choosing for a seat: the shelf shows only what goes there.
            ui::BeginCard("##choosing", ImVec2(0, ui::height::Large), { .padding = ui::space::XS });
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float right = at.x + ImGui::GetContentRegionAvail().x, middle = at.y + ui::height::Normal / 2;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const std::string text = Tf("Choose a figure for {}", SeatName(size_t(m_choosing)));
            ui::PushText(ui::TextStyle::Body, true);
            const float textWidth = ImGui::CalcTextSize(text.c_str()).x;
            ui::DrawText(draw, ImVec2(at.x + ui::space::S, middle - ui::type::Body / 2), ui::Col(ui::color::Ink), text.c_str());
            ui::PopText();
            ui::PushText(ui::TextStyle::Caption);
            ui::DrawText(draw, ImVec2(at.x + ui::space::S + textWidth + ui::space::S, middle - ui::type::Caption / 2), ui::Col(ui::color::InkSoft),
                Tf("only what {} reads", ShortGameName(m_model.gameId)).c_str());
            ui::PopText();
            ui::ButtonOptions cancel;
            cancel.size = ui::Size::Normal;
            ImGui::SetCursorScreenPos(ImVec2(right - ui::ButtonWidth(Tr("Cancel"), cancel), at.y));
            if (ui::Button(Tr("Cancel"), cancel))
                Choose(-1);
            ui::EndCard();
        }

        // The search first and wide, then the order (not a filter) and Filters (Element, Game, Type in one small panel); when
        // a filter is on, its chip, Clear all and the count of figures found.
        const int active = (m_filter.element >= 0) + !m_filter.game.empty() + (m_filter.category >= 0);
        ui::ButtonOptions filters;
        filters.trailing = ui::Icon::ChevronDown;
        const std::string filtersLabel = std::string(Tr("Filters")) + (active > 0 ? fmt::format(" · {}", active) : std::string());
        const float filtersWidth = ui::ButtonWidth(filtersLabel.c_str(), filters);
        ui::PushText(ui::TextStyle::Body);
        float sortWidth = 0;
        for (int sort = 0; sort < int(collection::Sort::Count); sort++)
            sortWidth = std::max(sortWidth, ImGui::CalcTextSize(Tf("Sort: {}", collection::SortLabel(collection::Sort(sort), French())).c_str()).x);
        ui::PopText();
        sortWidth += ui::space::XXL;
        const float searchWidth = std::max(ui::height::SettingControl / 2, ImGui::GetContentRegionAvail().x - filtersWidth - sortWidth - 2 * ui::space::S);
        const ImGuiID searchId = ImGui::GetID("##search");
        if (m_focusSearch)
        {
            ImGui::SetKeyboardFocusHere();
            m_focusSearch = false;
        }
        ui::SearchField("##search", m_search, sizeof(m_search), Tr("Name, element, game, level..."), searchWidth);
        m_navBar = ImGui::GetCurrentContext()->NavId == searchId;
        ImGui::SameLine(0, ui::space::S);

        ui::SelectOptions sortOptions;
        sortOptions.width = sortWidth;
        const bool sorting = ui::BeginSelect("##sort", Tf("Sort: {}", collection::SortLabel(m_filter.sort, French())).c_str(), sortOptions);
        m_navBar = m_navBar || ImGui::IsItemFocused();
        if (sorting)
        {
            for (int sort = 0; sort < int(collection::Sort::Count); sort++)
                if (ui::SelectOption(collection::SortLabel(collection::Sort(sort), French()).c_str(), m_filter.sort == collection::Sort(sort)))
                    m_filter.sort = collection::Sort(sort);
            ui::EndSelect();
        }
        ImGui::SameLine(0, ui::space::S);

        const bool pressed = ui::Button(filtersLabel.c_str(), filters);
        m_navBar = m_navBar || ImGui::IsItemFocused();
        if (pressed || m_openFilters)
            ui::OpenMenu("##filtersmenu", {.alignRight = true});
        m_openFilters = false;
        DrawFilters(active);
        DrawTags();
    }

    // The Filters panel: Element, Works in and Type, each a Select, and Clear all.
    void CollectionPage::DrawFilters(int active)
    {
        ImGui::SetNextWindowSize(ImVec2(FilterPanel, 0));
        if (!ui::BeginMenu("##filtersmenu"))
            return;
        if (m_closeFilters)
            ImGui::CloseCurrentPopup();
        m_closeFilters = false;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui::space::S, ui::space::S));
        // The labels' column fits the widest one (a longer translation never touches its box).
        ui::PushText(ui::TextStyle::Caption);
        float column = 0;
        for (const char* label : { N_("Element"), N_("Works in"), N_("Type") })
            column = std::max(column, ImGui::CalcTextSize(Tr(label)).x + ui::space::M);
        ui::PopText();
        const float room = ImGui::GetContentRegionAvail().x;
        auto row = [&](const char* label, const char* id, const std::string& value, const std::function<void()>& items) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            PutText(ImGui::GetWindowDrawList(), ImVec2(at.x + ui::space::XS, at.y + (ui::height::Normal - ui::type::Caption) / 2), ui::TextStyle::Caption, false,
                ui::color::InkSoft, Tr(label), column);
            ImGui::SetCursorScreenPos(ImVec2(at.x + column, at.y));
            ui::SelectOptions options;
            options.width = room - column;
            if (ui::BeginSelect(id, value.c_str(), options))
            {
                items();
                ui::EndSelect();
            }
        };
        row(N_("Element"), "##element", m_filter.element >= 0 ? ElementName(collection::Element(m_filter.element)) : Tr("All elements"), [&] {
            if (ui::SelectOption(Tr("All elements"), m_filter.element < 0))
                m_filter.element = -1;
            for (collection::Element element : MenuElements)
                if (ui::SelectOption(ElementName(element).c_str(), m_filter.element == int(element)))
                    m_filter.element = int(element);
        });
        row(N_("Works in"), "##game", m_filter.game.empty() ? Tr("All games") : ShortGameName(m_filter.game), [&] {
            if (ui::SelectOption(Tr("All games"), m_filter.game.empty()))
                m_filter.game.clear();
            for (const collection::Game& g : collection::Catalogue::Get().Games())
                if (ui::SelectOption(ShortGameName(g.id).c_str(), m_filter.game == g.id))
                    m_filter.game = g.id;
        });
        row(N_("Type"), "##type", m_filter.category >= 0 ? CategoryLabel(Category(m_filter.category)) : Tr("All types"), [&] {
            if (ui::SelectOption(Tr("All types"), m_filter.category < 0))
                m_filter.category = -1;
            // The types the collection has.
            for (int category = 0; category < int(Category::Count); category++)
                if (std::any_of(m_model.figures.begin(), m_model.figures.end(), [&](const Figure& f) { return f.toy != nullptr && int(f.toy->category) == category; }))
                    if (ui::SelectOption(CategoryLabel(Category(category)).c_str(), m_filter.category == category))
                        m_filter.category = category;
        });
        ui::ButtonOptions clear;
        clear.width = -1;
        clear.disabled = active == 0 && m_search[0] == 0;
        if (ui::Button(Tr("Clear all"), clear))
            ClearFilters();
        ImGui::PopStyleVar();
        ui::EndMenu();
    }

    void CollectionPage::ClearFilters()
    {
        m_search[0] = 0;
        ShelfFilter cleared;
        cleared.sort = m_filter.sort; // the order is not a filter
        m_filter = cleared;
    }

    // What is on, as chips (a click turns one off; the search is the field's own text), Clear all, and how many figures are found.
    void CollectionPage::DrawTags()
    {
        struct Tag
        {
            std::string label;
            unsigned color;
            int filter;
        };
        std::vector<Tag> tags;
        if (m_filter.element >= 0)
            tags.push_back({ ElementName(collection::Element(m_filter.element)), ElementInk(collection::Element(m_filter.element)), 0 });
        if (!m_filter.game.empty())
            tags.push_back({ Tf("Works in {}", ShortGameName(m_filter.game)), 0, 1 });
        if (m_filter.category >= 0)
            tags.push_back({ CategoryLabel(Category(m_filter.category)), 0, 2 });
        if (tags.empty() && m_search[0] == 0)
            return;

        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        int remove = -1;
        for (const Tag& tag : tags)
        {
            ui::ChipOptions options;
            options.selected = true;
            options.color = tag.color;
            if (ui::Chip(tag.label.c_str(), options))
                remove = tag.filter;
            m_navBar = m_navBar || ImGui::IsItemFocused();
            ImGui::SameLine(0, ui::space::XS);
        }
        ui::ButtonOptions clear;
        clear.kind = ui::ButtonKind::Ghost;
        clear.size = ui::Size::Small;
        if (ui::Button(Tr("Clear all"), clear))
            ClearFilters();
        m_navBar = m_navBar || ImGui::IsItemFocused();
        const std::string count = collection::FigureCount(FoundCount(), French());
        ui::PushText(ui::TextStyle::Caption);
        const float countWidth = ImGui::CalcTextSize(count.c_str()).x;
        ui::PopText();
        ImGui::SameLine(0, 0);
        ImGui::SetCursorScreenPos(ImVec2(right - countWidth, ImGui::GetCursorScreenPos().y + (ui::height::Small - ui::type::Caption) / 2));
        ui::Text(ui::TextStyle::Caption, count.c_str(), ui::color::InkSoft, -1);
        switch (remove)
        {
        case 0: m_filter.element = -1; break;
        case 1: m_filter.game.clear(); break;
        case 2: m_filter.category = -1; break;
        default: break;
        }
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
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float art = width - 2 * ui::space::S;
        const float height = ui::space::S + art + ui::space::XS + 2 * ui::type::Caption + ui::space::XXS + ui::space::S;
        const bool busy = Busy(figure);
        ImGui::PushID(figure.path.c_str());
        const bool pressed = ui::Hit("##figure", ImVec2(width, height));
        if (m_focusCard)
        {
            ui::FocusLastItem(true);
            m_focusCard = false;
        }
        const ui::State state = ui::ItemState();
        const bool hovered = state.hovered, focused = ImGui::IsItemFocused();
        ui::FocusRingIf(state, ui::radius::Card);
        const bool rightClick = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        if (hovered || focused)
            m_hovered = figure.path;
        if (focused)
            m_selected = figure.path;
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            ImGui::SetDragDropPayload("SKYFIG", figure.path.c_str(), figure.path.size() + 1);
            ui::Text(ui::TextStyle::Body, Name(figure).c_str(), ui::color::Ink, -1);
            ImGui::EndDragDropSource();
        }
        std::string tip = Name(figure);
        if (busy)
            tip += "\n" + Tf("On the portal in {}: read-only", LiveGame(figure));
        else if (dim)
            tip += "\n" + Tf("Not for {}", FullGameName(m_model.gameId));
        else if (m_choosing >= 0)
            tip += "\n" + Tf("Click: put it on {}", SeatName(size_t(m_choosing)));
        else
            tip += std::string("\n") + Tr("Click: sheet · drag to a seat");
        if (hovered) // under the pointer only: with the pad's focus the legend says what the buttons do
            ui::Bubble(tip.c_str(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
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
        const ImVec2 end(at.x + width, at.y + height), picture(at.x + ui::space::S, at.y + ui::space::S);
        const int seat = m_model.SeatOf(figure.path);
        const bool selected = m_selected == figure.path && focused;
        draw->AddRectFilled(at, end, CardFill(hovered), ui::radius::Card);
        draw->AddRect(at, end, ui::Col(seat >= 0 ? ui::color::AccentFill : selected || hovered ? ui::color::Edge : ui::color::Hairline), ui::radius::Card, 0,
            seat >= 0 || selected ? ui::stroke::Edge : ui::stroke::Hairline);
        if (collection::Card card = CardOf(figure); !DrawSwapper(draw, card, picture, art))
        {
            DrawIcon(draw, figure.toy, figure.Kind(), picture, art, false);
            { // the busts end flat: fade their foot into the card
                const ImVec2 top(picture.x, picture.y + art - ui::space::L), foot(picture.x + art, picture.y + art + 1);
                const ImU32 clear = ui::Col(hovered ? ui::color::Raised : ui::color::Surface, 0), solid = CardFill(hovered);
                draw->AddRectFilledMultiColor(top, foot, clear, clear, solid, solid);
            }
            if (card.Swapper())
                DrawBottom(draw, *card.second, ImVec2(picture.x + art * 0.62f, picture.y + art * 0.62f), art * 0.38f);
        }

        // The name (cut with "..." when it is long: the tooltip has it whole), then its level.
        const float room = width - 2 * ui::space::XS, textY = picture.y + art + ui::space::XS;
        auto centred = [&](ui::TextStyle style, bool bold, unsigned color, const std::string& text, float y) {
            ui::PushText(style, bold);
            const std::string cut = ui::Ellipsize(text.c_str(), room);
            const float w = ImGui::CalcTextSize(cut.c_str()).x;
            ui::DrawText(draw, ImVec2(at.x + (width - w) / 2, y), ui::Col(color), cut.c_str());
            ui::PopText();
        };
        centred(ui::TextStyle::Caption, true, ui::color::Ink, Name(figure), textY);
        if (std::string detail = Detail(figure); !detail.empty())
            centred(ui::TextStyle::Caption, false, busy ? ui::color::Success : ui::color::Dim, detail, textY + ui::type::Caption + ui::space::XXS);

        // Marks: the variant, favorite, seat.
        collection::VariantKind kind = figure.Kind();
        if (kind != collection::VariantKind::Base)
            ui::DrawBadge(draw, ImVec2(at.x + ui::space::XS, at.y + ui::space::XS), KindLabel(kind).c_str(), collection::VariantKindRgb(kind));
        if (figure.favourite)
            ui::DrawIcon(draw, ui::Icon::Star, ImVec2(end.x - ui::space::M, at.y + ui::space::M), ui::icon::Small, ui::Col(ui::color::Warning));
        if (seat >= 0)
            ui::DrawBadge(draw, ImVec2(at.x + ui::space::XS, picture.y + art - ui::space::L), SeatTag(size_t(seat)).c_str(), ui::color::AccentFill, ui::color::OnAccent);
        if (dim)
            draw->AddRectFilled(at, end, ui::Col(ui::color::Scrim, float(ui::elevation::ScrimAlpha) / 255.0f), ui::radius::Card);
        return pressed;
    }

    void CollectionPage::DrawList()
    {
        m_hovered.clear();
        m_navTopCard = false;
        bool filtered = m_search[0] != 0 || m_filter.element >= 0 || m_filter.category >= 0 || !m_filter.game.empty();
        if (m_list.empty())
        {
            const bool none = int(m_model.figures.size()) == m_hiddenStarters;
            if (none)
            {
                if (ui::EmptyState(ui::Icon::Plus, Tr("No figures yet. Add one, or drop your figure files on the window."), Tr("Add a figure")))
                {
                    m_overlay = Overlay::Add;
                    m_addToy = 0xFFFF;
                    m_created.clear();
                    m_addGame = m_model.gameId;
                }
                ui::ButtonOptions starters;
                const std::string label = Tf("Create the {} starter pack", GameName(m_model.gameId.c_str(), GameNameForm::Short));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - ui::ButtonWidth(label.c_str(), starters)) / 2);
                if (ui::Button(label.c_str(), starters))
                {
                    // The figures in the box of each game's starter pack.
                    if (m_model.gameId == "giants")
                    {
                        collection::Create(112);        // Tree Rex
                        collection::Create(100);        // Jet-Vac
                        collection::Create(32, 0x1801); // Series 2 Cynder
                    }
                    else if (m_model.gameId == "swapforce")
                    {
                        collection::CreateSwapper(2015, 1015); // Wash Buckler
                        collection::CreateSwapper(2004, 1004); // Blast Zone
                        collection::Create(26, 0x2805);        // Ninja Stealth Elf
                    }
                    else if (m_model.gameId == "trapteam")
                    {
                        collection::Create(462);         // Snap Shot
                        collection::Create(476);         // Food Fight
                        collection::Create(211, 0x3001); // a Water Trap
                        collection::Create(217, 0x3003); // a Life Trap
                    }
                    else
                        for (uint16_t type : { 16, 14, 19 }) // Spyro, Gill Grunt, Trigger Happy
                            collection::Create(type);
                    Reload();
                }
            }
            else if (ui::EmptyState(ui::Icon::Search, m_choosing >= 0 && !filtered ? Tr("Nothing for this seat yet") : Tr("No figure matches"),
                         filtered ? Tr("Clear search and filters") : nullptr))
                ClearFilters();
            return;
        }

        const float available = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, int((available + ShelfGap) / (ShelfCell + ShelfGap)));
        const float cell = (available - ShelfGap * float(columns - 1)) / float(columns);
        for (size_t s = 0; s < m_sections.size(); s++)
        {
            const Section& section = m_sections[s];
            bool foreign = section.kind == Section::Kind::Foreign;
            // The shelf has no title (the bar says what it shows); what follows does.
            if (section.kind != Section::Kind::Shelf)
            {
                const std::string count = collection::FigureCount(int(section.items.size()), French());
                ui::SectionHeader(SectionName(section).c_str(), count.c_str());
                ImGui::Dummy(ImVec2(0, ui::space::XXS));
            }
            ImGui::PushID(int(s));
            for (size_t i = 0; i < section.items.size(); i++)
            {
                if (i % columns != 0)
                    ImGui::SameLine(0, ShelfGap);
                DrawCard(m_list[section.items[i]], cell, foreign);
                if (s == 0 && i < size_t(columns) && ImGui::IsItemFocused())
                    m_navTopCard = true;
            }
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, ui::space::XS));
        }
        ImGui::Dummy(ImVec2(0, ui::space::L)); // the last row is never cut flush with the shelf's edge
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

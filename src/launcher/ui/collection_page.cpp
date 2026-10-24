// The figure collection: every figure file as a grid with filters and search, a sheet per figure (its stats from the
// toy, and what can be done to it), the figure creator, import, and what starts on the portal. The files and their
// format are the figure engine's (shared/figures); this file is only the pages.
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/launcher_ui.h"
#include "shared/figures/figure_icons.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cctype>
#include <cstring>
#include <fmt/format.h>

namespace launcher_ui
{
    namespace fs = std::filesystem;
    using figures::Category;
    using figures::FigureStats;
    using figures::ToyInfo;

    namespace
    {
        // Stands in for a toy this launcher doesn't know (its icon and category).
        constexpr ToyInfo UnknownToy = { 0xFFFF, "Unknown toy", nullptr, figures::Element::None, Category::Skylander };

        std::string Lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(tolower(c)); });
            return text;
        }

        // "AB" for "Anvil Rain", "G" for "Gill"...: the letters written over a generated icon.
        std::string Initials(const char* name)
        {
            std::string out;
            bool start = true;
            for (const char* c = name; *c != 0 && out.size() < 2; c++)
            {
                if (start && isalpha(static_cast<unsigned char>(*c)))
                    out += char(toupper(static_cast<unsigned char>(*c)));
                start = *c == ' ' || *c == '-';
            }
            return out;
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

        std::string JoinList(const std::vector<std::string>& items)
        {
            std::string list;
            for (const std::string& item : items)
                list += (list.empty() ? "" : ",") + item;
            return list;
        }

        std::vector<std::string> ChooseFigureFiles(SDL_Window* window)
        {
            static const SDL_DialogFileFilter filters[] = { { "Figure dumps (.sky, .bin, .dmp)", "sky;bin;dmp" } };
            struct Result { std::atomic<bool> done{ false }; std::vector<std::string> paths; };
            Result result;
            SDL_PropertiesID properties = SDL_CreateProperties();
            SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, const_cast<SDL_DialogFileFilter*>(filters));
            SDL_SetNumberProperty(properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
            SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, "Import figures");
            SDL_SetBooleanProperty(properties, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, true);
            SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window);
            SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE, [](void* userdata, const char* const* files, int) {
                auto* r = static_cast<Result*>(userdata);
                for (const char* const* file = files; file != nullptr && *file != nullptr; file++)
                    r->paths.emplace_back(*file);
                r->done = true;
            }, &result, properties);
            SDL_DestroyProperties(properties);
            while (!result.done)
            {
                SDL_PumpEvents();
                SDL_Delay(10);
            }
            return result.paths;
        }
    }

    // ---- Data -----------------------------------------------------------------------------------------------------

    fs::path App::FiguresFolder() const
    {
        return fs::path(PreferencePath("figures"));
    }

    std::vector<std::string> App::Favourites() const
    {
        return SplitList(SettingGet(m_config, "collection", "favourites"));
    }

    void App::SetFavourite(const std::string& file, bool on)
    {
        std::vector<std::string> list = Favourites();
        list.erase(std::remove(list.begin(), list.end(), file), list.end());
        if (on)
            list.push_back(file);
        Set("collection", "favourites", JoinList(list));
        if (CollectionFigure* figure = FindFigure(file))
            figure->favourite = on;
    }

    CollectionFigure* App::FindFigure(const std::string& file)
    {
        for (CollectionFigure& figure : m_collection)
            if (figure.file == file)
                return &figure;
        return nullptr;
    }

    void App::LoadCollection()
    {
        m_collectionLoaded = true;
        m_collection.clear();
        fs::path folder = FiguresFolder();
        std::error_code error;
        for (const fs::directory_entry& entry : fs::directory_iterator(folder, error))
        {
            if (!entry.is_regular_file() || !figures::IsFigureFileName(entry.path().filename()))
                continue;
            CollectionFigure figure;
            figure.file = entry.path().filename().string();
            m_collection.push_back(std::move(figure));
            RefreshFigure(m_collection.back().file);
        }
        std::sort(m_collection.begin(), m_collection.end(), [](const CollectionFigure& a, const CollectionFigure& b) {
            const ToyInfo& toyA = a.toy != nullptr ? figures::BaseToy(*a.toy) : UnknownToy;
            const ToyInfo& toyB = b.toy != nullptr ? figures::BaseToy(*b.toy) : UnknownToy;
            if (toyA.category != toyB.category)
                return toyA.category < toyB.category;
            if (toyA.toyType != toyB.toyType)
                return toyA.toyType < toyB.toyType;
            return a.file < b.file;
        });
    }

    void App::RefreshFigure(const std::string& file)
    {
        CollectionFigure* figure = FindFigure(file);
        figures::FigureData data;
        if (!figures::LoadFigure(FiguresFolder() / file, data))
        {
            if (figure != nullptr)
                m_collection.erase(m_collection.begin() + (figure - m_collection.data()));
            return;
        }
        if (figure == nullptr)
        {
            m_collection.push_back({});
            figure = &m_collection.back();
            figure->file = file;
        }
        figure->header = figures::ReadHeader(data);
        figure->stats = figures::ReadStats(data);
        figure->toy = figures::FindToy(figure->header.toyType);
        figure->locked = figures::IsLocked(FiguresFolder(), file);
        std::vector<std::string> favourites = Favourites(), startup = StartupFigures();
        figure->favourite = std::find(favourites.begin(), favourites.end(), file) != favourites.end();
        figure->onPortal = std::find(startup.begin(), startup.end(), file) != startup.end();
    }

    void App::RefreshLocks()
    {
        uint64_t now = SDL_GetTicks();
        if (now < m_locksCheckedAt)
            return;
        m_locksCheckedAt = now + 2000; // a game takes and releases figures while the launcher is open
        for (CollectionFigure& figure : m_collection)
            figure.locked = figures::IsLocked(FiguresFolder(), figure.file);
    }

    void App::ImportFigureFile(const std::string& path)
    {
        figures::ImportResult result = figures::ImportFigure(FiguresFolder(), path);
        if (result.file.empty())
        {
            Toast(result.error);
            return;
        }
        if (!m_collectionLoaded)
            LoadCollection();
        else
            RefreshFigure(result.file);
        Toast("Imported " + result.file);
    }

    void App::ImportFigures()
    {
        for (const std::string& path : ChooseFigureFiles(m_window))
            ImportFigureFile(path);
    }

    void App::ClearFigureIcons()
    {
        for (auto& [name, icon] : m_figureIcons)
            if (icon.texture != nullptr)
                SDL_DestroyTexture(icon.texture);
        m_figureIcons.clear();
    }

    const FigureIconTexture& App::FigureIcon(const ToyInfo* toy, bool large)
    {
        const ToyInfo& shown = toy != nullptr ? *toy : UnknownToy;
        std::string image = large ? figures::PortraitLargeImageName(shown) : figures::PortraitImageName(shown);
        std::string key = image.empty() ? fmt::format("gen:{}", shown.toyType) : image;
        auto found = m_figureIcons.find(key);
        if (found != m_figureIcons.end())
            return found->second;

        // The disc's picture when it has been cached, else a generated icon (kept under the same key until the cache
        // changes: ClearFigureIcons after a disc is verified).
        FigureIconTexture icon;
        if (std::string path = m_assets.FigurePortraitPath(image); !path.empty())
            icon.texture = LoadPngFile(m_renderer, path);
        if (icon.texture == nullptr)
        {
            constexpr int Size = 128;
            icon.texture = LoadRgba(m_renderer, figures::GenerateIcon(shown, Size), Size, Size);
            icon.generated = true;
        }
        return m_figureIcons[key] = icon;
    }

    // ---- Widgets --------------------------------------------------------------------------------------------------

    bool App::Chip(const char* label, bool selected, unsigned color)
    {
        ImVec2 at = ImGui::GetCursorScreenPos();
        const char* end = strstr(label, "##");
        ImVec2 text = ImGui::CalcTextSize(label, end);
        ImVec2 size(text.x + 26, text.y + 14);
        bool pressed = ImGui::InvisibleButton(label, size);
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

    void App::DrawFigureIcon(ImDrawList* draw, const ToyInfo* toy, ImVec2 at, float size, bool large)
    {
        const FigureIconTexture& icon = FigureIcon(toy, large);
        if (icon.texture != nullptr)
            draw->AddImage(Ref(icon.texture), at, ImVec2(at.x + size, at.y + size));
        if (!icon.generated)
            return;
        std::string letters = Initials(toy != nullptr ? toy->name : "?");
        ImGui::PushFont(m_titleFont, size * 0.3f);
        ImVec2 text = ImGui::CalcTextSize(letters.c_str());
        ImVec2 position(at.x + (size - text.x) / 2, at.y + (size - text.y) / 2);
        draw->AddText(ImVec2(position.x + 1, position.y + 2), IM_COL32(0, 0, 0, 120), letters.c_str());
        draw->AddText(position, IM_COL32_WHITE, letters.c_str());
        ImGui::PopFont();
    }

    // Variant badge colours: the same identity as the generated icons' badges.
    static unsigned VariantColor(figures::Variant variant)
    {
        switch (variant)
        {
        case figures::Variant::Legendary: return 0xFFD54A;
        case figures::Variant::Dark: return 0x2E2440;
        case figures::Variant::Crystal: return 0xB0E8F5;
        case figures::Variant::Glow: return 0x9BFF6B;
        case figures::Variant::Metallic: return 0xC8CDD6;
        case figures::Variant::LightCore: return 0xFFF4A8;
        default: return PanelRaised;
        }
    }

    static unsigned VariantInk(figures::Variant variant)
    {
        return variant == figures::Variant::Dark ? Text : AccentInk;
    }

    // ---- The page -------------------------------------------------------------------------------------------------

    void App::DrawCollection()
    {
        if (!m_collectionLoaded)
            LoadCollection();
        RefreshLocks();

        float available = ImGui::GetContentRegionAvail().x;
        float width = std::min(available, 1080.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) / 2);
        ImGui::BeginChild("##collectioncolumn", ImVec2(width, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

        // Title, counts and the page's actions.
        ImVec2 top = ImGui::GetCursorScreenPos();
        Title("COLLECTION", HeadlineSize, Text);
        size_t onPortal = std::count_if(m_collection.begin(), m_collection.end(), [](const CollectionFigure& f) { return f.onPortal; });
        size_t inUse = std::count_if(m_collection.begin(), m_collection.end(), [](const CollectionFigure& f) { return f.locked; });
        std::string summary = fmt::format("{} figure{} · {} starting on the portal", m_collection.size(), m_collection.size() == 1 ? "" : "s", onPortal);
        if (inUse > 0)
            summary += fmt::format(" · {} in use by a game", inUse);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", summary.c_str());
        ImVec2 after = ImGui::GetCursorScreenPos();

        float buttonsW = 170 + 130 + 150 + 2 * 10;
        ImGui::SetCursorScreenPos(ImVec2(top.x + width - buttonsW, top.y + 4));
        if (PillButton("+  New figure", true, 170, true))
        {
            m_newFigureOpen = true;
            m_newBase = 0xFFFF;
        }
        ImGui::SameLine(0, 10);
        if (PillButton("Import...", false, 130, true))
            ImportFigures();
        ImGui::SameLine(0, 10);
        if (PillButton("Open folder", false, 150, true))
        {
            std::error_code error;
            fs::create_directories(FiguresFolder(), error);
            SDL_OpenURL(("file://" + FiguresFolder().string()).c_str());
        }
        ImGui::SetCursorScreenPos(ImVec2(after.x, after.y + 10));

        DrawPortalBar();
        ImGui::Dummy(ImVec2(0, 4));
        DrawCollectionFilters();
        ImGui::Dummy(ImVec2(0, 6));

        // The grid of figures that pass the filters.
        std::string search = Lower(m_search);
        std::vector<const CollectionFigure*> shown;
        for (const CollectionFigure& figure : m_collection)
        {
            const ToyInfo& toy = figure.toy != nullptr ? *figure.toy : UnknownToy;
            if (m_categoryFilter >= 0 && int(toy.category) != m_categoryFilter)
                continue;
            if (m_gameFilter >= 0 && (figure.toy == nullptr || !figures::PlayableIn(toy, figures::Game(m_gameFilter))))
                continue;
            if (m_favouritesOnly && !figure.favourite)
                continue;
            if (!search.empty() && Lower(std::string(toy.name)).find(search) == std::string::npos &&
                Lower(figure.stats.nickname).find(search) == std::string::npos && Lower(figure.file).find(search) == std::string::npos)
                continue;
            shown.push_back(&figure);
        }

        ImGui::BeginChild("##figuregrid", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        if (m_collection.empty() || shown.empty())
        {
            ImGui::Dummy(ImVec2(0, 30));
            Title(m_collection.empty() ? "No figures yet" : "No figure matches", TitleSize, Text);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
            ImGui::TextWrapped("%s", m_collection.empty()
                ? "Create one with + New figure, import your own dumps (.sky, .bin, .dmp: Import..., or drop them on the window), "
                  "or start the game once: it creates a figure for every Skylander."
                : "Change the category, the game or the search.");
            ImGui::PopStyleColor();
        }
        else
        {
            constexpr float Cell = 148, Gap = 14;
            int columns = std::max(1, int((ImGui::GetContentRegionAvail().x + Gap) / (Cell + Gap)));
            for (size_t i = 0; i < shown.size(); i++)
            {
                if (i % columns != 0)
                    ImGui::SameLine(0, Gap);
                if (DrawFigureCard(*shown[i], Cell))
                {
                    m_sheetFile = shown[i]->file;
                    m_confirm.clear();
                    snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", shown[i]->stats.nickname.c_str());
                }
            }
            ImGui::Dummy(ImVec2(0, 10));
        }
        ImGui::EndChild();
        ImGui::EndChild();

        DrawFigureSheet();
        DrawNewFigure();
    }

    void App::DrawPortalBar()
    {
        ImVec2 at = ImGui::GetCursorScreenPos();
        float width = ImGui::GetContentRegionAvail().x, height = 64;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), Rgb(Panel, 235), Radius);
        draw->AddRect(at, ImVec2(at.x + width, at.y + height), Rgb(Border), Radius);

        ImGui::SetCursorScreenPos(ImVec2(at.x + 18, at.y + 10));
        ImGui::BeginGroup();
        ImGui::PushFont(nullptr, SmallSize);
        BoldText("PORTAL", TextDim);
        ImGui::PopFont();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 4));
        ImGui::PushFont(nullptr, SmallSize + 1);
        int mode = ChoiceIndex(PortalChoices, SettingGet(m_config, "portal", "mode"), 0);
        ImGui::SetNextItemWidth(300);
        if (ImGui::BeginCombo("##portalmode", PortalChoices[mode].label))
        {
            for (int i = 0; i < int(std::size(PortalChoices)); i++)
                if (ImGui::Selectable(PortalChoices[i].label, i == mode))
                    Set("portal", "mode", PortalChoices[i].value);
            ImGui::EndCombo();
        }
        ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::EndGroup();

        // What starts on the portal: the figures' icons, click one to take it off.
        float x = at.x + 18 + 300 + 30;
        ImGui::PushFont(nullptr, SmallSize);
        draw->AddText(ImVec2(x, at.y + 10), Rgb(TextDim), "STARTS ON THE PORTAL");
        ImGui::PopFont();
        std::vector<std::string> startup = StartupFigures();
        float iconX = x;
        if (startup.empty())
            draw->AddText(ImVec2(x, at.y + 32), Rgb(TextSecondary), "Nothing yet: open a figure and choose \"On the portal at start\".");
        for (const std::string& file : startup)
        {
            const CollectionFigure* figure = FindFigure(file);
            ImGui::SetCursorScreenPos(ImVec2(iconX, at.y + 28));
            ImGui::PushID(file.c_str());
            if (ImGui::InvisibleButton("##portalfigure", ImVec2(30, 30)))
            {
                SetStartupFigure(file, false);
                if (CollectionFigure* changed = FindFigure(file))
                    changed->onPortal = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\nClick to take it off the portal", figure != nullptr && figure->toy != nullptr ? figure->toy->name : file.c_str());
            ImGui::PopID();
            DrawFigureIcon(draw, figure != nullptr ? figure->toy : nullptr, ImVec2(iconX, at.y + 28), 30, false);
            iconX += 36;
        }
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + height + 10));
    }

    void App::DrawCollectionFilters()
    {
        // Search, the game the figures must work in, favourites.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 8));
        ImGui::SetNextItemWidth(300);
        ImGui::InputTextWithHint("##search", "Search figures, nicknames, files", m_search, sizeof(m_search));
        ImGui::SameLine(0, 12);
        ImGui::SetNextItemWidth(260);
        std::string gameLabel = m_gameFilter < 0 ? std::string("Playable in any game") : std::string("Playable in ") + figures::GameName(figures::Game(m_gameFilter));
        if (ImGui::BeginCombo("##gamefilter", gameLabel.c_str()))
        {
            if (ImGui::Selectable("Any game", m_gameFilter < 0))
                m_gameFilter = -1;
            for (int game = 0; game < int(figures::Game::Count); game++)
                if (ImGui::Selectable(figures::GameName(figures::Game(game)), game == m_gameFilter))
                    m_gameFilter = game;
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
        ImGui::SameLine(0, 12);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1);
        if (Chip(m_favouritesOnly ? "Favourites only" : "Favourites", m_favouritesOnly))
            m_favouritesOnly = !m_favouritesOnly;

        // Categories, with how many figures each has.
        ImGui::Dummy(ImVec2(0, 2));
        auto count = [&](int category) {
            return std::count_if(m_collection.begin(), m_collection.end(), [&](const CollectionFigure& f) {
                return category < 0 || int((f.toy != nullptr ? *f.toy : UnknownToy).category) == category;
            });
        };
        float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        for (int category = -1; category < int(Category::Count); category++)
        {
            std::string label = fmt::format("{}  {}##cat{}", category < 0 ? "All" : figures::CategoryName(Category(category)), count(category), category);
            ImVec2 size = ImGui::CalcTextSize(label.c_str(), strstr(label.c_str(), "##"));
            if (category > -1)
            {
                ImGui::SameLine(0, 8);
                if (ImGui::GetCursorScreenPos().x + size.x + 26 > right)
                    ImGui::NewLine();
            }
            if (Chip(label.c_str(), m_categoryFilter == category))
                m_categoryFilter = category;
        }
    }

    bool App::DrawFigureCard(const CollectionFigure& figure, float size)
    {
        const ToyInfo& toy = figure.toy != nullptr ? *figure.toy : UnknownToy;
        ImVec2 at = ImGui::GetCursorScreenPos();
        float height = size + 48;
        ImGui::PushID(figure.file.c_str());
        bool pressed = ImGui::InvisibleButton("##figure", ImVec2(size, height));
        bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 end(at.x + size, at.y + height);
        draw->AddRectFilled(at, end, Rgb(hovered ? PanelRaised : Panel, 240), Radius);
        draw->AddRect(at, end, Rgb(figure.onPortal ? Accent : hovered ? BorderBright : Border), Radius, 0, figure.onPortal ? 2.5f : 1.0f);

        float icon = size - 40;
        DrawFigureIcon(draw, figure.toy, ImVec2(at.x + 20, at.y + 12), icon, false);

        // Name (the nickname when it has one), then what it is.
        std::string name = !figure.stats.nickname.empty() ? figure.stats.nickname : toy.name;
        name = LauncherMiddleEllipsis(name, 17);
        ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
        AddBoldText(draw, ImVec2(at.x + std::max(8.0f, (size - nameSize.x) / 2), at.y + icon + 18), Rgb(Text), name.c_str());
        std::string detail;
        if (figure.toy == nullptr)
            detail = fmt::format("Toy type {}", figure.header.toyType);
        else if (!figures::HasProgress(toy))
            detail = figures::CategoryName(toy.category);
        else if (figure.stats.state == FigureStats::State::Valid)
            detail = fmt::format("Level {} · {} gold", figure.stats.level, figure.stats.gold);
        else if (figure.stats.state == FigureStats::State::Blank)
            detail = "New";
        else
            detail = "Progress unreadable";
        ImGui::PushFont(nullptr, SmallSize);
        ImVec2 detailSize = ImGui::CalcTextSize(detail.c_str());
        draw->AddText(ImVec2(at.x + std::max(8.0f, (size - detailSize.x) / 2), at.y + icon + 40), Rgb(TextDim), detail.c_str());
        ImGui::PopFont();

        // Badges: variant, favourite, portal, in use.
        if (toy.variant != figures::Variant::Normal)
            Badge(draw, ImVec2(at.x + 8, at.y + 8), figures::VariantName(toy.variant), VariantColor(toy.variant), VariantInk(toy.variant));
        if (figure.favourite)
            DrawStar(draw, ImVec2(end.x - 16, at.y + 16), 9, Rgb(0xFFD54A));
        if (figure.onPortal)
            Badge(draw, ImVec2(at.x + 8, at.y + icon - 6), "PORTAL", Accent, AccentInk);
        if (figure.locked)
        {
            draw->AddRectFilled(at, end, IM_COL32(0, 0, 0, 110), Radius);
            Badge(draw, ImVec2(at.x + (size - 70) / 2, at.y + icon / 2), "IN USE", Danger, Text);
        }
        if (hovered)
            ImGui::SetTooltip("%s\n%s%s", toy.name, figure.file.c_str(), figure.locked ? "\nIn use by a running game" : "");
        return pressed;
    }

    // ---- The figure sheet -----------------------------------------------------------------------------------------

    void App::DrawFigureSheet()
    {
        if (!m_sheetFile.empty() && !ImGui::IsPopupOpen("##figuresheet"))
            ImGui::OpenPopup("##figuresheet");
        ImVec2 display = ImGui::GetIO().DisplaySize;
        ImVec2 size(std::min(860.0f, display.x - 80), std::min(600.0f, display.y - 60));
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowPos(ImVec2(display.x / 2, display.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Rgb(Panel, 250));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 140));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, Pad));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, RadiusLarge);
        bool open = ImGui::BeginPopupModal("##figuresheet", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        if (!open)
            return;

        CollectionFigure* figure = m_sheetFile.empty() ? nullptr : FindFigure(m_sheetFile);
        if (figure == nullptr || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_sheetFile.clear();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        const ToyInfo& toy = figure->toy != nullptr ? *figure->toy : UnknownToy;
        const FigureStats& stats = figure->stats;
        std::string file = figure->file; // copied: actions below may reload the collection
        bool editable = !figure->locked;
        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Left: the large picture and its badges.
        constexpr float Picture = 250;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        draw->AddRectFilled(origin, ImVec2(origin.x + Picture, origin.y + Picture), Rgb(Field), RadiusLarge);
        DrawFigureIcon(draw, figure->toy, ImVec2(origin.x + 10, origin.y + 10), Picture - 20, true);
        float badgeX = origin.x;
        auto badge = [&](const char* text, unsigned fill, unsigned ink) {
            ImGui::PushFont(nullptr, SmallSize - 1);
            float w = ImGui::CalcTextSize(text).x + 12;
            ImGui::PopFont();
            Badge(draw, ImVec2(badgeX, origin.y + Picture + 10), text, fill, ink);
            badgeX += w + 6;
        };
        if (toy.variant != figures::Variant::Normal)
            badge(figures::VariantName(toy.variant), VariantColor(toy.variant), VariantInk(toy.variant));
        if (toy.element != figures::Element::None)
            badge(figures::ElementName(toy.element), PanelRaised, Text);
        badge(figures::CategoryName(toy.category), PanelRaised, Text);

        // Right: name, what it is, where it plays, its stats.
        float right = origin.x + Picture + 28;
        ImGui::SetCursorScreenPos(ImVec2(right, origin.y));
        ImGui::BeginGroup();
        Title(!stats.nickname.empty() ? stats.nickname.c_str() : toy.name, HeadlineSize - 6, Text);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s%s", !stats.nickname.empty() ? toy.name : "", !stats.nickname.empty() ? " · " : "");
        ImGui::SameLine(0, 0);
        ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", file.c_str());

        std::string games;
        for (int game = 0; game < int(figures::Game::Count); game++)
            if (figure->toy != nullptr && figures::PlayableIn(toy, figures::Game(game)))
                games += (games.empty() ? "" : ", ") + std::string(figures::GameName(figures::Game(game)));
        ImGui::PushTextWrapPos(ImGui::GetWindowPos().x + size.x - Pad);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "Playable in: %s", games.empty() ? "no game this launcher knows" : games.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 4));

        if (figure->locked)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Danger));
            ImGui::TextWrapped("In use by a running game: it is on the game's portal, so the launcher won't change it until the game lets it go.");
            ImGui::PopStyleColor();
        }

        if (ImGui::BeginTable("##stats", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 130);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
            auto row = [](const char* name, const std::string& value) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", name);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(value.c_str());
            };
            if (!figures::HasProgress(toy) && figure->toy != nullptr)
                row("Progress", std::string(figures::CategoryName(toy.category)) + " keep no progress.");
            else if (stats.state == FigureStats::State::Blank)
                row("Progress", "New: never played yet. Its stats show up once a game has used it.");
            else if (stats.state == FigureStats::State::Unreadable)
                row("Progress", "Unreadable: its checksums don't match Spyro's Adventure's layout. The launcher won't change it.");
            else
            {
                row("Level", fmt::format("{} ({} XP)", stats.level, stats.experience));
                row("Gold", std::to_string(stats.gold));
                row("Nickname", stats.nickname.empty() ? std::string("None") : stats.nickname);
                row("Hat", stats.hat == 0 ? std::string("None") : fmt::format("Hat {}", stats.hat));
                row("Upgrades", fmt::format("{} unlocked", std::popcount(stats.upgrades)));
                row("Play time", PlayTime(stats.playSeconds));
            }
            row("Toy type", fmt::format("{}{}", figure->header.toyType, figure->header.variantId != 0 ? fmt::format(" (variant {:04X})", figure->header.variantId) : ""));
            ImGui::EndTable();
        }
        ImGui::EndGroup();

        // Actions, along the bottom of the sheet.
        float bottom = ImGui::GetWindowPos().y + size.y - Pad;
        ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom - 40 - 52));
        bool canRename = editable && stats.state == FigureStats::State::Valid;
        ImGui::BeginDisabled(!canRename);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 10));
        ImGui::SetNextItemWidth(250);
        bool enter = ImGui::InputTextWithHint("##rename", canRename ? "Nickname (15 characters)" : "Rename: needs readable progress", m_renameBuffer,
            sizeof(m_renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleVar();
        ImGui::SameLine(0, 8);
        if (PillButton("Rename", false, 110, true) || enter)
        {
            figures::FigureData data;
            if (figures::LoadFigure(FiguresFolder() / file, data) && !figures::BackupFigure(FiguresFolder(), file).empty() &&
                figures::SetNickname(data, m_renameBuffer) && figures::SaveFigure(FiguresFolder() / file, data))
                Toast("Renamed (a backup was kept)");
            else
                Toast("Couldn't rename it: at most 15 characters");
            RefreshFigure(file);
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, 18);
        figure = FindFigure(file);
        if (figure != nullptr && PillButton(figure->favourite ? "Favourite" : "Add to favourites", figure->favourite, 170, true))
            SetFavourite(file, !figure->favourite);
        ImGui::SameLine(0, 8);
        figure = FindFigure(file);
        if (figure != nullptr && PillButton(figure->onPortal ? "On the portal at start" : "Not on the portal", figure->onPortal, 200, true))
        {
            SetStartupFigure(file, !figure->onPortal);
            figure->onPortal = !figure->onPortal;
        }

        ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom - 40));
        ImGui::BeginDisabled(!editable);
        if (PillButton("Back up", false, 110, true))
            Toast(figures::BackupFigure(FiguresFolder(), file).empty() ? "Couldn't back it up" : "Backed up");
        ImGui::SameLine(0, 8);
        std::vector<std::string> backups = figures::ListBackups(FiguresFolder(), file);
        ImGui::BeginDisabled(backups.empty());
        if (PillButton(backups.empty() ? "Restore (no backup)" : "Restore...", false, 170, true))
            ImGui::OpenPopup("##backups");
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("##backups"))
        {
            ImGui::TextColored(ImColor(Rgb(TextDim)), "Put back the copy from");
            for (auto backup = backups.rbegin(); backup != backups.rend(); ++backup)
                if (ImGui::Selectable(fs::path(*backup).stem().string().c_str()))
                    m_confirm = "restore:" + *backup;
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, 8);
        if (PillButton("Reset", false, 100, true))
            m_confirm = "reset";
        ImGui::SameLine(0, 8);
        if (PillButton("Delete", false, 100, true, Danger, 0xFF6A63, 0xC93C35))
            m_confirm = "delete";
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetCursorPosX(size.x - Pad - 110);
        if (PillButton("Close", false, 110, true))
        {
            m_sheetFile.clear();
            ImGui::CloseCurrentPopup();
        }

        // The confirmation bar for the actions that can't be taken back (a backup is kept for each).
        if (!m_confirm.empty())
        {
            ImVec2 barAt(origin.x, bottom - 40 - 52 - 60);
            float barW = size.x - 2 * Pad;
            draw->AddRectFilled(barAt, ImVec2(barAt.x + barW, barAt.y + 50), Rgb(Danger, 60), Radius);
            draw->AddRect(barAt, ImVec2(barAt.x + barW, barAt.y + 50), Rgb(Danger), Radius);
            std::string question = m_confirm == "reset" ? "Reset its progress? It goes back to how it left the box."
                : m_confirm == "delete" ? "Delete this figure? It is removed from the collection."
                : "Put back the backup from " + fs::path(m_confirm.substr(8)).stem().string() + "?";
            draw->AddText(ImVec2(barAt.x + 16, barAt.y + 15), Rgb(Text), (question + " A backup is kept.").c_str());
            ImGui::SetCursorScreenPos(ImVec2(barAt.x + barW - 240, barAt.y + 5));
            if (PillButton("Confirm", false, 120, true, Danger, 0xFF6A63, 0xC93C35))
            {
                fs::path folder = FiguresFolder();
                if (m_confirm == "reset")
                {
                    figures::FigureData data;
                    bool done = figures::LoadFigure(folder / file, data) && !figures::BackupFigure(folder, file).empty();
                    if (done)
                    {
                        figures::ResetProgress(data);
                        done = figures::SaveFigure(folder / file, data);
                    }
                    Toast(done ? "Progress reset (a backup was kept)" : "Couldn't reset it");
                    RefreshFigure(file);
                }
                else if (m_confirm == "delete")
                {
                    bool done = figures::DeleteFigure(folder, file);
                    Toast(done ? "Deleted (a backup was kept)" : "Couldn't delete it");
                    if (done)
                    {
                        SetStartupFigure(file, false);
                        SetFavourite(file, false);
                        RefreshFigure(file);
                        m_sheetFile.clear();
                        ImGui::CloseCurrentPopup();
                    }
                }
                else
                {
                    bool done = figures::RestoreBackup(folder, file, m_confirm.substr(8));
                    Toast(done ? "Backup restored" : "Couldn't restore it");
                    RefreshFigure(file);
                }
                m_confirm.clear();
            }
            ImGui::SameLine(0, 8);
            if (PillButton("Cancel", false, 110, true))
                m_confirm.clear();
        }
        ImGui::EndPopup();
    }

    // ---- The figure creator ---------------------------------------------------------------------------------------

    void App::DrawNewFigure()
    {
        if (m_newFigureOpen && !ImGui::IsPopupOpen("##newfigure"))
            ImGui::OpenPopup("##newfigure");
        ImVec2 display = ImGui::GetIO().DisplaySize;
        ImVec2 size(std::min(900.0f, display.x - 80), std::min(620.0f, display.y - 60));
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowPos(ImVec2(display.x / 2, display.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Rgb(Panel, 250));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 140));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, Pad));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, RadiusLarge);
        bool open = ImGui::BeginPopupModal("##newfigure", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        if (!open)
            return;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_newFigureOpen = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        Title("NEW FIGURE", HeadlineSize - 6, Text);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "Pick a category, a figure, then its variant. It is created new, as it leaves the box.");
        ImGui::Dummy(ImVec2(0, 4));

        // 1. Category.
        auto inCategory = [&](int category) {
            return std::count_if(figures::Catalog().begin(), figures::Catalog().end(), [&](const ToyInfo& toy) { return int(toy.category) == category; });
        };
        float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        for (int category = 0; category < int(Category::Count); category++)
        {
            bool empty = inCategory(category) == 0;
            std::string label = fmt::format("{}{}##newcat{}", figures::CategoryName(Category(category)), empty ? " (later)" : "", category);
            ImVec2 textSize = ImGui::CalcTextSize(label.c_str(), strstr(label.c_str(), "##"));
            if (category > 0)
            {
                ImGui::SameLine(0, 8);
                if (ImGui::GetCursorScreenPos().x + textSize.x + 26 > right)
                    ImGui::NewLine();
            }
            ImGui::BeginDisabled(empty);
            if (Chip(label.c_str(), m_newCategory == category))
            {
                m_newCategory = category;
                m_newBase = 0xFFFF;
            }
            ImGui::EndDisabled();
        }
        ImGui::Dummy(ImVec2(0, 6));

        // 2. The figure (plain toys of the category), 3. its variants.
        float listH = size.y - (ImGui::GetCursorPosY() + Pad + 60);
        ImGui::BeginChild("##newtoys", ImVec2(0, listH), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        constexpr float Cell = 104, Gap = 10;
        int columns = std::max(1, int((ImGui::GetContentRegionAvail().x + Gap) / (Cell + Gap)));
        int index = 0;
        for (const ToyInfo& toy : figures::Catalog())
        {
            if (int(toy.category) != m_newCategory || &figures::BaseToy(toy) != &toy)
                continue;
            if (index++ % columns != 0)
                ImGui::SameLine(0, Gap);
            ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::PushID(toy.toyType);
            if (ImGui::InvisibleButton("##toy", ImVec2(Cell, Cell + 26)))
            {
                m_newBase = toy.toyType;
                m_newVariant = toy.toyType;
            }
            bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            bool selected = m_newBase == toy.toyType;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(at, ImVec2(at.x + Cell, at.y + Cell + 26), Rgb(selected ? PanelRaisedActive : hovered ? PanelRaised : Field), Radius);
            if (selected)
                draw->AddRect(at, ImVec2(at.x + Cell, at.y + Cell + 26), Rgb(Accent), Radius, 0, 2.5f);
            DrawFigureIcon(draw, &toy, ImVec2(at.x + 16, at.y + 6), Cell - 32, false);
            ImGui::PushFont(nullptr, SmallSize);
            std::string name = LauncherMiddleEllipsis(toy.name, 14);
            ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
            draw->AddText(ImVec2(at.x + (Cell - nameSize.x) / 2, at.y + Cell - 18), Rgb(Text), name.c_str());
            ImGui::PopFont();
        }
        ImGui::EndChild();

        // The variants of the chosen figure, then Create.
        ImVec2 barAt(ImGui::GetWindowPos().x + Pad, ImGui::GetWindowPos().y + size.y - Pad - 42);
        ImGui::SetCursorScreenPos(barAt);
        const ToyInfo* chosen = nullptr;
        if (m_newBase != 0xFFFF)
        {
            for (const ToyInfo& toy : figures::Catalog())
                if (figures::BaseToy(toy).toyType == m_newBase)
                {
                    std::string label = fmt::format("{}##variant{}", toy.variant == figures::Variant::Normal ? "Normal" : figures::VariantName(toy.variant), toy.toyType);
                    if (Chip(label.c_str(), m_newVariant == toy.toyType, toy.variant == figures::Variant::Normal ? Accent : VariantColor(toy.variant)))
                        m_newVariant = toy.toyType;
                    ImGui::SameLine(0, 8);
                    if (m_newVariant == toy.toyType)
                        chosen = &toy;
                }
        }
        else
            ImGui::TextColored(ImColor(Rgb(TextDim)), "Choose a figure above.");

        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + size.x - Pad - 110 - 10 - 260, barAt.y));
        if (PillButton("Cancel", false, 110, true))
        {
            m_newFigureOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, 10);
        ImGui::BeginDisabled(chosen == nullptr);
        std::string create = chosen != nullptr ? fmt::format("Create {}", LauncherMiddleEllipsis(chosen->name, 22)) : std::string("Create");
        if (PillButton(create.c_str(), true, 260, true) && chosen != nullptr)
        {
            std::string file = figures::NewFigure(FiguresFolder(), *chosen);
            if (file.empty())
                Toast("Couldn't create the figure");
            else
            {
                Toast("Created " + file);
                if (!m_collectionLoaded)
                    LoadCollection();
                else
                    RefreshFigure(file);
                m_sheetFile = file;
                m_renameBuffer[0] = 0;
                m_confirm.clear();
            }
            m_newFigureOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}

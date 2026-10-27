// Manage: the trash, backups, import and export, unknown toys, the tidy of the starters the game made on its own.
// Also the screenshot test's scratch collection (Seed, TestScreen).
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/launcher_ui.h"
#include <algorithm>
#include <atomic>
#include <fmt/format.h>
#include <fstream>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace launcher_ui
{
    namespace fs = std::filesystem;
    using collection::Figure;

    namespace
    {
        // Manage's words that changed in the polish pass (the rest are still inline, for #4).
        namespace strings
        {
            constexpr const char* DeleteForGood = N_("Delete for good");
            constexpr const char* DeleteForGoodHint = N_("Hold to delete it for good.");
        }

        struct DialogResult
        {
            std::atomic<bool> done{ false };
            std::vector<std::string> paths;
        };

        void Wait(DialogResult& result)
        {
            while (!result.done)
            {
                SDL_PumpEvents();
                SDL_Delay(10);
            }
        }

        void Collect(void* userdata, const char* const* files, int)
        {
            auto* result = static_cast<DialogResult*>(userdata);
            for (const char* const* file = files; file != nullptr && *file != nullptr; file++)
                result->paths.emplace_back(*file);
            result->done = true;
        }

        std::vector<std::string> ChooseFigureFiles(SDL_Window* window)
        {
            static std::string filterName;
            filterName = Tr("Figure dumps (.sky, .bin, .dmp)"); // the dialog is asynchronous: the text must outlive this call
            static SDL_DialogFileFilter filters[1];
            filters[0] = { filterName.c_str(), "sky;bin;dmp" };
            DialogResult result;
            SDL_PropertiesID properties = SDL_CreateProperties();
            SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, const_cast<SDL_DialogFileFilter*>(filters));
            SDL_SetNumberProperty(properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
            SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, Tr("Import figures"));
            SDL_SetBooleanProperty(properties, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, true);
            SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window);
            SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE, Collect, &result, properties);
            SDL_DestroyProperties(properties);
            Wait(result);
            return result.paths;
        }

        std::string ChooseFolder(SDL_Window* window, const char* title)
        {
            DialogResult result;
            SDL_PropertiesID properties = SDL_CreateProperties();
            SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, title);
            SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window);
            SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER, Collect, &result, properties);
            SDL_DestroyProperties(properties);
            Wait(result);
            return result.paths.empty() ? std::string() : result.paths.front();
        }
    }

    void CollectionPage::ExportTo(const std::vector<std::string>& paths)
    {
        std::string folder = ChooseFolder(m_app.m_window, Tr("Export the figures to"));
        if (folder.empty())
            return;
        int count = collection::Export(paths, folder);
        Toast(count == 1 ? Tf("Exported {} figure to {}", count, folder) : Tf("Exported {} figures to {}", count, folder));
    }

    // "Export first" before uninstalling: every figure of the collection, to a folder the player picks.
    void App::ExportFiguresFirst()
    {
        std::string folder = ChooseFolder(m_window, Tr("Export the figures to"));
        if (folder.empty())
            return;
        std::vector<std::string> paths;
        for (const Figure& figure : collection::List())
            paths.push_back(figure.path);
        int count = collection::Export(paths, folder);
        Toast(count == 1 ? Tf("Exported {} figure to {}", count, folder) : Tf("Exported {} figures to {}", count, folder));
    }

    void CollectionPage::DrawManage(ImVec2 size)
    {
        m_app.Title(Tr("MANAGE"), HeadlineSize - 6, Text);
        RefreshDisk();
        size_t unknown = std::count_if(m_model.figures.begin(), m_model.figures.end(), [](const Figure& f) { return f.toy == nullptr; });
        const std::string tabs[] = { Tf("Trash  {}", m_trash.size()), Tr("Backups"), Tr("Import and export"), Tf("Unknown toys  {}", unknown),
            Tf("Tidy  {}", m_starters) };
        for (int i = 0; i < 5; i++)
        {
            if (i > 0)
                ImGui::SameLine(0, 8);
            if (m_app.Chip((tabs[i] + "##managetab" + std::to_string(i)).c_str(), m_manageTab == i))
                m_manageTab = i;
        }
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::BeginChild("##managebody", ImVec2(0, size.y - ImGui::GetCursorPosY() - Pad - 50), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        switch (m_manageTab)
        {
        case 0: DrawTrash(); break;
        case 1: DrawBackups(); break;
        case 2: DrawImportExport(); break;
        case 3: DrawUnknown(); break;
        default: DrawTidy(); break;
        }
        ImGui::EndChild();
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + size.x - Pad - 110, ImGui::GetWindowPos().y + size.y - Pad - 40));
        if (m_app.PillButton(Tr("Close"), false, 110, true))
            CloseOverlay();
    }

    void CollectionPage::DrawTrash()
    {
        const std::vector<collection::TrashEntry> trash = m_trash; // a copy: the buttons below reload it
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tr("Deleted figures are kept 30 days."));
        ImGui::Dummy(ImVec2(0, 6));
        if (trash.empty())
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", Tr("The trash is empty."));
        for (const collection::TrashEntry& entry : trash)
        {
            ImGui::PushID(entry.id.c_str());
            m_app.BoldText(entry.file.c_str(), Text);
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s · %s", ShortDate(entry.deleted).c_str(), entry.reason == "tidy" ? Tr("tidied") : Tr("deleted"));
            if (m_app.PillButton(Tr("Put back"), false, 120, true))
            {
                Toast(collection::Undelete(entry.id).empty() ? Tr("Couldn't put it back") : Tr("Put back: ") + entry.file);
                Reload();
            }
            ImGui::SameLine(0, 8);
            if (m_app.HoldButton(Tr(strings::DeleteForGood), 200, Tr(strings::DeleteForGoodHint)))
            {
                collection::DeleteForGood(entry.id);
                Reload();
            }
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, 6));
        }
    }

    void CollectionPage::DrawBackups()
    {
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tr("The latest 10 backups are kept."));
        ImGui::Dummy(ImVec2(0, 6));
        bool any = false;
        for (const Figure& figure : m_model.figures)
        {
            const std::vector<BackupRow> backups = Backups(figure.path); // a copy: Restore below reloads them
            if (backups.empty())
                continue;
            any = true;
            ImGui::PushID(figure.path.c_str());
            if (m_app.Chip(Tf("{}  {}##b", Name(figure), backups.size()).c_str(), m_backupsOf == figure.path))
                m_backupsOf = m_backupsOf == figure.path ? std::string() : figure.path;
            ImGui::PopID();
            if (m_backupsOf != figure.path)
                continue;
            for (const BackupRow& backup : backups)
            {
                ImGui::PushID(backup.path.c_str());
                ImGui::Indent(20);
                ImGui::TextUnformatted(backup.label.c_str());
                ImGui::SameLine(0, 14);
                ImGui::BeginDisabled(!Writable(figure));
                if (m_app.PillButton(Tr("Restore"), false, 100, true))
                {
                    std::string error;
                    Toast(collection::Restore(figure.path, backup.path, &error) ? Tr("Backup restored") : Tr("Couldn't restore it: ") + error);
                    Reload();
                }
                ImGui::EndDisabled();
                ImGui::Unindent(20);
                ImGui::PopID();
            }
        }
        if (!any)
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", Tr("No backups yet."));
    }

    void CollectionPage::DrawImportExport()
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + 640);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tr("Drop figure files or a folder on the window."));
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 8));
        if (m_app.PillButton(Tr("Import files..."), true, 190, true))
            Import(ChooseFigureFiles(m_app.m_window));
        ImGui::SameLine(0, 8);
        if (m_app.PillButton(Tr("Import a folder..."), false, 210, true))
            if (std::string folder = ChooseFolder(m_app.m_window, Tr("Import the figures of a folder")); !folder.empty())
                Import({ folder });
        ImGui::Dummy(ImVec2(0, 12));
        if (m_app.PillButton(Tr("Export everything..."), false, 230, true))
        {
            std::vector<std::string> paths;
            for (const Figure& figure : m_model.figures)
                paths.push_back(figure.path);
            ExportTo(paths);
        }
        ImGui::SameLine(0, 8);
        if (m_app.PillButton(Tr("Show in folder"), false, 190, true))
        {
            SDL_CreateDirectory(collection::Folder().c_str());
            { std::string folder = collection::Folder(); OpenFolder(std::filesystem::path(std::u8string(folder.begin(), folder.end()))); }
        }
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", collection::Folder().c_str());
    }

    void CollectionPage::DrawUnknown()
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + 640);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tr("Toys missing from the catalogue. Name one to show it."));
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 8));
        bool any = false;
        for (const Figure& figure : m_model.figures)
        {
            if (figure.toy != nullptr)
                continue;
            any = true;
            ImGui::PushID(figure.path.c_str());
            m_app.BoldText(Tf("Unknown toy {} (variant {:04X})", figure.type, figure.variant).c_str(), Text);
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", figure.file.c_str());
            if (m_unknownType == figure.type)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
                ImGui::SetNextItemWidth(260);
                ImGui::InputTextWithHint("##unknownname", Tr("Its name"), m_unknownName, sizeof(m_unknownName));
                ImGui::PopStyleVar();
                ImGui::SameLine(0, 8);
                if (m_app.PillButton(Tr("Save"), true, 100, true) && m_unknownName[0] != 0)
                {
                    Toast(NameUnknownToy(figure.type, m_unknownName, collection::Category::Core, m_model.gameId) ? Tr("Named: shown at the next start")
                                                                                                                : Tr("Couldn't save the name"));
                    m_unknownType = 0xFFFF;
                    m_unknownName[0] = 0;
                }
            }
            else if (m_app.PillButton(Tr("Name this toy"), false, 170, true))
                m_unknownType = figure.type;
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, 6));
        }
        if (!any)
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", Tr("No unknown toys."));
    }

    void CollectionPage::DrawTidy()
    {
        RefreshDisk();
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + 640);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tr("Blank figures made by old game versions. Tidy moves them to the trash."));
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::BeginDisabled(m_starters == 0);
        if (m_app.PillButton(Tf("Move {} to the trash", m_starters).c_str(), true, 260, true))
        {
            int moved = collection::Tidy();
            Toast(moved == 1 ? Tf("{} figure moved to the trash", moved) : Tf("{} figures moved to the trash", moved));
            Reload();
        }
        ImGui::EndDisabled();
    }

    // ---- The screenshot test's scratch collection -----------------------------------------------------------------

    namespace
    {
        // Screens, in order (the step is the index).
        constexpr const char* Shots[] = { "20-collection", "21-choose-player-1", "22-choose-magic-item", "23-filter-water", "24-figure-sheet",
            "25-figure-details", "26-figure-delete", "27-edit", "28-add-a-figure", "29-add-later-game", "30-manage-trash", "31-manage-backups",
            "32-live-portal", "33-after-delete", "34-giants-portal", "35-giants-figure", "36-giants-edit", "37-add-lightcore", "38-no-match",
            "39-choose-empty", "40-add-search-version", "41-giants-variant-sheet", "42-swapforce-portal", "43-swapper-sheet",
            "44-change-the-bottom", "45-swapforce-choose-player-1", "46-add-standard-version", "47-search-element", "48-search-level",
            "49-search-typo", "50-filters-panel", "51-filters-tags", "52-sort-name" };
    }

    const char* CollectionPage::TestShotName(int step)
    {
        return Shots[std::clamp(step, 0, int(std::size(Shots)) - 1)];
    }

    int CollectionPage::TestShotCount()
    {
        return int(std::size(Shots));
    }

    // Figures in the scratch collection only: SSA_COLLECTION must name it (and SSA_SAVE_DIR the seats' folder).
    void CollectionPage::Seed()
    {
        if (!m_opened)
            Open();
        if (getenv("SSA_COLLECTION") == nullptr || !collection::List().empty())
            return;
        auto make = [](uint16_t type, uint16_t variant, uint32_t experience, uint16_t gold, const char* nickname) {
            std::string path = collection::Create(type, variant);
            if (path.empty() || experience == 0)
                return path;
            Figure figure;
            collection::Read(path, figure);
            figure.stats.experience = experience;
            figure.stats.gold = gold;
            figure.stats.playSeconds = experience / 4;
            figures::SetNicknameUtf8(figure.stats, nickname);
            collection::WriteStats(path, figure.stats);
            return path;
        };
        std::string gill = make(14, 0, 9000, 2400, "Splash");
        std::string spyro = make(16, 0, 31000, 18500, "");
        std::string legendary = make(416, 0, 3000, 600, "");
        std::string trigger = make(19, 0, 1200, 300, "");
        std::string bash = make(4, 0, 150, 40, "");
        std::string chop = make(21, 0, 0, 0, "");
        std::string item = make(200, 0, 0, 0, "");
        std::string pack = make(300, 0, 0, 0, "");
        std::string sidekick = make(505, 0, 0, 0, "");
        std::string dark = make(28, 0, 0, 0, "");
        make(14, 0, 700, 90, ""); // a second Gill Grunt: "owned x2"
        std::string doomed = make(12, 0, 0, 0, ""), doomed2 = make(10, 0, 0, 0, ""); // two in the trash: one hold button each
        collection::SetLabel(gill, "Splash's");
        {
            Figure bought; // Gill Grunt: the first path chosen, two of its upgrades and two basic ones bought
            collection::Read(gill, bought);
            bought.stats.upgrades = 0x0075;
            collection::WriteStats(gill, bought.stats);
        }
        collection::SetFavourite(gill, true);
        collection::SetFavourite(spyro, true);
        collection::SetFavourite(legendary, true);
        for (const std::string& path : { gill, spyro, trigger, bash, legendary })
            collection::Touch(path, "ssa");
        collection::Backup(gill);
        // A toy of a later game, shown in "Not for this game".
        for (const collection::Toy& toy : collection::Catalogue::Get().Toys())
            if (toy.game == "giants" && toy.category == collection::Category::Giant)
            {
                collection::Create(toy.type);
                break;
            }
        // Swap Force: two swappers (one played to level 7) and a bottom on its own, shown as cards of their own game.
        {
            collection::SwapperPaths doom = collection::CreateSwapper(2003, 1003), night = collection::CreateSwapper(2012, 1004);
            Figure played;
            if (!doom.top.empty() && collection::Read(doom.top, played))
            {
                played.stats.experience = 12500;
                played.stats.gold = 3100;
                collection::WriteStats(doom.top, played.stats);
            }
            collection::Create(1007);
            (void)night;
        }
        // Level 15 and a hat (Giants' part of the figure): Giants shows 15, Spyro's Adventure 10.
        std::string eruptor = collection::Create(9);
        Figure high;
        if (!eruptor.empty() && collection::Read(eruptor, high))
        {
            figures::SetTotalExperience(high.stats, figures::LevelExperience[figures::MaxLevelGiants - 1]);
            high.stats.gold = 40000;
            high.stats.hat = 6;
            high.stats.upgrades = 0x0645;
            collection::WriteStats(eruptor, high.stats);
            collection::Touch(eruptor, "giants");
        }
        // A Series 2 Eruptor: a variant with its own artwork in Giants.
        if (const collection::Toy* toy = collection::Catalogue::Get().Find(9))
            for (const collection::Variant& variant : toy->variants)
                if (variant.kind == collection::VariantKind::Series && !variant.sameTag)
                {
                    std::string series = make(9, variant.id, 12000, 3100, ""); // played: its level and "last played" agree
                    if (!series.empty())
                        collection::Touch(series, "giants");
                    break;
                }
        collection::Delete(doomed);
        collection::Delete(doomed2);
        (void)chop; (void)item; (void)pack; (void)sidekick; (void)dark;
        if (getenv("SSA_SAVE_DIR") != nullptr)
            if (const collection::Game* game = m_model.Game())
                WriteSeats(*game, SessionFile(*game), { gill, spyro, item, pack });
        Reload();
    }

    void CollectionPage::TestScreen(int step)
    {
        m_overlay = Overlay::None;
        m_editing = false;
        m_details = false;
        m_confirm.clear();
        m_choosing = -1;
        m_filter = ShelfFilter{};
        m_search[0] = 0;
        m_addSearch[0] = 0;
        m_closeFilters = true;
        auto figureOf = [&](uint16_t type) -> std::string {
            for (const Figure& figure : m_model.figures)
                if (figure.type == type && !collection::UntouchedStarter(figure) && figure.level > 1)
                    return figure.path;
            return {};
        };
        switch (step)
        {
        case 1: Choose(0); break;
        case 2: Choose(2); break;
        case 3: m_filter.element = int(collection::Element::Water); break;
        case 4: OpenFigure(figureOf(14)); break;
        case 5:
            OpenFigure(figureOf(14));
            m_details = true;
            break;
        case 6:
            OpenFigure(figureOf(14));
            m_confirm = "delete";
            break;
        case 7:
            OpenFigure(figureOf(14));
            if (const Figure* figure = m_model.Find(m_figure))
                OpenEdit(*figure);
            break;
        case 8:
            m_overlay = Overlay::Add;
            m_addGame = "ssa";
            m_addToy = 14;
            m_addVersion = 0;
            m_addCategory = -1;
            break;
        case 9:
            m_overlay = Overlay::Add;
            m_addGame = "swapforce";
            m_addToy = 0xFFFF;
            for (const collection::Toy& toy : collection::Catalogue::Get().Toys())
                if (toy.game == "swapforce" && toy.category == collection::Category::SwapTop)
                {
                    m_addToy = toy.type;
                    break;
                }
            m_addCategory = int(collection::Category::SwapTop);
            break;
        case 10:
            m_overlay = Overlay::Manage;
            m_manageTab = 0;
            break;
        case 11:
            m_overlay = Overlay::Manage;
            m_manageTab = 1;
            m_backupsOf = figureOf(14);
            break;
        case 12:
        {
#ifndef _WIN32
            // A game holds the figures of the seats: a lock of a live process other than this one.
            for (const std::string& seat : m_model.seats)
                if (!seat.empty() && getenv("SSA_COLLECTION") != nullptr && getenv("SSA_LAUNCHER_TEST_FAKE_LOCKS") != nullptr)
                {
                    std::ofstream lock(seat + ".lock");
                    lock << fmt::format("{{ \"game\": \"ssa\", \"pid\": {}, \"since\": \"2026-10-24T10:15:00Z\" }}\n", (long long)getppid());
                }
            Reload();
#endif
            break;
        }
        case 13: // the game quit; deleting a figure from its sheet: back to the shelf, on the next figure
            for (const std::string& seat : m_model.seats)
                if (!seat.empty() && getenv("SSA_COLLECTION") != nullptr)
                    collection::BreakLock(seat);
            Reload();
            OpenFigure(figureOf(19));
            DeleteFigure(m_figure);
            break;
        case 14: // Giants: its own seats (a runtime now), the same shelf with Giants' levels
        {
            SelectPortalGame("giants");
            std::string eruptor;
            for (const Figure& figure : m_model.figures)
                if (figure.type == 9 && figure.level == figures::MaxLevelGiants)
                    eruptor = figure.path;
            TestSetSeats({ eruptor, figureOf(14), "" }); // the test's two games share SSA_SAVE_DIR, so one session file
            break;
        }
        case 15: // Giants: a figure at level 15 with a hat, its sheet and its Edit (the hat picker)
        case 16:
        {
            SelectPortalGame("giants");
            std::string eruptor;
            for (const Figure& figure : m_model.figures)
                if (figure.type == 9 && figure.level == figures::MaxLevelGiants)
                    eruptor = figure.path;
            OpenFigure(eruptor);
            if (const Figure* figure = step == 16 ? m_model.Find(m_figure) : nullptr)
                OpenEdit(*figure);
            break;
        }
        case 17: // Add a figure, Giants: Eruptor's versions, the LightCore chosen
        {
            m_overlay = Overlay::Add;
            m_addGame = "giants";
            m_addToy = 9;
            m_addCategory = -1;
            m_addVersion = 0;
            if (const collection::Toy* toy = collection::Catalogue::Get().Find(9))
                for (size_t i = 0; i < toy->variants.size(); i++)
                    if (toy->variants[i].kind == collection::VariantKind::LightCore)
                        m_addVersion = int(i) + 1;
            break;
        }
        case 18: // back to Spyro's Adventure (its seats as seeded): a search that finds nothing
        {
            SelectPortalGame("ssa");
            std::vector<std::string> seats(4);
            for (const Figure& figure : m_model.figures)
            {
                if (figure.type == 14 && figure.favourite)
                    seats[0] = figure.path;
                else if (figure.type == 16)
                    seats[1] = figure.path;
                else if (figure.type == 200)
                    seats[2] = figure.path;
                else if (figure.type == 300)
                    seats[3] = figure.path;
            }
            TestSetSeats(seats);
            snprintf(m_search, sizeof(m_search), "%s", "zzz");
            break;
        }
        case 19: // choosing for a seat nothing goes on: no trap seat in SSA, so the adventure seat with the pack taken off
            SelectPortalGame("ssa");
            TakeOff(3);
            for (const Figure& figure : m_model.figures)
                if (figure.toy != nullptr && figure.toy->category == collection::Category::Adventure)
                    collection::Delete(figure.path);
            Reload();
            Choose(3);
            break;
        case 20: // Add a figure: "polar" finds Polar Whirlwind (a version of Whirlwind), already chosen
        {
            m_overlay = Overlay::Add;
            m_addGame = "ssa";
            m_addCategory = -1;
            m_addToy = 0xFFFF;
            m_addVersion = 0;
            snprintf(m_addSearch, sizeof(m_addSearch), "%s", "polar");
            for (const collection::Toy& toy : collection::Catalogue::Get().Toys())
                for (size_t i = 0; i < toy.variants.size(); i++)
                    if (toy.variants[i].name.find("Polar") != std::string::npos && m_addToy == 0xFFFF)
                    {
                        m_addToy = toy.type;
                        m_addVersion = int(i) + 1;
                    }
            break;
        }
        case 21: // Giants: a Series 2 Eruptor's sheet, with its own artwork
        {
            SelectPortalGame("giants");
            for (const Figure& figure : m_model.figures)
                if (figure.type == 9 && figure.variant != 0)
                {
                    OpenFigure(figure.path);
                    break;
                }
            break;
        }
        case 22: // Swap Force: a swapper on player 1 (its two halves, one seat) and the shelf's swapper cards
        case 23: // its sheet
        case 24: // "Change the bottom"
        case 25: // choosing for player 1: the swapper cards among the figures
        {
            SelectPortalGame("swapforce");
            std::string doom;
            for (const Figure& figure : m_model.figures)
                if (figure.type == 2003)
                    doom = figure.path;
            if (step == 22)
                TestSetSeats({ doom, "", "", "" });
            else if (step == 25)
                Choose(0);
            else
            {
                OpenFigure(doom);
                if (step == 24)
                    if (const Figure* top = m_model.Find(doom))
                        m_changeKeep = top->path;
            }
            break;
        }
        case 26: // Add a figure, Giants: Flashwing's versions (Standard, then the others: its own entry is not a row)
            m_overlay = Overlay::Add;
            m_addGame = "giants";
            m_addToy = 103;
            m_addCategory = -1;
            m_addVersion = 0;
            break;
        case 27: // the smart search: an element, a level, a typo (both languages), then the Filters panel, its tags and a sort
        case 28:
        case 29:
        case 30:
        case 31:
        case 32:
            SelectPortalGame("ssa");
            if (step == 27)
                std::snprintf(m_search, sizeof(m_search), "%s", French() ? "feu" : "fire");
            else if (step == 28)
                std::snprintf(m_search, sizeof(m_search), "%s", French() ? "niveau 10" : "level 10");
            else if (step == 29)
                std::snprintf(m_search, sizeof(m_search), "%s", French() ? "legendiare" : "legendray");
            else if (step == 30)
            {
                m_openFilters = true;
                m_closeFilters = false;
            }
            else if (step == 31)
            {
                m_filter.element = int(collection::Element::Water);
                m_filter.game = "ssa";
                m_filter.sort = collection::Sort::Level;
            }
            else
                m_filter.sort = collection::Sort::Name;
            break;
        default: break;
        }
        m_generation++;
    }
}

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
            static const SDL_DialogFileFilter filters[] = { { "Figure dumps (.sky, .bin, .dmp)", "sky;bin;dmp" } };
            DialogResult result;
            SDL_PropertiesID properties = SDL_CreateProperties();
            SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, const_cast<SDL_DialogFileFilter*>(filters));
            SDL_SetNumberProperty(properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
            SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, "Import figures");
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
        std::string folder = ChooseFolder(m_app.m_window, "Export the figures to");
        if (folder.empty())
            return;
        int count = collection::Export(paths, folder);
        Toast(fmt::format("Exported {} figure{} to {}", count, count == 1 ? "" : "s", folder));
    }

    void CollectionPage::DrawManage(ImVec2 size)
    {
        m_app.Title("MANAGE", HeadlineSize - 6, Text);
        std::vector<collection::TrashEntry> trash = collection::Trash();
        size_t unknown = std::count_if(m_model.figures.begin(), m_model.figures.end(), [](const Figure& f) { return f.toy == nullptr; });
        size_t starters = collection::UntouchedStarters().size();
        const std::string tabs[] = { fmt::format("Trash  {}", trash.size()), "Backups", "Import and export", fmt::format("Unknown toys  {}", unknown),
            fmt::format("Tidy  {}", starters) };
        for (int i = 0; i < 5; i++)
        {
            if (i > 0)
                ImGui::SameLine(0, 8);
            if (m_app.Chip((tabs[i] + "##managetab" + std::to_string(i)).c_str(), m_manageTab == i))
                m_manageTab = i;
        }
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::BeginChild("##managebody", ImVec2(0, size.y - ImGui::GetCursorPosY() - Pad - 50), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
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
        if (m_app.PillButton("Close", false, 110, true))
            CloseOverlay();
    }

    void CollectionPage::DrawTrash()
    {
        std::vector<collection::TrashEntry> trash = collection::Trash();
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "Deleted figures stay here for 30 days. Their stats and name come back with them.");
        ImGui::Dummy(ImVec2(0, 6));
        if (trash.empty())
            ImGui::TextColored(ImColor(Rgb(TextDim)), "The trash is empty.");
        for (const collection::TrashEntry& entry : trash)
        {
            ImGui::PushID(entry.id.c_str());
            m_app.BoldText(entry.file.c_str(), Text);
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s · %s", ShortDate(entry.deleted).c_str(), entry.reason == "tidy" ? "tidied" : "deleted");
            if (m_app.PillButton("Put back", false, 120, true))
            {
                Toast(collection::Undelete(entry.id).empty() ? "Couldn't put it back" : "Put back: " + entry.file);
                Reload();
            }
            ImGui::SameLine(0, 8);
            if (HoldButton("Delete for good (hold)", 230))
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
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "Every change makes a backup; the newest 10 of each figure are kept.");
        ImGui::Dummy(ImVec2(0, 6));
        for (const Figure& figure : m_model.figures)
        {
            std::vector<std::string> backups = collection::Backups(figure.path);
            if (backups.empty())
                continue;
            ImGui::PushID(figure.path.c_str());
            if (m_app.Chip(fmt::format("{}  {}##b", Name(figure), backups.size()).c_str(), m_backupsOf == figure.path))
                m_backupsOf = m_backupsOf == figure.path ? std::string() : figure.path;
            ImGui::PopID();
            if (m_backupsOf != figure.path)
                continue;
            for (const std::string& backup : backups)
            {
                Figure old;
                std::string stem = fs::path(backup).stem().string();
                std::string text = stem.size() >= 16 ? fmt::format("{} {}:{}", stem.substr(0, 10), stem.substr(11, 2), stem.substr(13, 2)) : stem;
                if (collection::Read(backup, old) && HasProgress(old) && old.state == figures::AreaState::Valid)
                    text += fmt::format("   level {} · {} gold", old.level, old.stats.gold);
                ImGui::PushID(backup.c_str());
                ImGui::Indent(20);
                ImGui::TextUnformatted(text.c_str());
                ImGui::SameLine(0, 14);
                ImGui::BeginDisabled(!Writable(figure));
                if (m_app.PillButton("Restore", false, 100, true))
                {
                    std::string error;
                    Toast(collection::Restore(figure.path, backup, &error) ? "Backup restored" : "Couldn't restore it: " + error);
                    Reload();
                }
                ImGui::EndDisabled();
                ImGui::Unindent(20);
                ImGui::PopID();
            }
        }
    }

    void CollectionPage::DrawImportExport()
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + 640);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "Drop .sky, .bin or .dmp files (or a folder of them) on the window. Each is checked as a 1 KB tag and copied; a dump already in the collection is skipped.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 8));
        if (m_app.PillButton("Import files...", true, 190, true))
            Import(ChooseFigureFiles(m_app.m_window));
        ImGui::SameLine(0, 8);
        if (m_app.PillButton("Import a folder...", false, 210, true))
            if (std::string folder = ChooseFolder(m_app.m_window, "Import the figures of a folder"); !folder.empty())
                Import({ folder });
        ImGui::Dummy(ImVec2(0, 12));
        if (m_app.PillButton("Export everything...", false, 230, true))
        {
            std::vector<std::string> paths;
            for (const Figure& figure : m_model.figures)
                paths.push_back(figure.path);
            ExportTo(paths);
        }
        ImGui::SameLine(0, 8);
        if (m_app.PillButton("Show in folder", false, 190, true))
        {
            SDL_CreateDirectory(collection::Folder().c_str());
            SDL_OpenURL(("file://" + collection::Folder()).c_str());
        }
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", collection::Folder().c_str());
    }

    void CollectionPage::DrawUnknown()
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + 640);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "Toys the catalogue doesn't know. They stay usable; the game decides what to do with them. Naming one adds it to the collection's catalogue.json, shown at the next start.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 8));
        bool any = false;
        for (const Figure& figure : m_model.figures)
        {
            if (figure.toy != nullptr)
                continue;
            any = true;
            ImGui::PushID(figure.path.c_str());
            m_app.BoldText(fmt::format("Unknown toy {} (variant {:04X})", figure.type, figure.variant).c_str(), Text);
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", figure.file.c_str());
            if (m_unknownType == figure.type)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
                ImGui::SetNextItemWidth(260);
                ImGui::InputTextWithHint("##unknownname", "Its name", m_unknownName, sizeof(m_unknownName));
                ImGui::PopStyleVar();
                ImGui::SameLine(0, 8);
                if (m_app.PillButton("Save", true, 100, true) && m_unknownName[0] != 0)
                {
                    Toast(NameUnknownToy(figure.type, m_unknownName, collection::Category::Core, m_model.gameId) ? "Named: shown with its name from the next start"
                                                                                                                : "Couldn't write catalogue.json");
                    m_unknownType = 0xFFFF;
                    m_unknownName[0] = 0;
                }
            }
            else if (m_app.PillButton("Name this toy", false, 170, true))
                m_unknownType = figure.type;
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, 6));
        }
        if (!any)
            ImGui::TextColored(ImColor(Rgb(TextDim)), "Every toy of the collection is in the catalogue.");
    }

    void CollectionPage::DrawTidy()
    {
        std::vector<std::string> starters = collection::UntouchedStarters();
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + 640);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "Older versions of the game made one never-played figure of every character at each start. They are not yours, so My Skylanders hides them. Tidy moves them to the trash (30 days, recoverable).");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::BeginDisabled(starters.empty());
        if (m_app.PillButton(fmt::format("Move {} to the trash", starters.size()).c_str(), true, 260, true))
        {
            int moved = collection::Tidy();
            Toast(fmt::format("{} figure{} moved to the trash", moved, moved == 1 ? "" : "s"));
            Reload();
        }
        ImGui::EndDisabled();
    }

    // ---- The screenshot test's scratch collection -----------------------------------------------------------------

    namespace
    {
        // Screens, in order (the step is the index).
        constexpr const char* Shots[] = { "20-collection", "21-view-menu", "22-by-category", "23-figure-page", "24-edit-stats", "25-add-a-figure",
            "26-add-later-game", "27-manage-trash", "28-manage-backups", "29-live-portal" };
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
        std::string gill = make(14, 0, 9000, 2400, "Gilou");
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
        std::string doomed = make(12, 0, 0, 0, "");
        collection::SetLabel(gill, "Gilou's");
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
        for (const collection::Toy& toy : collection::Catalogue::Get().Toys())
            if (toy.game == "swap_force" && toy.category == collection::Category::SwapTop)
            {
                collection::Create(toy.type);
                break;
            }
        collection::Delete(doomed);
        (void)chop; (void)item; (void)pack; (void)sidekick; (void)dark;
        if (getenv("SSA_SAVE_DIR") != nullptr)
            if (const collection::Game* game = m_model.Game())
                WriteSeats(SessionFile(*game), { gill, spyro, item, pack });
        Reload();
    }

    void CollectionPage::TestScreen(int step)
    {
        m_overlay = Overlay::None;
        m_viewMenu = false;
        m_editing = false;
        m_confirm.clear();
        if (step != 1 && step != 2)
            m_view = collection::View{};
        auto figureOf = [&](uint16_t type) -> std::string {
            for (const Figure& figure : m_model.figures)
                if (figure.type == type && !collection::UntouchedStarter(figure) && figure.level > 1)
                    return figure.path;
            return {};
        };
        switch (step)
        {
        case 1: m_viewMenu = true; break;
        case 2:
            m_view.group = "category";
            m_view.order = "name";
            break;
        case 3: OpenFigure(figureOf(14)); break;
        case 4:
            OpenFigure(figureOf(14));
            if (const Figure* figure = m_model.Find(m_figure))
            {
                m_edit = figure->stats;
                snprintf(m_nickname, sizeof(m_nickname), "%s", figures::NicknameUtf8(figure->stats).c_str());
                m_editing = true;
            }
            break;
        case 5:
            m_overlay = Overlay::Add;
            m_addGame = "ssa";
            m_addToy = 14;
            m_addVersion = 0;
            m_addCategory = -1;
            break;
        case 6:
            m_overlay = Overlay::Add;
            m_addGame = "swap_force";
            m_addToy = 0xFFFF;
            for (const collection::Toy& toy : collection::Catalogue::Get().Toys())
                if (toy.game == "swap_force" && toy.category == collection::Category::SwapTop)
                {
                    m_addToy = toy.type;
                    break;
                }
            m_addCategory = int(collection::Category::SwapTop);
            break;
        case 7:
            m_overlay = Overlay::Manage;
            m_manageTab = 0;
            break;
        case 8:
            m_overlay = Overlay::Manage;
            m_manageTab = 1;
            m_backupsOf = figureOf(14);
            break;
        case 9:
        {
#ifndef _WIN32
            // A game holds the figures of the seats: a lock of a live process other than this one.
            for (const std::string& seat : m_model.seats)
                if (!seat.empty() && getenv("SSA_COLLECTION") != nullptr)
                {
                    std::ofstream lock(seat + ".lock");
                    lock << fmt::format("{{ \"game\": \"ssa\", \"pid\": {}, \"since\": \"2026-10-24T10:15:00Z\" }}\n", (long long)getppid());
                }
            Reload();
#endif
            break;
        }
        default: break;
        }
        m_generation++;
    }
}

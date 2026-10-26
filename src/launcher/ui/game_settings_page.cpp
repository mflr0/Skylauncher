// A game's own settings (the Settings button on its Play page): Video, Language, Disc, Saves and Game (shortcut, folders,
// reinstall, delete data, uninstall). Skylauncher's own settings and what both games share (the controller, the keys) are in
// the gear's Settings (launcher_ui.cpp). The video keys are the game's own copy in [<game>.video] (game_settings.h).
#include "launcher/ui/launcher_app.h"
#include "launcher/shortcut.h"
#include "shared/collection/datadir.h"
#include <fmt/format.h>

namespace launcher_ui
{
    namespace
    {
        namespace strings
        {
            constexpr const char* GameText = "Game text";
            constexpr const char* GameTextHint = "The game's menus, subtitles and on-screen text. Skylauncher itself is in English.";
            constexpr const char* GameVoices = "Game voices";
            constexpr const char* GameVoicesHint = "The game's spoken lines and cutscenes. They can differ from the text.";
            constexpr const char* RestoreSaves = "Restore";
            constexpr const char* RestoreSavesHint = "Hold to restore this backup. The saves you have now are backed up first.";
            constexpr const char* Reinstall = "Reinstall the game";
            constexpr const char* ReinstallHint = "Hold to build the game again from your disc.";
            constexpr const char* DeleteData = "Delete saves and data";
            constexpr const char* DeleteDataHint = "Hold to delete this game's saves, their backups, the next session's seats and its caches.";
            constexpr const char* Uninstall = "Uninstall the game";
            constexpr const char* UninstallHint = "Hold to delete the game Skylauncher built. Your saves and the disc stay.";
            constexpr const char* OpenFigures = "Open figures folder";
        }

        // "12.3 GB" for what a button would free, nothing for less than a megabyte of noise.
        std::string Freed(uint64_t bytes) { return bytes == 0 ? std::string("nothing yet") : LauncherFormatSize(bytes); }
    }

    // ---- The page ----------------------------------------------------------------------------------------------------

    void App::DrawGameSettings()
    {
        // A centred column, like the launcher's settings.
        float available = ImGui::GetContentRegionAvail().x;
        float width = std::min(available, 940.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) / 2);
        ImGui::BeginChild("##gamesettingscolumn", ImVec2(width, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);

        Title(m_s->game.name, HeadlineSize, Text);
        ImGui::SameLine(width - 150);
        if (PillButton("Back", false, 120))
            GoTo(Page::Play);
        ImGui::PushFont(nullptr, SmallSize);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::TextUnformatted("Settings of this game only. The controller and keys are in the launcher's settings, for both games.");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4));

        static const char* const Tabs[] = { "Video", "Language", "Disc", "Saves", "Game" };
        int tab = int(m_gameTab);
        DrawTabBar(Tabs, int(std::size(Tabs)), &tab);
        m_gameTab = GameTab(tab);
        BeginCard("##gamesettings", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
        switch (m_gameTab)
        {
        case GameTab::Video: DrawGameVideo(); break;
        case GameTab::Language: DrawGameLanguage(); break;
        case GameTab::Disc: DrawGameDisc(); break;
        case GameTab::Saves: DrawSaveBackups(); break;
        case GameTab::Game: DrawGameTab(); break;
        }
        EndCard();
        ImGui::EndChild();
    }

    // ---- Video and language ------------------------------------------------------------------------------------------

    void App::DrawGameVideo()
    {
        if (!BeginSettingsTable("##graphics"))
            return;
        const std::string videoSection = GameVideoSection(m_s->game.id);
        const char* video = videoSection.c_str();
        auto toggle = [&](const char* id, const char* key, const char* fallback) {
            bool value = ParseBool(SettingGet(m_config, video, key, fallback));
            if (Toggle(id, &value))
                Set(video, key, value ? "1" : "0");
        };

        SettingRow("Fullscreen", "Play in fullscreen instead of a window.");
        toggle("##fullscreen", "fullscreen", "0");

        SettingRow("Resolution scale", "Auto picks x1 on integrated graphics. x2 or x3 need a dedicated graphics card.");
        Combo("##scale", video, "resolution_scale", ScaleChoices, 0);
        if (int scale = SettingInt(m_config, video, "resolution_scale", 0); scale >= 1)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
            ImGui::Text("%d x %d for the 3D scene", 1120 * std::min(scale, 10), 704 * std::min(scale, 10));
            ImGui::PopStyleColor();
        }

        SettingRow("Frame rate limit", "The game was made for 30 FPS; 60 is experimental.");
        Combo("##fps", video, "fps_limit", FpsChoices, 0);

        SettingRow("Skip intro movies", "Skips the start-up logos; the story movies still play.");
        toggle("##skipintromovies", "skip_intro_movies", "0");

        SettingRow("VSync", "Waits for the screen's refresh: no tearing, a little more latency.");
        toggle("##vsync", "vsync", "1");

        SettingRow("Texture filtering", "Anisotropic filtering keeps ground textures sharp at an angle.");
        Combo("##filter", video, "anisotropy", FilterChoices, 0);

        SettingRow("Widescreen", "Auto shows more to the sides on a window wider than 16:9. 16:9 keeps the game's picture, with bars.");
        Combo("##aspect", video, "aspect", AspectChoices, 0);

        SettingRow("HD texture packs", "Replaces the game's textures with the files of the textures folder.");
        toggle("##texturepacks", "texture_packs", "1");
        if (PillButton("Open texture packs folder", false, 300))
        {
            std::error_code error;
            std::filesystem::create_directories(GameInstallFolder({}) / "textures", error);
            if (!OpenFolder(GameInstallFolder({}) / "textures"))
                Toast("Couldn't open the folder");
        }
        EndSettingsTable();

        if (ImGui::CollapsingHeader("Advanced") && BeginSettingsTable("##graphicsadvanced"))
        {
            SettingRow("Dump textures (for pack makers)", "Writes the game's textures as DDS files into the textures folder, to make a pack from.");
            toggle("##texturedump", "texture_dump", "0");
            EndSettingsTable();
        }
    }

    void App::DrawGameLanguage()
    {
        if (!BeginSettingsTable("##language"))
            return;
        SettingRow(strings::GameText, strings::GameTextHint);
        Combo("##text", m_s->game.id, "language", LanguageChoices, 0);
        SettingRow(strings::GameVoices, strings::GameVoicesHint);
        Combo("##voice", m_s->game.id, "voice_language", LanguageChoices, 0);
        EndSettingsTable();
    }

    // ---- Disc --------------------------------------------------------------------------------------------------------

    // The game forgets its disc image: PLAY asks for one again. The file, the built game and the saves are not touched.
    void App::ForgetDisc()
    {
        m_s->verify.Join();
        m_s->chosen.clear();
        m_s->disc = DiscState::None;
        m_s->discPath.clear();
        m_s->discSize = 0;
        m_s->check = {};
        m_s->installView = m_s->kitUsed = false;
        m_config[m_s->game.id].erase("iso");
        if (!SaveLauncherConfig(m_configPath, m_config))
            Toast("Couldn't save the settings");
        RefreshInstall();
    }

    void App::DrawGameDisc()
    {
        Title("Disc image", TitleSize, Text);
        auto note = [](const char* text) {
            ImGui::PushFont(nullptr, SmallSize);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        };
        if (m_s->fromCopy)
        {
            // The game plays from its copied game data: the image is not used, so its buttons are not offered.
            std::string iso = SettingGet(m_config, m_s->game.id, "iso");
            ImGui::TextWrapped("%s", iso.empty() ? "None chosen." : iso.c_str());
            note("Not needed: the game plays from its copied game data, below. Remove the copy to use the disc image again.");
        }
        else
        {
            ImGui::TextWrapped("%s", m_s->discPath.empty() ? "None chosen yet." : m_s->discPath.c_str());
            switch (m_s->disc)
            {
            case DiscState::Ready:
                ImGui::TextColored(ImColor(Rgb(Ok)), "Verified, %s", LauncherFormatSize(m_s->discSize).c_str());
                break;
            case DiscState::Verifying:
                ImGui::TextColored(ImColor(Rgb(Play)), "%s...", m_s->verify.Stage().c_str());
                break;
            case DiscState::Failed:
                ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Danger));
                ImGui::TextWrapped("%s", m_s->check.message.c_str());
                ImGui::PopStyleColor();
                if (const GameInfo* other = FindGame(m_s->check.otherGame); other != nullptr && m_s->check.status == GameSourceStatus::WrongGame)
                    if (PillButton(fmt::format("Use it for {}", other->name).c_str(), true, 0, true))
                        UseDiscForOtherGame();
                break;
            case DiscState::None:
                break;
            }
            const bool busy = Installing() || m_s->disc == DiscState::Verifying || m_s->copyJob.Running();
            ImGui::BeginDisabled(busy);
            if (PillButton(m_s->discPath.empty() ? "Choose disc..." : "Change disc...", true, 200))
                Browse();
            ImGui::SameLine();
            ImGui::BeginDisabled(m_s->discPath.empty());
            if (PillButton("Remove disc", false, 200))
                ForgetDisc();
            ImGui::SameLine();
            if (PillButton("Show in folder", false, 200))
            {
                std::filesystem::path parent = std::filesystem::path(std::u8string(m_s->discPath.begin(), m_s->discPath.end())).parent_path();
                if (!OpenFolder(parent))
                    Toast("Couldn't open the folder");
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            note("Removing the disc only makes Skylauncher forget it: the file stays where it is, and so do the game "
                "Skylauncher built and your saves. The game reads the disc image while it runs, so PLAY asks for it again, unless "
                "you copy the game data below.");
        }
        ImGui::Dummy(ImVec2(0, 16));
        DrawGameData();
    }

    // ---- Saves -------------------------------------------------------------------------------------------------------

    // The game's save backups (save_backup.h): one row per backup (date, time, size) and the button that puts it back.
    // The game keeps them itself before it overwrites a save; restoring one first keeps the saves the player has now.
    void App::RefreshSaveBackups()
    {
        m_s->saveBackups = savebackup::List(savebackup::Root(m_s->game.id));
        m_s->saveBackupsLoaded = true;
    }

    void App::RestoreSaveBackup(const std::string& stamp)
    {
        const savebackup::Restore restore = savebackup::RestoreBackup(savebackup::Root(m_s->game.id), stamp);
        m_s->saveBackupsLoaded = false; // the restore made a backup of its own
        if (restore.ok)
            Toast("Saves restored from " + stamp.substr(0, 4) + "-" + stamp.substr(4, 2) + "-" + stamp.substr(6, 2) + " " +
                stamp.substr(9, 2) + ":" + stamp.substr(11, 2));
        else if (restore.error == "the game is running")
            Toast("Quit the game before restoring a backup", true);
        else
            Toast("Couldn't restore the backup: " + restore.error);
    }

    void App::DrawSaveBackups()
    {
        Title("Save backups", TitleSize, Text);
        if (!m_s->saveBackupsLoaded)
            RefreshSaveBackups();
        const bool running = savebackup::GameRunning(savebackup::Root(m_s->game.id));
        if (PillButton("Open saves folder", false, 260))
        {
            std::filesystem::path saves = savebackup::Root(m_s->game.id);
            std::error_code error;
            std::filesystem::create_directories(saves, error);
            if (!OpenFolder(saves))
                Toast("Couldn't open the folder");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped(running ? "The game is running: quit it to restore a backup."
            : "The game keeps the previous version of a save here before it overwrites it. Restoring one keeps the saves you have now "
              "first.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (m_s->saveBackups.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
            ImGui::TextUnformatted("No backups yet.");
            ImGui::PopStyleColor();
            return;
        }

        // Ten rows at a time; the newest first.
        ImGui::BeginChild("##savebackuplist", ImVec2(0, std::min(300.0f, 44.0f * float(m_s->saveBackups.size()))), ImGuiChildFlags_NavFlattened);
        if (ImGui::BeginTable("##savebackups", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableSetupColumn("when");
            ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed, 128);
            for (const savebackup::Entry& backup : m_s->saveBackups)
            {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 40);
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(backup.date.c_str());
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
                ImGui::TextUnformatted(backup.time.c_str());
                ImGui::PopStyleColor();
                if (backup.files == 1 && ImGui::IsItemHovered())
                    ImGui::SetTooltip("One file");
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(LauncherFormatSize(backup.bytes).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::BeginDisabled(running);
                ImGui::PushID(backup.stamp.c_str());
                if (HoldButton(strings::RestoreSaves, 112, strings::RestoreSavesHint))
                    RestoreSaveBackup(backup.stamp);
                ImGui::PopID();
                ImGui::EndDisabled();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }

    // ---- Game: shortcut, folders, reinstall, delete data, uninstall -----------------------------------------------------

    void App::RefreshDataSizes()
    {
        m_s->dataSizes = MeasureGameData(FoldersOf(m_s->game.id));
        m_s->dataSizesLoaded = true;
    }

    void App::RunDataAction(const DataResult& result, const char* done)
    {
        m_s->saveBackupsLoaded = m_s->dataSizesLoaded = false;
        Toast(result.ok ? fmt::format("{}: {} freed", done, LauncherFormatSize(result.freed)) : result.error, !result.ok);
    }

    void App::RunUninstall(const DataResult& result)
    {
        if (!SaveLauncherConfig(m_configPath, m_config))
            Toast("Couldn't save the settings");
        m_s->installed = {};
        RefreshInstall();
        RunDataAction(result, "Game uninstalled");
    }

    // A shortcut on the Desktop that opens Skylauncher on this game and starts it, with the game's icon from its disc.
    void App::CreateShortcut()
    {
        ShortcutSpec spec;
        spec.gameId = m_s->game.id;
        spec.gameName = m_s->game.name;
        spec.launcher = SelfExecutable();
        spec.iconPng = m_s->meta.iconPng;
        spec.folder = DesktopFolder();
        spec.iconFolder = GameInstallFolder({});
        ShortcutResult result = CreateDesktopShortcut(spec);
        Toast(result.ok ? fmt::format("Shortcut added to the Desktop: {}", datadir::Text(result.file.filename())) : result.error, !result.ok);
    }

    void App::DrawGameTab()
    {
        if (!m_s->dataSizesLoaded)
            RefreshDataSizes();
        const GameFolders folders = FoldersOf(m_s->game.id);
        const bool busy = Installing(), running = GameIsRunning(folders);
        auto note = [](const char* text) {
            ImGui::PushFont(nullptr, SmallSize);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        };

        Title("Desktop shortcut", TitleSize, Text);
        ImGui::BeginDisabled(m_s->disc != DiscState::Ready);
        if (PillButton("Create a desktop shortcut", true, 300, true))
            CreateShortcut();
        ImGui::EndDisabled();
        note(m_s->disc == DiscState::Ready ? "Puts this game on your Desktop with its icon. It opens Skylauncher on this game and starts it."
                                           : "Choose and verify the disc first: the shortcut takes the game's icon from it.");
        ImGui::Dummy(ImVec2(0, 16));

        Title("Folders", TitleSize, Text);
        if (PillButton("Open the game folder", false, 260))
        {
            std::error_code error;
            std::filesystem::create_directories(folders.game, error);
            if (!OpenFolder(folders.game))
                Toast("Couldn't open the folder");
        }
        ImGui::SameLine();
        if (PillButton(strings::OpenFigures, false, 260))
        {
            SDL_CreateDirectory(m_s->collection->Folder().c_str());
            std::string folder = m_s->collection->Folder();
            OpenFolder(std::filesystem::path(std::u8string(folder.begin(), folder.end())));
        }
        ImGui::Dummy(ImVec2(0, 16));

        Title("Game", TitleSize, Text);
        if (!m_s->installed.runtime.empty())
        {
            ImGui::TextWrapped("Built on this computer for this disc: %s", m_s->installed.runtime.c_str());
            ImGui::BeginDisabled(busy || running);
            if (HoldButton(strings::Reinstall, 260, strings::ReinstallHint) && !Installing())
                StartInstall();
            ImGui::EndDisabled();
        }
        else
            ImGui::TextWrapped("Not built on this computer yet.");
        note(fmt::format("Skylauncher starts the game it built for the disc, else the game runtime ({}) next to it, or the one set as "
            "runtime= in the [launcher] section of {}.", m_s->game.runtime, m_configPath).c_str());
        ImGui::Dummy(ImVec2(0, 16));

        Title("Delete data", TitleSize, Text);
        ImGui::BeginDisabled(busy || running);
        if (PillButton("Clear caches", false, 200))
            RunDataAction(ClearGameCaches(folders), "Caches cleared");
        ImGui::SameLine();
        if (HoldButton(strings::DeleteData, 300, strings::DeleteDataHint))
            RunDataAction(DeleteGameData(folders), "Saves and data deleted");
        ImGui::EndDisabled();
        note(running ? "The game is running: quit it first."
                     : fmt::format("Clear caches frees {} (the shader cache, the art cache, the logs): the game makes them again. Delete saves and data "
                                   "frees {} more: the saves, their backups and the seats of the next session. Kept: the game and the disc, your "
                                   "settings, the textures folder, and the figure collection, which both games share.",
                           Freed(m_s->dataSizes.caches), Freed(m_s->dataSizes.saves)).c_str());
        ImGui::Dummy(ImVec2(0, 16));

        Title("Uninstall", TitleSize, Text);
        ImGui::BeginDisabled(busy || running || m_s->installed.runtime.empty());
        if (HoldButton(strings::Uninstall, 300, strings::UninstallHint))
            RunUninstall(UninstallGame(folders, m_config));
        ImGui::EndDisabled();
        note(fmt::format("Deletes the game Skylauncher built, {}. Kept: your saves and their backups, the textures folder, the disc image and "
                         "your settings. INSTALL builds it again.", Freed(m_s->dataSizes.installed)).c_str());
    }
}

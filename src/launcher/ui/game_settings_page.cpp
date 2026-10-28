// A game's own settings (the Settings button on its Play page): Video, Language, Disc, Saves and Game (shortcut, folders,
// reinstall, delete data, uninstall). Arkeyan Launcher's own settings and what both games share (the controller, the keys) are in
// the gear's Settings (launcher_ui.cpp). The video keys are the game's own copy in [<game>.video] (game_settings.h).
#include "launcher/ui/launcher_app.h"
#include "launcher/shortcut.h"
#include "shared/collection/datadir.h"
#include <fmt/format.h>
#include <fstream>
#include <iterator>

namespace launcher_ui
{
    namespace
    {
        namespace strings
        {
            constexpr const char* GameText = N_("Game text");
            constexpr const char* GameTextHint = N_("Menus, subtitles and on-screen text.");
            constexpr const char* GameVoices = N_("Game voices");
            constexpr const char* GameVoicesHint = N_("Spoken lines and cutscenes.");
            constexpr const char* RestoreSaves = N_("Restore");
            constexpr const char* RestoreSavesHint = N_("Hold to restore; your current saves are kept first.");
            constexpr const char* Reinstall = N_("Reinstall");
            constexpr const char* ReinstallHint = N_("Hold to rebuild the game.");
            constexpr const char* DeleteData = N_("Delete saves and data");
            constexpr const char* DeleteDataHint = N_("Hold to delete saves, backups and caches.");
            constexpr const char* Uninstall = N_("Remove game");
            constexpr const char* UninstallHint = N_("Hold to delete the built game. Saves are kept.");
            constexpr const char* OpenFigures = N_("Figures");
        }

        // "12.3 GB" for what a button would free, nothing for less than a megabyte of noise.
        std::string Freed(uint64_t bytes) { return bytes == 0 ? std::string(Tr("nothing yet")) : LauncherFormatSize(bytes); }
    }

    // ---- The page ----------------------------------------------------------------------------------------------------

    void App::DrawGameSettings()
    {
        // A centred column, like the launcher's settings.
        float available = ImGui::GetContentRegionAvail().x;
        float width = std::min(available, 940.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) / 2);
        ImGui::BeginChild("##gamesettingscolumn", ImVec2(width, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        ImGui::SetScrollX(0.0f);

        Title(m_s->game.name, HeadlineSize, Text);
        ImGui::SameLine(width - PillWidth(Tr("Back"), 120) - 30);
        if (PillButton(Tr("Back"), false, 120))
            GoTo(Page::Play);
        ImGui::PushFont(nullptr, SmallSize);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::TextUnformatted(Tr("For this game only."));
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4));

        static const char* const Tabs[] = { N_("Video"), N_("Language"), N_("Disc"), N_("Saves"), N_("Game") };
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

        SettingRow(Tr("Fullscreen"), nullptr);
        toggle("##fullscreen", "fullscreen", "0");

        SettingRow(Tr("Resolution scale"), Tr("Higher scales need a dedicated graphics card."));
        Combo("##scale", video, "resolution_scale", ScaleChoices, 0);
        if (int scale = SettingInt(m_config, video, "resolution_scale", 0); scale >= 1)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
            ImGui::Text("%s", Tf("{} x {} for the 3D scene", 1120 * std::min(scale, 10), 704 * std::min(scale, 10)).c_str());
            ImGui::PopStyleColor();
        }

        SettingRow(Tr("Frame rate limit"), Tr("The game was made for 30 FPS; 60 is experimental."));
        Combo("##fps", video, "fps_limit", FpsChoices, 0);

        SettingRow(Tr("Skip intro movies"), Tr("Skips the start-up logos."));
        toggle("##skipintromovies", "skip_intro_movies", "0");

        if (m_s->game.id == std::string_view("swapforce"))
        {
            SettingRow(Tr("Original resolution"), Tr("The 3D image at the console's size, stretched to the screen."));
            toggle("##originalresolution", "original_resolution", "0");
        }

        SettingRow(Tr("VSync"), Tr("No tearing, a little more latency."));
        toggle("##vsync", "vsync", "1");

        SettingRow(Tr("Texture filtering"), Tr("Keeps textures sharp at an angle."));
        Combo("##filter", video, "anisotropy", FilterChoices, 0);

        SettingRow(Tr("Widescreen"), Tr("Auto shows more on wide windows; 16:9 adds bars."));
        Combo("##aspect", video, "aspect", AspectChoices, 0);

        SettingRow(Tr("HD texture packs"), Tr("Replaces the game's textures with your pack."));
        toggle("##texturepacks", "texture_packs", "1");
        if (PillButton(Tr("Show in folder"), false, 300))
        {
            std::error_code error;
            std::filesystem::create_directories(GameInstallFolder({}) / "textures", error);
            if (!OpenFolder(GameInstallFolder({}) / "textures"))
                Toast(Tr("Couldn't open the folder"), true);
        }
        EndSettingsTable();

        if (ImGui::CollapsingHeader(Tr("Advanced")) && BeginSettingsTable("##graphicsadvanced"))
        {
            SettingRow(Tr("Dump textures"), Tr("For pack makers: saves the textures as DDS files."));
            toggle("##texturedump", "texture_dump", "0");
            EndSettingsTable();
        }
    }

    void App::DrawGameLanguage()
    {
        if (!BeginSettingsTable("##language"))
            return;
        SettingRow(Tr(strings::GameText), Tr(strings::GameTextHint));
        const std::vector<Choice> text = GameLanguageChoices(m_config, m_s->game.id, false);
        Combo("##text", m_s->game.id, "language", text, 0);
        SettingRow(Tr(strings::GameVoices), Tr(strings::GameVoicesHint));
        const std::vector<Choice> voice = GameLanguageChoices(m_config, m_s->game.id, true);
        Combo("##voice", m_s->game.id, "voice_language", voice, 0);
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
        StoreDiscLanguages(m_config, m_s->game.id, {});
        if (!SaveLauncherConfig(m_configPath, m_config))
            Toast(Tr("Couldn't save the settings"), true);
        RefreshInstall();
    }

    void App::DrawGameDisc()
    {
        Title(Tr("Disc image"), TitleSize, Text);
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
            ImGui::TextWrapped("%s", iso.empty() ? Tr("None chosen.") : LauncherMiddleEllipsis(std::filesystem::path(std::u8string(iso.begin(), iso.end())).filename().string(), 60).c_str());
            note(Tr("Not needed: the game plays from its copied data."));
        }
        else
        {
            ImGui::TextWrapped("%s", m_s->discPath.empty() ? Tr("None chosen yet.") : LauncherMiddleEllipsis(std::filesystem::path(std::u8string(m_s->discPath.begin(), m_s->discPath.end())).filename().string(), 60).c_str());
            switch (m_s->disc)
            {
            case DiscState::Ready:
                ImGui::TextColored(ImColor(Rgb(Ok)), "%s", Tf("Verified, {}", LauncherFormatSize(m_s->discSize)).c_str());
                break;
            case DiscState::Verifying:
                ImGui::TextColored(ImColor(Rgb(Play)), "%s...", m_s->verify.Stage().c_str());
                break;
            case DiscState::Failed:
                ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Danger));
                ImGui::TextWrapped("%s", m_s->check.message.c_str());
                ImGui::PopStyleColor();
                if (const GameInfo* other = FindGame(m_s->check.otherGame); other != nullptr && m_s->check.status == GameSourceStatus::WrongGame)
                    if (PillButton(Tf("Use it for {}", other->name).c_str(), true, 0, true))
                        UseDiscForOtherGame();
                break;
            case DiscState::None:
                break;
            }
            const bool busy = Installing() || m_s->disc == DiscState::Verifying || m_s->copyJob.Running();
            ImGui::BeginDisabled(busy);
            if (PillButton(m_s->discPath.empty() ? Tr("Choose...") : Tr("Change..."), true, 200))
                Browse();
            ImGui::SameLine();
            ImGui::BeginDisabled(m_s->discPath.empty());
            if (PillButton(Tr("Remove"), false, 200))
                ForgetDisc();
            ImGui::SameLine();
            if (PillButton(Tr("Show in folder"), false, 200))
            {
                std::filesystem::path parent = std::filesystem::path(std::u8string(m_s->discPath.begin(), m_s->discPath.end())).parent_path();
                if (!OpenFolder(parent))
                    Toast(Tr("Couldn't open the folder"), true);
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            note(Tr("Forgets the disc; the file stays where it is."));
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
            Toast(Tf("Saves restored from {}", stamp.substr(0, 4) + "-" + stamp.substr(4, 2) + "-" + stamp.substr(6, 2) + " " +
                stamp.substr(9, 2) + ":" + stamp.substr(11, 2)));
        else if (restore.error == "the game is running")
            Toast(Tr("Quit the game before restoring a backup"), true);
        else
            Toast(Tf("Couldn't restore the backup: {}", restore.error), true);
    }

    void App::DrawSaveBackups()
    {
        Title(Tr("Save backups"), TitleSize, Text);
        if (!m_s->saveBackupsLoaded)
            RefreshSaveBackups();
        const bool running = savebackup::GameRunning(savebackup::Root(m_s->game.id));
        if (PillButton(Tr("Show in folder"), false, 260))
        {
            std::filesystem::path saves = savebackup::Root(m_s->game.id);
            std::error_code error;
            std::filesystem::create_directories(saves, error);
            if (!OpenFolder(saves))
                Toast(Tr("Couldn't open the folder"), true);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("%s", running ? Tr("The game is running: quit it to restore a backup.")
            : Tr("Earlier versions of your saves."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (m_s->saveBackups.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
            ImGui::TextUnformatted(Tr("No backups yet."));
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
                    ImGui::SetTooltip("%s", Tr("One file"));
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(LauncherFormatSize(backup.bytes).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::BeginDisabled(running);
                ImGui::PushID(backup.stamp.c_str());
                if (HoldButton(Tr(strings::RestoreSaves), 112, Tr(strings::RestoreSavesHint)))
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

    void App::RunDataAction(const DataResult& result, const char* done) // `done`: an English string, translated here
    {
        m_s->saveBackupsLoaded = m_s->dataSizesLoaded = false;
        Toast(result.ok ? Tf("{}: {} freed", Tr(done), LauncherFormatSize(result.freed)) : result.error, !result.ok);
    }

    void App::RunUninstall(const DataResult& result)
    {
        if (!SaveLauncherConfig(m_configPath, m_config))
            Toast(Tr("Couldn't save the settings"), true);
        m_s->installed = {};
        RefreshInstall();
        RunDataAction(result, N_("Game uninstalled"));
    }

    // A shortcut on the Desktop that opens Arkeyan Launcher on this game and starts it, with the game's icon from its disc.
    void App::CreateShortcut()
    {
        ShortcutSpec spec;
        spec.gameId = m_s->game.id;
        spec.gameName = m_s->game.name;
        spec.launcher = SelfExecutable();
        // The built game's own icon (game_icon.png in its folder: the disc's logo, made by SkyBuild), else the disc's title icon.
        spec.iconFolder = GameInstallFolder({});
        spec.iconFolder = GameInstallFolder({});
        if (std::ifstream file(spec.iconFolder / "game_icon.png", std::ios::binary); file)
            spec.iconPng.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (spec.iconPng.empty())
            spec.iconPng = m_s->meta.iconPng;
        spec.folder = DesktopFolder();
        ShortcutResult result = CreateDesktopShortcut(spec);
        Toast(result.ok ? Tr("Shortcut added to the Desktop") : result.error, !result.ok);
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

        Title(Tr("Desktop shortcut"), TitleSize, Text);
        ImGui::BeginDisabled(m_s->disc != DiscState::Ready);
        if (PillButton(Tr("Create"), true, 300, true))
            CreateShortcut();
        ImGui::EndDisabled();
        if (m_s->disc != DiscState::Ready)
            note(Tr("Choose the disc image first."));
        ImGui::Dummy(ImVec2(0, 16));

        Title(Tr("Folders"), TitleSize, Text);
        if (PillButton(Tr("Game files"), false, 260))
        {
            std::error_code error;
            std::filesystem::create_directories(folders.game, error);
            if (!OpenFolder(folders.game))
                Toast(Tr("Couldn't open the folder"), true);
        }
        ImGui::SameLine();
        if (PillButton(Tr(strings::OpenFigures), false, 260))
        {
            SDL_CreateDirectory(m_s->collection->Folder().c_str());
            std::string folder = m_s->collection->Folder();
            OpenFolder(std::filesystem::path(std::u8string(folder.begin(), folder.end())));
        }
        ImGui::Dummy(ImVec2(0, 16));

        Title(Tr("Game"), TitleSize, Text);
        if (!m_s->installed.runtime.empty())
        {
            ImGui::TextWrapped("%s", Tr("Built for this disc."));
            ImGui::BeginDisabled(busy || running);
            if (HoldButton(Tr(strings::Reinstall), 260, Tr(strings::ReinstallHint)) && !Installing())
                StartInstall();
            ImGui::EndDisabled();
        }
        else
            ImGui::TextWrapped("%s", Tr("Not built yet."));
        ImGui::Dummy(ImVec2(0, 16));

        Title(Tr("Clean up"), TitleSize, Text);
        ImGui::BeginDisabled(busy || running);
        if (PillButton(Tr("Clear caches"), false, 200))
            RunDataAction(ClearGameCaches(folders), N_("Caches cleared"));
        ImGui::SameLine();
        if (HoldButton(Tr(strings::DeleteData), 300, Tr(strings::DeleteDataHint)))
            RunDataAction(DeleteGameData(folders), N_("Saves and data deleted"));
        ImGui::EndDisabled();
        note(running ? Tr("The game is running: quit it first.")
                     : Tf("Clearing caches frees {}; deleting saves and data frees {} more.",
                           Freed(m_s->dataSizes.caches), Freed(m_s->dataSizes.saves)).c_str());
        ImGui::Dummy(ImVec2(0, 16));

        Title(Tr("Uninstall"), TitleSize, Text);
        ImGui::BeginDisabled(busy || running || m_s->installed.runtime.empty());
        if (HoldButton(Tr(strings::Uninstall), 300, Tr(strings::UninstallHint)))
            RunUninstall(UninstallGame(folders, m_config));
        ImGui::EndDisabled();
        note(Tf("Frees {}. Saves and settings are kept.", Freed(m_s->dataSizes.installed)).c_str());
    }
}

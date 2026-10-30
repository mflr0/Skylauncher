// A game's own settings (the Settings button on its Play page): Video, Keyboard, Language, Disc, Saves and Game (shortcut, folders,
// reinstall, delete data, uninstall). Arkeyan Launcher's own settings and what all games share (the controller) are in
// the gear's Settings (launcher_ui.cpp). The video keys and the keyboard keys are the game's own copy in [<game>.video] and [<game>.input] (game_settings.h, game_keys.h).
#include "launcher/ui/launcher_app.h"
#include "launcher/shortcut.h"
#include "shared/collection/datadir.h"
#include <ctime>
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
            constexpr const char* ClearCachesHint = N_("Hold to clear the caches.");
            constexpr const char* DeleteData = N_("Delete saves and data");
            constexpr const char* DeleteDataHint = N_("Hold to delete saves, backups and caches.");
            constexpr const char* Uninstall = N_("Remove game");
            constexpr const char* UninstallHint = N_("Hold to delete the built game. Saves are kept.");
            constexpr const char* OpenFigures = N_("Figures");
            constexpr const char* ResetKeys = N_("Reset to defaults");
            constexpr const char* ResetKeysHint = N_("Hold to reset every key of this game.");
            constexpr const char* ResetPadHint = N_("Hold to give every button its own job back.");
        }

        // "12.3 GB" for what a button would free, nothing for less than a megabyte of noise.
        std::string Freed(uint64_t bytes) { return bytes == 0 ? std::string(Tr("nothing yet")) : ui::FormatSize(bytes); }

        // A save backup's folder name ("YYYYMMDD-HHMMSS", local time) as the time it was made.
        std::time_t StampTime(const std::string& stamp)
        {
            std::tm when{};
            int year = 0, month = 0, day = 0, hour = 0, minute = 0;
            if (std::sscanf(stamp.c_str(), "%4d%2d%2d-%2d%2d", &year, &month, &day, &hour, &minute) != 5)
                return 0;
            when.tm_year = year - 1900, when.tm_mon = month - 1, when.tm_mday = day, when.tm_hour = hour, when.tm_min = minute;
            when.tm_isdst = -1;
            return std::mktime(&when);
        }

        // "Today 09:30", "Yesterday 09:30", else the date and the time (backups come several a day).
        std::string BackupWhen(const std::string& stamp)
        {
            const std::time_t when = StampTime(stamp);
            const std::string text = ui::FormatWhen(when, std::time(nullptr));
            return text.find(':') != std::string::npos ? text : fmt::format("{}, {}", text, ui::FormatTime(when));
        }
    }

    // ---- The page ----------------------------------------------------------------------------------------------------

    void App::DrawGameSettings()
    {
        // A centred column, like the launcher's settings.
        const char* title = GameName(m_s->game.id, GameNameForm::Full);
        BeginSettingsPage("##gamesettingscolumn", title != nullptr ? title : m_s->game.name, true);

        static const char* const Tabs[] = { N_("Video"), N_("Keyboard"), N_("Controller"), N_("Language"), N_("Disc"), N_("Saves"), N_("Game") };
        int tab = int(m_gameTab);
        DrawTabBar(Tabs, int(std::size(Tabs)), &tab);
        m_gameTab = GameTab(tab);
        BeginCard("##gamesettings", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
        switch (m_gameTab)
        {
        case GameTab::Video: DrawGameVideo(); break;
        case GameTab::Keyboard: DrawGameKeyboard(); break;
        case GameTab::Controller: DrawGameController(); break;
        case GameTab::Language: DrawGameLanguage(); break;
        case GameTab::Disc: DrawGameDisc(); break;
        case GameTab::Saves: DrawSaveBackups(); break;
        case GameTab::Game: DrawGameTab(); break;
        }
        EndCard();
        EndSettingsPage();
    }

    // ---- Video and language ------------------------------------------------------------------------------------------

    void App::DrawGameVideo()
    {
        if (!ui::BeginSettings("##graphics"))
            return;
        const std::string videoSection = GameVideoSection(m_s->game.id);
        const char* video = videoSection.c_str();
        auto toggle = [&](const char* id, const char* key, const char* fallback) {
            bool value = ParseBool(SettingGet(m_config, video, key, fallback));
            if (ui::Toggle(id, &value))
                Set(video, key, value ? "1" : "0");
        };

        ui::SettingRow(Tr("Fullscreen"), nullptr);
        toggle("##fullscreen", "fullscreen", "0");

        std::string scaleHint = Tr("Higher scales need a dedicated graphics card.");
        if (int scale = SettingInt(m_config, video, "resolution_scale", 0); scale >= 1)
            scaleHint += "\n" + Tf("{} x {} for the 3D scene", 1120 * std::min(scale, 10), 704 * std::min(scale, 10));
        ui::SettingRow(Tr("Resolution scale"), scaleHint.c_str());
        SettingSelect("##scale", video, "resolution_scale", ScaleChoices, 0);

        ui::SettingRow(Tr("Frame rate limit"), Tr("Made for 30 FPS; 60 and above are experimental."));
        int fpsDefault = 0;
        for (size_t i = 0; i < std::size(FpsChoices); i++)
            if (std::string_view(FpsChoices[i].value) == DefaultFrameRate(m_s->game.id))
                fpsDefault = int(i);
        SettingSelect("##fps", video, "fps_limit", FpsChoices, fpsDefault);

        ui::SettingRow(Tr("FPS counter"), Tr("A small frame rate readout in the game (F3 in the game turns it on too)."));
        toggle("##showfps", "show_fps", "0");

        ui::SettingRow(Tr("Volume"), Tr("The game's master volume (F3 in the game changes it too)."));
        {
            const std::string audioSection = std::string(m_s->game.id) + ".audio";
            SettingSelect("##volume", audioSection.c_str(), "volume", VolumeChoices, 10);
        }

        ui::SettingRow(Tr("Skip intro movies"), nullptr);
        toggle("##skipintromovies", "skip_intro_movies", "0");

        if (m_s->game.id == std::string_view("swapforce"))
        {
            ui::SettingRow(Tr("Original resolution"), Tr("The 3D scene at the console's size."));
            toggle("##originalresolution", "original_resolution", "0");
        }

        ui::SettingRow(Tr("VSync"), Tr("No tearing, a little more latency."));
        toggle("##vsync", "vsync", "1");

        ui::SettingRow(Tr("Texture filtering"), Tr("Keeps textures sharp at an angle."));
        SettingSelect("##filter", video, "anisotropy", FilterChoices, 0);

        ui::SettingRow(Tr("Widescreen"), Tr("Auto shows more when wide; 16:9 adds bars."));
        SettingSelect("##aspect", video, "aspect", AspectChoices, 0);

        ui::SettingRow(Tr("HD texture packs"), Tr("Replaces the game's textures with your pack."));
        toggle("##texturepacks", "texture_packs", "1");
        ui::SettingRow(Tr("Texture pack folder"), nullptr);
        ui::ButtonOptions folder;
        folder.icon = ui::Icon::Folder;
        if (ui::Button(Tr("Show in folder"), folder))
        {
            std::error_code error;
            std::filesystem::create_directories(GameInstallFolder({}) / "textures", error);
            if (!OpenFolder(GameInstallFolder({}) / "textures"))
                Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
        }
        ui::EndSettings();

        ui::SectionHeader(Tr("Advanced"));
        if (ui::BeginSettings("##graphicsadvanced"))
        {
            ui::SettingRow(Tr("Dump textures"), Tr("For pack makers: saves the textures as DDS files."));
            toggle("##texturedump", "texture_dump", "0");
            ui::EndSettings();
        }
    }

    // The keys of this game, as key caps: the movement (the left stick) as a cross of four keys, the right stick's as another
    // when the game uses it, then one row per other action, named as the game names it, the pad's button after the name.
    // [<game>.input] holds them; PLAY puts them in [input] (game_keys.h).
    void App::DrawGameKeyboard()
    {
        const std::string& game = m_s->game.id;
        const std::string section = GameInputSection(game);
        const std::span<const GameKey> keys = GameKeys(game);
        const PadFamily family = PadFamilyNow();
        auto find = [&](std::string_view action) -> const GameKey* {
            for (const GameKey& key : keys)
                if (action == key.action)
                    return &key;
            return nullptr;
        };
        // What the key is for, in words: the right stick's directions are named by the stick (their rows say "Same").
        auto purpose = [&](const GameKey& action) {
            return std::string_view(action.action).starts_with("right_") ? PadButtonName(action, family) : std::string(Tr(action.label));
        };
        auto keyCap = [&](const GameKey& action) {
            const std::string key = KeyDisplayName(GameKeyName(m_config, game, action));
            const std::string tip = purpose(action);
            ui::KeyButtonOptions options;
            options.capturing = m_capturing == &action;
            options.conflict = GameKeyConflict(m_config, game, action);
            options.tooltip = options.conflict ? Tr("This key is used by another action") : tip.c_str();
            ImGui::PushID(action.action);
            if (ui::KeyButton("##key", key.c_str(), options))
                m_capturing = &action;
            ImGui::PopID();
        };
        auto capWidth = [&](const GameKey* action) {
            ui::KeyButtonOptions options;
            options.capturing = m_capturing == action;
            options.conflict = GameKeyConflict(m_config, game, *action);
            return ui::KeyButtonWidth(KeyDisplayName(GameKeyName(m_config, game, *action)).c_str(), options);
        };

        // What to do, or what is awaited.
        ui::Text(ui::TextStyle::Body, m_capturing != nullptr ? Tf("Press a key for \"{}\" (Escape cancels)", purpose(*m_capturing)).c_str()
                                                              : Tr("Click a key to change it."), m_capturing != nullptr ? ui::color::Ink : ui::color::Dim);
        ImGui::Dummy(ImVec2(0, ui::space::XS));

        // The crosses, side by side: a title (the action, the stick after it), then up over down, left and right beside it.
        struct Cross
        {
            std::string title, note;
            const GameKey *up, *left, *down, *right;
        };
        std::vector<Cross> crosses;
        if (const GameKey* up = find("left_up"); up != nullptr && find("left_down") && find("left_left") && find("left_right"))
            crosses.push_back({ Tr("Movement"), "(" + PadButtonName(*up, family) + ")", up, find("left_left"), find("left_down"), find("left_right") });
        if (const GameKey* up = find("right_up"); up != nullptr && find("right_down") && find("right_left") && find("right_right"))
            crosses.push_back({ Tr(up->label), "(" + std::string(Tr("R stick")) + ")", up, find("right_left"), find("right_down"), find("right_right") });
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float columnW = ImGui::GetContentRegionAvail().x / 2;
        const float crossH = ui::type::Body + ui::space::S + 2 * ui::height::Normal + ui::space::XS;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        for (size_t i = 0; i < crosses.size(); i++)
        {
            const Cross& cross = crosses[i];
            const float x = origin.x + float(i) * columnW;
            ui::PushText(ui::TextStyle::Body, true);
            ui::DrawText(draw, ImVec2(x, origin.y), ui::Col(ui::color::Ink), cross.title.c_str());
            const float titleEnd = x + ImGui::CalcTextSize(cross.title.c_str()).x;
            ui::PopText();
            ui::PushText(ui::TextStyle::Caption);
            ui::DrawText(draw, ImVec2(titleEnd + ui::space::XS, origin.y + ui::Baseline(ui::TextStyle::Body, true) - ui::Baseline(ui::TextStyle::Caption)),
                ui::Col(ui::color::Dim), cross.note.c_str());
            ui::PopText();
            const float wL = capWidth(cross.left), wD = capWidth(cross.down), wR = capWidth(cross.right), wU = capWidth(cross.up);
            const float top = origin.y + ui::type::Body + ui::space::S, below = top + ui::height::Normal + ui::space::XS;
            // The keys' row starts on the title's edge; the top key over the middle one.
            const float middle = x + wL + ui::space::XS + wD / 2;
            ImGui::SetCursorScreenPos(ImVec2(std::round(middle - wU / 2), top));
            keyCap(*cross.up);
            ImGui::SetCursorScreenPos(ImVec2(x, below));
            keyCap(*cross.left);
            ImGui::SetCursorScreenPos(ImVec2(x + wL + ui::space::XS, below));
            keyCap(*cross.down);
            ImGui::SetCursorScreenPos(ImVec2(x + wL + wD + 2 * ui::space::XS, below));
            keyCap(*cross.right);
        }
        if (!crosses.empty())
        {
            // A hairline (M below the keys) parts them from the list, like the list's rows from each other.
            const float line = origin.y + crossH + ui::space::M;
            draw->AddLine(ImVec2(origin.x, line + 0.5f), ImVec2(origin.x + ImGui::GetContentRegionAvail().x, line + 0.5f),
                ui::Col(ui::color::Hairline, 110.0f / 255), ui::stroke::Hairline);
            ImGui::SetCursorScreenPos(ImVec2(origin.x, line + ui::stroke::Hairline));
        }

        // The other actions: a settings row each, the key right-aligned in the control column.
        if (ui::BeginSettings("##keys"))
        {
            for (const GameKey& action : keys)
            {
                const std::string_view name = action.action;
                if (name.starts_with("left_") || name.starts_with("right_"))
                    continue;
                const std::string pad = "(" + PadButtonName(action, family) + ")";
                ui::SettingRow(Tr(action.label), nullptr, pad.c_str());
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - capWidth(&action));
                keyCap(action);
            }
            ui::EndSettings();
        }
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        ui::ButtonOptions reset;
        reset.kind = ui::ButtonKind::Danger;
        reset.hold = true;
        reset.tooltip = Tr(strings::ResetKeysHint);
        if (ui::Button(Tr(strings::ResetKeys), reset))
        {
            ResetGameKeys(m_config, game);
            Set(section.c_str(), "seeded", "1");
            m_capturing = nullptr;
            Toast(Tr("Keyboard bindings reset"), ui::ToastKind::Success);
        }
    }

    // The pad of this game: which button does the job of which, and the rumble. [<game>.input] holds them (pad_<button>,
    // rumble_p1, rumble_p2); the runtime reads them from there at the game's start (game_keys.h). A settings row per button
    // (its name, the game's word for its job after it, what it does now in the control column), the rumble, one Reset.
    void App::DrawGameController()
    {
        const std::string& game = m_s->game.id;
        const std::string section = GameInputSection(game);
        const PadFamily family = PadFamilyNow();
        ui::Text(ui::TextStyle::Body, Tr("Choose what each button does."), ui::color::Dim);
        ImGui::Dummy(ImVec2(0, ui::space::XXS));

        // "B (Attack 3 / Back)": a button as the pad prints it, then the game's word for its job.
        auto describe = [&](int button) {
            const std::string job = PadActionName(game, button), name = PadButtonLabel(button, family);
            return job.empty() ? name : name + " (" + job + ")";
        };
        if (ui::BeginSettings("##padmap"))
        {
            for (int button = 0; button < PadButtonTotal; button++)
            {
                const std::string job = PadActionName(game, button);
                const std::string note = job.empty() ? std::string() : "(" + job + ")";
                const std::string name = PadButtonLabel(button, family);
                ui::SettingRow(name.c_str(), nullptr, note.empty() ? nullptr : note.c_str());
                const int target = PadTarget(m_config, game, button);
                const std::string preview = target == button ? std::string(Tr("Default")) : target < 0 ? std::string(Tr("Nothing")) : describe(target);
                ImGui::PushID(button);
                if (ui::BeginSelect("##job", preview.c_str()))
                {
                    int chosen = -2;
                    if (ui::SelectOption(Tr("Default"), target == button))
                        chosen = button;
                    for (int other = 0; other < PadButtonTotal; other++)
                        if (other != button && ui::SelectOption((describe(other) + "##" + std::to_string(other)).c_str(), target == other))
                            chosen = other;
                    if (ui::SelectOption(Tr("Nothing"), target < 0))
                        chosen = -1;
                    ui::EndSelect();
                    if (chosen != -2)
                    {
                        SetPadTarget(m_config, game, button, chosen);
                        Set(section.c_str(), "seeded", "1");
                    }
                }
                ImGui::PopID();
            }
            ui::EndSettings();
        }

        ui::SectionHeader(Tr("Rumble"));
        if (ui::BeginSettings("##rumble"))
        {
            ui::SettingRow(Tr("Player 1"), nullptr);
            SettingSelect("##rumble1", section.c_str(), "rumble_p1", RumbleChoices, 2);
            ui::SettingRow(Tr("Player 2"), nullptr);
            SettingSelect("##rumble2", section.c_str(), "rumble_p2", RumbleChoices, 2);
            ui::EndSettings();
        }
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        ui::ButtonOptions reset;
        reset.kind = ui::ButtonKind::Danger;
        reset.hold = true;
        reset.tooltip = Tr(strings::ResetPadHint);
        if (ui::Button(Tr(strings::ResetKeys), reset))
        {
            ResetPadMap(m_config, game);
            Set(section.c_str(), "seeded", "1");
            Toast(Tr("Controller buttons reset"), ui::ToastKind::Success);
        }
    }

    void App::DrawGameLanguage()
    {
        if (!ui::BeginSettings("##language"))
            return;
        ui::SettingRow(Tr(strings::GameText), Tr(strings::GameTextHint));
        const std::vector<Choice> text = GameLanguageChoices(m_config, m_s->game.id, false);
        SettingSelect("##text", m_s->game.id, "language", text, 0);
        ui::SettingRow(Tr(strings::GameVoices), Tr(strings::GameVoicesHint));
        const std::vector<Choice> voice = GameLanguageChoices(m_config, m_s->game.id, true);
        SettingSelect("##voice", m_s->game.id, "voice_language", voice, 0);
        ui::EndSettings();
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
            Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
        RefreshInstall();
    }

    void App::DrawGameDisc()
    {
        ui::SectionHeader(Tr("Disc image"), nullptr, false);
        auto filename = [](const std::string& path) {
            return LauncherMiddleEllipsis(std::filesystem::path(std::u8string(path.begin(), path.end())).filename().string(), 60);
        };
        if (m_s->fromCopy)
        {
            // The game plays from its copied game data: the image is not used, so its buttons are not offered.
            const std::string iso = SettingGet(m_config, m_s->game.id, "iso");
            ui::Text(ui::TextStyle::Body, iso.empty() ? Tr("None chosen.") : filename(iso).c_str());
            ui::Text(ui::TextStyle::Caption, Tr("Not needed: the game plays from its copied data."), ui::color::Dim);
        }
        else
        {
            ui::Text(ui::TextStyle::Body, m_s->discPath.empty() ? Tr("None chosen yet.") : filename(m_s->discPath).c_str());
            const GameInfo* other = nullptr;
            switch (m_s->disc)
            {
            case DiscState::Ready:
                ui::StatusPill(ui::Status::Success, Tf("Verified, {}", ui::FormatSize(m_s->discSize)).c_str());
                break;
            case DiscState::Verifying:
                ui::StatusPill(ui::Status::Busy, (m_s->verify.Stage() + "...").c_str());
                break;
            case DiscState::Failed:
                ui::StatusPill(ui::Status::Error, SlotStatus(m_s).text.c_str());
                ui::Text(ui::TextStyle::Body, m_s->check.message.c_str(), ui::color::InkSoft);
                if (m_s->check.status == GameSourceStatus::WrongGame)
                    other = FindGame(m_s->check.otherGame);
                break;
            case DiscState::None:
                break;
            }
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            const bool busy = Installing() || m_s->disc == DiscState::Verifying || m_s->copyJob.Running();
            if (other != nullptr)
            {
                ui::ButtonOptions use;
                use.kind = ui::ButtonKind::Primary;
                use.disabled = busy;
                if (ui::Button(Tf("Use it for {}", other->name).c_str(), use))
                    UseDiscForOtherGame();
                ImGui::SameLine(0, ui::space::S);
            }
            ui::ButtonOptions choose;
            choose.kind = other != nullptr || !m_s->discPath.empty() ? ui::ButtonKind::Secondary : ui::ButtonKind::Primary; // Primary: the disc to choose
            choose.disabled = busy;
            if (ui::Button(m_s->discPath.empty() ? Tr("Choose...") : Tr("Change..."), choose))
                Browse();
            ImGui::SameLine(0, ui::space::S);
            ui::ButtonOptions remove;
            remove.disabled = busy || m_s->discPath.empty();
            if (ui::Button(Tr("Remove"), remove))
                ForgetDisc();
            ImGui::SameLine(0, ui::space::S);
            ui::ButtonOptions folder;
            folder.icon = ui::Icon::Folder;
            folder.disabled = busy || m_s->discPath.empty();
            if (ui::Button(Tr("Show in folder"), folder))
            {
                std::filesystem::path parent = std::filesystem::path(std::u8string(m_s->discPath.begin(), m_s->discPath.end())).parent_path();
                if (!OpenFolder(parent))
                    Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
            }
            ui::Text(ui::TextStyle::Caption, Tr("The file stays where it is."), ui::color::Dim);
        }
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
            Toast(Tf("Saves restored from {}", BackupWhen(stamp)), ui::ToastKind::Success);
        else if (restore.error == "the game is running")
            Toast(Tr("Quit the game before restoring a backup"), ui::ToastKind::Error);
        else
            Toast(Tf("Couldn't restore the backup: {}", restore.error), ui::ToastKind::Error);
    }

    void App::DrawSaveBackups()
    {
        if (!m_s->saveBackupsLoaded)
            RefreshSaveBackups();
        const bool running = savebackup::GameRunning(savebackup::Root(m_s->game.id)) || m_session.Running() == m_s->game.id;
        ui::SectionHeader(Tr("Save backups"), running ? Tr("The game is running: quit it to restore a backup.") : Tr("Earlier versions of your saves."), false);
        ui::ButtonOptions folder;
        folder.icon = ui::Icon::Folder;
        if (ui::Button(Tr("Show in folder"), folder))
        {
            std::filesystem::path saves = savebackup::Root(m_s->game.id);
            std::error_code error;
            std::filesystem::create_directories(saves, error);
            if (!OpenFolder(saves))
                Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
        }
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        if (m_s->saveBackups.empty())
        {
            ui::EmptyState(ui::Icon::Folder, Tr("No backups yet."));
            return;
        }

        // Five rows at a time; the newest first. Each: when, how many files, the size, and the hold button that puts it back.
        ui::ButtonOptions restore;
        restore.kind = ui::ButtonKind::Danger;
        restore.hold = true;
        restore.disabled = running;
        restore.tooltip = Tr(strings::RestoreSavesHint);
        const float rowHeight = ui::height::Large + ui::space::XXS + ImGui::GetStyle().ItemSpacing.y; // a row and the gap after it
        ui::BeginRoomyChild("##savebackuplist", ImVec2(0, std::min(5 * rowHeight, rowHeight * float(m_s->saveBackups.size()))), ImGuiChildFlags_NavFlattened);
        const float buttonWidth = ui::ButtonWidth(Tr(strings::RestoreSaves), restore);
        for (const savebackup::Entry& backup : m_s->saveBackups)
        {
            ImGui::PushID(backup.stamp.c_str());
            const float top = ImGui::GetCursorPosY();
            const std::string when = BackupWhen(backup.stamp), size = ui::FormatSize(backup.bytes);
            const std::string files = backup.files == 1 ? std::string(Tr("One file")) : Tf("{} files", ui::FormatNumber(backup.files));
            ui::ListRowOptions row;
            row.subtitle = files.c_str();
            row.trailing = size.c_str();
            row.width = ImGui::GetContentRegionAvail().x - buttonWidth - ui::space::S;
            ui::ListRow(when.c_str(), row);
            ImGui::SameLine(0, ui::space::S);
            ImGui::SetCursorPosY(top + (ui::height::Large - ui::height::Normal) / 2);
            if (ui::Button(Tr(strings::RestoreSaves), restore))
                RestoreSaveBackup(backup.stamp);
            ImGui::SetCursorPosY(top + ui::height::Large);
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            ImGui::PopID();
        }
        ui::EndRoomyChild();
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
        Toast(result.ok ? Tf("{}: {} freed", Tr(done), ui::FormatSize(result.freed)) : result.error, result.ok ? ui::ToastKind::Success : ui::ToastKind::Error);
    }

    void App::RunUninstall(const DataResult& result)
    {
        if (!SaveLauncherConfig(m_configPath, m_config))
            Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
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
        if (std::ifstream file(spec.iconFolder / "game_icon.png", std::ios::binary); file)
            spec.iconPng.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (spec.iconPng.empty())
            spec.iconPng = m_s->meta.iconPng;
        spec.folder = DesktopFolder();
        ShortcutResult result = CreateDesktopShortcut(spec);
        Toast(result.ok ? Tr("Shortcut added to the Desktop") : result.error, result.ok ? ui::ToastKind::Success : ui::ToastKind::Error);
    }

    void App::DrawGameTab()
    {
        if (!m_s->dataSizesLoaded)
            RefreshDataSizes();
        const GameFolders folders = FoldersOf(m_s->game.id);
        const bool busy = Installing(), running = GameIsRunning(folders) || m_session.Running() == m_s->game.id;
        auto hold = [](const char* tooltip, bool disabled = false) {
            ui::ButtonOptions options;
            options.kind = ui::ButtonKind::Danger;
            options.hold = true;
            options.tooltip = tooltip;
            options.disabled = disabled;
            return options;
        };

        const bool swapForce = m_s->game.id == std::string_view("swapforce");
        if (swapForce)
        {
            // The only game with a locked difficulty: Nightmare is offered at once, the save is not changed ([swapforce] in launcher.ini).
            ui::SectionHeader(Tr("Difficulty"), nullptr, false);
            if (ui::BeginSettings("##nightmare"))
            {
                ui::SettingRow(Tr("Nightmare from the start"), Tr("Offered before you beat the game. Your save is not changed."));
                bool value = ParseBool(SettingGet(m_config, m_s->game.id, "nightmare_from_start", "0"));
                if (ui::Toggle("##nightmarefromstart", &value))
                    Set(m_s->game.id, "nightmare_from_start", value ? "1" : "0");
                ui::EndSettings();
            }
        }

        ui::SectionHeader(Tr("Desktop shortcut"), m_s->disc != DiscState::Ready ? Tr("Choose the disc image first.") : nullptr, swapForce);
        ui::ButtonOptions shortcut;
        shortcut.disabled = m_s->disc != DiscState::Ready;
        if (ui::Button(Tr("Create"), shortcut))
            CreateShortcut();

        ui::SectionHeader(Tr("Folders"));
        ui::ButtonOptions folder;
        folder.icon = ui::Icon::Folder;
        if (ui::Button(Tr("Game files"), folder))
        {
            std::error_code error;
            std::filesystem::create_directories(folders.game, error);
            if (!OpenFolder(folders.game))
                Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
        }
        ImGui::SameLine(0, ui::space::S);
        if (ui::Button(Tr(strings::OpenFigures), folder))
        {
            SDL_CreateDirectory(m_s->collection->Folder().c_str());
            std::string figures = m_s->collection->Folder();
            OpenFolder(std::filesystem::path(std::u8string(figures.begin(), figures.end())));
        }

        ui::SectionHeader(Tr("Game"), !m_s->installed.runtime.empty() ? Tr("Built for this disc.") : Tr("Not built yet."));
        if (ui::Button(Tr(strings::Reinstall), hold(Tr(strings::ReinstallHint), busy || running || m_s->installed.runtime.empty())) && !Installing())
            StartInstall();

        ui::SectionHeader(Tr("Clean up"), running ? Tr("The game is running: quit it first.")
                                                  : Tf("Clearing caches frees {}; deleting saves and data frees {} more.",
                                                        Freed(m_s->dataSizes.caches), Freed(m_s->dataSizes.saves)).c_str());
        if (ui::Button(Tr("Clear caches"), hold(Tr(strings::ClearCachesHint), busy || running)))
            RunDataAction(ClearGameCaches(folders), N_("Caches cleared"));
        ImGui::SameLine(0, ui::space::S);
        if (ui::Button(Tr(strings::DeleteData), hold(Tr(strings::DeleteDataHint), busy || running)))
            RunDataAction(DeleteGameData(folders), N_("Saves and data deleted"));

        ui::SectionHeader(Tr("Uninstall"), Tf("Frees {}. Saves and settings are kept.", Freed(m_s->dataSizes.installed)).c_str());
        if (ui::Button(Tr(strings::Uninstall), hold(Tr(strings::UninstallHint), busy || running || m_s->installed.runtime.empty())))
            RunUninstall(UninstallGame(folders, m_config));
    }
}

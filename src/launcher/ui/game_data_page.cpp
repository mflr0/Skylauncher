// "Copy files" in the window (docs/launcher_ui.md "Game data"): the game settings' Disc tab section, where a game's disc
// comes from at start (its copy or the remembered image), and the wrong-disc button. The files and the checks are
// game_copy.h / shared/collection/game_data.h; the script at the end (SKY_LAUNCHER_TEST_COPY) drives all of it in an offscreen window.
#include "launcher/ui/launcher_app.h"
#include "launcher/env.h"
#include "shared/collection/datadir.h"
#include <fmt/format.h>

namespace launcher_ui
{
    // ---- Where a game's disc comes from ----------------------------------------------------------------------------------

    // At the game's first visit: its complete copy if it has one (the disc image is then not needed), else the image the
    // player chose last time, else nothing.
    void App::StartGameDisc()
    {
        GameSourcePick pick = PickGameSource(m_s->game, SettingGet(m_config, m_s->game.id, "iso"));
        m_s->copy = pick.copy;
        m_s->verify.Join();
        if (!pick.path.empty())
        {
            StartVerify(pick.path);
            return;
        }
        m_s->disc = DiscState::None;
        m_s->discPath.clear();
        m_s->discSize = 0;
        m_s->fromCopy = false;
        m_s->check = {};
    }

    // The disc check found the other supported game's disc: the window goes to that game and gives it the disc. The game it
    // was chosen for goes back to the disc it had.
    void App::UseDiscForOtherGame()
    {
        const GameInfo* other = FindEnabledGame(m_s->check.otherGame, m_config);
        if (other == nullptr || m_s->disc != DiscState::Failed)
            return;
        GameSlot& target = SlotFor(*other);
        if (target.installer.Running() || target.kitDownload.Running() || target.copyJob.Running() || target.disc == DiscState::Verifying)
        {
            Toast(Tf("{} is busy: try again when it is done", other->name), ui::ToastKind::Error);
            return;
        }
        const std::string path = m_s->discPath;
        if (SettingGet(m_config, m_s->game.id, "iso") == path)
            m_config[m_s->game.id].erase("iso"); // it never was this game's disc
        StartGameDisc();
        if (!SaveLauncherConfig(m_configPath, m_config))
            Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);

        target.started = true; // its own start (below) replaces the first-visit one
        SelectGame(*other);
        m_s->copy = gamedata::Inspect(gamedata::Folder(m_s->game.id));
        if (m_s->copy.state == gamedata::State::Complete)
        {
            StartGameDisc();
            Toast(Tf("{} already plays from its copied game data", other->name));
        }
        else
            StartVerify(path); // remembered once it is verified
        GoTo(Page::Play);
    }

    // ---- The copy --------------------------------------------------------------------------------------------------------

    void App::StartCopy()
    {
        if (m_s->disc != DiscState::Ready || m_s->fromCopy || Installing() || m_s->copyJob.Running())
            return;
        m_s->copyMessage.clear();
        m_s->copyJob.Start(m_s->game, m_s->discPath, m_s->check.xexSha256);
    }

    void App::FinishCopy()
    {
        using Outcome = gamedata::CopyResult::Outcome;
        gamedata::CopyResult result = m_s->copyJob.Collect();
        m_s->copy = gamedata::Inspect(gamedata::Folder(m_s->game.id));
        if (result.outcome == Outcome::Done)
        {
            Toast(Tf("{}: game data copied", m_s->game.name), ui::ToastKind::Success);
            m_s->copyMessage.clear();
            StartVerify(datadir::Text(gamedata::Folder(m_s->game.id))); // from now on the game plays from the copy
        }
        else if (result.outcome == Outcome::Cancelled)
            Toast(Tr("Copy stopped"));
        else
        {
            m_s->copyMessage = CopyFailureText(result);
            Toast(m_s->copyMessage, ui::ToastKind::Error);
        }
    }

    void App::RemoveCopy()
    {
        if (m_s->copyJob.Running())
            return;
        const std::filesystem::path folder = gamedata::Folder(m_s->game.id);
        const uint64_t size = m_s->copy.total;
        std::string error;
        if (!gamedata::Remove(folder, error))
        {
            Toast(Tf("Couldn't remove the copy: {}", error), ui::ToastKind::Error);
            return;
        }
        m_s->copyMessage.clear();
        StartGameDisc(); // the disc image again, if the player has one
        Toast(Tf("Copy removed: {} freed", ui::FormatSize(size)), ui::ToastKind::Success);
    }

    void App::DrawGameData()
    {
        ui::SectionHeader(Tr("Game data"));
        auto note = [](const std::string& text) { ui::Text(ui::TextStyle::Caption, text.c_str(), ui::color::Dim); };
        const gamedata::Status& copy = m_s->copy;
        const bool ready = m_s->disc == DiscState::Ready && !m_s->fromCopy;
        const bool canCopy = ready && !Installing();
        auto primary = [](bool disabled) {
            ui::ButtonOptions options;
            options.kind = disabled ? ui::ButtonKind::Secondary : ui::ButtonKind::Primary; // one Primary in view: the disc's, until then
            options.disabled = disabled;
            return options;
        };
        auto remove = [](const char* hint) {
            ui::ButtonOptions options;
            options.kind = ui::ButtonKind::Danger;
            options.hold = true;
            options.tooltip = hint;
            return options;
        };
        auto showFolder = [&](const std::filesystem::path& folder) {
            ui::ButtonOptions options;
            options.icon = ui::Icon::Folder;
            if (ui::Button(Tr("Show in folder"), options))
                if (!OpenFolder(folder))
                    Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
        };

        if (m_s->copyJob.Running())
        {
            const uint64_t total = m_s->copyJob.Total();
            note(total == 0 ? Tr("Copying...") : Tf("Copying: {} of {}", ui::FormatSize(m_s->copyJob.Bytes()), ui::FormatSize(total)));
            ui::ProgressBar(m_s->copyJob.Fraction(), -1, true);
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            if (ui::Button(Tr("Stop")))
                m_s->copyJob.Cancel();
            note(Tr("Stopping keeps the files already copied."));
            return;
        }

        switch (copy.state)
        {
        case gamedata::State::Complete:
            ui::StatusPill(ui::Status::Success, (copy.files == 1 ? Tf("Copied: {}, 1 file", ui::FormatSize(copy.total))
                                                                 : Tf("Copied: {}, {} files", ui::FormatSize(copy.total), copy.files)).c_str());
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            showFolder(gamedata::Folder(m_s->game.id));
            ImGui::SameLine(0, ui::space::S);
            if (ui::Button(Tr("Remove the copy"), remove(Tr("Hold to delete the copy."))))
                RemoveCopy();
            note(Tr("No disc image needed."));
            break;
        case gamedata::State::Partial:
            ui::StatusPill(ui::Status::Warning, Tf("Interrupted: {} of {} copied", ui::FormatSize(copy.present), ui::FormatSize(copy.total)).c_str());
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            if (ui::Button(Tr("Carry on copying"), primary(!canCopy)))
                StartCopy();
            ImGui::SameLine(0, ui::space::S);
            if (ui::Button(Tr("Remove the copy"), remove(Tr("Hold to delete the partial copy."))))
                RemoveCopy();
            if (!ready)
                note(Tr("Choose the disc image to carry on."));
            break;
        case gamedata::State::Damaged:
            ui::StatusPill(ui::Status::Error, Tr("Game data damaged"));
            ui::Text(ui::TextStyle::Body, copy.problem.c_str(), ui::color::InkSoft);
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            if (ui::Button(Tr("Repair the copy"), primary(!canCopy)))
                StartCopy();
            ImGui::SameLine(0, ui::space::S);
            if (ui::Button(Tr("Remove the copy"), remove(Tr("Hold to delete the copy."))))
                RemoveCopy();
            note(ready ? Tr("The game uses the disc image instead.") : Tr("Choose the disc image to repair."));
            break;
        case gamedata::State::None:
            if (ui::Button(Tr("Copy files"), primary(!canCopy)))
                StartCopy();
            ImGui::SameLine(0, ui::space::S);
            showFolder(gamedata::Folder(m_s->game.id).parent_path());
            note(!ready ? Tr("Choose the disc image first.")
                : m_s->discSize > 0 ? Tf("Copies the disc's files ({}). No image needed after.", ui::FormatSize(m_s->discSize))
                                    : Tr("Copies the disc's files. No image needed after."));
            break;
        }
        if (!m_s->copyMessage.empty())
        {
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            ui::StatusPill(ui::Status::Error, Tr("The copy failed"));
            ui::Text(ui::TextStyle::Body, m_s->copyMessage.c_str(), ui::color::InkSoft);
        }
    }

    // ---- The test script -------------------------------------------------------------------------------------------------

    // SKY_LAUNCHER_TEST_COPY, in an offscreen window, on tiny stand-in discs (the real default.xex alone): Giants is given SSA's
    // disc, offers it to SSA (the wrong-disc button), and the copy goes through its states (not enough space, interrupted, carried on,
    // played from with no disc image, damaged and falling back to the disc image, repaired, removed). The stand-in discs and the
    // fake kit are the C++ test's (tests/copy_test.cpp). Screenshots in SKY_LAUNCHER_SHOTS.
    void App::TestCopyScript(uint64_t now)
    {
        const GameInfo& ssa = *FindGame("ssa");
        const GameInfo& giants = *FindGame("giants");
        GameSlot& a = SlotFor(ssa);
        GameSlot& g = SlotFor(giants);
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(stderr, "FAIL copy step %d: %s (SSA: \"%s\")\n", m_testStep, what, SlotStatus(&a).text.c_str());
                m_testFailures++;
            }
        };
        auto env = [](const char* name, const char* value) {
            if (value != nullptr)
                SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value, true);
            else
                SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), name);
        };
        auto shoot = [&](const char* name, GameTab tab, uint64_t wait = 400) {
            m_page = Page::GameSettings;
            m_gameTab = tab;
            m_testShot = fmt::format("{:02}-{}", m_testStep, name);
            m_testAt = now + wait;
        };
        auto busy = [&](GameSlot& slot) { return slot.disc == DiscState::Verifying || slot.copyJob.Running() || slot.installer.Running(); };
        const std::string folder = datadir::Text(gamedata::Folder("ssa"));
        const std::string giantsIso = LauncherEnv("SKY_LAUNCHER_TEST_COPY_GIANTS_ISO") != nullptr ? LauncherEnv("SKY_LAUNCHER_TEST_COPY_GIANTS_ISO") : "";
        if (m_testBegan == 0)
            m_testBegan = now;
        if (now - m_testBegan > 120000)
        {
            expect(false, "timeout");
            m_running = false;
            return;
        }
        switch (m_testStep)
        {
        case 0: // Giants was given SSA's disc: the check says so and names SSA
            m_page = Page::Play;
            if (m_testBegan == now)
            {
                m_testPath = SettingGet(m_config, "giants", "iso");
                SelectGame(giants);
                return;
            }
            if (busy(g))
                return;
            expect(g.disc == DiscState::Failed && g.check.status == GameSourceStatus::WrongGame && g.check.otherGame == "ssa", "the check names the other game");
            expect(SlotStatus(&g).state == SlotState::DiscWrong, "Wrong disc");
            m_testStep++;
            break;
        case 1:
            shoot("wrong-disc", GameTab::Disc);
            m_page = Page::Play;
            break;
        case 2: // the button: SSA gets the disc, Giants forgets it
            UseDiscForOtherGame();
            expect(m_s == &a && m_page == Page::Play && a.disc == DiscState::Verifying, "the window went to SSA and checks the disc");
            expect(g.disc == DiscState::None && SettingGet(m_config, "giants", "iso").empty() && SlotStatus(&g).state == SlotState::NoDisc,
                "Giants has no disc any more");
            m_testStep++;
            break;
        case 3:
            if (busy(a))
                return;
            expect(a.disc == DiscState::Ready && !a.fromCopy && SettingGet(m_config, "ssa", "iso") == m_testPath, "SSA has the disc");
            expect(SlotStatus(&a).state == SlotState::ReadyToInstall, "ready to install");
            // Not enough space: said, nothing copied.
            env("SKY_LAUNCHER_COPY_FREE_BYTES", "1000");
            StartCopy();
            expect(a.copyJob.Running(), "the copy started");
            m_testStep++;
            break;
        case 4:
            if (a.copyJob.Running())
                return;
            env("SKY_LAUNCHER_COPY_FREE_BYTES", nullptr);
            expect(a.copyMessage.rfind("Not enough space", 0) == 0 && a.copy.state == gamedata::State::None && !a.fromCopy, "not enough space is said");
            expect(!std::filesystem::exists(std::filesystem::path(folder) / "default.xex"), "nothing was copied");
            m_testStep++;
            break;
        case 5: // interrupted: stopped before the first file
            a.copyMessage.clear();
            a.copyJob.Start(ssa, a.discPath, a.check.xexSha256, true);
            m_testStep++;
            break;
        case 6:
            if (a.copyJob.Running())
                return;
            expect(a.copy.state == gamedata::State::Partial && !a.fromCopy && SlotStatus(&a).state == SlotState::ReadyToInstall, "interrupted: partial, the disc image still plays");
            m_testStep++;
            break;
        case 7:
            shoot("interrupted", GameTab::Disc);
            break;
        case 8: // carried on
            StartCopy();
            m_testStep++;
            break;
        case 9:
            if (a.copyJob.Running() || a.disc == DiscState::Verifying)
                return;
            expect(a.copy.state == gamedata::State::Complete && a.fromCopy && a.discPath == folder && a.disc == DiscState::Ready, "copied: the game plays from it");
            expect(a.discSize == a.copy.total && a.check.xexSha256 == ssa.releases[0].defaultXexSha256, "verified from the copy");
            m_testStep++;
            break;
        case 10:
            shoot("copied", GameTab::Disc);
            break;
        case 11: // no disc image at all (a launcher start with the image gone): still ready, from the copy
            m_config["ssa"].erase("iso");
            a.started = false;
            SelectGame(ssa);
            expect(a.disc == DiscState::Verifying, "the copy is what is checked at start");
            m_testStep++;
            break;
        case 12:
            if (busy(a))
                return;
            expect(a.disc == DiscState::Ready && a.fromCopy && SettingGet(m_config, "ssa", "iso").empty(), "ready with no disc image");
            expect(SlotStatus(&a).state == SlotState::ReadyToInstall, "to install");
            StartInstall(); // the build reads the copy
            expect(Installing(), "installing from the copy");
            m_testStep++;
            break;
        case 13:
            if (AnyInstalling() || a.installView)
                return;
            expect(!a.installed.runtime.empty() && SlotStatus(&a).state == SlotState::Ready && std::string(SlotStatus(&a).text) == "Ready to play" &&
                std::string(SlotStatus(&a).button) == "PLAY", "Ready to play without the disc image");
            m_page = Page::Play;
            PlayNow();
            expect(!m_running && a.chosen == folder && a.chosenRuntime == a.installed.runtime, "PLAY runs the game on the copy");
            m_running = true;
            m_testStep++;
            break;
        case 14:
            shoot("play-from-copy", GameTab::Disc);
            m_page = Page::Play;
            break;
        case 15: // a file goes missing: PLAY notices, and with no disc image the state says so
            std::filesystem::remove(std::filesystem::path(folder) / "default.xex");
            PlayNow();
            expect(m_running && a.disc == DiscState::Failed && !a.fromCopy && a.copy.state == gamedata::State::Damaged, "damaged copy, no disc image");
            expect(SlotStatus(&a).state == SlotState::DataDamaged && std::string(SlotStatus(&a).text) == "Game data damaged", "Game data damaged");
            m_testStep++;
            break;
        case 16:
            shoot("damaged-no-disc", GameTab::Disc);
            m_page = Page::Play;
            break;
        case 17: // the disc image is chosen again: it is used, and the copy says what is wrong
            Set("ssa", "iso", m_testPath);
            StartGameDisc();
            expect(a.copy.state == gamedata::State::Damaged, "the copy is damaged");
            m_testStep++;
            break;
        case 18:
            if (busy(a))
                return;
            expect(a.disc == DiscState::Ready && !a.fromCopy && a.discPath == m_testPath && SlotStatus(&a).text == "Ready to play", "falls back to the disc image");
            m_testStep++;
            break;
        case 19:
            shoot("damaged-fallback", GameTab::Disc);
            break;
        case 20: // repaired
            StartCopy();
            m_testStep++;
            break;
        case 21:
            if (a.copyJob.Running() || a.disc == DiscState::Verifying)
                return;
            expect(a.copy.state == gamedata::State::Complete && a.fromCopy && a.disc == DiscState::Ready, "repaired");
            RemoveCopy();
            m_testStep++;
            break;
        case 22: // removed: the folder is gone, the disc image is read again
            if (busy(a))
                return;
            expect(a.copy.state == gamedata::State::None && !a.fromCopy && !std::filesystem::exists(folder), "removed");
            expect(a.disc == DiscState::Ready && a.discPath == m_testPath && SlotStatus(&a).text == "Ready to play", "back on the disc image");
            m_testStep++;
            break;
        case 23: // Giants: its own folder and copy
            if (giantsIso.empty())
            {
                m_testStep += 3;
                break;
            }
            SelectGame(giants);
            Set("giants", "iso", giantsIso);
            StartGameDisc();
            m_testStep++;
            break;
        case 24:
            if (busy(g))
                return;
            expect(g.disc == DiscState::Ready, "Giants' disc");
            StartCopy();
            m_testStep++;
            break;
        case 25:
        {
            if (busy(g))
                return;
            expect(g.copy.state == gamedata::State::Complete && g.fromCopy && g.discPath == datadir::Text(gamedata::Folder("giants")), "Giants plays from its own copy");
            expect(a.copy.state == gamedata::State::None, "SSA's copy is its own");
            std::string error;
            gamedata::Remove(gamedata::Folder("giants"), error);
            m_testStep++;
            break;
        }
        default:
            m_running = false;
            break;
        }
    }
}

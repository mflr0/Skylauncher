// Installing the game: the INSTALL flow's state (StartInstall, PollInstall, what a launcher restart finds) and the
// progress card that replaces the logo on Play (docs/install.md).
#include "launcher/install_checklist.h"
#include "launcher/env.h"
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/text_fit.h"
#include "shared/platform/platform.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <fstream>
#include <fmt/format.h>

namespace launcher_ui
{
    namespace
    {
        std::string Utf8(const std::filesystem::path& path)
        {
            std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }
    }

    bool OpenFolder(const std::filesystem::path& folder)
    {
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        std::string path = Utf8(folder);
#if defined(_WIN32)
        return SDL_OpenURL(path.c_str()); // ShellExecute opens a folder from its path
#else
        // A file URL: everything but unreserved characters and '/' percent-encoded ("Application Support").
        std::string url = "file://";
        for (unsigned char c : path)
        {
            if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
                url += char(c);
            else
                url += fmt::format("%{:02X}", c);
        }
        return SDL_OpenURL(url.c_str());
#endif
    }

    void App::RefreshInstall()
    {
        m_s->kit = FindInstallKit(m_config, m_s->game.id);
        m_s->installed = FindInstalledGame(m_config, m_s->check.xexSha256);
        m_s->ownRuntime = FindRuntime(m_s->game, m_config);

        // An install of an earlier session that never finished (the launcher was closed, or it failed): shown again, with
        // its steps read back from its log, so the player can resume it.
        if (m_s->installer.Running() || m_s->installView || m_s->check.xexSha256.empty())
            return;
        std::string section = "install." + m_s->check.xexSha256;
        std::string state = SettingGet(m_config, section, "state");
        if (state != "running" && state != "failed")
            return;
        m_s->installPast = ReadInstallLog(GameInstallFolder(m_s->check.xexSha256));
        if (m_s->installPast.state == InstallProgress::State::Idle || m_s->installPast.state == InstallProgress::State::Done)
            return;
        if (state == "running")
        {
            uint32_t pid = uint32_t(SettingInt(m_config, section, "pid", 0));
            m_s->installPastPid = PlatformProcessAlive(pid) ? pid : 0;
        }
        m_s->installSha = m_s->check.xexSha256;
        m_s->installView = true;
        m_s->installLog = false;
        m_toasts.Clear(); // the page comes back with the card's own buttons at the toasts' corner: nothing stale over them
        if (m_shots.empty())
            m_page = Page::Play;
    }

    bool App::NeedsInstall() const
    {
        return NeedsInstall(*m_s);
    }

    bool App::NeedsInstall(const GameSlot& slot) const
    {
        return slot.disc == DiscState::Ready && slot.installed.runtime.empty() && slot.ownRuntime.empty();
    }

    bool App::UpdateAvailable() const
    {
        return UpdateAvailable(*m_s);
    }

    bool App::UpdateAvailable(const GameSlot& slot) const
    {
        if (slot.disc != DiscState::Ready || slot.installed.runtime.empty() || slot.skipUpdate)
            return false;
        const GameUpdate* pending = PendingUpdate(slot);
        // A build from before versions (or by a kit that did not say) is older than any published version.
        return pending != nullptr && (slot.installed.gameVersion.empty() || IsNewer(pending->version, slot.installed.gameVersion));
    }

    bool App::AnyGameUpdate() const
    {
        for (const auto& slot : m_slots)
            if (UpdateAvailable(*slot))
                return true;
        return false;
    }

    const GameUpdate* App::PendingUpdate(const GameSlot& slot) const
    {
        for (const GameUpdate& update : m_gameUpdates)
            if (update.id == slot.game.id)
                return &update;
        return nullptr;
    }

    std::string App::WantedKitTag(const GameSlot& slot) const
    {
        // The launcher's own release; a game whose published version needs a newer release's kit asks for that one.
        std::string tag = "v" + LauncherVersion();
        const GameUpdate* pending = PendingUpdate(slot);
        return pending != nullptr && IsNewer(pending->kit, tag) ? pending->kit : tag;
    }

    std::string App::GameVersion(const GameSlot& slot) const
    {
        if (!slot.installed.runtime.empty())
            return slot.installed.gameVersion;
        return slot.kit ? slot.kit->GameVersion(slot.game.id) : std::string();
    }

    void App::SetInstallState(const std::string& sha, const char* state)
    {
        auto& section = m_config["install." + sha];
        if (state == nullptr)
        {
            section.erase("state");
            section.erase("pid");
            section.erase("started");
        }
        else
            section["state"] = state;
        if (!SaveLauncherConfig(m_configPath, m_config))
            Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
    }

    bool App::KitNeedsFetch(const GameSlot& slot) const
    {
        if (!slot.kit)
            return true;
        if (slot.kit->chosen)
            return false;
        // Once the launcher's own release was fetched (or the nearest one there is), an older tag is the best there is.
        std::string tag = WantedKitTag(slot);
        bool fetched = SettingGet(m_config, "update", "kit_fetched") == tag;
        // A kit at hand (even one fetched under the same tag) that builds an older version than the update offers would
        // rebuild the same old game and offer the update again, forever: fetch the release's kit again instead.
        if (const GameUpdate* pending = PendingUpdate(slot); pending != nullptr && !slot.kit->GameVersion(slot.game.id).empty())
        {
            SemVer kitBuilds = ParseSemVer(slot.kit->GameVersion(slot.game.id)), offered = ParseSemVer(pending->version);
            // Once per release and version: if that release's own kit still builds the older one, do not download it
            // again and again (the update then says what it can build).
            const std::string once = tag + " " + slot.game.id + " " + pending->version;
            if (kitBuilds.valid && offered.valid && CompareSemVer(kitBuilds, offered) < 0 && SettingGet(m_config, "update", "kit_refetched") != once)
                return true;
        }
        return !slot.kit->Builds(slot.game.id) || (KitOlderThanLauncher(*slot.kit, tag) && !fetched);
    }

    void App::StartInstall()
    {
        if (!DiscStillThere())
            return;
        if (m_session.Running() == m_s->game.id) // its build can't be replaced while it runs
        {
            Toast(Tf("{} is running", GameName(m_s->game.id, GameNameForm::Short)), ui::ToastKind::Error);
            return;
        }
        RefreshInstall();
        if (m_s->kit && m_s->kit->chosen && !m_s->kit->Builds(m_s->game.id))
        {
            Toast(Tf("These game tools can't build {}", m_s->game.name), ui::ToastKind::Error); // the player's own kit: not ours to replace
            return;
        }
        m_s->installSha = m_s->check.xexSha256;
        m_s->installKitVersion = m_s->kit ? m_s->kit->version : std::string();
        m_s->installView = true;
        m_s->installLog = false;
        m_s->installPast = {};
        m_s->installPastPid = 0;
        m_s->kitUsed = false;
        m_s->kitError.clear();
        m_page = Page::Play;
        if (KitNeedsFetch(*m_s))
        {
            if (const GameUpdate* pending = PendingUpdate(*m_s))
                m_config["update"]["kit_refetched"] = WantedKitTag(*m_s) + " " + m_s->game.id + " " + pending->version;
            StartKitDownload();
        }
        else
            StartInstaller();
    }

    // ---- Update all -----------------------------------------------------------------------------------------------------
    // The games that have an update are started one after the other, each as UPDATE on its page would (StartInstall, with
    // its own slot in place, the page the player is on left alone). The next one starts when the last has finished, built
    // or not; a failure shows on that game's page as usual and the run goes on. Cancel stops the running one and the rest.

    int App::GamesToUpdate() const
    {
        int count = 0;
        for (const auto& slot : m_slots)
            count += UpdateAvailable(*slot) ? 1 : 0;
        return count;
    }

    void App::StartUpdateAll()
    {
        if (UpdatingAll())
            return;
        m_updateAll = {};
        for (const auto& slot : m_slots)
            if (UpdateAvailable(*slot) && !slot->installer.Running() && !slot->kitDownload.Running())
                m_updateAll.queue.push_back(slot->game.id);
        m_updateAll.total = int(m_updateAll.queue.size());
        PollUpdateAll();
    }

    void App::CancelUpdateAll()
    {
        m_updateAll.queue.clear();
        for (const auto& slot : m_slots)
            if (std::string_view(slot->game.id) == m_updateAll.current)
            {
                slot->installer.Cancel();
                slot->kitDownload.Cancel();
            }
        // PollInstall sees the cancel and says so; the run ends when the game has stopped.
    }

    void App::PollUpdateAll()
    {
        if (!UpdatingAll())
            return;
        if (!m_updateAll.current.empty())
        {
            GameSlot* slot = nullptr;
            for (const auto& opened : m_slots)
                if (std::string_view(opened->game.id) == m_updateAll.current)
                    slot = opened.get();
            if (slot != nullptr && (slot->installer.Running() || slot->kitDownload.Running()))
                return;
            if (slot != nullptr && (UpdateAvailable(*slot) || !slot->kitError.empty()))
                m_updateAll.failed++; // stopped before it was built (failed or cancelled)
            m_updateAll.current.clear();
        }
        while (!m_updateAll.queue.empty())
        {
            const std::string id = m_updateAll.queue.front();
            m_updateAll.queue.erase(m_updateAll.queue.begin());
            GameSlot* slot = nullptr;
            for (const auto& opened : m_slots)
                if (std::string_view(opened->game.id) == id)
                    slot = opened.get();
            if (slot == nullptr || !UpdateAvailable(*slot) || m_session.Running() == id) // a running game keeps its build
                continue;
            GameSlot* shown = m_s;
            const Page page = m_page;
            m_s = slot;
            SetActiveGame(id);
            StartInstall();
            const bool started = m_s->installer.Running() || m_s->kitDownload.Running();
            m_s = shown;
            SetActiveGame(m_s->game.id);
            m_page = page;
            if (started)
            {
                m_updateAll.current = id;
                m_updateAll.started++;
                return;
            }
            m_updateAll.failed++;
        }
        if (m_updateAll.total > 1)
            Toast(m_updateAll.failed == 0 ? Tr("All games are updated") : Tf("Games updated: {} of {}", m_updateAll.total - m_updateAll.failed, m_updateAll.total),
                m_updateAll.failed != 0 ? ui::ToastKind::Error : ui::ToastKind::Success);
    }

    // No kit, or one that is older than the release wanted or can't build this game: the first step of the install fetches
    // the right one (docs/install.md).
    void App::StartKitDownload()
    {
        m_s->kitUsed = true;
        m_s->kitTag = WantedKitTag(*m_s);
        m_s->kitStep = { "kit", "Downloading the game tools", SDL_GetTicks(), 0 };
        m_s->kitDownload.Start(SettingGet(m_config, "update", "kit_repo", "mflr0/Skylauncher"), m_s->kitTag, DownloadedKitsFolder());
    }

    void App::StartInstaller()
    {
        if (m_s->installer.Start(m_s->kit, m_s->discPath, GameInstallFolder(m_s->installSha)))
        {
            auto& section = m_config["install." + m_s->installSha];
            section["pid"] = std::to_string(m_s->installer.ProcessId());
            section["started"] = std::to_string(int64_t(std::time(nullptr)));
            SetInstallState(m_s->installSha, "running");
        }
    }

    InstallProgress App::InstallSnapshot(size_t logTail)
    {
        return InstallSnapshot(*m_s, logTail);
    }

    InstallProgress App::InstallSnapshot(GameSlot& slot, size_t logTail)
    {
        if (slot.kitUsed)
        {
            KitDownloader::Snapshot_ kit = slot.kitDownload.Snapshot();
            using Kit = KitDownloader::State;
            InstallProgress progress;
            progress.steps.push_back(slot.kitStep);
            if (!slot.kitError.empty() || kit.state != Kit::Done)
            {
                progress.state = !slot.kitError.empty() || kit.state == Kit::Failed ? InstallProgress::State::Failed
                    : kit.state == Kit::Cancelled                            ? InstallProgress::State::Cancelled
                                                                             : InstallProgress::State::Running;
                progress.error = !slot.kitError.empty() ? slot.kitError : kit.error;
                progress.done = kit.done >> 20; // MB
                progress.total = kit.total >> 20;
                return progress;
            }
            // Downloaded: the kit's step, done, then SSABuild's.
            progress = slot.installer.Snapshot(logTail);
            progress.steps.insert(progress.steps.begin(), slot.kitStep);
            return progress;
        }
        if (slot.installPast.state == InstallProgress::State::Idle)
            return slot.installer.Snapshot(logTail);
        InstallProgress progress = slot.installPast;
        size_t first = progress.log.size() - std::min(logTail, progress.log.size());
        progress.log.erase(progress.log.begin(), progress.log.begin() + ptrdiff_t(first));
        return progress;
    }

    void App::PollInstall()
    {
        // An SSABuild left running by an earlier launcher: wait for it to stop before offering to resume.
        if (m_s->installPastPid != 0 && SDL_GetTicks() >= m_s->installPastCheck)
        {
            m_s->installPastCheck = SDL_GetTicks() + 1000;
            if (!PlatformProcessAlive(m_s->installPastPid))
                m_s->installPastPid = 0;
        }
        if (m_s->kitUsed && m_s->kitError.empty() && m_s->installView && !m_s->installer.Running() && m_s->kitStep.endedAt == 0)
        {
            KitDownloader::Snapshot_ kit = m_s->kitDownload.Snapshot();
            if (kit.state == KitDownloader::State::Done)
            {
                m_s->kitStep.endedAt = SDL_GetTicks();
                Set("update", "kit_tag", kit.tag);
                Set("update", "kit_fetched", m_s->kitTag);
                RefreshInstall();
                if (m_s->kit && m_s->kit->Builds(m_s->game.id))
                {
                    m_s->installKitVersion = m_s->kit->version;
                    StartInstaller();
                }
                else
                    m_s->kitError = m_s->kit ? Tf("These game tools can't build {}", m_s->game.name) : Tr("The game tools are incomplete.");
            }
            else if (kit.state == KitDownloader::State::Cancelled)
            {
                m_s->installView = m_s->kitUsed = false;
                Toast(Tr("Installation canceled"), ui::ToastKind::Info);
            }
            return;
        }
        if (!m_s->installView || m_s->installer.Running() || m_s->installPast.state != InstallProgress::State::Idle)
            return;
        InstallProgress progress = m_s->installer.Snapshot();
        if (progress.state == InstallProgress::State::Done)
        {
            // The version built: what the kit says, else (a kit that does not) what the release published for the game.
            std::string version = m_s->kit ? m_s->kit->GameVersion(m_s->game.id) : std::string();
            if (const GameUpdate* pending = PendingUpdate(*m_s); version.empty() && pending != nullptr)
                version = pending->version;
            RecordInstalledGame(m_config, m_s->installSha, progress.runtime, m_s->installKitVersion, version);
            SetInstallState(m_s->installSha, nullptr);
            Toast(Tr("Game installed: press PLAY"), ui::ToastKind::Success);
            m_s->installView = false;
            RefreshInstall();
        }
        else if (progress.state == InstallProgress::State::Cancelled)
        {
            SetInstallState(m_s->installSha, nullptr);
            m_s->installView = false;
            Toast(Tr("Installation canceled"), ui::ToastKind::Info);
        }
        else if (progress.state == InstallProgress::State::Failed && SettingGet(m_config, "install." + m_s->installSha, "state") != "failed")
            SetInstallState(m_s->installSha, "failed");
    }

    // The progress card: a checklist of SSABuild's steps (waiting, running with a spinner, done with a tick and its time,
    // failed), the overall bar and the time left, the current action on one line, the log behind "Show details".
    void App::DrawInstall(ImVec2 at, ImVec2 size)
    {
        InstallProgress progress = InstallSnapshot(m_s->installLog ? 600 : 0);
        uint64_t now = SDL_GetTicks();
        InstallChecklist list = MakeInstallChecklist(progress, now);
        using State = InstallProgress::State;
        bool running = progress.state == State::Running;
        bool failed = progress.state == State::Failed;
        bool interrupted = progress.state == State::Interrupted;

        ImGui::SetCursorScreenPos(at);
        BeginCard("##install", size);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float width = ImGui::GetContentRegionAvail().x;
        const float left = ImGui::GetCursorScreenPos().x;
        using RowState = InstallChecklist::RowState;

        // Title, and once stopped a close button on its right. The percentage is the bar's, said once.
        const char* title = failed ? Tr("INSTALLATION STOPPED") : interrupted ? Tr("INSTALLATION INTERRUPTED") : Tr(m_s->installed.runtime.empty() ? N_("INSTALLING THE GAME") : N_("UPDATING THE GAME")); // a built game being rebuilt is an update
        const ImVec2 titleAt = ImGui::GetCursorScreenPos();
        ui::Text(ui::TextStyle::Title, title, failed ? ui::color::Danger : ui::color::Ink, -1);
        const float titleBottom = ImGui::GetCursorPosY();
        if (failed || interrupted)
        {
            const float closeSize = ui::HeightOf(ui::Size::Normal);
            ImGui::SetCursorScreenPos(ImVec2(left + width - closeSize, titleAt.y - (closeSize - ui::SizeOf(ui::TextStyle::Title)) / 2));
            if (ui::IconButton("##closeinstall", ui::Icon::Cross, Tr("Close")))
            {
                m_s->installView = m_s->kitUsed = false;
                SetInstallState(m_s->installSha, nullptr);
                m_s->installPast = {};
            }
            ImGui::SetCursorPosY(titleBottom);
        }

        // The overall bar with its percentage, then the step count and the times under it.
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        ui::ProgressBar(list.overall, -1, true);
        {
            int stepNumber = list.current >= 0 ? list.current + 1 : int(list.rows.size());
            std::string where = Tf("Step {} of {}", stepNumber, list.rows.size());
            std::string times;
            if (list.elapsedMs > 0)
                times = Tf("{} elapsed", InstallDuration(list.elapsedMs));
            if (running)
            {
                std::string timeLeft = InstallTimeLeft(list.etaMs);
                times += (times.empty() || timeLeft.empty() ? "" : "  \xC2\xB7  ") + timeLeft;
            }
            const ImVec2 rowAt = ImGui::GetCursorScreenPos();
            ui::Text(ui::TextStyle::Caption, where.c_str(), ui::color::InkSoft, -1);
            ui::PushText(ui::TextStyle::Caption);
            const float w = ImGui::CalcTextSize(times.c_str()).x;
            ui::DrawText(draw, ImVec2(left + width - w, rowAt.y), ui::Col(ui::color::InkSoft), times.c_str());
            ui::PopText();
        }
        ImGui::Dummy(ImVec2(0, ui::space::XS));

        // The checklist. With the details open, only the current step: the log takes the room.
        // Once stopped, the steps done fold into one line, to leave room for the message.
        const float iconSize = ui::icon::Small;
        const float rowH = ui::SizeOf(ui::TextStyle::Body) + ui::space::XS;
        bool fold = (failed || interrupted) && list.current > 0;
        auto rowIcon = [&](RowState state, ImVec2 centre) {
            switch (state)
            {
            case RowState::Done: ui::DrawStatusIcon(draw, ui::Status::Success, centre, iconSize); break;
            case RowState::Running: ui::DrawStatusIcon(draw, ui::Status::Busy, centre, iconSize); break;
            case RowState::Failed: ui::DrawStatusIcon(draw, ui::Status::Error, centre, iconSize); break;
            case RowState::Stopped: ui::DrawIcon(draw, ui::Icon::Minus, centre, iconSize, ui::Col(ui::color::Dim)); break;
            case RowState::Waiting: ui::DrawIcon(draw, ui::Icon::Unknown, centre, iconSize, ui::Col(ui::color::Edge)); break;
            }
        };
        const float textX = iconSize + ui::space::S;
        if (fold && !m_s->installLog)
        {
            const ImVec2 rowAt = ImGui::GetCursorScreenPos();
            rowIcon(RowState::Done, ImVec2(rowAt.x + iconSize / 2, rowAt.y + rowH / 2));
            ImGui::SetCursorScreenPos(ImVec2(rowAt.x + textX, rowAt.y));
            ui::Text(ui::TextStyle::Body, Tf("{} of {} steps done", list.current, list.rows.size()).c_str(), ui::color::InkSoft, -1);
            ImGui::SetCursorScreenPos(ImVec2(rowAt.x, rowAt.y + rowH));
        }
        for (size_t i = 0; i < list.rows.size(); i++)
        {
            const InstallChecklist::Row& row = list.rows[i];
            bool current = int(i) == list.current;
            if ((m_s->installLog || fold) && !current)
                continue;
            const ImVec2 rowAt = ImGui::GetCursorScreenPos();
            rowIcon(row.state, ImVec2(rowAt.x + iconSize / 2, rowAt.y + rowH / 2 - 1));
            ImGui::SetCursorScreenPos(ImVec2(rowAt.x + textX, rowAt.y));
            unsigned ink = row.state == RowState::Waiting ? ui::color::Dim : row.state == RowState::Failed ? ui::color::Danger : current ? ui::color::Ink : ui::color::InkSoft;
            ui::Text(ui::TextStyle::Body, row.title.c_str(), ink, -1, current);
            const float titleW = ImGui::GetItemRectSize().x;

            // Its time on the right: the running time, or how long a done step took.
            std::string time;
            if (current && running)
                time = InstallDuration(row.ms);
            else if (row.state == RowState::Done && row.timed && row.ms >= 1000)
                time = InstallDuration(row.ms);
            float timeW = 0;
            if (!time.empty())
            {
                ui::PushText(ui::TextStyle::Caption);
                timeW = ImGui::CalcTextSize(time.c_str()).x;
                ui::DrawText(draw, ImVec2(left + width - timeW, rowAt.y + (rowH - ImGui::GetFontSize()) / 2 - 1), ui::Col(current ? ui::color::Ink : ui::color::Dim), time.c_str());
                ui::PopText();
            }
            // The running step's own bar, between its title and its time, with its count.
            if (current && running && row.fraction >= 0)
            {
                const std::string count = fmt::format("{} / {}", row.done, row.total);
                const float countW = ui::TextSize(ui::TextStyle::Caption, count.c_str()).x;
                const float barLeft = rowAt.x + textX + std::max(titleW + ui::space::M, width * 0.42f);
                const float barW = left + width - timeW - ui::space::M - countW - ui::space::S - barLeft;
                if (barW > 2 * ui::space::XXL)
                {
                    ImGui::SetCursorScreenPos(ImVec2(barLeft, rowAt.y + (rowH - ui::space::XS) / 2 - 1));
                    ui::ProgressBar(row.fraction, barW);
                    ui::PushText(ui::TextStyle::Caption);
                    ui::DrawText(draw, ImVec2(barLeft + barW + ui::space::S, rowAt.y + (rowH - ImGui::GetFontSize()) / 2 - 1), ui::Col(ui::color::InkSoft), count.c_str());
                    ui::PopText();
                }
            }
            ImGui::SetCursorScreenPos(ImVec2(rowAt.x, rowAt.y + rowH));
        }

        // Under the steps: what SSABuild does now (one line that replaces itself), or why it stopped.
        ImGui::Dummy(ImVec2(0, ui::space::XXS));
        if (running)
        {
            ui::PushText(ui::TextStyle::Caption);
            const std::string action = ui::Ellipsize(list.action.c_str(), width);
            ui::PopText();
            ui::Text(ui::TextStyle::Caption, action.c_str(), ui::color::InkSoft, -1);
        }
        else if (failed)
        {
            ui::Text(ui::TextStyle::Label, InstallFailureHeadline(list, progress).c_str(), ui::color::Ink, 0, true);
            if (!progress.error.empty())
                ui::Text(ui::TextStyle::Caption, progress.error.c_str(), ui::color::InkSoft);
            ui::Text(ui::TextStyle::Caption, Tr("Retry carries on from this step."), ui::color::InkSoft);
        }
        else if (interrupted)
            ui::Text(ui::TextStyle::Body, m_s->installPastPid != 0 ? Tr("The earlier install is still stopping. Resume in a moment.")
                                                                  : Tr("Arkeyan Launcher closed during the install. Resume carries on."), ui::color::InkSoft);

        // The log, in the space left above the buttons: monospace, scrollable, following its end.
        const float buttonsH = ui::HeightOf(ui::Size::Normal) + ui::space::M;
        if (m_s->installLog)
        {
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            float logH = std::max(60.0f, ImGui::GetContentRegionAvail().y - buttonsH);
            ImFont* mono = ui::Ctx().fonts.mono;
            const float monoSize = ui::SizeOf(ui::TextStyle::Caption);
            {
                // A whole number of lines (and its padding), so the line at the top is never half cut while it follows the end.
                ImGui::PushFont(mono, monoSize);
                const float step = ImGui::GetTextLineHeight() + ui::space::XXS / 2;
                ImGui::PopFont();
                logH = std::max(60.0f, std::floor((logH - 2 * ui::space::S) / step) * step + 2 * ui::space::S);
            }
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Rgb(ui::color::Inset));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ui::radius::Control);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::space::S, ui::space::S));
            ImGui::BeginChild("##installlog", ImVec2(0, logH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PushTextWrapPos(0.0f); // the kit's lines are long and not ours: they wrap inside the box, nothing scrolls sideways
            bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4;
            ImGui::PushFont(mono, monoSize);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ui::space::XXS / 2));
            if (progress.logLines > progress.log.size())
                ImGui::TextColored(ImColor(Rgb(ui::color::Dim)), "%s", Tf("({} earlier lines in Copy log)", progress.logLines - progress.log.size()).c_str());
            for (const std::string& line : progress.log)
            {
                bool error = line.starts_with("error") || line.find(": error") != std::string::npos;
                bool heading = line.starts_with("== ");
                ImGui::TextColored(ImColor(Rgb(error ? ui::color::Danger : heading ? ui::color::Ink : ui::color::InkSoft)), "%s", line.c_str());
            }
            ImGui::PopTextWrapPos();
            ImGui::PopStyleVar();
            ImGui::PopFont();
            if (atBottom)
                ImGui::SetScrollY(ImGui::GetScrollMaxY());
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
        }

        // Buttons along the card's bottom edge: the action to take first (Retry, Resume), then the quieter ones; Cancel while it runs.
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ui::space::L - ui::HeightOf(ui::Size::Normal)));
        if (failed || interrupted)
        {
            ImGui::BeginDisabled(m_s->installPastPid != 0);
            if (ui::Button(failed ? Tr("Retry") : Tr("Resume"), { .kind = ui::ButtonKind::Primary }))
                StartInstall();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::Button(Tr("Copy log")))
            {
                std::string text;
                if (m_s->installPast.state == State::Idle)
                    text = m_s->installer.FullLog();
                if (m_s->kitUsed && !progress.error.empty())
                    text += progress.error + "\n";
                for (const std::string& line : m_s->installPast.log)
                    text += line + "\n";
                SDL_SetClipboardText(text.c_str());
                Toast(Tr("Log copied"), ui::ToastKind::Success);
            }
            ImGui::SameLine();
            if (ui::Button(Tr("Open logs folder"), { .icon = ui::Icon::Folder }))
                OpenFolder(GameInstallFolder(m_s->installSha));
            ImGui::SameLine();
        }
        if (ui::Button(m_s->installLog ? Tr("Hide details") : Tr("Show details"), { .kind = failed || interrupted ? ui::ButtonKind::Ghost : ui::ButtonKind::Secondary }))
            m_s->installLog = !m_s->installLog;
        if (running)
        {
            const ui::ButtonOptions cancel;
            ImGui::SameLine(ImGui::GetWindowWidth() - ui::space::L - ui::ButtonWidth(Tr("Cancel"), cancel));
            if (ui::Button(Tr("Cancel"), cancel))
            {
                m_s->installer.Cancel();
                m_s->kitDownload.Cancel();
            }
        }
        EndCard();
    }

    // The install screens (SKY_LAUNCHER_TEST_INSTALL, with SKY_LAUNCHER_KIT set to <build>/fake-kit): the Install button, the
    // progress card early and during the compile, with its log, a failure, a retry that succeeds, Game data, and an
    // install read back after a restart.
    void App::TestInstallScript(uint64_t now)
    {
        InstallProgress progress = m_s->installer.Snapshot();
        if (m_testReal)
        {
            // The real kit: INSTALL, then a screenshot every few minutes until it ends (and PLAY with SKY_LAUNCHER_TEST_PLAY).
            const char* every = LauncherEnv("SKY_LAUNCHER_TEST_EVERY");
            uint64_t interval = (every != nullptr ? uint64_t(std::atoi(every)) : 120) * 1000;
            if (m_testStep == 0)
            {
                m_page = Page::Play;
                if (m_s->disc == DiscState::None && !m_testDrop.empty())
                    StartVerify(m_testDrop);
                if (m_s->disc == DiscState::Ready)
                    StartInstall();
                if (m_s->installView)
                    m_testAt = now + 3000;
            }
            else if (m_s->installer.Running() || m_s->installView)
                m_testAt = now + (m_s->installer.Running() ? interval : 2000);
            else if (m_testStep < 1000)
            {
                m_testStep = 1000; // after the last shot
                m_testAt = now + 1500;
            }
            else
            {
                // SKY_LAUNCHER_TEST_PLAY: then PLAY, as the button does, once the game is built.
                if (LauncherEnv("SKY_LAUNCHER_TEST_PLAY") != nullptr && m_s->disc == DiscState::Ready && !NeedsInstall())
                {
                    m_s->chosen = m_s->discPath;
                    m_s->chosenRuntime = m_s->installed.runtime;
                    UnsetGameVariable("SKY_LAUNCHER_SHOTS"); // else the game runs its own shots
                }
                m_running = false;
            }
            return;
        }
        switch (m_testStep)
        {
        case 0:
            m_page = Page::Play;
            if (m_s->disc == DiscState::None && !m_testDrop.empty())
                StartVerify(m_testDrop);
            if (m_s->disc == DiscState::Ready || m_s->disc == DiscState::Failed || m_testDrop.empty())
                m_testAt = now + 400;
            break;
        case 1:
            if (!m_s->installView)
            {
                SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_SSABUILD_FAIL", "movies", true);
                SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_SSABUILD_DELAY", "0.08", true);
                StartInstall();
                break;
            }
            if (progress.steps.size() >= 3 || progress.state != InstallProgress::State::Running)
                m_testAt = now + 300;
            break;
        case 2:
            // Closing the window now asks first.
            if ((progress.steps.size() >= 5 && progress.done >= 30) || progress.state != InstallProgress::State::Running)
            {
                m_quitPrompt = AnyInstalling();
                m_testAt = now + 100;
            }
            break;
        case 3:
            m_quitPrompt = false;
            m_s->installLog = true;
            m_testAt = now + 300;
            break;
        case 4:
            m_s->installLog = false;
            if (progress.state != InstallProgress::State::Running)
                m_testAt = now + 300;
            break;
        case 5:
            if (progress.state == InstallProgress::State::Failed)
            {
                SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_SSABUILD_FAIL");
                SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_SSABUILD_DELAY", "0.01", true);
                StartInstall();
            }
            else if (!m_s->installView)
                m_testAt = now + 400;
            break;
        case 6:
            m_page = Page::GameSettings;
            m_gameTab = GameTab::Game;
            m_testAt = now + 400;
            break;
        case 7:
            if (!m_s->installView)
            {
                // As if the launcher had been closed during the compile.
                std::ofstream(GameInstallFolder(m_s->check.xexSha256) / "install.log", std::ios::trunc)
                    << "@step toolchain Preparing the compiler\n@step disc Reading the disc\n@step recompile Translating the game code\n"
                       "@step shaders Translating the shaders\n@step compile Compiling the game\n@progress 812 2304\n";
                m_config["install." + m_s->check.xexSha256]["pid"] = "0";
                SetInstallState(m_s->check.xexSha256, "running");
                RefreshInstall();
                m_page = Page::Play;
                m_testAt = now + 400;
            }
            break;
        default:
            m_running = false;
            break;
        }
    }
}

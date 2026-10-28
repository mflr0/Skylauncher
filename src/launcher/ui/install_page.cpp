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

        // `text` cut to `width` with "..." at its end.
        std::string Fit(const std::string& text, float width)
        {
            return launcher_ui::FitText(text, width, [](const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x; });
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
        m_toasts.clear(); // the page comes back with the card's own buttons at the toasts' corner: nothing stale over them
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
            Toast(Tr("Couldn't save the settings"), true);
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
        return !slot.kit->Builds(slot.game.id) || (KitOlderThanLauncher(*slot.kit, tag) && !fetched);
    }

    void App::StartInstall()
    {
        if (!DiscStillThere())
            return;
        RefreshInstall();
        if (m_s->kit && m_s->kit->chosen && !m_s->kit->Builds(m_s->game.id))
        {
            Toast(Tf("These game tools can't build {}", m_s->game.name), true); // the player's own kit: not ours to replace
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
            StartKitDownload();
        else
            StartInstaller();
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
                Toast(Tr("Installation cancelled"));
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
            Toast(Tr("Game installed: press PLAY"));
            m_s->installView = false;
            RefreshInstall();
        }
        else if (progress.state == InstallProgress::State::Cancelled)
        {
            SetInstallState(m_s->installSha, nullptr);
            m_s->installView = false;
            Toast(Tr("Installation cancelled"));
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
        float width = ImGui::GetContentRegionAvail().x;
        float left = ImGui::GetCursorScreenPos().x;

        // Title, and on its right the percentage, or once stopped a close button.
        const char* title = failed ? Tr("INSTALLATION STOPPED") : interrupted ? Tr("INSTALLATION INTERRUPTED") : Tr(m_s->installed.runtime.empty() ? N_("INSTALLING THE GAME") : N_("UPDATING THE GAME")); // a built game being rebuilt is an update
        ImVec2 titleAt = ImGui::GetCursorScreenPos();
        Title(title, TitleSize, failed ? Danger : Text);
        if (failed || interrupted)
        {
            constexpr float CloseSize = 30;
            ImVec2 closeAt(left + width - CloseSize, titleAt.y - 4);
            ImGui::SetCursorScreenPos(closeAt);
            if (ImGui::InvisibleButton("##closeinstall", ImVec2(CloseSize, CloseSize), ImGuiButtonFlags_EnableNav))
            {
                m_s->installView = m_s->kitUsed = false;
                SetInstallState(m_s->installSha, nullptr);
                m_s->installPast = {};
            }
            bool hovered = ImGui::IsItemHovered();
            FocusRing(CloseSize / 2);
            if (hovered)
                ImGui::SetTooltip("%s", Tr("Close"));
            ImVec2 c(closeAt.x + CloseSize / 2, closeAt.y + CloseSize / 2);
            if (hovered)
                draw->AddCircleFilled(c, CloseSize / 2, Rgb(PanelRaisedHover), 24);
            draw->AddLine(ImVec2(c.x - 6, c.y - 6), ImVec2(c.x + 6, c.y + 6), Rgb(Text), 2.0f);
            draw->AddLine(ImVec2(c.x - 6, c.y + 6), ImVec2(c.x + 6, c.y - 6), Rgb(Text), 2.0f);
            ImGui::SetCursorScreenPos(ImVec2(left, titleAt.y + ImGui::GetFrameHeight() - 2));
        }
        else
        {
            std::string percent = fmt::format("{}%", int(std::floor(list.overall * 100)));
            ImGui::PushFont(m_titleFont, TitleSize);
            float w = ImGui::CalcTextSize(percent.c_str()).x;
            draw->AddText(ImVec2(left + width - w, titleAt.y), Rgb(Accent), percent.c_str());
            ImGui::PopFont();
        }

        // The overall bar, then the step count and the times under it.
        ImGui::Dummy(ImVec2(0, 2));
        ImVec2 barAt = ImGui::GetCursorScreenPos();
        constexpr float BarH = 10;
        draw->AddRectFilled(barAt, ImVec2(barAt.x + width, barAt.y + BarH), Rgb(Field), BarH / 2);
        if (list.overall > 0)
            draw->AddRectFilled(barAt, ImVec2(barAt.x + std::max(BarH, width * list.overall), barAt.y + BarH),
                Rgb(failed ? Danger : interrupted ? TextDim : Accent), BarH / 2);
        ImGui::Dummy(ImVec2(width, BarH));
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
            ImGui::PushFont(nullptr, SmallSize);
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", where.c_str());
            float w = ImGui::CalcTextSize(times.c_str()).x;
            ImGui::SameLine(Pad + width - w);
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", times.c_str());
            ImGui::PopFont();
        }
        ImGui::Dummy(ImVec2(0, 4));

        // The checklist. With the details open, only the current step: the log takes the room.
        // Once stopped, the steps done fold into one line, to leave room for the message.
        float lineH = ImGui::GetTextLineHeight();
        float rowH = lineH + 10;
        bool fold = (failed || interrupted) && list.current > 0;
        if (fold && !m_s->installLog)
        {
            ImVec2 rowAt = ImGui::GetCursorScreenPos();
            ImVec2 dot(rowAt.x + 10, rowAt.y + lineH / 2 + 1);
            draw->AddCircleFilled(dot, 9, Rgb(Ok), 20);
            draw->AddPolyline(std::array{ ImVec2(dot.x - 4.5f, dot.y), ImVec2(dot.x - 1.2f, dot.y + 3.5f), ImVec2(dot.x + 4.5f, dot.y - 3.5f) }.data(),
                3, Rgb(Panel), 0, 2.2f);
            ImGui::SetCursorScreenPos(ImVec2(rowAt.x + 30, rowAt.y));
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tf("{} of {} steps done", list.current, list.rows.size()).c_str());
            ImGui::SetCursorScreenPos(ImVec2(rowAt.x, rowAt.y + rowH));
        }
        for (size_t i = 0; i < list.rows.size(); i++)
        {
            const InstallChecklist::Row& row = list.rows[i];
            bool current = int(i) == list.current;
            if ((m_s->installLog || fold) && !current)
                continue;
            ImVec2 rowAt = ImGui::GetCursorScreenPos();
            ImVec2 dot(rowAt.x + 10, rowAt.y + lineH / 2 + 1);
            using RowState = InstallChecklist::RowState;
            switch (row.state)
            {
            case RowState::Done:
                draw->AddCircleFilled(dot, 9, Rgb(Ok), 20);
                draw->AddPolyline(std::array{ ImVec2(dot.x - 4.5f, dot.y), ImVec2(dot.x - 1.2f, dot.y + 3.5f), ImVec2(dot.x + 4.5f, dot.y - 3.5f) }.data(),
                    3, Rgb(Panel), 0, 2.2f);
                break;
            case RowState::Running:
            {
                draw->AddCircle(dot, 8, Rgb(Border), 24, 2.5f);
                float angle = float(now % 900) / 900.0f * 6.2832f;
                draw->PathArcTo(dot, 8, angle, angle + 2.2f, 14);
                draw->PathStroke(Rgb(Accent), 0, 2.8f);
                break;
            }
            case RowState::Failed:
                draw->AddCircleFilled(dot, 9, Rgb(Danger), 20);
                draw->AddLine(ImVec2(dot.x - 3.5f, dot.y - 3.5f), ImVec2(dot.x + 3.5f, dot.y + 3.5f), Rgb(Panel), 2.2f);
                draw->AddLine(ImVec2(dot.x - 3.5f, dot.y + 3.5f), ImVec2(dot.x + 3.5f, dot.y - 3.5f), Rgb(Panel), 2.2f);
                break;
            case RowState::Stopped:
                draw->AddCircleFilled(dot, 9, Rgb(TextDim), 20);
                draw->AddRectFilled(ImVec2(dot.x - 3.5f, dot.y - 1.2f), ImVec2(dot.x + 3.5f, dot.y + 1.2f), Rgb(Panel));
                break;
            case RowState::Waiting:
                draw->AddCircle(dot, 8, Rgb(BorderBright), 24, 1.5f);
                break;
            }
            ImGui::SetCursorScreenPos(ImVec2(rowAt.x + 30, rowAt.y));
            unsigned ink = row.state == RowState::Waiting ? TextDim : row.state == RowState::Failed ? Danger : current ? Text : TextSecondary;
            if (current)
                BoldText(row.title.c_str(), ink);
            else
                ImGui::TextColored(ImColor(Rgb(ink)), "%s", row.title.c_str());

            // Its time on the right: the running time, or how long a done step took.
            std::string time;
            if (current && running)
                time = InstallDuration(row.ms);
            else if (row.state == RowState::Done && row.timed && row.ms >= 1000)
                time = InstallDuration(row.ms);
            if (!time.empty())
            {
                float w = ImGui::CalcTextSize(time.c_str()).x;
                draw->AddText(ImVec2(left + width - w, rowAt.y), Rgb(current ? Text : TextDim), time.c_str());
            }
            // The running step's own bar, between its title and its time.
            if (current && running && row.fraction >= 0)
            {
                std::string count = fmt::format("{} / {}", row.done, row.total);
                ImGui::PushFont(nullptr, SmallSize);
                float countW = ImGui::CalcTextSize(count.c_str()).x;
                float barLeft = rowAt.x + 30 + std::max(ImGui::GetItemRectSize().x + 20, width * 0.42f);
                float barRight = left + width - 70 - countW - 12;
                if (barRight - barLeft > 40)
                {
                    float y = rowAt.y + lineH / 2 - 3;
                    draw->AddRectFilled(ImVec2(barLeft, y), ImVec2(barRight, y + 6), Rgb(Field), 3);
                    draw->AddRectFilled(ImVec2(barLeft, y), ImVec2(barLeft + std::max(6.0f, (barRight - barLeft) * row.fraction), y + 6), Rgb(Accent), 3);
                    draw->AddText(ImVec2(barRight + 12, rowAt.y + (lineH - ImGui::GetTextLineHeight()) / 2), Rgb(TextSecondary), count.c_str());
                }
                ImGui::PopFont();
            }
            ImGui::SetCursorScreenPos(ImVec2(rowAt.x, rowAt.y + rowH));
        }

        // Under the steps: what SSABuild does now (one line that replaces itself), or why it stopped.
        ImGui::Dummy(ImVec2(0, 2));
        if (running)
        {
            ImGui::PushFont(nullptr, SmallSize);
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Fit(list.action, width).c_str());
            ImGui::PopFont();
        }
        else if (failed)
        {
            BoldText(InstallFailureHeadline(list, progress).c_str(), Text);
            ImGui::PushFont(nullptr, SmallSize);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
            ImGui::PushTextWrapPos(left + width);
            if (!progress.error.empty())
                ImGui::TextWrapped("%s", progress.error.c_str());
            ImGui::TextWrapped("%s", Tr("Retry carries on from this step."));
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        else if (interrupted)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
            ImGui::PushTextWrapPos(left + width);
            ImGui::TextWrapped("%s", m_s->installPastPid != 0 ? Tr("The earlier install is still stopping. Resume in a moment.")
                                                           : Tr("Arkeyan Launcher closed during the install. Resume carries on."));
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        // The log, in the space left above the buttons: monospace, scrollable, following its end.
        constexpr float ButtonsH = 52;
        if (m_s->installLog)
        {
            ImGui::Dummy(ImVec2(0, 2));
            float logH = std::max(60.0f, ImGui::GetContentRegionAvail().y - ButtonsH);
            {
                // A whole number of lines (and its padding), so the line at the top is never half cut while it follows the end.
                ImGui::PushFont(m_monoFont, SmallSize);
                const float step = ImGui::GetTextLineHeight() + 2;
                ImGui::PopFont();
                logH = std::max(60.0f, std::floor((logH - 16) / step) * step + 16);
            }
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Rgb(Field));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, RadiusSmall);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
            ImGui::BeginChild("##installlog", ImVec2(0, logH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PushTextWrapPos(0.0f); // the kit's lines are long and not ours: they wrap inside the box, nothing scrolls sideways
            bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4;
            ImGui::PushFont(m_monoFont, SmallSize);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
            if (progress.logLines > progress.log.size())
                ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", Tf("({} earlier lines in Copy log)", progress.logLines - progress.log.size()).c_str());
            for (const std::string& line : progress.log)
            {
                bool error = line.starts_with("error") || line.find(": error") != std::string::npos;
                bool heading = line.starts_with("== ");
                ImGui::TextColored(ImColor(Rgb(error ? Danger : heading ? Text : TextSecondary)), "%s", line.c_str());
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

        // Buttons along the card's bottom edge: details on the left, the actions on the right.
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - Pad - 40));
        float rowY = ImGui::GetCursorPosY();
        if (PillButton(m_s->installLog ? Tr("Hide details") : Tr("Show details"), false, 130))
            m_s->installLog = !m_s->installLog;
        constexpr float ButtonW = 150;
        // The action on the right edge, at its real width (a longer translation widens it).
        auto rightButton = [&](const char* label, bool bold) {
            ImGui::SetCursorPos(ImVec2(Pad + width - PillWidth(label, ButtonW, bold), rowY));
        };
        if (failed || interrupted)
        {
            ImGui::SameLine();
            if (PillButton(Tr("Copy log"), false, 120))
            {
                std::string text;
                if (m_s->installPast.state == State::Idle)
                    text = m_s->installer.FullLog();
                if (m_s->kitUsed && !progress.error.empty())
                    text += progress.error + "\n";
                for (const std::string& line : m_s->installPast.log)
                    text += line + "\n";
                SDL_SetClipboardText(text.c_str());
                Toast(Tr("Log copied"));
            }
            ImGui::SameLine();
            if (PillButton(Tr("Open logs folder"), false, 140))
                OpenFolder(GameInstallFolder(m_s->installSha));
            rightButton(failed ? Tr("Retry") : Tr("Resume"), true);
            ImGui::BeginDisabled(m_s->installPastPid != 0);
            if (PillButton(failed ? Tr("Retry") : Tr("Resume"), true, ButtonW, true))
                StartInstall();
            ImGui::EndDisabled();
        }
        else if (running)
        {
            rightButton(Tr("Cancel"), false);
            if (PillButton(Tr("Cancel"), false, ButtonW))
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

// Installing the game: the INSTALL flow's state (StartInstall, PollInstall, what a launcher restart finds) and the
// progress card that replaces the logo on Play (docs/install.md).
#include "launcher/install_checklist.h"
#include "launcher/ui/launcher_app.h"
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
            if (ImGui::CalcTextSize(text.c_str()).x <= width)
                return text;
            std::string cut = text;
            while (!cut.empty() && ImGui::CalcTextSize((cut + "...").c_str()).x > width)
            {
                cut.pop_back();
                while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) // a whole UTF-8 character
                    cut.pop_back();
            }
            return cut + "...";
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
        m_kit = FindInstallKit(m_config);
        m_installed = FindInstalledGame(m_config, m_check.xexSha256);
        m_ownRuntime = FindRuntime(m_game, m_config);

        // An install of an earlier session that never finished (the launcher was closed, or it failed): shown again, with
        // its steps read back from its log, so the player can resume it.
        if (m_installer.Running() || m_installView || m_check.xexSha256.empty())
            return;
        std::string section = "install." + m_check.xexSha256;
        std::string state = SettingGet(m_config, section, "state");
        if (state != "running" && state != "failed")
            return;
        m_installPast = ReadInstallLog(GameInstallFolder(m_check.xexSha256));
        if (m_installPast.state == InstallProgress::State::Idle || m_installPast.state == InstallProgress::State::Done)
            return;
        if (state == "running")
        {
            uint32_t pid = uint32_t(SettingInt(m_config, section, "pid", 0));
            m_installPastPid = PlatformProcessAlive(pid) ? pid : 0;
        }
        m_installSha = m_check.xexSha256;
        m_installView = true;
        m_installLog = false;
        if (m_shots.empty())
            m_page = Page::Play;
    }

    bool App::NeedsInstall() const
    {
        return m_disc == DiscState::Ready && m_installed.runtime.empty() && m_ownRuntime.empty();
    }

    bool App::UpdateAvailable() const
    {
        return m_disc == DiscState::Ready && !m_installed.runtime.empty() && m_kit && m_kit->version != m_installed.kitVersion;
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
            Toast("Couldn't save the settings");
    }

    void App::StartInstall()
    {
        RefreshInstall();
        m_installSha = m_check.xexSha256;
        m_installKitVersion = m_kit ? m_kit->version : std::string();
        m_installView = true;
        m_installLog = false;
        m_installPast = {};
        m_installPastPid = 0;
        m_kitUsed = false;
        m_kitError.clear();
        m_page = Page::Play;
        if (m_kit)
            StartInstaller();
        else
            StartKitDownload();
    }

    // No kit next to the launcher or downloaded yet: the first step of the install fetches it (docs/install.md).
    void App::StartKitDownload()
    {
        m_kitUsed = true;
        m_kitStep = { "kit", "Downloading the game tools", SDL_GetTicks(), 0 };
        m_kitDownload.Start(SettingGet(m_config, "update", "kit_repo", "mflr0/Skylauncher"), "v" + LauncherVersion(), DownloadedKitsFolder());
    }

    void App::StartInstaller()
    {
        if (m_installer.Start(m_kit, m_discPath, GameInstallFolder(m_installSha)))
        {
            auto& section = m_config["install." + m_installSha];
            section["pid"] = std::to_string(m_installer.ProcessId());
            section["started"] = std::to_string(int64_t(std::time(nullptr)));
            SetInstallState(m_installSha, "running");
        }
    }

    InstallProgress App::InstallSnapshot(size_t logTail)
    {
        if (m_kitUsed)
        {
            KitDownloader::Snapshot_ kit = m_kitDownload.Snapshot();
            using Kit = KitDownloader::State;
            InstallProgress progress;
            progress.steps.push_back(m_kitStep);
            if (!m_kitError.empty() || kit.state != Kit::Done)
            {
                progress.state = !m_kitError.empty() || kit.state == Kit::Failed ? InstallProgress::State::Failed
                    : kit.state == Kit::Cancelled                               ? InstallProgress::State::Cancelled
                                                                                : InstallProgress::State::Running;
                progress.error = !m_kitError.empty() ? m_kitError : kit.error;
                progress.done = kit.done >> 20; // MB
                progress.total = kit.total >> 20;
                return progress;
            }
            // Downloaded: the kit's step, done, then SSABuild's.
            progress = m_installer.Snapshot(logTail);
            progress.steps.insert(progress.steps.begin(), m_kitStep);
            return progress;
        }
        if (m_installPast.state == InstallProgress::State::Idle)
            return m_installer.Snapshot(logTail);
        InstallProgress progress = m_installPast;
        size_t first = progress.log.size() - std::min(logTail, progress.log.size());
        progress.log.erase(progress.log.begin(), progress.log.begin() + ptrdiff_t(first));
        return progress;
    }

    void App::PollInstall()
    {
        // An SSABuild left running by an earlier launcher: wait for it to stop before offering to resume.
        if (m_installPastPid != 0 && SDL_GetTicks() % 1000 < 20 && !PlatformProcessAlive(m_installPastPid))
            m_installPastPid = 0;
        if (m_kitUsed && m_kitError.empty() && m_installView && !m_installer.Running() && m_kitStep.endedAt == 0)
        {
            KitDownloader::Snapshot_ kit = m_kitDownload.Snapshot();
            if (kit.state == KitDownloader::State::Done)
            {
                m_kitStep.endedAt = SDL_GetTicks();
                Set("update", "kit_tag", kit.tag);
                RefreshInstall();
                if (m_kit)
                {
                    m_installKitVersion = m_kit->version;
                    StartInstaller();
                }
                else
                    m_kitError = "The game tools were downloaded but SSABuild wasn't found in them.";
            }
            else if (kit.state == KitDownloader::State::Cancelled)
            {
                m_installView = m_kitUsed = false;
                Toast("Installation cancelled");
            }
            return;
        }
        if (!m_installView || m_installer.Running() || m_installPast.state != InstallProgress::State::Idle)
            return;
        InstallProgress progress = m_installer.Snapshot();
        if (progress.state == InstallProgress::State::Done)
        {
            RecordInstalledGame(m_config, m_installSha, progress.runtime, m_installKitVersion);
            SetInstallState(m_installSha, nullptr);
            Toast("The game is installed: press PLAY");
            m_installView = false;
            RefreshInstall();
        }
        else if (progress.state == InstallProgress::State::Cancelled)
        {
            SetInstallState(m_installSha, nullptr);
            m_installView = false;
            Toast("Installation cancelled");
        }
        else if (progress.state == InstallProgress::State::Failed && SettingGet(m_config, "install." + m_installSha, "state") != "failed")
            SetInstallState(m_installSha, "failed");
    }

    // The progress card: a checklist of SSABuild's steps (waiting, running with a spinner, done with a tick and its time,
    // failed), the overall bar and the time left, the current action on one line, the log behind "Show details".
    void App::DrawInstall(ImVec2 at, ImVec2 size)
    {
        InstallProgress progress = InstallSnapshot(m_installLog ? 600 : 0);
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
        const char* title = failed ? "INSTALLATION STOPPED" : interrupted ? "INSTALLATION INTERRUPTED" : "INSTALLING THE GAME";
        ImVec2 titleAt = ImGui::GetCursorScreenPos();
        Title(title, TitleSize, failed ? Danger : Text);
        if (failed || interrupted)
        {
            constexpr float CloseSize = 30;
            ImVec2 closeAt(left + width - CloseSize, titleAt.y - 4);
            ImGui::SetCursorScreenPos(closeAt);
            if (ImGui::InvisibleButton("##closeinstall", ImVec2(CloseSize, CloseSize)))
            {
                m_installView = m_kitUsed = false;
                SetInstallState(m_installSha, nullptr);
                m_installPast = {};
            }
            bool hovered = ImGui::IsItemHovered();
            if (hovered)
                ImGui::SetTooltip("Close");
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
            std::string where = fmt::format("Step {} of {}", stepNumber, list.rows.size());
            std::string times;
            if (list.elapsedMs > 0)
                times = InstallDuration(list.elapsedMs) + " elapsed";
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
        if (fold && !m_installLog)
        {
            ImVec2 rowAt = ImGui::GetCursorScreenPos();
            ImVec2 dot(rowAt.x + 10, rowAt.y + lineH / 2 + 1);
            draw->AddCircleFilled(dot, 9, Rgb(Ok), 20);
            draw->AddPolyline(std::array{ ImVec2(dot.x - 4.5f, dot.y), ImVec2(dot.x - 1.2f, dot.y + 3.5f), ImVec2(dot.x + 4.5f, dot.y - 3.5f) }.data(),
                3, Rgb(Panel), 0, 2.2f);
            ImGui::SetCursorScreenPos(ImVec2(rowAt.x + 30, rowAt.y));
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%d of %zu steps done", list.current, list.rows.size());
            ImGui::SetCursorScreenPos(ImVec2(rowAt.x, rowAt.y + rowH));
        }
        for (size_t i = 0; i < list.rows.size(); i++)
        {
            const InstallChecklist::Row& row = list.rows[i];
            bool current = int(i) == list.current;
            if ((m_installLog || fold) && !current)
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
            ImGui::TextWrapped("What was already built is kept: Retry carries on from this step.");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        else if (interrupted)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
            ImGui::PushTextWrapPos(left + width);
            ImGui::TextWrapped(m_installPastPid != 0 ? "The installer started earlier is still stopping. Resume will be possible in a moment."
                                                     : "Skylauncher was closed before the game was built. Resume carries on where it stopped.");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        // The log, in the space left above the buttons: monospace, scrollable, following its end.
        constexpr float ButtonsH = 52;
        if (m_installLog)
        {
            ImGui::Dummy(ImVec2(0, 2));
            float logH = std::max(60.0f, ImGui::GetContentRegionAvail().y - ButtonsH);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Rgb(Field));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, RadiusSmall);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
            ImGui::BeginChild("##installlog", ImVec2(0, logH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_HorizontalScrollbar);
            bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4;
            ImGui::PushFont(m_monoFont, SmallSize);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
            if (progress.logLines > progress.log.size())
                ImGui::TextColored(ImColor(Rgb(TextDim)), "(%zu earlier lines: Copy log has them all)", progress.logLines - progress.log.size());
            for (const std::string& line : progress.log)
            {
                bool error = line.starts_with("error") || line.find(": error") != std::string::npos;
                bool heading = line.starts_with("== ");
                ImGui::TextColored(ImColor(Rgb(error ? Danger : heading ? Text : TextSecondary)), "%s", line.c_str());
            }
            ImGui::PopStyleVar();
            ImGui::PopFont();
            if (atBottom)
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
        }

        // Buttons along the card's bottom edge: details on the left, the actions on the right.
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - Pad - 40));
        float rowY = ImGui::GetCursorPosY();
        if (PillButton(m_installLog ? "Hide details" : "Show details", false, 150))
            m_installLog = !m_installLog;
        constexpr float ButtonW = 150;
        float gap = ImGui::GetStyle().ItemSpacing.x;
        auto rightButtons = [&](int count) {
            ImGui::SetCursorPos(ImVec2(Pad + width - count * ButtonW - (count - 1) * gap, rowY));
        };
        if (failed || interrupted)
        {
            ImGui::SameLine();
            if (PillButton("Copy log", false, 120))
            {
                std::string text;
                if (m_installPast.state == State::Idle)
                    text = m_installer.FullLog();
                if (m_kitUsed && !progress.error.empty())
                    text += progress.error + "\n";
                for (const std::string& line : m_installPast.log)
                    text += line + "\n";
                SDL_SetClipboardText(text.c_str());
                Toast("Log copied");
            }
            ImGui::SameLine();
            if (PillButton("Open logs folder", false, 170))
                OpenFolder(GameInstallFolder(m_installSha));
            rightButtons(1);
            ImGui::BeginDisabled(m_installPastPid != 0);
            if (PillButton(failed ? "Retry" : "Resume", true, ButtonW, true))
                StartInstall();
            ImGui::EndDisabled();
        }
        else if (running)
        {
            rightButtons(1);
            if (PillButton("Cancel", false, ButtonW))
            {
                m_installer.Cancel();
                m_kitDownload.Cancel();
            }
        }
        EndCard();
    }

    // The install screens (SSA_LAUNCHER_TEST_INSTALL, with SKYLAUNCHER_KIT set to <build>/fake-kit): the Install button, the
    // progress card early and during the compile, with its log, a failure, a retry that succeeds, Game data, and an
    // install read back after a restart.
    void App::TestInstallScript(uint64_t now)
    {
        InstallProgress progress = m_installer.Snapshot();
        if (m_testReal)
        {
            // The real kit: INSTALL, then a screenshot every few minutes until it ends (and PLAY with SSA_LAUNCHER_TEST_PLAY).
            const char* every = SDL_getenv("SSA_LAUNCHER_TEST_EVERY");
            uint64_t interval = (every != nullptr ? uint64_t(std::atoi(every)) : 120) * 1000;
            if (m_testStep == 0)
            {
                m_page = Page::Play;
                if (m_disc == DiscState::None && !m_testDrop.empty())
                    StartVerify(m_testDrop);
                if (m_disc == DiscState::Ready)
                    StartInstall();
                if (m_installView)
                    m_testAt = now + 3000;
            }
            else if (m_installer.Running() || m_installView)
                m_testAt = now + (m_installer.Running() ? interval : 2000);
            else if (m_testStep < 1000)
            {
                m_testStep = 1000; // after the last shot
                m_testAt = now + 1500;
            }
            else
            {
                // SSA_LAUNCHER_TEST_PLAY: then PLAY, as the button does, once the game is built.
                if (SDL_getenv("SSA_LAUNCHER_TEST_PLAY") != nullptr && m_disc == DiscState::Ready && !NeedsInstall())
                {
                    m_chosen = m_discPath;
                    m_chosenRuntime = m_installed.runtime;
                    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "SSA_LAUNCHER_SHOTS"); // else the game runs its own shots
                }
                m_running = false;
            }
            return;
        }
        switch (m_testStep)
        {
        case 0:
            m_page = Page::Play;
            if (m_disc == DiscState::None && !m_testDrop.empty())
                StartVerify(m_testDrop);
            if (m_disc == DiscState::Ready || m_disc == DiscState::Failed || m_testDrop.empty())
                m_testAt = now + 400;
            break;
        case 1:
            if (!m_installView)
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
            if ((progress.steps.size() >= 5 && progress.done >= 30) || progress.state != InstallProgress::State::Running)
                m_testAt = now + 100;
            break;
        case 3:
            m_installLog = true;
            m_testAt = now + 300;
            break;
        case 4:
            m_installLog = false;
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
            else if (!m_installView)
                m_testAt = now + 400;
            break;
        case 6:
            m_page = Page::Settings;
            m_tab = SettingsTab::GameData;
            m_testAt = now + 400;
            break;
        case 7:
            if (!m_installView)
            {
                // As if the launcher had been closed during the compile.
                std::ofstream(GameInstallFolder(m_check.xexSha256) / "install.log", std::ios::trunc)
                    << "@step toolchain Preparing the compiler\n@step disc Reading the disc\n@step recompile Translating the game code\n"
                       "@step shaders Translating the shaders\n@step compile Compiling the game\n@progress 812 2304\n";
                m_config["install." + m_check.xexSha256]["pid"] = "0";
                SetInstallState(m_check.xexSha256, "running");
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

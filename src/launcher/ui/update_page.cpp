// Updates in the window: the check at start, the banner ("Update available" and its progress), What's new, and the About
// page (version, "Check for updates", credits and licences). The logic is in launcher/updater.cpp (docs/updates.md).
#include "launcher/ui/launcher_app.h"
#include <ctime>
#include <fmt/format.h>

namespace launcher_ui
{
    namespace
    {
        constexpr float BannerH = 56;
        constexpr char Project[] = "https://github.com/mflr0/Skylauncher";

        int64_t Now() { return int64_t(std::time(nullptr)); }

        std::string Fit(const std::string& text, float width)
        {
            if (ImGui::CalcTextSize(text.c_str()).x <= width)
                return text;
            std::string cut = text;
            while (!cut.empty() && ImGui::CalcTextSize((cut + "...").c_str()).x > width)
            {
                cut.pop_back();
                while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80)
                    cut.pop_back();
            }
            return cut + "...";
        }

        const char* PlatformName()
        {
#if defined(_WIN32)
            return "Windows";
#elif defined(__APPLE__)
            return "macOS";
#else
            return "Linux";
#endif
        }

        std::string AgoText(int64_t seconds)
        {
            if (seconds < 90) return Tr("just now");
            if (seconds < 5400) return Tf("{} min ago", seconds / 60);
            if (seconds < 36 * 3600) return Tf("{} h ago", seconds / 3600);
            return Tf("{} days ago", seconds / 86400);
        }
    }

    void App::StartUpdateCheck(bool manual)
    {
        if (m_updates.Busy())
            return;
        m_updateManual = manual;
        if (manual)
        {
            m_updateLater = m_kitLater = false;
            m_updateDoneNote.clear();
        }
        UpdateManager::Settings settings;
        settings.repo = SettingGet(m_config, "update", "repo", "mflr0/Skylauncher");
        settings.kitRepo = SettingGet(m_config, "update", "kit_repo", "mflr0/Skylauncher");
        settings.kitInstalled = SettingGet(m_config, "update", "kit_tag", m_s->kit ? m_s->kit->release : std::string());
        settings.kitRoot = DownloadedKitsFolder();
        // A kit next to the launcher (development, offline) is updated in place; a downloaded one gets its own kit/<tag>/.
        if (m_s->kit && m_s->kit->folder.parent_path() != settings.kitRoot)
            settings.kitFolder = m_s->kit->folder;
        m_updateWasBusy = true;
        m_updates.Check(settings);
    }

    // At the end of a check or an update: remember it, say the answer to a manual check, carry on with the game update.
    void App::PollUpdates()
    {
        if (m_updateWasBusy && !m_updates.Busy())
        {
            m_updateWasBusy = false;
            UpdateManager::State state = m_updates.Snapshot();
            if (state.phase == UpdateManager::Phase::Idle)
            {
                Set("update", "last_check", std::to_string(Now()));
                if (m_updateManual)
                    m_updateDoneNote = !state.checked ? Tf("Couldn't check: {}", state.checkError)
                        : state.launcherNewer          ? Tf("Version {} is available.", state.launcher.tag)
                        : state.kitNewer               ? Tr("Skylauncher is up to date. A new version of the game is available.")
                                                       : Tr("Skylauncher is up to date.");
            }
            else if (state.phase == UpdateManager::Phase::Done && state.doing == "kit")
            {
                Set("update", "kit_tag", state.kit.tag);
                RefreshInstall();
                if (m_s->disc == DiscState::Ready && !Installing())
                    StartInstall(); // the new kit builds the game
            }
        }
    }

    float App::BannerHeight()
    {
        UpdateManager::State state = m_updates.Snapshot();
        using Phase = UpdateManager::Phase;
        bool progress = state.phase == Phase::Downloading || state.phase == Phase::Verifying || state.phase == Phase::Installing;
        bool offer = (state.launcherNewer && !m_updateLater) || (state.kitNewer && !m_kitLater && m_s->disc == DiscState::Ready);
        bool shown = progress || state.restartNeeded || (state.phase == Phase::Failed && !state.error.empty()) || offer ||
            (state.phase == Phase::Done && !state.note.empty());
        return shown ? BannerH : 0;
    }

    void App::DrawUpdateBanner(float height)
    {
        UpdateManager::State state = m_updates.Snapshot();
        using Phase = UpdateManager::Phase;
        ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(SidebarW, 0));
        ImGui::SetNextWindowSize(ImVec2(display.x - SidebarW, height));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Rgb(BottomBar));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, 8));
        ImGui::Begin("##updatebanner", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 at = ImGui::GetWindowPos();
        draw->AddRectFilled(ImVec2(at.x, at.y + height - 2), ImVec2(at.x + display.x - SidebarW, at.y + height), Rgb(AccentDark));

        // Buttons are laid out from the right; the text takes what is left.
        struct Button { const char* label; bool primary; float width; int action; };
        std::vector<Button> buttons;
        std::string text;
        float barFraction = -1;
        bool progress = state.phase == Phase::Downloading || state.phase == Phase::Verifying || state.phase == Phase::Installing;
        enum { None, WhatsNew, UpdateLauncher, UpdateKit, Later, KitLater, Restart, Cancel, Retry, Dismiss };
        if (state.restartNeeded)
        {
            text = Tf("Skylauncher {} is installed. Restart to use it.", state.launcher.tag);
            buttons = { { Tr("Restart now"), true, 150, Restart } };
        }
        else if (progress)
        {
            bool kit = state.doing == "kit";
            text = state.phase == Phase::Downloading ? Tr(kit ? N_("Downloading the game kit") : N_("Downloading Skylauncher"))
                : state.phase == Phase::Verifying    ? Tr(kit ? N_("Checking the game kit") : N_("Checking Skylauncher"))
                                                     : Tr(kit ? N_("Installing the game kit") : N_("Installing Skylauncher"));
            barFraction = state.phase == Phase::Downloading ? state.progress : -1;
            if (state.phase == Phase::Downloading)
                buttons = { { Tr("Cancel"), false, 120, Cancel } };
        }
        else if (state.phase == Phase::Failed)
        {
            text = Tf("The update failed: {}", state.error);
            buttons = { { Tr("Retry"), true, 110, Retry }, { Tr("Close"), false, 110, Dismiss } };
        }
        else if (state.phase == Phase::Done && !state.note.empty())
        {
            text = state.note;
            buttons = { { Tr("OK"), false, 90, Dismiss } };
        }
        else if (state.launcherNewer && !m_updateLater)
        {
            text = Tf("Update available: {}", state.launcher.tag);
            buttons = { { Tr("Update now"), true, 140, UpdateLauncher }, { Tr("What's new"), false, 130, WhatsNew }, { Tr("Later"), false, 100, Later } };
        }
        else
        {
            text = Tr("A new version of the game is available");
            buttons = { { Tr("Update the game"), true, 170, UpdateKit }, { Tr("Later"), false, 100, KitLater } };
        }

        float gap = 10, buttonsW = 0;
        for (Button& button : buttons)
            button.width = PillWidth(button.label, button.width, button.primary);
        for (const Button& button : buttons)
            buttonsW += button.width + gap;
        float contentW = display.x - SidebarW - 2 * Pad;
        float textW = contentW - buttonsW - 16;
        ImGui::SetCursorPos(ImVec2(Pad, (height - 2 - ImGui::GetTextLineHeight()) / 2));
        std::string shown = Fit(text, textW);
        if (barFraction >= 0 || progress)
        {
            float labelW = ImGui::CalcTextSize(shown.c_str()).x;
            BoldText(shown.c_str(), Text);
            ImVec2 barAt(ImGui::GetWindowPos().x + Pad + labelW + 18, at.y + (height - 2) / 2 - 4);
            float barW = std::max(0.0f, textW - labelW - 18);
            draw->AddRectFilled(barAt, ImVec2(barAt.x + barW, barAt.y + 8), Rgb(Field), 4);
            float fraction = barFraction >= 0 ? barFraction : float(std::fmod(SDL_GetTicks() / 1200.0, 1.0));
            if (barFraction >= 0)
                draw->AddRectFilled(barAt, ImVec2(barAt.x + std::max(8.0f, barW * fraction), barAt.y + 8), Rgb(Accent), 4);
            else
            {
                float w = barW * 0.25f, x = barAt.x + (barW - w) * fraction;
                draw->AddRectFilled(ImVec2(x, barAt.y), ImVec2(x + w, barAt.y + 8), Rgb(Accent), 4);
            }
        }
        else
            BoldText(shown.c_str(), Text);
        if (shown != text && ImGui::IsItemHovered()) // cut to fit: the whole message under the pointer
            ImGui::SetTooltip("%s", text.c_str());

        float x = Pad + contentW - buttonsW + gap;
        int pressed = None;
        for (const Button& button : buttons)
        {
            ImGui::SetCursorPos(ImVec2(x, (height - 2 - 40) / 2));
            ImGui::PushID(button.action);
            if (PillButton(button.label, button.primary, button.width, button.primary, BottomBarRaised, BottomBarRaisedHover, BottomBarRaisedActive))
                pressed = button.action;
            ImGui::PopID();
            x += button.width + gap;
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        switch (pressed)
        {
        case WhatsNew: m_whatsNewOpen = true; break;
        case UpdateLauncher: m_updates.UpdateLauncher(); m_updateWasBusy = true; break;
        case UpdateKit: m_updates.UpdateKit(); m_updateWasBusy = true; break;
        case Later: m_updateLater = true; break;
        case KitLater: m_kitLater = true; break;
        case Cancel: m_updates.Cancel(); break;
        case Retry: if (state.doing == "kit") m_updates.UpdateKit(); else m_updates.UpdateLauncher(); m_updateWasBusy = true; break;
        case Dismiss: m_updates.Dismiss(); break;
        case Restart:
        {
            std::filesystem::path self = LauncherSelf();
            std::string program = self.string();
            std::vector<const char*> arguments;
#if defined(__APPLE__)
            arguments = { "/usr/bin/open", "-n", program.c_str(), nullptr };
#else
            arguments = { program.c_str(), nullptr };
#endif
            if (SDL_Process* process = SDL_CreateProcess(arguments.data(), false))
            {
                SDL_DestroyProcess(process);
                m_running = false;
            }
            else
                Toast(Tr("Couldn't restart: start Skylauncher again"), true);
            break;
        }
        }
    }

    // The release notes of the update on offer.
    void App::DrawWhatsNew()
    {
        if (m_whatsNewOpen && !ImGui::IsPopupOpen("##whatsnew"))
            ImGui::OpenPopup("##whatsnew");
        ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(ImVec2(std::min(680.0f, display.x - 80), std::min(520.0f, display.y - 60)));
        ImGui::SetNextWindowPos(ImVec2(display.x / 2, display.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Rgb(Panel, 250));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, Pad));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, RadiusLarge);
        if (ImGui::BeginPopupModal("##whatsnew", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
        {
            if (!m_whatsNewOpen)
                ImGui::CloseCurrentPopup();
            UpdateManager::State state = m_updates.Snapshot();
            Title(Tf("What's new in {}", state.launcher.tag).c_str(), TitleSize, Text);
            ImGui::BeginChild("##notes", ImVec2(0, -52), ImGuiChildFlags_None);
            std::string notes = state.launcher.notes.empty() ? std::string(Tr("No notes were published with this version.")) : state.launcher.notes;
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(notes.c_str()); // plain text: the release's Markdown as written
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
            if (PillButton(Tr("Release page"), false, 160) && !state.launcher.pageUrl.empty())
                SDL_OpenURL(state.launcher.pageUrl.c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - Pad - 2 * 140 - ImGui::GetStyle().ItemSpacing.x);
            if (PillButton(Tr("Close"), false, 140) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
            {
                m_whatsNewOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (PillButton(Tr("Update now"), true, 140, true))
            {
                m_whatsNewOpen = false;
                ImGui::CloseCurrentPopup();
                m_updates.UpdateLauncher();
                m_updateWasBusy = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    // Settings > Launcher: one check a day when it is on, nothing else is ever sent.
    void App::DrawUpdates()
    {
        UpdateManager::State state = m_updates.Snapshot();
        bool busy = m_updates.Busy();
        ImGui::BeginDisabled(busy);
        if (PillButton(state.phase == UpdateManager::Phase::Checking ? Tr("Checking...") : Tr("Check for updates"), false, 220))
            StartUpdateCheck(true);
        ImGui::EndDisabled();
        ImGui::SameLine(0, 14);
        ImGui::AlignTextToFramePadding();
        if (!m_updateDoneNote.empty())
            ImGui::TextColored(ImColor(Rgb(state.checked && !state.launcherNewer ? Ok : TextSecondary)), "%s", m_updateDoneNote.c_str());
        else if (int64_t last = SettingInt(m_config, "update", "last_check", 0); last > 0)
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tf("Last checked {}", AgoText(Now() - last)).c_str());
        ImGui::Dummy(ImVec2(0, 2));
        bool automatic = ParseBool(SettingGet(m_config, "update", "auto_check", "1"));
        if (Toggle("##autocheck", &automatic))
            Set("update", "auto_check", automatic ? "1" : "0");
        ImGui::SameLine(0, 12);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Tr("Check for updates when Skylauncher starts (once a day)"));
        ImGui::PushFont(nullptr, SmallSize);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
        ImGui::TextWrapped("%s", Tr("A check asks github.com for the latest release of Skylauncher: no account, no identifier, nothing else is sent. "
            "Updates are checked against the release's SHA-256 before they are installed."));
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    void App::DrawAbout()
    {
        Title("Skylauncher", HeadlineSize);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tf("Version {}, {}", LauncherVersion(), PlatformName()).c_str());
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextWrapped("%s", Tr("A launcher for the native PC ports of the Skylanders games. It contains no game code or art: everything shown "
            "from the game is read from your own disc and kept on this computer."));

        ImGui::Dummy(ImVec2(0, 14));
        Title(Tr("Credits"), TitleSize, Text);
        ImGui::TextWrapped("%s", Tr("The launcher's look, colours, fonts, background and layout are dorydev's design."));
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextWrapped("%s", Tr("Made with SDL 3 (zlib), Dear ImGui, {fmt}, stb, miniz and XenonRecomp's XEX readers (MIT), and the Outfit and "
            "Bungee fonts (SIL Open Font License). Skylauncher is MIT licensed."));
        ImGui::Dummy(ImVec2(0, 6));
        if (PillButton(Tr("Project page"), false, 180))
            SDL_OpenURL(Project);
        ImGui::SameLine();
        if (PillButton(Tr("Licences"), false, 150))
            SDL_OpenURL((std::string(Project) + "/blob/main/THIRD_PARTY_NOTICES.md").c_str());
        ImGui::SameLine();
        if (PillButton(Tr("Report a bug"), false, 180))
            SDL_OpenURL((std::string(Project) + "/issues/new").c_str());
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::PushFont(nullptr, SmallSize);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextDim));
        ImGui::TextWrapped("%s", Tr("Skylanders is a trademark of Activision. This project is not affiliated with or endorsed by Activision."));
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    // The update screens (SSA_LAUNCHER_TEST_UPDATE, SKYLAUNCHER_UPDATE_URL on a fake release): the banner, What's new, About
    // before and after a manual check, and a failure.
    void App::TestUpdateScript(uint64_t now)
    {
        switch (m_testStep)
        {
        case 0:
            m_page = Page::Play;
            if (!m_updateWasBusy && !m_updates.Snapshot().checked)
                StartUpdateCheck(true);
            else if (!m_updates.Busy())
                m_testAt = now + 400;
            break;
        case 1:
            m_whatsNewOpen = true;
            m_testAt = now + 500;
            break;
        case 2:
            m_whatsNewOpen = false;
            m_page = Page::Settings;
            m_tab = SettingsTab::Launcher; // the updates are in the launcher's tab
            m_updateDoneNote.clear();
            m_testAt = now + 500;
            break;
        case 3:
            m_updateDoneNote = Tr("Skylauncher is up to date.");
            m_testAt = now + 400;
            break;
        case 4:
            m_updates.UpdateLauncher(); // the fake release has no SHA256SUMS: refused
            m_updateWasBusy = true;
            m_page = Page::Play;
            if (m_updates.Snapshot().phase == UpdateManager::Phase::Failed)
                m_testAt = now + 400;
            break;
        default:
            m_running = false;
            break;
        }
    }
}

// Support in Settings: the game's logs (Copy game log, Open logs folder), "Report a problem" (one text file with the
// logs and the player's versions, shown before anything is saved or copied; nothing is uploaded) and "Test the portal"
// (docs/support.md).
#include "launcher/report.h"
#include "launcher/ui/launcher_app.h"
#include <fmt/format.h>
#include <fstream>
#include <memory>
#include <mutex>

namespace launcher_ui
{
    namespace
    {
        // The save dialog answers on a thread of its own: the callback writes the file and leaves the outcome here,
        // the window tells the player on its next frame.
        std::mutex g_saveMutex;
        bool g_saveOk = false, g_saveFailed = false;

        void SDLCALL SaveReport(void* userdata, const char* const* files, int)
        {
            std::unique_ptr<std::string> text(static_cast<std::string*>(userdata));
            std::lock_guard lock(g_saveMutex);
            if (files == nullptr)
            {
                g_saveFailed = true;
                return;
            }
            if (files[0] == nullptr) // cancelled
                return;
            std::ofstream out(files[0], std::ios::binary | std::ios::trunc);
            out.write(text->data(), std::streamsize(text->size()));
            out.close();
            if (out)
            {
                g_saveOk = true;
            }
            else
                g_saveFailed = true;
        }

        // What the game will do with the portal, given the mode and what the test found.
        std::string PortalVerdict(const std::string& mode, const PortalProbeResult& result, bool& good)
        {
            bool opened = false;
            for (const PortalProbeEntry& entry : result.portals)
                opened = opened || entry.opened;
            good = opened || mode == "emulated";
            if (mode == "emulated")
                return Tr("Virtual portal chosen.");
            if (opened)
                return Tr("Real portal: the game will use it.");
            if (result.portals.empty())
                return mode == "real" ? Tr("No portal found: the game will wait for one.")
                                      : Tr("No portal found: using the virtual portal.");
            return mode == "real" ? Tr("The portal can't be opened: the game will wait.")
                                  : Tr("The portal can't be opened: using the virtual portal.");
        }
    }

    // One text file with the last logs, the settings and the versions; it is shown before it can be saved or copied.
    void App::OpenProblemReport()
    {
        launcher_report::Sources sources;
        sources.runtimeFolder = RuntimeLogFolder();
        sources.logFolder = GameLogFolder();
        sources.settingsFile = std::filesystem::path(std::u8string(m_configPath.begin(), m_configPath.end()));
        sources.iso = m_s->discPath;
        std::string kit = m_s->installed.kitVersion;
        if (kit.empty())
            if (std::optional<InstallKit> found = FindInstallKit(m_config))
                kit = found->version;
        sources.versions = launcher_report::ReadVersions(LauncherVersion(), kit, sources.runtimeFolder);
        launcher_report::Identity identity = launcher_report::CurrentIdentity(sources.iso);
        sources.userName = identity.userName;
        sources.computerName = identity.computerName;
        sources.homeFolder = identity.homeFolder;
        m_reportText = launcher_report::BuildReport(sources);
        m_reportOpen = true;
    }

    void App::DrawProblemReport()
    {
        // Tell the player how the save went, whatever the dialog's thread answered.
        {
            std::lock_guard lock(g_saveMutex);
            if (g_saveOk)
            {
                Toast(Tr("Report saved"));
                g_saveOk = false;
            }
            else if (g_saveFailed)
            {
                Toast(Tr("Couldn't save the report"), true);
                g_saveFailed = false;
            }
        }

        if (m_reportOpen && !ImGui::IsPopupOpen("##problemreport"))
            ImGui::OpenPopup("##problemreport");
        ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(ImVec2(std::min(880.0f, display.x - 80), std::min(680.0f, display.y - 60)));
        ImGui::SetNextWindowPos(ImVec2(display.x / 2, display.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Rgb(Panel, 250));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, Pad));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, RadiusLarge);
        if (ImGui::BeginPopupModal("##problemreport", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
        {
            if (!m_reportOpen)
                ImGui::CloseCurrentPopup();
            Title(Tr("Report a problem"), TitleSize, Text);
            ImGui::PushFont(nullptr, SmallSize);
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s",
                Tr("Your names are removed and nothing is sent."));
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Rgb(Field));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, RadiusSmall);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
            ImGui::BeginChild("##reporttext", ImVec2(0, -52), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_NoBackground);
            if (m_monoFont != nullptr)
                ImGui::PushFont(m_monoFont, SmallSize);
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(m_reportText.c_str());
            ImGui::PopTextWrapPos();
            if (m_monoFont != nullptr)
                ImGui::PopFont();
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
            if (PillButton(Tr("Save as..."), true, 160, true))
            {
                std::filesystem::path folder = GameLogFolder();
                std::string name = launcher_report::FileName();
                std::string start = (folder / name).string();
                static const SDL_DialogFileFilter Filters[] = { { "Text file", "txt" } };
                SDL_ShowSaveFileDialog(SaveReport, new std::string(m_reportText), m_window, Filters, 1, start.c_str());
            }
            ImGui::SameLine();
            if (PillButton(Tr("Copy"), false, 140))
            {
                SDL_SetClipboardText(m_reportText.c_str());
                Toast(Tr("Report copied"));
            }
            ImGui::SameLine(ImGui::GetWindowWidth() - Pad - 120);
            // B / Escape close it, as every dialog (A presses the focused button).
            if (PillButton(Tr("Close"), false, 120) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
            {
                m_reportOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    void App::DrawPortalTest()
    {
        std::string runtime = !m_s->installed.runtime.empty() ? m_s->installed.runtime : m_s->ownRuntime;
        bool testing = !m_probe.done;
        ImGui::BeginDisabled(testing || runtime.empty());
        if (PillButton(testing ? Tr("Testing...") : Tr("Test the portal"), false, 200))
            m_probe.Start(runtime);
        ImGui::EndDisabled();
        float buttonY = ImGui::GetItemRectMin().y, buttonH = ImGui::GetItemRectSize().y;
        ImGui::SameLine(0, 14);
        ImGui::PushFont(nullptr, SmallSize);
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, buttonY + (buttonH - ImGui::GetTextLineHeight()) / 2)); // centred on the button
        if (runtime.empty())
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tr("Install the game first."));
        else if (!m_probe.ran)
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tr("Plug in the portal, then test it."));
        ImGui::PopFont();
        if (!m_probe.ran || testing)
            return;

        const PortalProbeResult& result = m_probe.result;
        std::string mode = SettingGet(m_config, "portal", "mode", "auto");
        if (!result.error.empty())
        {
            ImGui::TextColored(ImColor(Rgb(Danger)), "%s", result.error.c_str());
            return;
        }
        for (const PortalProbeEntry& entry : result.portals)
        {
            std::string name = entry.product.empty() ? std::string(entry.kind == "usb" ? Tr("Xbox portal") : Tr("Portal")) : entry.product;
            ImGui::TextColored(ImColor(Rgb(entry.opened ? Ok : Danger)), "%s", entry.opened ? Tr("Found") : Tr("Found, not usable"));
            ImGui::SameLine(0, 8);
            ImGui::TextUnformatted(fmt::format("{} ({} {}:{})", name, entry.kind == "usb" ? "USB" : "HID", entry.vid, entry.pid).c_str());
            if (!entry.model.empty())
            {
                // The model from the id the portal answered to the game's reset, and what the game thinks of it.
                ImGui::PushFont(nullptr, SmallSize);
                ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tf("Model: {} (id {})", entry.model, entry.id).c_str());
                if (entry.verdict == "works")
                    ImGui::TextColored(ImColor(Rgb(Ok)), "%s", Tr("Spyro's Adventure accepts this portal."));
                else if (entry.verdict == "warns" || entry.verdict == "rejected")
                    ImGui::TextColored(ImColor(Rgb(Danger)), "%s", (entry.verdict == "rejected" ? Tf("Spyro's Adventure rejects this portal: {}.", entry.verdictReason)
                                                           : Tf("Spyro's Adventure warns about this portal: {}.", entry.verdictReason)).c_str());
                ImGui::PopFont();
            }
            if (!entry.opened)
            {
                std::string why = entry.status.starts_with("error: ") ? entry.status.substr(7) : entry.status;
                ImGui::PushFont(nullptr, SmallSize);
                ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", why.c_str());
                ImGui::PopFont();
            }
        }
        for (const std::string& note : result.notes)
        {
            ImGui::PushFont(nullptr, SmallSize);
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", note.c_str());
            ImGui::PopFont();
        }
        bool good = false;
        std::string verdict = PortalVerdict(mode, result, good);
        ImGui::TextColored(ImColor(Rgb(good ? Ok : TextSecondary)), "%s", verdict.c_str());
    }

    void App::DrawLogs()
    {
        std::filesystem::path folder = GameLogFolder();
        std::error_code error;
        bool any = std::filesystem::exists(RuntimeLogFolder() / RuntimeLogName, error) || std::filesystem::exists(folder / CaptureLogName, error);
        if (PillButton(Tr("Report a problem"), true, 220, true))
            OpenProblemReport();
        ImGui::SameLine();
        ImGui::BeginDisabled(!any);
        if (PillButton(Tr("Copy game log"), false, 220))
        {
            SDL_SetClipboardText(GameLogText().c_str());
            Toast(Tr("Game log copied"));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (PillButton(Tr("Open logs folder"), false, 220) && !OpenFolder(folder))
            Toast(Tr("Couldn't open the folder"), true);
        ImGui::PushFont(nullptr, SmallSize);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
        ImGui::TextWrapped("%s", any ? Tr("The game's last log, for a bug report.")
                                     : Tr("No game log yet."));
        ImGui::TextWrapped("%s", Tr("You see the report before saving it; nothing is sent."));
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
}

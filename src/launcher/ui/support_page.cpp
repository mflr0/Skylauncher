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
        std::string g_savedPath;
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
                g_savedPath = files[0];
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
                return "Virtual portal, as chosen: no portal needed.";
            if (opened)
                return "Real portal: the game will use it.";
            if (result.portals.empty())
                return mode == "real" ? "No portal found: the game will wait for one to be plugged in."
                                      : "No portal found: the game will use the virtual portal.";
            return mode == "real" ? "The portal can't be opened: the game will wait for it."
                                  : "The portal can't be opened: the game will use the virtual portal.";
        }
    }

    // One text file with the last logs, the settings and the versions; it is shown before it can be saved or copied.
    void App::OpenProblemReport()
    {
        launcher_report::Sources sources;
        sources.runtimeFolder = RuntimeLogFolder();
        sources.logFolder = GameLogFolder();
        sources.settingsFile = std::filesystem::path(std::u8string(m_configPath.begin(), m_configPath.end()));
        sources.iso = m_discPath;
        std::string kit = m_installed.kitVersion;
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
                Toast("Report saved to " + g_savedPath);
                g_saveOk = false;
            }
            else if (g_saveFailed)
            {
                Toast("Couldn't save the report");
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
            Title("Report a problem", TitleSize, Text);
            ImGui::PushFont(nullptr, SmallSize);
            ImGui::TextColored(ImColor(Rgb(TextSecondary)),
                "This is everything the report holds. Your account, your computer's name and your home folder are replaced. "
                "Nothing is sent: save it, or copy it into an issue.");
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
            if (PillButton("Save as...", true, 160, true))
            {
                std::filesystem::path folder = GameLogFolder();
                std::string name = launcher_report::FileName();
                std::string start = (folder / name).string();
                static const SDL_DialogFileFilter Filters[] = { { "Text file", "txt" } };
                SDL_ShowSaveFileDialog(SaveReport, new std::string(m_reportText), m_window, Filters, 1, start.c_str());
            }
            ImGui::SameLine();
            if (PillButton("Copy", false, 140))
            {
                SDL_SetClipboardText(m_reportText.c_str());
                Toast("Report copied");
            }
            ImGui::SameLine(ImGui::GetWindowWidth() - Pad - 120);
            // B / Escape close it, as every dialog (A presses the focused button).
            if (PillButton("Close", false, 120) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
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
        std::string runtime = !m_installed.runtime.empty() ? m_installed.runtime : m_ownRuntime;
        bool testing = !m_probe.done;
        ImGui::BeginDisabled(testing || runtime.empty());
        if (PillButton(testing ? "Testing..." : "Test the portal", false, 200))
            m_probe.Start(runtime);
        ImGui::EndDisabled();
        float buttonY = ImGui::GetItemRectMin().y, buttonH = ImGui::GetItemRectSize().y;
        ImGui::SameLine(0, 14);
        ImGui::PushFont(nullptr, SmallSize);
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, buttonY + (buttonH - ImGui::GetTextLineHeight()) / 2)); // centred on the button
        if (runtime.empty())
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "The test is part of the game: install it first.");
        else if (!m_probe.ran)
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "Plug the portal in, then check that the game sees it.");
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
            std::string name = entry.product.empty() ? std::string(entry.kind == "usb" ? "Xbox portal" : "Portal") : entry.product;
            ImGui::TextColored(ImColor(Rgb(entry.opened ? Ok : Danger)), "%s", entry.opened ? "Found" : "Found, not usable");
            ImGui::SameLine(0, 8);
            ImGui::Text("%s (%s %s:%s)", name.c_str(), entry.kind == "usb" ? "USB" : "HID", entry.vid.c_str(), entry.pid.c_str());
            if (!entry.model.empty())
            {
                // The model from the id the portal answered to the game's reset, and what the game thinks of it.
                ImGui::PushFont(nullptr, SmallSize);
                ImGui::TextColored(ImColor(Rgb(TextSecondary)), "Model: %s (id %s)", entry.model.c_str(), entry.id.c_str());
                if (entry.verdict == "works")
                    ImGui::TextColored(ImColor(Rgb(Ok)), "Spyro's Adventure accepts this portal.");
                else if (entry.verdict == "warns" || entry.verdict == "rejected")
                    ImGui::TextColored(ImColor(Rgb(Danger)), "Spyro's Adventure %s this portal: %s.", entry.verdict == "rejected" ? "rejects" : "warns about",
                        entry.verdictReason.c_str());
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
        if (PillButton("Report a problem", true, 220, true))
            OpenProblemReport();
        ImGui::SameLine();
        ImGui::BeginDisabled(!any);
        if (PillButton("Copy game log", false, 220))
        {
            SDL_SetClipboardText(GameLogText().c_str());
            Toast("Game log copied");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (PillButton("Open logs folder", false, 220) && !OpenFolder(folder))
            Toast("Couldn't open the folder");
        ImGui::PushFont(nullptr, SmallSize);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
        ImGui::TextWrapped(any ? "The log of the last time the game ran, to send with a bug report. It stays on your computer until you paste it."
                               : "No game log yet: it appears after the game has run once.");
        ImGui::TextWrapped("Report a problem makes one text file with the last logs, your settings and your versions, with your "
                           "account, computer and folder names replaced. It is shown before you save or copy it; nothing is sent.");
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
}

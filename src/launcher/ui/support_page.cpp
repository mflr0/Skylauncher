// Support in Settings: the game's logs (Copy game log, Open logs folder) and "Test the portal" (docs/support.md).
#include "launcher/ui/launcher_app.h"
#include <fmt/format.h>

namespace launcher_ui
{
    namespace
    {
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

    void App::DrawPortalTest()
    {
        std::string runtime = !m_installed.runtime.empty() ? m_installed.runtime : m_ownRuntime;
        bool testing = !m_probe.done;
        ImGui::BeginDisabled(testing || runtime.empty());
        if (PillButton(testing ? "Testing..." : "Test the portal", false, 200))
            m_probe.Start(runtime);
        ImGui::EndDisabled();
        ImGui::SameLine(0, 14);
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(nullptr, SmallSize);
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
            if (!entry.opened)
            {
                std::string why = entry.status.starts_with("error: ") ? entry.status.substr(7) : entry.status;
                ImGui::PushFont(nullptr, SmallSize);
                ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", why.c_str());
                ImGui::PopFont();
            }
        }
        bool good = false;
        std::string verdict = PortalVerdict(mode, result, good);
        ImGui::TextColored(ImColor(Rgb(good ? Ok : TextSecondary)), "%s", verdict.c_str());
    }

    void App::DrawLogs()
    {
        std::filesystem::path folder = GameLogFolder();
        std::error_code error;
        bool any = std::filesystem::exists(folder / RuntimeLogName, error) || std::filesystem::exists(folder / CaptureLogName, error);
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
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
}

// Support in Settings: the game's logs (Copy game log, Open logs folder), "Report a problem" (one text file with the
// logs and the player's versions, shown before anything is saved or copied; nothing is uploaded) and "Test the portal"
// (docs/support.md).
#include "launcher/problem_report.h"
#include "launcher/report.h"
#include "launcher/ui/launcher_app.h"
#include "launcher/game_names.h"
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
        launcher_report::Sources sources = CurrentReportSources(m_configPath, m_config, m_s->game.id, m_s->discPath);
        if (!m_s->installed.kitVersion.empty())
            sources.versions.kit = m_s->installed.kitVersion;
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
                Toast(Tr("Report saved"), ui::ToastKind::Success);
                g_saveOk = false;
            }
            else if (g_saveFailed)
            {
                Toast(Tr("Couldn't save the report"), ui::ToastKind::Error);
                g_saveFailed = false;
            }
        }

        if (!ui::BeginModal("##problemreport", Tr("Report a problem"), &m_reportOpen, ui::ModalSize::Large))
            return;
        ui::Text(ui::TextStyle::Caption, Tr("Your names are removed and nothing is sent."), ui::color::Dim);
        // The room under the text is the title, the line above and the actions (Normal high); the rest is the report.
        const float room = ImGui::GetIO().DisplaySize.y - 2 * ui::space::XL - 3 * ui::height::Large;
        ui::BeginCard("##reporttext", ImVec2(0, std::clamp(room, 3.0f * ui::height::Large, 8.0f * ui::height::Large)), { .inset = true, .padding = ui::space::M });
        ImGui::SetScrollX(0.0f);
        ImGui::PushFont(m_monoFont, ui::type::Caption); // logs keep the monospaced font (null: the text font)
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(m_reportText.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ui::EndCard();
        // Save to Desktop is the action (the same file the game's F3 menu writes: on the Desktop, else in the logs folder);
        // Save as... and Copy stay at the left of the same row.
        ui::ModalResult result = ui::ModalActions(&m_reportOpen, Tr("Save to Desktop"), Tr("Close"));
        const float afterY = ImGui::GetCursorPosY();
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetCursorStartPos().x, ImGui::GetItemRectMin().y)); // on the actions' row
        if (ui::Button(Tr("Save as...")))
        {
            std::filesystem::path folder = GameLogFolder();
            std::string name = launcher_report::FileName();
            std::string start = (folder / name).string();
            static const SDL_DialogFileFilter Filters[] = { { "Text file", "txt" } };
            SDL_ShowSaveFileDialog(SaveReport, new std::string(m_reportText), m_window, Filters, 1, start.c_str());
        }
        ImGui::SameLine(0, ui::space::S);
        if (ui::Button(Tr("Copy")))
        {
            SDL_SetClipboardText(m_reportText.c_str());
            Toast(Tr("Report copied"), ui::ToastKind::Success);
        }
        ImGui::SetCursorPosY(afterY);
        ImGui::Dummy(ImVec2(0, 0)); // the cursor went back and forth: an item ends it
        if (result == ui::ModalResult::Confirm)
        {
            SavedReport saved = SaveProblemReportText(m_reportText);
            if (saved.file.empty())
                Toast(Tr("Couldn't save the report"), ui::ToastKind::Error);
            else
                Toast(Tf("Report saved: {}", saved.file.filename().string()), ui::ToastKind::Success);
        }
        ui::EndModal();
    }

    // The "Test the portal" row, drawn inside the open settings table; DrawPortalResult shows what the test found under it.
    void App::DrawPortalTest()
    {
        const std::string runtime = !m_s->installed.runtime.empty() ? m_s->installed.runtime : m_s->ownRuntime;
        const bool testing = !m_probe.done;
        ui::SettingRow(Tr("Test the portal"), runtime.empty() ? Tr("Install the game first.") : !m_probe.ran ? Tr("Plug in the portal, then test it.") : nullptr);
        ui::ButtonOptions options;
        options.loading = testing;
        options.disabled = runtime.empty();
        if (ui::Button(testing ? Tr("Testing...") : Tr("Test"), options) && !testing)
            m_probe.Start(runtime);
    }

    void App::DrawPortalResult()
    {
        if (!m_probe.ran || !m_probe.done)
            return;
        const PortalProbeResult& result = m_probe.result;
        const std::string mode = SettingGet(m_config, "portal", "mode", "auto");
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        if (!result.error.empty())
        {
            ui::StatusPill(ui::Status::Error, Tr("The test failed"));
            ui::Text(ui::TextStyle::Caption, result.error.c_str(), ui::color::Dim);
            return;
        }
        const char* spyro = GameName("ssa");
        for (const PortalProbeEntry& entry : result.portals)
        {
            std::string name = entry.product.empty() ? std::string(entry.kind == "usb" ? Tr("Xbox portal") : Tr("Portal")) : entry.product;
            ui::StatusPill(entry.opened ? ui::Status::Success : ui::Status::Error, entry.opened ? Tr("Found") : Tr("Found, not usable"));
            ImGui::SameLine(0, ui::space::XS);
            ImGui::AlignTextToFramePadding();
            ui::Text(ui::TextStyle::Body, fmt::format("{} ({} {}:{})", name, entry.kind == "usb" ? "USB" : "HID", entry.vid, entry.pid).c_str(), ui::color::Ink, -1);
            if (!entry.model.empty())
            {
                // The model from the id the portal answered to the game's reset, and what the game thinks of it.
                ui::Text(ui::TextStyle::Caption, Tf("Model: {} (id {})", entry.model, entry.id).c_str(), ui::color::Dim);
                if (entry.verdict == "works")
                    ui::Text(ui::TextStyle::Caption, Tf("{} accepts this portal.", spyro).c_str(), ui::color::InkSoft);
                else if (entry.verdict == "warns" || entry.verdict == "rejected")
                    ui::Text(ui::TextStyle::Caption, (entry.verdict == "rejected" ? Tf("{} rejects this portal: {}.", spyro, entry.verdictReason)
                                                           : Tf("{} warns about this portal: {}.", spyro, entry.verdictReason)).c_str(), ui::color::InkSoft);
            }
            if (!entry.opened)
            {
                std::string why = entry.status.starts_with("error: ") ? entry.status.substr(7) : entry.status;
                ui::Text(ui::TextStyle::Caption, why.c_str(), ui::color::Dim);
            }
        }
        for (const std::string& note : result.notes)
            ui::Text(ui::TextStyle::Caption, note.c_str(), ui::color::Dim);
        bool good = false;
        const std::string verdict = PortalVerdict(mode, result, good);
        ui::StatusPill(good ? ui::Status::Success : ui::Status::Neutral, verdict.c_str());
    }

    void App::DrawLogs()
    {
        const std::filesystem::path folder = GameLogFolder();
        std::error_code error;
        const bool any = std::filesystem::exists(RuntimeLogFolder() / RuntimeLogName, error) || std::filesystem::exists(folder / CaptureLogName, error);
        if (!ui::BeginSettings("##support"))
            return;
        ui::SettingRow(Tr("Problem report"), Tr("You see the report before saving it; nothing is sent."));
        ui::ButtonOptions report;
        report.kind = ui::ButtonKind::Primary;
        report.width = -1;
        if (ui::Button(Tr("Report a problem"), report))
            OpenProblemReport();
        ui::SettingRow(Tr("Game log"), any ? Tr("The game's last log, for a bug report.") : Tr("No game log yet."));
        ui::ButtonOptions copy;
        copy.width = -1;
        copy.disabled = !any;
        if (ui::Button(Tr("Copy game log"), copy))
        {
            SDL_SetClipboardText(GameLogText().c_str());
            Toast(Tr("Game log copied"), ui::ToastKind::Success);
        }
        ui::SettingRow(Tr("Logs folder"), nullptr);
        ui::ButtonOptions open;
        open.icon = ui::Icon::Folder;
        open.width = -1;
        if (ui::Button(Tr("Open logs folder"), open) && !OpenFolder(folder))
            Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
        ui::EndSettings();
    }
}

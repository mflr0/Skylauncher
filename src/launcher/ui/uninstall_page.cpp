// "Uninstall everything" in Settings > Launcher: the list of what goes with its sizes, the box for the saves and figures,
// the hold, and the result. What is removed, and the safety rules, are in launcher/uninstall.h (docs/install.md).
#include "launcher/ui/launcher_app.h"
#include <algorithm>

namespace launcher_ui
{
    namespace
    {
        using uninstall::Group;
        using uninstall::Why;

        constexpr const char* HoldHint = N_("Hold to delete all of this.");

        std::string GroupName(Group group)
        {
            switch (group)
            {
            case Group::Games: return Tr("Games");
            case Group::GameData: return Tr("Game data copies");
            case Group::Tools: return Tr("Game tools");
            case Group::Caches: return Tr("Caches and logs");
            case Group::OldFolders: return Tr("Older folders");
            case Group::Settings: return Tr("Settings");
            case Group::Shortcuts: return Tr("Desktop shortcuts");
            case Group::Launcher: return "Skylauncher";
            case Group::Saves: return Tr("Saves");
            case Group::Figures: return Tr("Figures");
            }
            return {};
        }

        // What a left-over is called: the file's name (no path in the text), else its group's.
        std::string NameOf(const uninstall::Left& left)
        {
            std::string name = left.path.filename().string();
            return name.empty() ? GroupName(left.group) : name;
        }

        // One line for what stayed, in the player's words. Empty for what needs no word (a stale settings entry).
        std::string Sentence(const uninstall::Left& left)
        {
            switch (left.why)
            {
            case Why::Elsewhere: return Tf("{} is not in its folder: it stays.", NameOf(left));
            case Why::Missing: return {};
            case Why::Link: return Tf("{} is a link: it stays.", NameOf(left));
            case Why::Outside:
            case Why::Unsafe:
                return left.group == Group::Launcher ? std::string(Tr("Skylauncher can't remove itself from here: delete it by hand."))
                                                     : Tf("{} is outside Skylauncher's folders: it stays.", NameOf(left));
            case Why::DiscImage: return Tf("{} is a disc image: it stays.", NameOf(left));
            case Why::InUse: return Tr("A game is running: quit it first.");
            case Why::NotRemoved: return Tf("{} couldn't be removed.", NameOf(left));
            case Why::Unfit: return left.group == Group::Launcher ? std::string(Tr("Skylauncher can't remove itself from here: delete it by hand."))
                                                                    : Tf("{} couldn't be removed.", NameOf(left));
            case Why::Kept: return left.group == Group::OldFolders ? Tr("Kept: Xbox signing keys") : Tr("Other files in the figures folder stay.");
            }
            return {};
        }
    }

    // The section of the Launcher tab: a button that opens the list.
    void App::DrawUninstallSection()
    {
        Title(Tr("Remove Skylauncher"), TitleSize, Text);
        const bool busy = AnyInstalling() || AnyVerifying();
        ImGui::BeginDisabled(busy);
        if (PillButton(Tr("Uninstall everything..."), false, 300))
            OpenUninstall();
        ImGui::EndDisabled();
        ImGui::PushFont(nullptr, SmallSize);
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(TextSecondary));
        ImGui::TextWrapped("%s", busy ? Tr("Wait for the game to finish.") : Tr("Removes the games, their files and Skylauncher."));
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    // The dry run: nothing is touched.
    void App::OpenUninstall()
    {
        m_un = {};
        m_un.env = uninstall::ThisMachine(m_config);
        m_un.plan = uninstall::MakePlan(m_un.env);
        m_un.stage = UninstallStage::List;
    }

    void App::RunUninstallEverything()
    {
        m_un.notices = m_un.plan.left;
        m_un.result = uninstall::RunUninstall(m_un.plan, m_un.env, m_un.tick);
        const uninstall::LauncherRemoval removal = uninstall::PlanLauncherRemoval(m_un.env);
        // The launcher itself, once it has quit: only after everything else, and only when nothing refused the rest.
        const bool interrupted = std::any_of(m_un.result.left.begin(), m_un.result.left.end(), [](const uninstall::Left& left) { return left.why == Why::InUse || left.why == Why::Unsafe; });
        if (!interrupted && removal.ok)
        {
            m_un.launcherScheduled = uninstall::StartScheduled(uninstall::ScheduleCommand(m_un.env.os, removal, uninstall::ProcessId()));
            if (!m_un.launcherScheduled)
                m_un.result.left.push_back({ Why::NotRemoved, Group::Launcher, m_un.env.launcherSelf, {} });
        }
        if (!interrupted)
        {
            // The settings are gone: nothing writes them back, and the window only shows the result.
            FreezeLauncherConfig();
            m_uninstalled = true;
        }
        m_un.stage = UninstallStage::Result;
    }

    void App::DrawUninstall()
    {
        using uninstall::Plan;
        auto note = [](const std::string& text, unsigned color = TextSecondary) {
            ImGui::PushFont(nullptr, SmallSize);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(color));
            ImGui::TextWrapped("%s", text.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        };
        // A label on the left and its size on the right of the card.
        auto row = [&](const std::string& label, const std::string& size, unsigned color, bool bold = false) {
            ImVec2 at = ImGui::GetCursorScreenPos();
            float width = ImGui::GetContentRegionAvail().x;
            if (bold)
                ImGui::PushFont(m_boldFont, ImGui::GetFontSize());
            ImGui::TextColored(ImColor(Rgb(color)), "%s", label.c_str());
            if (!size.empty())
            {
                float sizeW = ImGui::CalcTextSize(size.c_str()).x;
                ImGui::SetCursorScreenPos(ImVec2(at.x + width - sizeW, at.y));
                ImGui::TextColored(ImColor(Rgb(color)), "%s", size.c_str());
            }
            if (bold)
                ImGui::PopFont();
        };

        if (m_un.stage == UninstallStage::Result)
        {
            const uninstall::Result& result = m_un.result;
            const bool interrupted = !m_uninstalled;
            std::vector<std::string> lines;
            for (const uninstall::Left& left : m_un.notices)
                if (std::string text = Sentence(left); !text.empty() && left.group != Group::Launcher)
                    lines.push_back(text);
            for (const uninstall::Left& left : result.left)
                if (std::string text = Sentence(left); !text.empty())
                    lines.push_back(text);
            // A launcher that could not be removed was said once, by the plan or by the run.
            lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
            Title(interrupted ? Tr("Nothing was removed") : lines.empty() ? Tr("Everything is removed") : Tr("Removed, except"), TitleSize, Text);
            if (!interrupted)
                note(Tf("{} freed.", LauncherFormatSize(result.freed)));
            ImGui::Dummy(ImVec2(0, 8));
            const size_t shown = std::min<size_t>(lines.size(), 8);
            for (size_t i = 0; i < shown; i++)
                ImGui::TextWrapped("%s", lines[i].c_str());
            if (lines.size() > shown)
                note(Tf("and {} more", lines.size() - shown));
            ImGui::Dummy(ImVec2(0, 12));
            if (m_un.launcherScheduled)
                note(m_un.env.os == uninstall::Os::MacOS ? Tr("Skylauncher goes to the Trash when you quit.") : Tr("Skylauncher deletes itself when you quit."));
            ImGui::Dummy(ImVec2(0, 8));
            if (interrupted && PillButton(Tr("Back"), false, 200))
                m_un.stage = UninstallStage::Closed;
            if (!interrupted && PillButton(Tr("Quit"), true, 200, true))
                m_running = false;
            return;
        }

        const Plan& plan = m_un.plan;
        Title(Tr("What goes"), TitleSize, Text);
        note(Tr("Your disc images are never touched."));
        ImGui::Dummy(ImVec2(0, 8));

        // The list: each built game, then the other groups.
        bool any = false;
        for (const uninstall::GameEntry& game : m_un.env.games)
        {
            uint64_t bytes = 0;
            bool has = false;
            for (const uninstall::Item& item : plan.items)
                if (item.group == Group::Games && item.game == game.id)
                    bytes += item.bytes, has = true;
            if (has)
                row(game.name, LauncherFormatSize(bytes), Text), any = true;
        }
        for (Group group : { Group::GameData, Group::Tools, Group::Caches, Group::OldFolders, Group::Settings, Group::Shortcuts, Group::Launcher })
            if (plan.Has(group))
            {
                row(GroupName(group), LauncherFormatSize(plan.Bytes(group)), Text);
                any = true;
            }
        if (!any)
            note(Tr("Nothing to remove."));

        // What needs the tick.
        ImGui::Dummy(ImVec2(0, 8));
        const uint64_t mine = plan.Bytes(Group::Saves) + plan.Bytes(Group::Figures);
        ImGui::AlignTextToFramePadding();
        Toggle("##alsodelete", &m_un.tick);
        ImGui::SameLine(0, 12);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Tr("Also delete my saves and my figures"));
        if (plan.Has(Group::Figures))
        {
            ImGui::SameLine(0, 16);
            if (PillButton(Tr("Export first"), false, 160))
                ExportFiguresFirst();
        }
        note(mine == 0 ? Tr("No saves or figures yet.")
                       : Tf("Saves {}, figures {}.", LauncherFormatSize(plan.Bytes(Group::Saves)), LauncherFormatSize(plan.Bytes(Group::Figures))));
        ImGui::Dummy(ImVec2(0, 8));
        row(Tr("Total"), LauncherFormatSize(plan.Total(m_un.tick)), Text, true);

        // What the plan already knows will stay.
        std::vector<std::string> stays;
        for (const uninstall::Left& left : plan.left)
            if (std::string text = Sentence(left); !text.empty())
                stays.push_back(text);
        if (!stays.empty())
            ImGui::Dummy(ImVec2(0, 8));
        for (const std::string& text : stays)
            note(text);

        ImGui::Dummy(ImVec2(0, 12));
        const bool blocked = plan.refused || !plan.running.empty() || plan.figuresInUse || AnyInstalling() || AnyVerifying();
        if (!plan.running.empty())
            note(Tf("{} is running: quit it first.", plan.running.front()), Danger);
        else if (plan.figuresInUse)
            note(Tr("A figure is in use: quit the game first."), Danger);
        else if (plan.refused)
            note(Tr("The data folder isn't safe to remove."), Danger);
        else if (AnyInstalling() || AnyVerifying())
            note(Tr("Wait for the game to finish."), Danger);
        ImGui::BeginDisabled(blocked);
        if (HoldButton(Tr("Uninstall everything"), 300, Tr(HoldHint)))
            RunUninstallEverything();
        m_un.holdId = ImGui::GetItemID();
        ImGui::EndDisabled();
        ImGui::SameLine(0, 12);
        if (PillButton(Tr("Cancel"), false, 160))
            m_un = {};
    }

    // The screens of the screenshot test (SSA_LAUNCHER_TEST_UNINSTALL), in a scratch data tree. Each is captured once settled.
    void App::TestUninstallScript(uint64_t now)
    {
        switch (m_testStep)
        {
        case 0: // Settings > Launcher, the section
            m_page = Page::Settings;
            m_tab = SettingsTab::Launcher;
            m_testScrollEnd = true;
            m_testAt = now + 700;
            break;
        case 1: // the list
            m_testScrollEnd = false;
            if (m_un.stage == UninstallStage::Closed)
                OpenUninstall();
            m_testAt = now + 500;
            break;
        case 2: // the box ticked
            m_un.tick = true;
            m_testAt = now + 400;
            break;
        case 3: // the hold, half way
            m_testScrollEnd = true; // the hold button is at the end of the list
            if (m_testHoldAt == 0)
            {
                m_testHoldAt = now; // HoldButton treats the button as held from now on
            }
            else if (now - m_testHoldAt > 500)
                m_testAt = now;
            break;
        case 4: // held to the end: done, the result
            if (m_un.stage == UninstallStage::Result)
            {
                m_testScrollEnd = false;
                m_testAt = now + 500;
            }
            break;
        default:
            m_running = false;
            break;
        }
    }
}

// The overlays of the collection page and the Figure page: stats read from the dump, edit stats, rename, favourite,
// duplicate, back up and restore, reset and delete (held), "it is not in use any more".
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/launcher_ui.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fmt/format.h>

namespace launcher_ui
{
    namespace fs = std::filesystem;
    using collection::Figure;

    // One modal window for the three overlays (Figure, Add a figure, Manage): the content changes, the popup stays.
    void CollectionPage::DrawOverlay()
    {
        if (m_overlay == Overlay::None)
            return;
        if (!ImGui::IsPopupOpen("##overlay"))
            ImGui::OpenPopup("##overlay");
        ImVec2 display = ImGui::GetIO().DisplaySize;
        ImVec2 size(std::min(m_overlay == Overlay::Figure ? 880.0f : 960.0f, display.x - 60), std::min(640.0f, display.y - 50));
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowPos(ImVec2(display.x / 2, display.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Rgb(Panel, 250));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 140));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, Pad));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, RadiusLarge);
        bool open = ImGui::BeginPopupModal("##overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        if (!open)
        {
            m_overlay = Overlay::None;
            return;
        }
        // B / Escape go back one step: out of editing, then out of the overlay.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
        {
            if (m_editing)
                m_editing = false;
            else if (m_renaming)
                m_renaming = false;
            else if (!m_confirm.empty())
                m_confirm.clear();
            else
            {
                CloseOverlay();
                ImGui::EndPopup();
                return;
            }
        }
        switch (m_overlay)
        {
        case Overlay::Figure: DrawFigurePage(size); break;
        case Overlay::Add: DrawAdd(size); break;
        case Overlay::Manage: DrawManage(size); break;
        case Overlay::None: break;
        }
        ImGui::EndPopup();
    }

    // A button that acts after being held for a second (reset, delete): the mouse button or A. The fill shows the time.
    bool CollectionPage::HoldButton(const char* label, float width)
    {
        constexpr uint64_t HoldMs = 1000;
        ImGui::PushStyleColor(ImGuiCol_Button, Rgb(Danger, 70));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Rgb(Danger, 110));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Rgb(Danger, 150));
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Text));
        ImGui::Button(label, ImVec2(width, 40));
        ImGui::PopStyleColor(4);
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        ImVec2 text = ImGui::CalcTextSize(label);
        ImGui::GetWindowDrawList()->AddText(ImVec2((min.x + max.x - text.x) / 2 + BoldOffset, (min.y + max.y - text.y) / 2), Rgb(Text), label);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hold to confirm");
        bool done = false;
        if (ImGui::IsItemActive())
        {
            uint64_t now = SDL_GetTicks();
            if (m_hold != label)
            {
                m_hold = label;
                m_holdSince = now;
            }
            float t = std::min(1.0f, float(now - m_holdSince) / HoldMs);
            ImGui::GetWindowDrawList()->AddRectFilled(min, ImVec2(min.x + (max.x - min.x) * t, max.y), Rgb(Danger, 190), Radius);
            ImGui::GetWindowDrawList()->AddText(ImVec2((min.x + max.x - text.x) / 2, (min.y + max.y - text.y) / 2), Rgb(Text), label);
            if (t >= 1.0f)
            {
                done = true;
                m_hold.clear();
            }
        }
        else if (m_hold == label)
            m_hold.clear();
        return done;
    }

    namespace
    {
        std::string BackupLabel(const std::string& backup)
        {
            std::string stem = fs::path(backup).stem().string(); // 2026-10-24T101500Z
            if (stem.size() >= 16)
                return fmt::format("{} {}:{}", stem.substr(0, 10), stem.substr(11, 2), stem.substr(13, 2));
            return stem;
        }
    }

    void CollectionPage::DrawStats(const Figure& f, float width)
    {
        ImGui::BeginGroup();
        if (!HasProgress(f) && f.toy != nullptr)
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s keep no progress.", collection::CategoryName(f.toy->category));
        else if (f.state == figures::AreaState::Blank)
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "New: never played yet. Its stats show up once a game has used it.");
        else if (f.state == figures::AreaState::Damaged)
            ImGui::TextColored(ImColor(Rgb(Danger)), "Unreadable: its checksums don't match. Restore a backup, or reset it.");
        else
        {
            // Level, and the way to the next one.
            uint32_t xp = f.stats.experience;
            int level = f.level;
            uint32_t from = figures::LevelExperience[std::clamp(level - 1, 0, figures::MaxLevel - 1)];
            uint32_t to = level >= figures::MaxLevel ? from : figures::LevelExperience[level];
            float fraction = to > from ? std::clamp(float(xp - from) / float(to - from), 0.0f, 1.0f) : 1.0f;
            ImGui::PushFont(m_app.m_titleFont, 26);
            ImGui::TextColored(ImColor(Rgb(Text)), "Level %d", level);
            ImGui::PopFont();
            ImVec2 at = ImGui::GetCursorScreenPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(at, ImVec2(at.x + width, at.y + 12), Rgb(Field), 6);
            draw->AddRectFilled(at, ImVec2(at.x + width * fraction, at.y + 12), Rgb(Accent), 6);
            ImGui::Dummy(ImVec2(0, 16));
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%u XP%s", xp, level >= figures::MaxLevel ? " (maximum level)" : fmt::format(" · {} for level {}", to - xp, level + 1).c_str());
        }
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6, 3));
        if (ImGui::BeginTable("##stats", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 120);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
            auto row = [](const char* name, const std::string& value) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", name);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", value.c_str());
            };
            if (HasProgress(f) && f.state == figures::AreaState::Valid)
            {
                row("Gold", fmt::format("{}   ·   play time {}", f.stats.gold, PlayTime(f.stats.playSeconds)));
                row("Hero points", fmt::format("{}   ·   {}   ·   {} upgrades", f.stats.heroPoints,
                    f.stats.hat == 0 ? std::string("no hat") : fmt::format("hat {}", f.stats.hat), std::popcount(f.stats.upgrades)));
                std::string nickname = figures::NicknameUtf8(f.stats);
                row("Nickname", nickname.empty() ? std::string("None") : nickname);
            }
            std::string last = f.lastGame.empty() ? std::string("Never") : fmt::format("{} · {}", ShortGameName(f.lastGame), ShortDate(f.lastUsed));
            row("Last played", last);
            std::string games;
            for (const collection::Game& game : collection::Catalogue::Get().Games())
                if (f.toy != nullptr && collection::Catalogue::Get().Accepts(game.id, f.type))
                    games += (games.empty() ? "" : ", ") + ShortGameName(game.id);
            row("Plays in", games.empty() ? std::string("No game this launcher knows") : std::count(games.begin(), games.end(), ',') >= 5 ? "all six games" : games);
            row("Toy", fmt::format("{} · type {}{}", f.toy != nullptr ? f.toy->name : "Unknown", f.type, f.variant != 0 ? fmt::format(" · variant {:04X}", f.variant) : ""));
            row("File", f.file + (f.origin.empty() ? "" : " · " + f.origin));
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ImGui::EndGroup();
    }

    void CollectionPage::DrawFigurePage(ImVec2 size)
    {
        const Figure* found = m_model.Find(m_figure);
        if (found == nullptr)
        {
            CloseOverlay();
            return;
        }
        if (m_editing)
        {
            Figure copy = *found;
            DrawEditStats(copy);
            return;
        }
        Figure f = *found; // actions below reload the collection
        const std::string path = f.path;
        bool writable = Writable(f);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Left: the picture and what kind of toy it is.
        constexpr float Picture = 240;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        draw->AddRectFilled(origin, ImVec2(origin.x + Picture, origin.y + Picture), Rgb(Field), RadiusLarge);
        DrawIcon(draw, f.toy, f.Kind(), ImVec2(origin.x + 10, origin.y + 10), Picture - 20, true);
        float badgeX = origin.x;
        auto badge = [&](const std::string& text, unsigned fill, unsigned ink) {
            ImGui::PushFont(nullptr, SmallSize - 1);
            float w = ImGui::CalcTextSize(text.c_str()).x + 12;
            ImGui::PopFont();
            m_app.Badge(draw, ImVec2(badgeX, origin.y + Picture + 10), text.c_str(), fill, ink);
            badgeX += w + 6;
        };
        if (f.Kind() != collection::VariantKind::Base)
            badge(collection::VariantKindName(f.Kind()), collection::VariantKindRgb(f.Kind()), AccentInk);
        if (f.toy != nullptr && f.toy->element != collection::Element::None)
            badge(ElementName(f.toy->element), PanelRaised, Text);
        if (f.toy != nullptr)
            badge(collection::CategoryName(f.toy->category), PanelRaised, Text);

        // Right: name, lock state, stats.
        float right = origin.x + Picture + 28, textW = size.x - 2 * Pad - Picture - 28;
        ImGui::SetCursorScreenPos(ImVec2(right, origin.y));
        ImGui::BeginGroup();
        m_app.Title(Name(f).c_str(), HeadlineSize - 6, Text);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s%s", f.variantInfo.name.c_str(), f.favourite ? "  ·  favourite" : "");
        if (f.lock.Busy())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Danger));
            ImGui::PushTextWrapPos(right + textW);
            ImGui::TextWrapped("On the portal in %s (pid %lld). Read-only until the game lets it go.", LiveGame(f).c_str(), (long long)f.lock.pid);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0, 4));
        DrawStats(f, textW);
        ImGui::EndGroup();

        // Actions, along the bottom: two rows, and the confirmation bar above them.
        float bottom = ImGui::GetWindowPos().y + size.y - Pad;
        float row1 = bottom - 40 - 8 - 40, row2 = bottom - 40;
        if (m_renaming)
        {
            ImGui::SetCursorScreenPos(ImVec2(origin.x, row1 - 50));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 10));
            ImGui::SetNextItemWidth(320);
            bool enter = ImGui::InputTextWithHint("##label", "Name (empty: the toy's name)", m_label, sizeof(m_label), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopStyleVar();
            ImGui::SameLine(0, 8);
            if (m_app.PillButton("Save the name", true, 160, true) || enter)
            {
                collection::SetLabel(path, m_label);
                m_renaming = false;
                Reload();
            }
            ImGui::SameLine(0, 8);
            ImGui::TextColored(ImColor(Rgb(TextDim)), "The nickname the game shows is in Edit stats");
        }
        if (!m_confirm.empty())
        {
            ImVec2 barAt(origin.x, row1 - 56);
            float barW = size.x - 2 * Pad;
            draw->AddRectFilled(barAt, ImVec2(barAt.x + barW, barAt.y + 46), Rgb(Danger, 60), Radius);
            draw->AddRect(barAt, ImVec2(barAt.x + barW, barAt.y + 46), Rgb(Danger), Radius);
            std::string backup = m_confirm.substr(8);
            draw->AddText(ImVec2(barAt.x + 16, barAt.y + 13), Rgb(Text), fmt::format("Put back the copy from {}? The present state is backed up first.", BackupLabel(backup)).c_str());
            ImGui::SetCursorScreenPos(ImVec2(barAt.x + barW - 250, barAt.y + 3));
            if (m_app.PillButton("Restore", true, 120, true))
            {
                std::string error;
                Toast(collection::Restore(path, backup, &error) ? "Backup restored" : "Couldn't restore it: " + error);
                m_confirm.clear();
                Reload();
            }
            ImGui::SameLine(0, 8);
            if (m_app.PillButton("Cancel", false, 110, true))
                m_confirm.clear();
        }

        ImGui::SetCursorScreenPos(ImVec2(origin.x, row1));
        const collection::Game* game = m_model.Game();
        if (m_app.PillButton("Put on...", true, 130, true))
            ImGui::OpenPopup("##puton");
        if (ImGui::BeginPopup("##puton"))
        {
            ImGui::TextColored(ImColor(Rgb(TextDim)), "Next session, in %s", ShortGameName(m_model.gameId).c_str());
            for (size_t i = 0; game != nullptr && i < game->seats.size(); i++)
            {
                std::string text = SeatName(i);
                if (!m_model.seats[i].empty() && m_model.seats[i] != path)
                    if (const Figure* held = m_model.Find(m_model.seats[i]))
                        text += "  (replaces " + Name(*held) + ")";
                if (ImGui::Selectable((text + "##seat" + std::to_string(i)).c_str(), m_model.SeatOf(path) == int(i), SeatAccepts(game->seats[i], f) ? 0 : ImGuiSelectableFlags_Disabled))
                    PutOn(i, path);
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, 8);
        ImGui::BeginDisabled(!writable || !HasProgress(f) || f.state == figures::AreaState::Damaged);
        if (m_app.PillButton("Edit stats", false, 130, true))
        {
            m_edit = f.stats;
            snprintf(m_nickname, sizeof(m_nickname), "%s", figures::NicknameUtf8(f.stats).c_str());
            m_editing = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, 8);
        if (m_app.PillButton("Rename", false, 110, true))
            m_renaming = !m_renaming;
        ImGui::SameLine(0, 8);
        if (m_app.PillButton(f.favourite ? "Favourite" : "Add to favourites", f.favourite, 180, true))
        {
            collection::SetFavourite(path, !f.favourite);
            Reload();
        }
        ImGui::SameLine(0, 8);
        if (m_app.PillButton("Duplicate", false, 120, true))
        {
            std::string error;
            std::string copy = collection::Duplicate(path, &error);
            Toast(copy.empty() ? "Couldn't duplicate it: " + error : "Duplicated: " + fs::path(copy).filename().string());
            Reload();
        }

        ImGui::SetCursorScreenPos(ImVec2(origin.x, row2));
        ImGui::BeginDisabled(!writable);
        if (m_app.PillButton("Back up now", false, 130, true))
        {
            std::string backup = collection::Backup(path);
            Toast(backup.empty() ? "Couldn't back it up" : "Backed up");
        }
        ImGui::SameLine(0, 8);
        std::vector<std::string> backups = collection::Backups(path);
        ImGui::BeginDisabled(backups.empty());
        if (m_app.PillButton(backups.empty() ? "Restore (no backup)" : "Restore...", false, 170, true))
            ImGui::OpenPopup("##backups");
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("##backups"))
        {
            ImGui::TextColored(ImColor(Rgb(TextDim)), "Put back the copy from");
            for (const std::string& backup : backups)
            {
                Figure old;
                std::string text = BackupLabel(backup);
                if (collection::Read(backup, old) && HasProgress(old) && old.state == figures::AreaState::Valid)
                    text += fmt::format("   level {} · {} gold", old.level, old.stats.gold);
                if (ImGui::Selectable((text + "##" + backup).c_str()))
                    m_confirm = "restore:" + backup;
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, 8);
        if (HoldButton("Reset (hold)", 140))
        {
            std::string error;
            Toast(collection::ResetToNew(path, &error) ? "Reset to new (a backup was kept)" : "Couldn't reset it: " + error);
            Reload();
        }
        ImGui::SameLine(0, 8);
        if (HoldButton("Delete (hold)", 140))
        {
            std::string error;
            if (collection::Delete(path, "deleted", &error))
            {
                if (int seat = m_model.SeatOf(path); seat >= 0)
                    m_model.SetSeat(size_t(seat), "");
                Toast("Moved to the trash (Manage > Trash)");
                Reload();
                CloseOverlay();
                return;
            }
            Toast("Couldn't delete it: " + error);
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, 8);
        if (m_app.PillButton("Folder", false, 90, true))
            { std::string folder = collection::Folder(); OpenFolder(std::filesystem::path(std::u8string(folder.begin(), folder.end()))); }
        ImGui::SameLine(0, 8);
        ImGui::SetCursorPosX(size.x - Pad - 90);
        if (m_app.PillButton("Close", false, 90, true))
            CloseOverlay();

        // A figure held by a game: the lock can be broken when the game is really gone (a crash on another machine...).
        if (f.lock.Busy())
        {
            ImGui::SetCursorScreenPos(ImVec2(right, row1 - 56));
            if (HoldButton("It is not in use any more (hold)", 330))
            {
                collection::BreakLock(path);
                Reload();
            }
        }
    }

    void CollectionPage::DrawEditStats(const Figure& f)
    {
        m_app.Title("EDIT STATS", HeadlineSize - 6, Text);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s · %s. A backup is kept before the change.", Name(f).c_str(), f.variantInfo.name.c_str());
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 8));
        auto label = [](const char* text) {
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", text);
        };
        ImGui::SetNextItemWidth(380);
        int level = figures::LevelForExperience(m_edit.experience);
        label("Level");
        if (ImGui::SliderInt("##level", &level, 1, figures::MaxLevel))
            m_edit.experience = figures::LevelExperience[level - 1];
        int experience = int(m_edit.experience);
        label("Experience");
        ImGui::SetNextItemWidth(380);
        if (ImGui::InputInt("##xp", &experience, 100, 1000))
            m_edit.experience = uint32_t(std::clamp(experience, 0, 0xFFFFFF));
        int gold = m_edit.gold;
        label("Gold");
        ImGui::SetNextItemWidth(380);
        if (ImGui::InputInt("##gold", &gold, 100, 1000))
            m_edit.gold = uint16_t(std::clamp(gold, 0, int(figures::MaxGold)));
        int hero = m_edit.heroPoints;
        label("Hero points");
        ImGui::SetNextItemWidth(380);
        if (ImGui::InputInt("##hero", &hero, 1, 10))
            m_edit.heroPoints = uint16_t(std::clamp(hero, 0, 100));
        label("Nickname (16 characters)");
        ImGui::SetNextItemWidth(380);
        ImGui::InputText("##nick", m_nickname, sizeof(m_nickname));
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0, 14));
        if (m_app.PillButton("Save", true, 140, true))
        {
            figures::FigureStats stats = m_edit;
            figures::SetNicknameUtf8(stats, m_nickname);
            std::string error;
            if (collection::WriteStats(f.path, stats, &error))
            {
                Toast("Saved (a backup was kept)");
                m_editing = false;
                Reload();
            }
            else
                Toast("Couldn't save it: " + error);
        }
        ImGui::SameLine(0, 8);
        if (m_app.PillButton("Cancel", false, 140, true))
            m_editing = false;
    }
}

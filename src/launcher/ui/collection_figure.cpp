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

    namespace
    {
        // The Figure page's words that changed in the polish pass (the rest are still inline, for #4).
        namespace strings
        {
            constexpr const char* Reset = "Reset";
            constexpr const char* ResetHint = "Hold to reset it to new. A backup is kept.";
            constexpr const char* Delete = "Delete";
            constexpr const char* DeleteHint = "Hold to move it to the trash. Manage > Trash puts it back.";
            constexpr const char* Release = "Mark as not in use";
            constexpr const char* ReleaseHint = "Hold if the game that took it has closed or crashed.";
            constexpr const char* Locked = "On the portal in %s. Read-only until the game lets it go.";
        }

        // A hat's name from the first game whose catalogue lists it (Giants' "hats"), else its number.
        std::string HatText(uint16_t hat)
        {
            if (hat == 0)
                return "no hat";
            for (const collection::Game& game : collection::Catalogue::Get().Games())
                if (auto found = game.hats.find(hat); found != game.hats.end())
                    return found->second;
            return fmt::format("hat {}", hat);
        }

        std::string BackupLabel(const std::string& backup)
        {
            std::string stem = fs::path(backup).stem().string(); // 2026-10-24T101500Z
            if (stem.size() >= 16)
                return fmt::format("{} {}:{}", stem.substr(0, 10), stem.substr(11, 2), stem.substr(13, 2));
            return stem;
        }
    }

    void CollectionPage::RefreshDisk()
    {
        if (m_diskGeneration == m_generation)
            return;
        m_diskGeneration = m_generation;
        m_trash = collection::Trash();
        m_starters = size_t(std::count_if(m_model.figures.begin(), m_model.figures.end(), [](const Figure& f) { return collection::UntouchedStarter(f); }));
        m_backups.clear();
    }

    const std::vector<CollectionPage::BackupRow>& CollectionPage::Backups(const std::string& figure)
    {
        RefreshDisk();
        auto found = m_backups.find(figure);
        if (found != m_backups.end())
            return found->second;
        std::vector<BackupRow> rows;
        for (const std::string& backup : collection::Backups(figure))
        {
            Figure old;
            std::string label = BackupLabel(backup);
            if (collection::Read(backup, old) && HasProgress(old) && old.state == figures::AreaState::Valid)
                label += fmt::format("   level {} · {} gold", old.level, old.stats.gold);
            rows.push_back({ backup, label });
        }
        return m_backups[figure] = std::move(rows);
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
            // Level, and the way to the next one: the figure's real level (up to Giants' 15), whatever the game shown.
            uint32_t xp = figures::TotalExperience(f.stats);
            int level = f.level;
            uint32_t from = figures::LevelExperience[std::clamp(level - 1, 0, figures::MaxLevelGiants - 1)];
            uint32_t to = level >= figures::MaxLevelGiants ? from : figures::LevelExperience[level];
            float fraction = to > from ? std::clamp(float(xp - from) / float(to - from), 0.0f, 1.0f) : 1.0f;
            ImGui::PushFont(m_app.m_titleFont, 26);
            ImGui::TextColored(ImColor(Rgb(Text)), "Level %d", level);
            ImGui::PopFont();
            ImVec2 at = ImGui::GetCursorScreenPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(at, ImVec2(at.x + width, at.y + 12), Rgb(Field), 6);
            draw->AddRectFilled(at, ImVec2(at.x + width * fraction, at.y + 12), Rgb(Accent), 6);
            ImGui::Dummy(ImVec2(0, 16));
            std::string next = level >= figures::MaxLevelGiants ? " (maximum level)" : fmt::format(" · {} for level {}", to - xp, level + 1);
            if (level > figures::MaxLevel)
                next += fmt::format(" · level {} in Spyro's Adventure", figures::MaxLevel); // its cap; the figure keeps its levels
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%u XP%s", xp, next.c_str());
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
                row("Hero points", fmt::format("{}   ·   {}   ·   {} upgrades", f.stats.heroPoints, HatText(f.stats.hat), std::popcount(f.stats.upgrades)));
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
            ImGui::TextWrapped(strings::Locked, LiveGame(f).c_str());
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
                collection::SetLabel(path, Trimmed(m_label));
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
            m_edit = m_editBase = f.stats;
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
            m_generation++; // Restore... lists it
        }
        ImGui::SameLine(0, 8);
        const std::vector<BackupRow>& backups = Backups(path);
        ImGui::BeginDisabled(backups.empty());
        if (m_app.PillButton(backups.empty() ? "Restore (no backup)" : "Restore...", false, 170, true))
            ImGui::OpenPopup("##backups");
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("##backups"))
        {
            ImGui::TextColored(ImColor(Rgb(TextDim)), "Put back the copy from");
            for (const BackupRow& backup : backups)
                if (ImGui::Selectable((backup.label + "##" + backup.path).c_str()))
                    m_confirm = "restore:" + backup.path;
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, 8);
        if (m_app.HoldButton(strings::Reset, 140, strings::ResetHint))
        {
            std::string error;
            Toast(collection::ResetToNew(path, &error) ? "Reset to new (a backup was kept)" : "Couldn't reset it: " + error);
            Reload();
        }
        ImGui::SameLine(0, 8);
        bool deleteIt = m_app.HoldButton(strings::Delete, 140, strings::DeleteHint);
        ImGui::EndDisabled();
        if (deleteIt) // after EndDisabled: the page is gone once it is deleted
        {
            DeleteFigure(path);
            return;
        }
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
            if (m_app.HoldButton(strings::Release, 240, strings::ReleaseHint))
            {
                collection::BreakLock(path);
                Reload();
            }
        }
    }

    // Moves the figure to the trash, empties its seat and goes back to the list, focused on the next figure. A refusal is
    // reported once, with its reason, and the page stays.
    void CollectionPage::DeleteFigure(const std::string& path)
    {
        std::string next;
        for (size_t i = 0; i < m_list.size(); i++)
            if (m_list[i].path == path)
            {
                if (i + 1 < m_list.size())
                    next = m_list[i + 1].path;
                else if (i > 0)
                    next = m_list[i - 1].path;
                break;
            }
        std::string error;
        if (!m_model.Delete(path, &error))
        {
            Toast("Couldn't delete it: " + error);
            return;
        }
        m_selected = next;
        m_figure.clear();
        Toast("Moved to the trash (Manage > Trash)");
        m_generation++;
        CloseOverlay();
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
        // The limits of the game the page shows: Spyro's Adventure edits its own part (up to level 10, its hat in the
        // first area), Giants the whole (up to 15) and its hat; the rest of the figure stays as it is.
        const int cap = figures::LevelCap(m_model.gameId);
        ImGui::SetNextItemWidth(380);
        int level = figures::LevelForExperience(figures::ExperienceFor(m_edit, cap), cap);
        label(fmt::format("Level in {}", ShortGameName(m_model.gameId)).c_str());
        if (ImGui::SliderInt("##level", &level, 1, cap, "%d", ImGuiSliderFlags_AlwaysClamp))
            figures::SetExperienceFor(m_edit, cap, figures::LevelExperience[std::clamp(level, 1, cap) - 1]);
        int experience = int(figures::ExperienceFor(m_edit, cap));
        label("Experience");
        ImGui::SetNextItemWidth(380);
        if (ImGui::InputInt("##xp", &experience, 100, 1000))
            figures::SetExperienceFor(m_edit, cap, uint32_t(std::clamp(experience, 0, int(figures::LevelExperience[cap - 1]))));
        // Giants and later: the hat picker, by the game's names (the catalogue's "hats").
        const collection::Game* game = m_model.Game();
        if (game != nullptr && !game->hats.empty())
        {
            label("Hat");
            ImGui::SetNextItemWidth(380);
            if (ImGui::BeginCombo("##hat", m_edit.hat == 0 ? "No hat" : HatText(m_edit.hat).c_str()))
            {
                if (ImGui::Selectable("No hat", m_edit.hat == 0))
                    m_edit.hat = 0;
                for (const auto& [number, name] : game->hats)
                    if (ImGui::Selectable(fmt::format("{}##hat{}", name, number).c_str(), m_edit.hat == number))
                        m_edit.hat = number;
                ImGui::EndCombo();
            }
        }
        label("Gold");
        ImGui::SetNextItemWidth(380);
        char gold[8];
        snprintf(gold, sizeof gold, "%u", unsigned(m_edit.gold));
        auto digitsOnly = [](ImGuiInputTextCallbackData* data) { return data->EventChar >= '0' && data->EventChar <= '9' ? 0 : 1; };
        if (ImGui::InputText("##gold", gold, sizeof gold, ImGuiInputTextFlags_CallbackCharFilter, digitsOnly))
            m_edit.gold = uint16_t(std::min(strtol(gold, nullptr, 10), long(figures::MaxGold)));
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
            // The figure as it is now (a game may have played it since the editor opened), with only the fields changed here.
            Figure now;
            figures::FigureStats current = collection::Read(f.path, now) && now.state == figures::AreaState::Valid ? now.stats : m_editBase;
            figures::FigureStats stats = MergeEditedStats(current, m_editBase, m_edit, figures::NicknameUtf8(m_editBase), m_nickname);
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

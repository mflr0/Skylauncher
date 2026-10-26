// The overlays of the collection page and the figure sheet: the picture, the level for the portal's game and the facts
// that matter; three main actions (Put on the portal, Edit, Favourite), the rest behind "..." (duplicate, back up,
// restore, show the file, reset and delete, held), the technical details behind "Details".
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
        // B / Escape go back one step: out of editing or a confirmation, then out of the overlay. A menu of the sheet
        // closes first, by itself.
        bool menuOpen = ImGui::IsPopupOpen("##puton") || ImGui::IsPopupOpen("##figuremenu");
        if (!menuOpen && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))
        {
            if (m_editing)
                m_editing = false;
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
        // The figure sheet's words that changed in the polish pass (the rest are still inline, for #4).
        namespace strings
        {
            constexpr const char* ResetHint = "Hold to reset it to new. A backup is kept.";
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

        // A two-column table of facts: the name dim, the value as text.
        void FactRows(const char* id, const std::vector<std::pair<std::string, std::string>>& rows)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6, 3));
            if (ImGui::BeginTable(id, 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 120);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                for (const auto& [name, value] : rows)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", value.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
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
            if (collection::Read(backup, old) && collection::HasProgress(old) && old.state == figures::AreaState::Valid)
                label += fmt::format("   level {} · {} gold", old.level, old.stats.gold);
            rows.push_back({ backup, label });
        }
        return m_backups[figure] = std::move(rows);
    }

    // The level for the portal's game, the way to the next one, gold and play time, last played.
    void CollectionPage::DrawFigureFacts(const Figure& f, float width)
    {
        ImGui::BeginGroup();
        if (!collection::HasProgress(f) && f.toy != nullptr)
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s keep no level or gold.", collection::CategoryName(f.toy->category));
        else if (f.state == figures::AreaState::Blank)
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "New: never played yet. Its level shows once a game has used it.");
        else if (f.state == figures::AreaState::Damaged)
            ImGui::TextColored(ImColor(Rgb(Danger)), "Unreadable: its checksums don't match. Restore a backup, or reset it.");
        else
        {
            // "Level 10 (15)": the portal's game's cap first, the figure's own level (a later game's) in brackets. The
            // bar is the figure's own way to its next level.
            uint32_t xp = figures::TotalExperience(f.stats);
            int level = f.level;
            uint32_t from = figures::LevelExperience[std::clamp(level - 1, 0, figures::MaxLevelGiants - 1)];
            uint32_t to = level >= figures::MaxLevelGiants ? from : figures::LevelExperience[level];
            float fraction = to > from ? std::clamp(float(xp - from) / float(to - from), 0.0f, 1.0f) : 1.0f;
            ImGui::PushFont(m_app.m_titleFont, 26);
            ImGui::TextColored(ImColor(Rgb(Text)), "%s", collection::LevelText(f, m_model.gameId).c_str());
            ImGui::PopFont();
            ImVec2 at = ImGui::GetCursorScreenPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(at, ImVec2(at.x + width, at.y + 12), Rgb(Field), 6);
            draw->AddRectFilled(at, ImVec2(at.x + width * fraction, at.y + 12), Rgb(Accent), 6);
            ImGui::Dummy(ImVec2(0, 16));
            std::string next = level >= figures::MaxLevelGiants ? " · maximum level" : fmt::format(" · {} to level {}", to - xp, level + 1);
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%u XP%s", xp, next.c_str());
        }
        ImGui::Dummy(ImVec2(0, 4));
        std::vector<std::pair<std::string, std::string>> rows;
        if (collection::HasProgress(f) && f.state == figures::AreaState::Valid)
            rows.push_back({ "Gold", fmt::format("{}   ·   {} played", f.stats.gold, PlayTime(f.stats.playSeconds)) });
        rows.push_back({ "Last played", f.lastGame.empty() ? std::string("Never") : fmt::format("{} · {}", ShortGameName(f.lastGame), ShortDate(f.lastUsed)) });
        FactRows("##facts", rows);
        ImGui::EndGroup();
    }

    // "Details": what only a curious owner or a bug report needs.
    void CollectionPage::DrawDetails(const Figure& f)
    {
        std::vector<std::pair<std::string, std::string>> rows;
        if (collection::HasProgress(f) && f.state == figures::AreaState::Valid)
        {
            std::string nickname = figures::NicknameUtf8(f.stats);
            rows.push_back({ "Nickname", nickname.empty() ? std::string("None") : nickname });
            rows.push_back({ "Hero points", fmt::format("{}   ·   {}   ·   {} upgrades", f.stats.heroPoints, HatText(f.stats.hat), std::popcount(f.stats.upgrades)) });
        }
        std::string games;
        int count = 0;
        for (const collection::Game& game : collection::Catalogue::Get().Games())
            if (f.toy != nullptr && collection::Catalogue::Get().Accepts(game.id, f.type))
                games += (games.empty() ? "" : ", ") + ShortGameName(game.id), count++;
        rows.push_back({ "Plays in", games.empty() ? std::string("No game this launcher knows") : count == int(collection::Catalogue::Get().Games().size()) ? "every game" : games });
        rows.push_back({ "Toy", fmt::format("{} · type {} · variant {:04X}", f.toy != nullptr ? f.toy->name : "Unknown", f.type, f.variant) });
        rows.push_back({ "File", f.file + (f.origin.empty() ? "" : " · " + f.origin) });
        size_t backups = Backups(f.path).size();
        rows.push_back({ "Backups", backups == 0 ? std::string("None yet") : fmt::format("{}", backups) });
        FactRows("##details", rows);
    }

    void CollectionPage::OpenEdit(const Figure& f)
    {
        m_edit = m_editBase = f.stats;
        snprintf(m_nickname, sizeof(m_nickname), "%s", figures::NicknameUtf8(f.stats).c_str());
        snprintf(m_label, sizeof(m_label), "%s", f.label.c_str());
        m_editing = true;
        m_confirm.clear();
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
        constexpr float Picture = 220;
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

        // Right: name, where it is, the facts, Details.
        float right = origin.x + Picture + 28, textW = size.x - 2 * Pad - Picture - 28;
        ImGui::SetCursorScreenPos(ImVec2(right, origin.y));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(right + textW);
        m_app.Title(Name(f).c_str(), HeadlineSize - 6, Text);
        std::string what = f.toy != nullptr ? f.variantInfo.name : fmt::format("Toy type {}", f.type);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", what.c_str());
        if (f.lock.Busy())
            ImGui::TextColored(ImColor(Rgb(Danger)), strings::Locked, LiveGame(f).c_str());
        else if (int seat = m_model.SeatOf(path); seat >= 0)
            ImGui::TextColored(ImColor(Rgb(Accent)), "%s", fmt::format("On {} at the next start of {}", SeatName(size_t(seat)), ShortGameName(m_model.gameId)).c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 4));
        DrawFigureFacts(f, textW);
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Rgb(PanelRaised));
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Accent));
        if (ImGui::Button(m_details ? "Details  ^" : "Details  v"))
            m_details = !m_details;
        ImGui::PopStyleColor(3);
        m_app.FocusRing(RadiusSmall);
        if (m_details)
            DrawDetails(f);
        ImGui::EndGroup();

        // Actions along the bottom: the three main ones, "...", Close; a confirmation bar above them.
        float bottom = ImGui::GetWindowPos().y + size.y - Pad;
        float row = bottom - 40;
        if (!m_confirm.empty())
        {
            ImVec2 barAt(origin.x, row - 58);
            float barW = size.x - 2 * Pad;
            draw->AddRectFilled(barAt, ImVec2(barAt.x + barW, barAt.y + 48), Rgb(Danger, 50), Radius);
            draw->AddRect(barAt, ImVec2(barAt.x + barW, barAt.y + 48), Rgb(Danger), Radius);
            bool restore = m_confirm.rfind("restore:", 0) == 0;
            std::string text = restore ? fmt::format("Put back the copy from {}? The present state is backed up first.", BackupLabel(m_confirm.substr(8)))
                : m_confirm == "reset" ? fmt::format("Reset {} to new? It loses its level, gold and upgrades; a backup is kept.", Name(f))
                : fmt::format("Move {} to the trash? Manage > Trash puts it back for 30 days.", Name(f));
            ImGui::PushFont(nullptr, SmallSize);
            draw->AddText(ImVec2(barAt.x + 16, barAt.y + 15), Rgb(Text), text.c_str());
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(barAt.x + barW - 290, barAt.y + 4));
            if (restore)
            {
                if (m_app.PillButton("Restore", true, 160, true))
                {
                    std::string error;
                    Toast(collection::Restore(path, m_confirm.substr(8), &error) ? "Backup restored" : "Couldn't restore it: " + error);
                    m_confirm.clear();
                    Reload();
                }
            }
            else if (m_confirm == "reset")
            {
                if (m_app.HoldButton("Hold to reset", 160, strings::ResetHint))
                {
                    std::string error;
                    Toast(collection::ResetToNew(path, &error) ? "Reset to new (a backup was kept)" : "Couldn't reset it: " + error);
                    m_confirm.clear();
                    Reload();
                }
            }
            else if (m_app.HoldButton("Hold to delete", 160, strings::DeleteHint))
            {
                m_confirm.clear();
                DeleteFigure(path);
                return;
            }
            ImGui::SameLine(0, 8);
            if (m_app.PillButton("Cancel", false, 110, true))
                m_confirm.clear();
        }

        ImGui::SetCursorScreenPos(ImVec2(origin.x, row));
        const collection::Game* game = m_model.Game();
        int seated = m_model.SeatOf(path);
        // 1. Put on the portal (of the next start), on a seat it fits.
        ImGui::BeginDisabled(!m_model.CanEditSeats());
        ImVec2 putAt = ImGui::GetCursorScreenPos();
        if (m_app.PillButton("Put on the portal  v", true, 210, true))
            ImGui::OpenPopup("##puton");
        ImGui::EndDisabled();
        if (!m_model.CanEditSeats() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", m_model.RunningGame().empty() ? "This game can't start with prepared seats yet" : "The game is running: use its Toy Box");
        ImGui::SetNextWindowPos(ImVec2(putAt.x, putAt.y), ImGuiCond_Always, ImVec2(0, 1));
        if (ImGui::BeginPopup("##puton"))
        {
            ImGui::TextColored(ImColor(Rgb(TextDim)), "At the next start of %s", ShortGameName(m_model.gameId).c_str());
            ImGui::Separator();
            for (size_t i = 0; game != nullptr && i < game->seats.size(); i++)
            {
                std::string text = SeatName(i);
                if (!m_model.seats[i].empty() && m_model.seats[i] != path)
                    if (const Figure* held = m_model.Find(m_model.seats[i]))
                        text += "  (replaces " + Name(*held) + ")";
                bool fits = SeatAccepts(game->seats[i], f) && (f.toy == nullptr || collection::Catalogue::Get().Accepts(m_model.gameId, f.type));
                if (ImGui::Selectable((text + "##seat" + std::to_string(i)).c_str(), seated == int(i), fits ? 0 : ImGuiSelectableFlags_Disabled))
                    PutOn(i, path);
            }
            if (seated >= 0)
            {
                ImGui::Separator();
                if (ImGui::Selectable("Take it off"))
                    TakeOff(size_t(seated));
            }
            ImGui::EndPopup();
        }
        // 2. Edit: the name, and the stats of a figure that keeps them.
        ImGui::SameLine(0, 8);
        ImGui::BeginDisabled(!writable || f.state == figures::AreaState::Damaged);
        if (m_app.PillButton("Edit", false, 110, true))
            OpenEdit(f);
        ImGui::EndDisabled();
        // 3. Favourite: shown first everywhere.
        ImGui::SameLine(0, 8);
        if (m_app.PillButton(f.favourite ? "Favourite" : "Add to favourites", f.favourite, 180, true))
        {
            collection::SetFavourite(path, !f.favourite);
            Reload();
        }
        // The rest.
        ImGui::SameLine(0, 8);
        ImVec2 moreAt = ImGui::GetCursorScreenPos();
        if (m_app.PillButton("...##figuremore", false, 52, true))
            ImGui::OpenPopup("##figuremenu");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Duplicate, back up, restore, reset, delete");
        ImGui::SetNextWindowPos(ImVec2(moreAt.x, moreAt.y), ImGuiCond_Always, ImVec2(0, 1));
        if (ImGui::BeginPopup("##figuremenu"))
        {
            if (ImGui::MenuItem("Duplicate"))
            {
                std::string error;
                std::string copy = collection::Duplicate(path, &error);
                Toast(copy.empty() ? "Couldn't duplicate it: " + error : "Duplicated: " + fs::path(copy).filename().string());
                Reload();
            }
            if (ImGui::MenuItem("Back up now", nullptr, false, writable))
            {
                std::string backup = collection::Backup(path);
                Toast(backup.empty() ? "Couldn't back it up" : "Backed up");
                m_generation++; // Restore lists it
            }
            const std::vector<BackupRow>& backups = Backups(path);
            if (ImGui::BeginMenu("Restore a backup", writable && !backups.empty()))
            {
                for (const BackupRow& backup : backups)
                    if (ImGui::MenuItem((backup.label + "##" + backup.path).c_str()))
                        m_confirm = "restore:" + backup.path;
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Show in the folder"))
            {
                std::string folder = collection::Folder();
                OpenFolder(std::filesystem::path(std::u8string(folder.begin(), folder.end())));
            }
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Danger));
            if (collection::HasProgress(f) && ImGui::MenuItem("Reset to new...", nullptr, false, writable))
                m_confirm = "reset";
            if (ImGui::MenuItem("Delete...", nullptr, false, writable))
                m_confirm = "delete";
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, 8);
        ImGui::SetCursorPosX(size.x - Pad - 110);
        if (m_app.PillButton("Close", false, 110, true))
            CloseOverlay();

        // A figure held by a game: the lock can be broken when the game is really gone (a crash on another machine...).
        if (f.lock.Busy() && m_confirm.empty())
        {
            ImGui::SetCursorScreenPos(ImVec2(right, row - 52));
            if (m_app.HoldButton(strings::Release, 240, strings::ReleaseHint))
            {
                collection::BreakLock(path);
                Reload();
            }
        }
    }

    // Moves the figure to the trash, empties its seat and goes back to the shelf, focused on the next figure. A refusal
    // is reported once, with its reason, and the sheet stays.
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

    // Edit: the name the lists show, and for a figure that keeps them, its level, experience, gold, hero points, hat and
    // the nickname the game shows.
    void CollectionPage::DrawEditStats(const Figure& f)
    {
        bool stats = collection::HasProgress(f) && f.state != figures::AreaState::Damaged;
        m_app.Title("EDIT", HeadlineSize - 6, Text);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s · %s%s", Name(f).c_str(), f.variantInfo.name.c_str(), stats ? ". A backup is kept before the change." : "");
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 8));
        auto label = [](const char* text) {
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", text);
        };
        // Two columns: who it is (name, level, experience, hat), then what it carries (gold, hero points, nickname).
        constexpr float FieldW = 380, ColumnGap = 36;
        ImGui::BeginGroup();
        label("Name in the lists (empty: the toy's name)");
        ImGui::SetNextItemWidth(FieldW);
        ImGui::InputTextWithHint("##label", f.variantInfo.name.c_str(), m_label, sizeof(m_label));
        if (stats)
        {
            // The limits of the portal's game: Spyro's Adventure edits its own part (up to level 10, its hat in the first
            // area), Giants the whole (up to 15) and its hat; the rest of the figure stays as it is.
            const int cap = figures::LevelCap(m_model.gameId);
            int level = figures::LevelForExperience(figures::ExperienceFor(m_edit, cap), cap);
            label(fmt::format("Level in {}", ShortGameName(m_model.gameId)).c_str());
            ImGui::SetNextItemWidth(FieldW);
            if (ImGui::SliderInt("##level", &level, 1, cap, "%d", ImGuiSliderFlags_AlwaysClamp))
                figures::SetExperienceFor(m_edit, cap, figures::LevelExperience[std::clamp(level, 1, cap) - 1]);
            int experience = int(figures::ExperienceFor(m_edit, cap));
            label("Experience");
            ImGui::SetNextItemWidth(FieldW);
            if (ImGui::InputInt("##xp", &experience, 100, 1000))
                figures::SetExperienceFor(m_edit, cap, uint32_t(std::clamp(experience, 0, int(figures::LevelExperience[cap - 1]))));
            // Giants and later: the hat picker, by the game's names (the catalogue's "hats").
            const collection::Game* game = m_model.Game();
            if (game != nullptr && !game->hats.empty())
            {
                label("Hat");
                ImGui::SetNextItemWidth(FieldW);
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
        }
        ImGui::EndGroup();
        if (stats)
        {
            ImGui::SameLine(0, ColumnGap);
            ImGui::BeginGroup();
            label("Gold");
            ImGui::SetNextItemWidth(FieldW);
            char gold[8];
            snprintf(gold, sizeof gold, "%u", unsigned(m_edit.gold));
            auto digitsOnly = [](ImGuiInputTextCallbackData* data) { return data->EventChar >= '0' && data->EventChar <= '9' ? 0 : 1; };
            if (ImGui::InputText("##gold", gold, sizeof gold, ImGuiInputTextFlags_CallbackCharFilter, digitsOnly))
                m_edit.gold = uint16_t(std::min(strtol(gold, nullptr, 10), long(figures::MaxGold)));
            int hero = m_edit.heroPoints;
            label("Hero points");
            ImGui::SetNextItemWidth(FieldW);
            if (ImGui::InputInt("##hero", &hero, 1, 10))
                m_edit.heroPoints = uint16_t(std::clamp(hero, 0, 100));
            label("Nickname in the game (16 characters)");
            ImGui::SetNextItemWidth(FieldW);
            ImGui::InputText("##nick", m_nickname, sizeof(m_nickname));
            ImGui::EndGroup();
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0, 14));
        if (m_app.PillButton("Save", true, 140, true))
        {
            std::string error;
            bool saved = true;
            if (Trimmed(m_label) != f.label)
                saved = collection::SetLabel(f.path, Trimmed(m_label));
            if (saved && stats)
            {
                // The figure as it is now (a game may have played it since the editor opened), with only the fields
                // changed here.
                Figure now;
                figures::FigureStats current = collection::Read(f.path, now) && now.state == figures::AreaState::Valid ? now.stats : m_editBase;
                figures::FigureStats edited = MergeEditedStats(current, m_editBase, m_edit, figures::NicknameUtf8(m_editBase), m_nickname);
                // Only the name changed: no write to the figure (and no backup of it).
                bool changed = m_edit.experience != m_editBase.experience || m_edit.giantsExperience != m_editBase.giantsExperience ||
                    m_edit.gold != m_editBase.gold || m_edit.heroPoints != m_editBase.heroPoints || m_edit.hat != m_editBase.hat ||
                    figures::NicknameUtf8(m_editBase) != Trimmed(m_nickname);
                if (changed)
                    saved = collection::WriteStats(f.path, edited, &error);
            }
            if (saved)
            {
                Toast(stats ? "Saved (a backup was kept)" : "Saved");
                m_editing = false;
                Reload();
            }
            else
                Toast("Couldn't save it: " + (error.empty() ? std::string("the collection's index can't be written") : error));
        }
        ImGui::SameLine(0, 8);
        if (m_app.PillButton("Cancel", false, 140, true))
            m_editing = false;
    }
}

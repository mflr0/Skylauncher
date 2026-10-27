// The overlays of the collection page and the figure sheet: the picture, the level for the portal's game and the facts
// that matter; three main actions (Put on the portal, Edit, Favourite), the rest behind "..." (duplicate, back up,
// restore, show the file, reset and delete, held), the technical details behind "Details".
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/launcher_ui.h"
#include "shared/collection/upgrades.h"
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
            else if (!m_changeKeep.empty())
                m_changeKeep.clear();
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
            constexpr const char* ResetHint = N_("Hold to reset it.");
            constexpr const char* DeleteHint = N_("Hold to move it to the trash.");
            constexpr const char* Release = N_("Mark as not in use");
            constexpr const char* ReleaseHint = N_("Hold if its game has quit.");
        }

        // A hat's name from the first game whose catalogue lists it (Giants' "hats"), else its number.
        std::string HatText(uint16_t hat)
        {
            if (hat == 0)
                return Tr("no hat");
            for (const collection::Game& game : collection::Catalogue::Get().Games())
                if (auto found = game.hats.find(hat); found != game.hats.end())
                    return found->second;
            return Tf("hat {}", hat);
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
                label += Tf("   level {} · {} gold", old.level, old.stats.gold);
            rows.push_back({ backup, label });
        }
        return m_backups[figure] = std::move(rows);
    }

    // The level for the portal's game, the way to the next one, gold and play time, last played.
    void CollectionPage::DrawFigureFacts(const Figure& f, float width)
    {
        ImGui::BeginGroup();
        if (!collection::HasProgress(f) && f.toy != nullptr)
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tf("{} keep no level or gold.", CategoryLabel(f.toy->category)).c_str());
        else if (f.state == figures::AreaState::Blank)
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tr("New: never played."));
        else if (f.state == figures::AreaState::Damaged)
            ImGui::TextColored(ImColor(Rgb(Danger)), "%s", Tr("Unreadable. Restore a backup or reset it."));
        else
        {
            // "Level 10 (15)": the portal's game's cap first, the figure's own level (a later game's) in brackets. The
            // bar is the figure's own way to its next level.
            uint32_t xp = figures::TotalExperience(f.stats);
            int level = f.level;
            uint32_t from = figures::LevelExperience[std::clamp(level - 1, 0, figures::MaxLevelSwapForce - 1)];
            uint32_t to = level >= figures::MaxLevelSwapForce ? from : figures::LevelExperience[level];
            float fraction = to > from ? std::clamp(float(xp - from) / float(to - from), 0.0f, 1.0f) : 1.0f;
            ImGui::PushFont(m_app.m_titleFont, 26);
            ImGui::TextColored(ImColor(Rgb(Text)), "%s", collection::LevelText(f, m_model.gameId, French()).c_str());
            ImGui::PopFont();
            ImVec2 at = ImGui::GetCursorScreenPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(at, ImVec2(at.x + width, at.y + 12), Rgb(Field), 6);
            draw->AddRectFilled(at, ImVec2(at.x + width * fraction, at.y + 12), Rgb(Accent), 6);
            ImGui::Dummy(ImVec2(0, 16));
            std::string next = level >= figures::MaxLevelSwapForce ? std::string(Tr(" · maximum level")) : Tf(" · {} to level {}", to - xp, level + 1);
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%u XP%s", xp, next.c_str());
        }
        ImGui::Dummy(ImVec2(0, 4));
        std::vector<std::pair<std::string, std::string>> rows;
        if (collection::HasProgress(f) && f.state == figures::AreaState::Valid)
            rows.push_back({ Tr("Gold"), Tf("{}   ·   {} played", f.stats.gold, PlayTime(f.stats.playSeconds)) });
        rows.push_back({ Tr("Last played"), f.lastGame.empty() || f.state == figures::AreaState::Blank ? std::string(Tr("Never")) : fmt::format("{} · {}", ShortGameName(f.lastGame), ShortDate(f.lastUsed)) });
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
            rows.push_back({ Tr("Nickname"), nickname.empty() ? std::string(Tr("None")) : nickname });
            rows.push_back({ Tr("Hero points"), Tf("{}   ·   {}   ·   {} upgrades", f.stats.heroPoints, HatText(f.stats.hat), std::popcount(uint16_t(collection::EditorBits(f.stats.upgrades, f.stats.wowPow) & 0x07FF))) });
        }
        std::string games;
        int count = 0;
        for (const collection::Game& game : collection::Catalogue::Get().Games())
            if (f.toy != nullptr && collection::Catalogue::Get().Accepts(game.id, f.type))
                games += (games.empty() ? "" : ", ") + ShortGameName(game.id), count++;
        rows.push_back({ Tr("Plays in"), games.empty() ? std::string(Tr("No game this launcher knows")) : count == int(collection::Catalogue::Get().Games().size()) ? std::string(Tr("every game")) : games });
        rows.push_back({ Tr("Toy"), Tf("{} · type {} · variant {:04X}", f.toy != nullptr ? f.toy->name : std::string(Tr("Unknown")), f.type, f.variant) });
        rows.push_back({ Tr("File"), f.file + (f.origin.empty() ? "" : " · " + f.origin) });
        if (collection::Card card = CardOf(f); card.Swapper())
            rows.push_back({ Tr("Bottom file"), (card.first == &f ? card.second : card.first)->file });
        size_t backups = Backups(f.path).size();
        rows.push_back({ Tr("Backups"), backups == 0 ? std::string(Tr("None yet")) : fmt::format("{}", backups) });
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
        if (!m_changeKeep.empty())
        {
            DrawChangeHalf(size);
            return;
        }
        Figure f = *found; // actions below reload the collection
        const std::string path = f.path;
        const collection::Card card = CardOf(f); // f is the swapper's top, or a figure (also a half on its own)
        bool writable = Writable(f);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Left: the picture and what kind of toy it is.
        constexpr float Picture = 220;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        draw->AddRectFilled(origin, ImVec2(origin.x + Picture, origin.y + Picture), Rgb(Field), RadiusLarge);
        DrawPicture(draw, f.toy, f.Kind(), f.variant, ImVec2(origin.x + 6, origin.y + 6), Picture - 12);
        if (card.Swapper())
            DrawBottom(draw, *card.second, ImVec2(origin.x + Picture - 76, origin.y + Picture - 76), 68);
        float badgeX = origin.x;
        auto badge = [&](const std::string& text, unsigned fill, unsigned ink) {
            ImGui::PushFont(nullptr, SmallSize - 1);
            float w = ImGui::CalcTextSize(text.c_str()).x + 12;
            ImGui::PopFont();
            m_app.Badge(draw, ImVec2(badgeX, origin.y + Picture + 10), text.c_str(), fill, ink);
            badgeX += w + 6;
        };
        if (f.Kind() != collection::VariantKind::Base)
            badge(KindLabel(f.Kind()), collection::VariantKindRgb(f.Kind()), AccentInk);
        if (f.toy != nullptr && f.toy->element != collection::Element::None)
            badge(ElementName(f.toy->element), PanelRaised, Text);
        if (card.Swapper())
            badge(Tr("Swapper"), PanelRaised, Text);
        else if (f.toy != nullptr)
            badge(CategoryLabel(f.toy->category), PanelRaised, Text);

        // Right: name, where it is, the facts, Details.
        float right = origin.x + Picture + 28, textW = size.x - 2 * Pad - Picture - 28;
        ImGui::SetCursorScreenPos(ImVec2(right, origin.y));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(right + textW);
        m_app.Title(Name(f).c_str(), HeadlineSize - 6, Text);
        std::string what = f.toy != nullptr ? f.variantInfo.name : Tf("Toy type {}", f.type);
        if (card.Swapper())
            what = Tf("Top: {} · bottom: {}", WithoutHalf(f.variantInfo.name), WithoutHalf(card.second->variantInfo.name));
        if (what != Name(f)) // the second line only when it says more (a nickname over the toy's name)
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", what.c_str());
        if (card.Busy())
            ImGui::TextColored(ImColor(Rgb(Danger)), "%s", Tf("On the portal in {}: read-only", LiveGame(f)).c_str());
        else if (int seat = m_model.SeatOf(path); seat >= 0)
            ImGui::TextColored(ImColor(Rgb(Accent)), "%s", Tf("On {} at the next start of {}", SeatName(size_t(seat)), ShortGameName(m_model.gameId)).c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 4));
        DrawFigureFacts(f, textW);
        ImGui::Dummy(ImVec2(0, 2));
        // A swapper changes one half at a time ("Change the bottom": mixing stays possible); a half on its own looks for its partner.
        if (f.IsSwapTop() || f.IsSwapBottom())
        {
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::BeginDisabled(!writable);
            if (f.IsSwapTop() && m_app.PillButton(Tr("Change the bottom"), false, 210, true))
                m_changeKeep = f.path;
            if (f.IsSwapTop() && card.Swapper())
                ImGui::SameLine(0, 8);
            if (card.Swapper() && m_app.PillButton(Tr("Change the top"), false, 190, true))
                m_changeKeep = card.second->path;
            if (f.IsSwapBottom() && !card.Swapper() && m_app.PillButton(Tr("Change the top"), false, 190, true))
                m_changeKeep = f.path;
            ImGui::EndDisabled();
            ImGui::Dummy(ImVec2(0, 4));
        }
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Rgb(PanelRaised));
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Accent));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
        ImVec2 detailsSize(m_app.PillWidth(Tr("Details"), 0, false, 1), 0);
        if (ImGui::Button(Tr("Details"), detailsSize))
            m_details = !m_details;
        ImGui::PopStyleColor();
        m_app.DrawLabelChevron(Tr("Details"), Accent, m_details ? -1 : 1, false);
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
            std::string text = restore ? Tf("Put back the copy from {}?", BackupLabel(m_confirm.substr(8)))
                : m_confirm == "reset" ? Tf("Reset {} to new? Level, gold and upgrades are lost.", Name(f))
                : Tf("Move {} to the trash?", Name(f));
            // The buttons' real widths (a longer translation widens the first one), so they are laid out from the bar's right
            // edge inwards: 8 px of margin, 8 px between them. The message stops before them.
            float firstW = restore ? m_app.PillWidth(Tr("Restore"), 160, true)
                : m_confirm == "reset" ? m_app.PillWidth(Tr("Hold to reset"), 160, true)
                : m_app.PillWidth(Tr("Hold to delete"), 160, true);
            float cancelW = m_app.PillWidth(Tr("Cancel"), 110, true);
            float btnX = barAt.x + barW - 8 - (firstW + 8 + cancelW);
            ImGui::PushFont(nullptr, SmallSize);
            draw->PushClipRect(ImVec2(barAt.x + 16, barAt.y), ImVec2(btnX - 12, barAt.y + 48), true);
            draw->AddText(ImVec2(barAt.x + 16, barAt.y + 15), Rgb(Text), text.c_str());
            draw->PopClipRect();
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(btnX, barAt.y + 4));
            if (restore)
            {
                if (m_app.PillButton(Tr("Restore"), true, 160, true))
                {
                    std::string error;
                    Toast(collection::Restore(path, m_confirm.substr(8), &error) ? Tr("Backup restored") : Tr("Couldn't restore it: ") + error);
                    m_confirm.clear();
                    Reload();
                }
            }
            else if (m_confirm == "reset")
            {
                if (m_app.HoldButton(Tr("Hold to reset"), 160, Tr(strings::ResetHint)))
                {
                    std::string error;
                    Toast(collection::ResetToNew(path, &error) ? Tr("Reset to new") : Tr("Couldn't reset it: ") + error);
                    m_confirm.clear();
                    Reload();
                }
            }
            else if (m_app.HoldButton(Tr("Hold to delete"), 160, Tr(strings::DeleteHint)))
            {
                m_confirm.clear();
                DeleteFigure(path);
                return;
            }
            ImGui::SameLine(0, 8);
            if (m_app.PillButton(Tr("Cancel"), false, 110, true))
                m_confirm.clear();
        }

        ImGui::SetCursorScreenPos(ImVec2(origin.x, row));
        const collection::Game* game = m_model.Game();
        int seated = m_model.SeatOf(path);
        // 1. Put on the portal (of the next start), on a seat it fits.
        ImGui::BeginDisabled(!m_model.CanEditSeats());
        ImVec2 putAt = ImGui::GetCursorScreenPos();
        if (m_app.PillButton(Tr("Put on the portal"), true, 210, true, PanelRaised, PanelRaisedHover, PanelRaisedActive, 1))
            ImGui::OpenPopup("##puton");
        ImGui::EndDisabled();
        if (!m_model.CanEditSeats() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", m_model.RunningGame().empty() ? Tr("Not available for this game yet") : Tr("The game is running: use its Toy Box"));
        ImGui::SetNextWindowPos(ImVec2(putAt.x, putAt.y), ImGuiCond_Always, ImVec2(0, 1));
        if (ImGui::BeginPopup("##puton"))
        {
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", Tf("At the next start of {}", ShortGameName(m_model.gameId)).c_str());
            ImGui::Separator();
            for (size_t i = 0; game != nullptr && i < game->seats.size(); i++)
            {
                std::string text = SeatName(i);
                if (!m_model.seats[i].empty() && m_model.seats[i] != path)
                    if (const Figure* held = m_model.Find(m_model.seats[i]))
                        text += Tf("  (replaces {})", Name(*held));
                bool fits = SeatAccepts(game->seats[i], f) && (f.toy == nullptr || collection::Catalogue::Get().Accepts(m_model.gameId, f.type));
                if (ImGui::Selectable((text + "##seat" + std::to_string(i)).c_str(), seated == int(i), fits ? 0 : ImGuiSelectableFlags_Disabled))
                    PutOn(i, path);
            }
            if (seated >= 0)
            {
                ImGui::Separator();
                if (ImGui::Selectable(Tr("Take it off")))
                    TakeOff(size_t(seated));
            }
            ImGui::EndPopup();
        }
        // 2. Edit: the name, and the stats of a figure that keeps them.
        ImGui::SameLine(0, 8);
        ImGui::BeginDisabled(!writable || f.state == figures::AreaState::Damaged);
        if (m_app.PillButton(Tr("Edit"), false, 110, true))
            OpenEdit(f);
        ImGui::EndDisabled();
        // 3. Favourite: shown first everywhere.
        ImGui::SameLine(0, 8);
        if (m_app.PillButton(f.favourite ? Tr("Favourite") : Tr("Add to favourites"), f.favourite, 180, true))
        {
            collection::SetFavourite(path, !f.favourite);
            Reload();
        }
        // The rest.
        ImGui::SameLine(0, 8);
        ImVec2 moreAt = ImGui::GetCursorScreenPos();
        if (m_app.PillButton((std::string("...") + "##figuremore").c_str(), false, 52, true))
            ImGui::OpenPopup("##figuremenu");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("More actions"));
        ImGui::SetNextWindowPos(ImVec2(moreAt.x, moreAt.y), ImGuiCond_Always, ImVec2(0, 1));
        if (ImGui::BeginPopup("##figuremenu"))
        {
            if (ImGui::MenuItem(Tr("Duplicate")))
            {
                std::string error;
                std::string copy = collection::Duplicate(path, &error);
                Toast(copy.empty() ? Tr("Couldn't duplicate it: ") + error : Tr("Duplicated: ") + fs::path(copy).filename().string());
                Reload();
            }
            if (ImGui::MenuItem(Tr("Back up now"), nullptr, false, writable))
            {
                std::string backup = collection::Backup(path);
                Toast(backup.empty() ? Tr("Couldn't back it up") : Tr("Backed up"));
                m_generation++; // Restore lists it
            }
            const std::vector<BackupRow>& backups = Backups(path);
            if (ImGui::BeginMenu(Tr("Restore a backup"), writable && !backups.empty()))
            {
                for (const BackupRow& backup : backups)
                    if (ImGui::MenuItem((backup.label + "##" + backup.path).c_str()))
                        m_confirm = "restore:" + backup.path;
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(Tr("Show in folder")))
            {
                std::string folder = collection::Folder();
                OpenFolder(std::filesystem::path(std::u8string(folder.begin(), folder.end())));
            }
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(Danger));
            if (collection::HasProgress(f) && ImGui::MenuItem(Tr("Reset to new..."), nullptr, false, writable))
                m_confirm = "reset";
            if (ImGui::MenuItem(Tr("Delete..."), nullptr, false, writable))
                m_confirm = "delete";
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, 8);
        ImGui::SetCursorPosX(size.x - Pad - m_app.PillWidth(Tr("Close"), 110, true));
        if (m_app.PillButton(Tr("Close"), false, 110, true))
            CloseOverlay();

        // A figure held by a game: the lock can be broken when the game is really gone (a crash on another machine...).
        if (card.Busy() && m_confirm.empty())
        {
            ImGui::SetCursorScreenPos(ImVec2(right, row - 52));
            if (m_app.HoldButton(Tr(strings::Release), 240, Tr(strings::ReleaseHint)))
            {
                for (const Figure* half : card.Halves())
                    collection::BreakLock(half->path);
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
        if (!m_model.DeleteCard(path, &error))
        {
            Toast(Tr("Couldn't delete it: ") + error);
            return;
        }
        m_selected = next;
        m_figure.clear();
        Toast(Tr("Moved to the trash"));
        m_generation++;
        CloseOverlay();
    }

    // "Change the bottom" / "Change the top": the card keeps `keep` and takes `replacement` as its other half. A card on a seat
    // stays there: the seat is written again, so the next start puts the new half on the portal.
    void CollectionPage::ChangeHalf(const Figure& keep, const Figure& replacement)
    {
        const Figure* oldTop = keep.IsSwapTop() ? &keep : CardOf(keep).first;
        const int seat = m_model.SeatOf(oldTop->path);
        const std::string seated = oldTop->path, newTop = keep.IsSwapTop() ? keep.path : replacement.path;
        // Off the seat while the halves change: the portal file must not keep the old bottom, which would read as a seat of its own.
        if (seat >= 0)
            m_model.SetSeat(size_t(seat), "");
        std::string error;
        if (!collection::ChangeHalf(keep.path, replacement.path, &error))
        {
            if (seat >= 0)
                m_model.SetSeat(size_t(seat), seated);
            Toast(Tr("Couldn't change it: ") + error);
            return;
        }
        Reload();
        if (seat >= 0)
            m_model.SetSeat(size_t(seat), newTop);
        if (const Figure* now = m_model.Find(keep.path))
            m_figure = CardOf(*now).first->path;
        m_changeKeep.clear();
        Reload();
        Toast(Tr("Changed"));
    }

    // The list of the other halves to take: one row each, the ones in use or on a seat left out; a half that belongs to another
    // swapper says so (that swapper takes the old half in exchange).
    void CollectionPage::DrawChangeHalf(ImVec2 size)
    {
        const Figure* keep = m_model.Find(m_changeKeep);
        if (keep == nullptr || !(keep->IsSwapTop() || keep->IsSwapBottom()))
        {
            m_changeKeep.clear();
            return;
        }
        const bool wantBottom = keep->IsSwapTop();
        collection::Card keepCard = CardOf(*keep);
        const Figure* current = keepCard.Swapper() ? (wantBottom ? keepCard.second : keepCard.first) : nullptr;
        m_app.Title(wantBottom ? Tr("CHANGE THE BOTTOM") : Tr("CHANGE THE TOP"), HeadlineSize - 6, Text);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tf("{}: pick the new {}", Name(*keep), wantBottom ? Tr("bottom") : Tr("top")).c_str());
        ImGui::Dummy(ImVec2(0, 8));

        // The candidates: lone halves first.
        std::vector<const Figure*> rows;
        for (const Figure& half : m_model.figures)
        {
            if (!(wantBottom ? half.IsSwapBottom() : half.IsSwapTop()) || (current != nullptr && half.path == current->path))
                continue;
            collection::Card card = CardOf(half);
            if (card.Busy() || m_model.SeatOf(card.first->path) >= 0 || collection::UntouchedStarter(half))
                continue;
            rows.push_back(&half);
        }
        std::stable_partition(rows.begin(), rows.end(), [&](const Figure* half) { return half->partner.empty(); });

        ImGui::BeginChild("##changehalf", ImVec2(0, std::max(160.0f, ImGui::GetContentRegionAvail().y - 64)), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        ImGui::SetScrollX(0.0f);
        constexpr float Row = 54;
        const Figure* chosen = nullptr;
        for (const Figure* half : rows)
        {
            ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::PushID(half->path.c_str());
            if (ImGui::Selectable("##half", false, 0, ImVec2(0, Row)))
                chosen = half;
            ImGui::PopID();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            DrawIcon(draw, half->toy, half->Kind(), ImVec2(at.x + 6, at.y + 3), Row - 6, false);
            collection::Card card = CardOf(*half);
            ImGui::PushFont(m_app.m_boldFont, TextSize - 1);
            draw->AddText(ImVec2(at.x + Row + 10, at.y + 6), Rgb(Text), (half->label.empty() ? WithoutHalf(half->DisplayName()) : half->label).c_str());
            ImGui::PopFont();
            ImGui::PushFont(nullptr, SmallSize - 1);
            std::string note = card.Swapper() ? Tf("Now with {}", WithoutHalf((half->IsSwapTop() ? card.second : card.first)->DisplayName())) : std::string(Tr("On its own"));
            draw->AddText(ImVec2(at.x + Row + 10, at.y + 29), Rgb(TextDim), note.c_str());
            ImGui::PopFont();
        }
        if (rows.empty())
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", wantBottom ? Tr("No other bottom to take.") : Tr("No other top to take."));
        ImGui::EndChild();
        if (chosen != nullptr)
        {
            Figure keepNow = *keep, replacement = *chosen; // the call reloads the collection
            ChangeHalf(keepNow, replacement);
            return;
        }
        ImGui::Dummy(ImVec2(0, 8));
        if (m_app.PillButton(Tr("Cancel"), false, 140, true))
            m_changeKeep.clear();
    }

    // The toy's upgrades by name (collection::Upgrades): the basic ones, the two paths side by side (the chosen one lit, the
    // other greyed), the Soul Gem and the Wow Pow under the basic ones; the focused one's description below. Same rules and
    // order as the Toy Box (collection/upgrades.h).
    void CollectionPage::DrawUpgradeEditor(const Figure& f)
    {
        if (f.toy == nullptr)
            return;
        std::string language = French() ? "fr" : "en";
        collection::UpgradeTable table = collection::Upgrades(*f.toy, m_model.gameId, language);
        if (table.upgrades.empty())
            return;
        // The editors' bits: the upgrade field with bit 10 = the Wow Pow, which the figure keeps in its extra area.
        uint16_t bits = collection::EditorBits(m_edit.upgrades, m_edit.wowPow);
        auto setBits = [&](uint16_t edited) {
            bits = edited;
            collection::StoreEditorBits(edited, m_edit.upgrades, m_edit.wowPow);
        };
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", Tr("Upgrades"));
        ImGui::SameLine(0, 12);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Tf("{} of {}", collection::BoughtCount(bits, table), table.upgrades.size()).c_str());
        ImGui::SameLine(0, 16);
        if (ImGui::SmallButton(Tr("All")))
            setBits(collection::AllUpgrades(bits, table));
        ImGui::SameLine(0, 6);
        if (ImGui::SmallButton(Tr("None")))
            setBits(collection::NoUpgrades(bits));
        constexpr float Gap = 10, Row = 40;
        float columnW = (ImGui::GetContentRegionAvail().x - 2 * Gap) / 3;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        bool chosen = (bits >> collection::UpgradePathChosenBit & 1) != 0, second = (bits >> collection::UpgradePathSecondBit & 1) != 0;
        const std::string titles[3] = { Tr("Basic"), table.pathNames[0], table.pathNames[1] };
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImGui::PushFont(m_app.m_boldFont, SmallSize);
        for (int column = 0; column < 3; column++)
        {
            bool lit = column == 0 || (chosen && second == (column == 2));
            draw->AddText(ImVec2(origin.x + column * (columnW + Gap), origin.y + 4), Rgb(lit ? Accent : TextDim), titles[column].c_str());
        }
        ImGui::PopFont();
        int rows[3] = { 0, 0, 0 };
        float bottom = origin.y;
        std::string hint;
        for (size_t i = 0; i < table.upgrades.size(); i++)
        {
            const collection::Upgrade& upgrade = table.upgrades[i];
            using collection::UpgradePath;
            int column = upgrade.path == UpgradePath::First ? 1 : upgrade.path == UpgradePath::Second ? 2 : 0;
            ImVec2 at(origin.x + column * (columnW + Gap), origin.y + 26 + rows[column]++ * (Row + 6));
            ImGui::SetCursorScreenPos(at);
            ImGui::PushID(int(i));
            if (ImGui::InvisibleButton("##upgrade", ImVec2(columnW, Row), ImGuiButtonFlags_EnableNav))
                setBits(collection::ToggleUpgrade(bits, upgrade));
            ImGui::PopID();
            bool hovered = ImGui::IsItemHovered(), focused = ImGui::IsItemFocused();
            m_app.FocusRing(Radius);
            bool bought = collection::UpgradeBought(bits, upgrade), greyed = collection::UpgradeGreyed(bits, upgrade);
            draw->AddRectFilled(at, ImVec2(at.x + columnW, at.y + Row), Rgb(hovered || focused ? PanelRaised : Field), Radius);
            ImVec2 mark(at.x + 20, at.y + Row / 2);
            draw->AddCircleFilled(mark, 9, Rgb(bought ? Accent : PanelRaisedActive));
            if (bought)
            {
                draw->AddLine(ImVec2(mark.x - 4, mark.y), ImVec2(mark.x - 1, mark.y + 3), Rgb(AccentInk), 2.0f);
                draw->AddLine(ImVec2(mark.x - 1, mark.y + 3), ImVec2(mark.x + 5, mark.y - 4), Rgb(AccentInk), 2.0f);
            }
            std::string name = LauncherMiddleEllipsis(collection::UpgradeName(upgrade.name, language), 26);
            std::string price = Tf("{} gold", upgrade.price);
            ImGui::PushFont(nullptr, SmallSize);
            float priceW = ImGui::CalcTextSize(price.c_str()).x;
            draw->AddText(ImVec2(at.x + columnW - priceW - 10, at.y + (Row - ImGui::GetFontSize()) / 2), Rgb(bought ? Accent : TextDim), price.c_str());
            ImGui::PopFont();
            ImGui::PushFont(m_app.m_boldFont, SmallSize + 1);
            draw->AddText(ImVec2(at.x + 38, at.y + (Row - ImGui::GetFontSize()) / 2), Rgb(greyed ? TextDim : Text), name.c_str());
            ImGui::PopFont();
            if (hovered || focused)
                hint = collection::UpgradeLine(upgrade.description, language);
            bottom = std::max(bottom, at.y + Row);
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom + 8));
        ImGui::Dummy(ImVec2(columnW, 0));
        ImGui::PushFont(nullptr, SmallSize);
        ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", hint.empty() ? " " : hint.c_str());
        ImGui::PopFont();
    }

    // Edit: the name the lists show, and for a figure that keeps them, its level, experience, gold, hero points, hat and
    // the nickname the game shows.
    void CollectionPage::DrawEditStats(const Figure& f)
    {
        bool stats = collection::HasProgress(f) && f.state != figures::AreaState::Damaged;
        m_app.Title(Tr("EDIT"), HeadlineSize - 6, Text);
        if (f.variantInfo.name != Name(f))
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s · %s", Name(f).c_str(), f.variantInfo.name.c_str());
        else
            ImGui::TextColored(ImColor(Rgb(TextSecondary)), "%s", Name(f).c_str());
        ImGui::Dummy(ImVec2(0, 8));
        // The fields scroll above the Save row (the upgrades list makes the sheet taller than a small window).
        ImGui::BeginChild("##editbody", ImVec2(0, std::max(200.0f, ImGui::GetContentRegionAvail().y - 64)), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        ImGui::SetScrollX(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 8));
        auto label = [](const char* text) {
            ImGui::TextColored(ImColor(Rgb(TextDim)), "%s", text);
        };
        // Two columns: who it is (name, level, experience, hat), then what it carries (gold, hero points, nickname).
        constexpr float FieldW = 380, ColumnGap = 36;
        ImGui::BeginGroup();
        label(Tr("Name (empty: the toy's name)"));
        ImGui::SetNextItemWidth(FieldW);
        ImGui::InputTextWithHint("##label", f.variantInfo.name.c_str(), m_label, sizeof(m_label));
        if (stats)
        {
            // The limits of the portal's game: Spyro's Adventure edits its own part (up to level 10, its hat in the first
            // area), Giants the first two parts (up to 15) and its hat, Swap Force all three (up to 20); the rest of the figure stays as it is.
            const int cap = figures::LevelCap(m_model.gameId);
            int level = figures::LevelForExperience(figures::ExperienceFor(m_edit, cap), cap);
            label(Tf("Level in {}", ShortGameName(m_model.gameId)).c_str());
            ImGui::SetNextItemWidth(FieldW);
            if (ImGui::SliderInt("##level", &level, 1, cap, "%d", ImGuiSliderFlags_AlwaysClamp))
                figures::SetExperienceFor(m_edit, cap, figures::LevelExperience[std::clamp(level, 1, cap) - 1]);
            int experience = int(figures::ExperienceFor(m_edit, cap));
            label(Tr("Experience"));
            ImGui::SetNextItemWidth(FieldW);
            if (ImGui::InputInt("##xp", &experience, 100, 1000))
                figures::SetExperienceFor(m_edit, cap, uint32_t(std::clamp(experience, 0, int(figures::LevelExperience[cap - 1]))));
            // Giants and later: the hat picker, by the game's names (the catalogue's "hats").
            const collection::Game* game = m_model.Game();
            if (game != nullptr && !game->hats.empty())
            {
                label(Tr("Hat"));
                ImGui::SetNextItemWidth(FieldW);
                if (ImGui::BeginCombo("##hat", m_edit.hat == 0 ? Tr("No hat") : HatText(m_edit.hat).c_str()))
                {
                    if (ImGui::Selectable(Tr("No hat"), m_edit.hat == 0))
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
            label(Tr("Gold"));
            ImGui::SetNextItemWidth(FieldW);
            char gold[8];
            snprintf(gold, sizeof gold, "%u", unsigned(m_edit.gold));
            auto digitsOnly = [](ImGuiInputTextCallbackData* data) { return data->EventChar >= '0' && data->EventChar <= '9' ? 0 : 1; };
            if (ImGui::InputText("##gold", gold, sizeof gold, ImGuiInputTextFlags_CallbackCharFilter, digitsOnly))
                m_edit.gold = uint16_t(std::min(strtol(gold, nullptr, 10), long(figures::MaxGold)));
            int hero = m_edit.heroPoints;
            label(Tr("Hero points"));
            ImGui::SetNextItemWidth(FieldW);
            if (ImGui::InputInt("##hero", &hero, 1, 10))
                m_edit.heroPoints = uint16_t(std::clamp(hero, 0, 100));
            label(Tr("Nickname (16 max)"));
            ImGui::SetNextItemWidth(FieldW);
            ImGui::InputText("##nick", m_nickname, sizeof(m_nickname));
            ImGui::EndGroup();
        }
        ImGui::PopStyleVar();
        if (stats)
            DrawUpgradeEditor(f);
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(0, 8));
        if (m_app.PillButton(Tr("Save"), true, 140, true))
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
                    m_edit.swapForceExperience != m_editBase.swapForceExperience || m_edit.gold != m_editBase.gold || m_edit.heroPoints != m_editBase.heroPoints || m_edit.hat != m_editBase.hat ||
                    m_edit.upgrades != m_editBase.upgrades || m_edit.wowPow != m_editBase.wowPow || figures::NicknameUtf8(m_editBase) != Trimmed(m_nickname);
                if (changed)
                    saved = collection::WriteStats(f.path, edited, &error);
            }
            if (saved)
            {
                Toast(stats ? Tr("Saved") : Tr("Saved"));
                m_editing = false;
                Reload();
            }
            else
                Toast(Tr("Couldn't save it: ") + (error.empty() ? std::string(Tr("the collection's index can't be written")) : error));
        }
        ImGui::SameLine(0, 8);
        if (m_app.PillButton(Tr("Cancel"), false, 140, true))
            m_editing = false;
    }
}

// The overlays of the collection page and the figure sheet: the picture, the level for the portal's game and the facts
// that matter; three main actions (Put on the portal, Edit, Favorite), the rest in a menu (duplicate, back up, restore,
// show the file, reset and delete, asked first), the technical details behind "Details". Every overlay is one ui::Modal.
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/launcher_ui.h"
#include "shared/collection/upgrades.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <ctime>
#include <fmt/format.h>
#include <imgui_internal.h> // the key owners, the popup stack
#include <utility>

namespace launcher_ui
{
    namespace fs = std::filesystem;
    using collection::Figure;

    float OverlayBody(float chrome, float least, float most)
    {
        return std::clamp(ImGui::GetIO().DisplaySize.y - 2 * ui::space::XL - chrome, least, most);
    }

    ui::ModalResult OverlayActions(const char* cancel, const char* confirm, bool confirmDisabled)
    {
        ui::ButtonOptions cancelOptions, confirmOptions;
        confirmOptions.kind = ui::ButtonKind::Primary;
        confirmOptions.disabled = confirmDisabled;
        const float width = ui::ButtonWidth(cancel, cancelOptions) + (confirm != nullptr ? ui::space::S + ui::ButtonWidth(confirm, confirmOptions) : 0);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
        ui::ModalResult result = ui::ModalResult::None;
        if (ui::Button(cancel, cancelOptions))
            result = ui::ModalResult::Cancel;
        if (confirm != nullptr)
        {
            ImGui::SameLine(0, ui::space::S);
            if (ui::Button(confirm, confirmOptions))
                result = ui::ModalResult::Confirm;
        }
        return result;
    }

    // One modal for the three overlays (Figure, Add a figure, Manage): the content changes, the dialog stays.
    void CollectionPage::DrawOverlay()
    {
        if (m_overlay == Overlay::None)
        {
            m_sheetShown.clear();
            return;
        }
        // B / Escape go back one step: out of Edit or "Change the bottom", then out of the dialog (which closes itself). A menu
        // or a question open (a deeper popup), or a field being typed in, answers it first.
        const bool deeper = ImGui::GetCurrentContext()->OpenPopupStack.Size > 1;
        if (m_overlay == Overlay::Figure && (m_editing || !m_changeKeep.empty()) && !deeper && !ImGui::IsAnyItemActive() &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))
        {
            if (m_editing)
                m_editing = false;
            else
                m_changeKeep.clear();
            ImGui::SetKeyOwner(ImGuiKey_Escape, ImGuiKeyOwner_NoOwner, ImGuiInputFlags_LockThisFrame);
            ImGui::SetKeyOwner(ImGuiKey_GamepadFaceRight, ImGuiKeyOwner_NoOwner, ImGuiInputFlags_LockThisFrame);
        }
        std::string title;
        switch (m_overlay)
        {
        case Overlay::Figure:
            if (m_editing)
                title = Tr("Edit");
            else if (!m_changeKeep.empty())
            {
                const Figure* keep = m_model.Find(m_changeKeep);
                title = keep != nullptr && keep->IsSwapTop() ? Tr("Change the bottom") : Tr("Change the top");
            }
            else if (const Figure* figure = m_model.Find(m_figure))
                title = Name(*figure);
            break;
        case Overlay::Add: title = Tr("Add a figure"); break;
        default: title = Tr("Manage"); break;
        }
        // Each figure, and each of its views (the sheet, Edit, Change the bottom), opens at the top: the dialog and its scrolling
        // body are the same windows for all of them and would keep the last one's place.
        const std::string sheet = m_overlay == Overlay::Figure ? m_figure + (m_editing ? "|edit|" : "|sheet|") + m_changeKeep : std::string();
        if (sheet != m_sheetShown)
        {
            m_sheetShown = sheet;
            m_sheetTop = true;
            ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
        }
        bool open = true;
        if (!ui::BeginModal("##overlay", title.c_str(), &open, ui::ModalSize::Large))
        {
            m_overlay = Overlay::None;
            m_editing = false;
            m_confirm.clear();
            return;
        }
        switch (m_overlay)
        {
        case Overlay::Figure: DrawFigurePage(); break;
        case Overlay::Add: DrawAdd(); break;
        case Overlay::Manage: DrawManage(); break;
        case Overlay::None: break;
        }
        if (!m_confirm.empty())
        {
            // The question asked over the sheet dims everything under it: the sheet, which the question's own dim leaves out of
            // the rest, goes with the page.
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->PushClipRectFullScreen();
            draw->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, ui::Col(ui::color::Scrim, float(ui::elevation::ScrimAlpha) / 255.0f));
            draw->PopClipRect();
        }
        ui::EndModal();
        if (!open && m_overlay != Overlay::None) // the cross, B or Escape
        {
            m_overlay = Overlay::None;
            m_editing = false;
            m_confirm.clear();
        }
    }

    namespace
    {
        namespace strings
        {
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

        // A backup's file is named for its time ("2026-10-24T101500Z"): when it was made, in the player's language.
        std::string BackupLabel(const std::string& backup)
        {
            std::string stem = fs::path(backup).stem().string();
            if (stem.size() >= 16)
                return When(fmt::format("{}:{}:{}Z", stem.substr(0, 13), stem.substr(13, 2), stem.substr(15, 2)));
            return stem;
        }

        // A two-column table of facts: the name dim, the value as text.
        void FactRows(const char* id, const std::vector<std::pair<std::string, std::string>>& rows)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui::space::XS, ui::space::XS));
            ImGui::PushStyleColor(ImGuiCol_TableBorderLight, LauncherTheme::Rgb(ui::color::Hairline));
            if (ImGui::BeginTable(id, 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 2 * ui::height::Main);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                for (const auto& [name, value] : rows)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    // The name on the value's baseline (the value is Body, the name Caption).
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ui::Baseline(ui::TextStyle::Body) - ui::Baseline(ui::TextStyle::Caption));
                    ui::Text(ui::TextStyle::Caption, name.c_str(), ui::color::Dim);
                    ImGui::TableSetColumnIndex(1);
                    ui::Text(ui::TextStyle::Body, value.c_str(), ui::color::Ink);
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        // A number typed as digits, clamped.
        long long NumberFromText(const char* text, long long most)
        {
            return std::clamp(std::strtoll(text, nullptr, 10), 0LL, most);
        }
        int DigitsOnly(ImGuiInputTextCallbackData* data) { return data->EventChar >= '0' && data->EventChar <= '9' ? 0 : 1; }
    }

    void CollectionPage::RefreshDisk()
    {
        if (m_diskGeneration == m_generation)
            return;
        m_diskGeneration = m_generation;
        m_trash = collection::Trash();
        // What each trashed dump is, by its toy: the trash lists file names ("012 Zap.sky"), the player knows figures.
        m_trashNames.clear();
        for (const collection::TrashEntry& entry : m_trash)
        {
            Figure trashed;
            m_trashNames.push_back(collection::Read(entry.path, trashed) && trashed.toy != nullptr ? trashed.DisplayName() : entry.file);
        }
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
            std::string detail;
            if (collection::Read(backup, old) && collection::HasProgress(old) && old.state == figures::AreaState::Valid)
                detail = Tf("level {} · {} gold", old.level, ui::FormatNumber(old.stats.gold));
            rows.push_back({ backup, BackupLabel(backup), detail });
        }
        return m_backups[figure] = std::move(rows);
    }

    // The level for the portal's game, the way to the next one, gold and play time, last played.
    void CollectionPage::DrawFigureFacts(const Figure& f)
    {
        if (!collection::HasProgress(f) && f.toy != nullptr)
            ui::Text(ui::TextStyle::Body, Tf("{} keep no level or gold.", CategoryLabel(f.toy->category)).c_str(), ui::color::InkSoft);
        else if (f.state == figures::AreaState::Blank)
            ui::Text(ui::TextStyle::Body, Tr("New: never played."), ui::color::InkSoft);
        else if (f.state == figures::AreaState::Damaged)
            ui::StatusPill(ui::Status::Error, Tr("Unreadable. Restore a backup or reset it."));
        else
        {
            // "Level 10 (15)": the portal's game's cap first, the figure's own level (a later game's) in brackets. The
            // bar is the figure's own way to its next level.
            uint32_t xp = figures::TotalExperience(f.stats);
            int level = f.level;
            uint32_t from = figures::LevelExperience[std::clamp(level - 1, 0, figures::MaxLevelSwapForce - 1)];
            uint32_t to = level >= figures::MaxLevelSwapForce ? from : figures::LevelExperience[level];
            float fraction = to > from ? std::clamp(float(xp - from) / float(to - from), 0.0f, 1.0f) : 1.0f;
            ui::Text(ui::TextStyle::Title, collection::LevelText(f, m_model.gameId, French()).c_str(), ui::color::Ink, -1);
            ui::ProgressBar(fraction, -1, false);
            std::string next = level >= figures::MaxLevelSwapForce ? std::string(Tr("maximum level")) : Tf("{} to level {}", ui::FormatNumber(to - xp), level + 1);
            ui::Text(ui::TextStyle::Caption, Tf("{} XP · {}", ui::FormatNumber(xp), next).c_str(), ui::color::Dim);
        }
        std::vector<std::pair<std::string, std::string>> rows;
        if (collection::HasProgress(f) && f.state == figures::AreaState::Valid)
            rows.push_back({ Tr("Gold"), Tf("{} · {} played", ui::FormatNumber(f.stats.gold), PlayTime(f.stats.playSeconds)) });
        // A never-played figure already says so above: no "Last played: Never" row for it.
        if (f.state != figures::AreaState::Blank)
            rows.push_back({ Tr("Last played"), f.lastGame.empty() ? std::string(Tr("Never")) : fmt::format("{} · {}", FullGameName(f.lastGame), When(f.lastUsed)) });
        // What the chosen game makes of it (a variant it does not tell apart plays as another), and the games whose wheel has it.
        if (f.toy != nullptr)
        {
            collection::Catalogue::Reading reading = collection::Catalogue::Get().Read(m_model.gameId, f.toy, f.variant);
            if (reading.works && !reading.exact && f.edition.empty() && f.variant != 0 && f.variantInfo.kind != collection::VariantKind::Base)
                rows.push_back({ Tf("In {}", ShortGameName(m_model.gameId)),
                    Tf("plays as {}", reading.seen != nullptr ? reading.seen->name : f.toy->name) });
            std::string wheels;
            for (const collection::Game& g : collection::Catalogue::Get().Games())
                if (!g.runtime.empty() && collection::OnWheel(g.id, f.file))
                    wheels += (wheels.empty() ? "" : ", ") + ShortGameName(g.id);
            if (!wheels.empty())
                rows.push_back({ Tr("On the wheel"), wheels });
        }
        if (!rows.empty())
            FactRows("##facts", rows);
    }

    // "Details": what only a curious owner or a bug report needs.
    void CollectionPage::DrawDetails(const Figure& f)
    {
        std::vector<std::pair<std::string, std::string>> rows;
        if (collection::HasProgress(f) && f.state == figures::AreaState::Valid)
        {
            std::string nickname = figures::NicknameUtf8(f.stats);
            rows.push_back({ Tr("Nickname"), nickname.empty() ? std::string(Tr("None")) : nickname });
            rows.push_back({ Tr("Hero points"), Tf("{} · {} · {} upgrades", f.stats.heroPoints, HatText(f.stats.hat), std::popcount(uint16_t(collection::EditorBits(f.stats.upgrades, f.stats.wowPow) & 0x07FF))) });
        }
        std::string games;
        int count = 0;
        for (const collection::Game& game : collection::Catalogue::Get().Games())
            if (f.toy != nullptr && collection::Catalogue::Get().Accepts(game.id, f.type))
                games += (games.empty() ? "" : ", ") + ShortGameName(game.id), count++;
        rows.push_back({ Tr("Plays in"), games.empty() ? std::string(Tr("No game this launcher knows")) : count == int(collection::Catalogue::Get().Games().size()) ? std::string(Tr("every game")) : games });
        rows.push_back({ Tr("Toy"), Tf("{} · type {} · variant {:04X}", f.toy != nullptr ? f.toy->name : std::string(Tr("Unknown")), f.type, f.variant) });
        {
            // How the file came to be: the collection stores a word (created, imported, portal), shown in the player's language.
            static const char* const Origins[] = { N_("created"), N_("imported"), N_("portal") };
            std::string origin = f.origin;
            for (const char* known : Origins)
                if (origin == known)
                    origin = Tr(known);
            rows.push_back({ Tr("File"), f.file + (origin.empty() ? "" : " · " + origin) });
        }
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

    void CollectionPage::DrawFigurePage()
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
            DrawChangeHalf();
            return;
        }
        Figure f = *found; // actions below reload the collection
        const std::string path = f.path;
        const collection::Card card = CardOf(f); // f is the swapper's top, or a figure (also a half on its own)
        const bool writable = Writable(f);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Left: the picture and what kind of toy it is.
        constexpr float Picture = 3 * ui::height::Main + ui::height::Small;
        ImGui::BeginGroup();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        draw->AddRectFilled(origin, ImVec2(origin.x + Picture, origin.y + Picture), ui::Col(ui::color::Inset), ui::radius::Card);
        if (!DrawSwapper(draw, card, ImVec2(origin.x + ui::space::XS, origin.y + ui::space::XS), Picture - 2 * ui::space::XS))
        {
            DrawPicture(draw, f.toy, f.Kind(), f.variant, ImVec2(origin.x + ui::space::XS, origin.y + ui::space::XS), Picture - 2 * ui::space::XS);
            if (card.Swapper())
                DrawBottom(draw, *card.second, ImVec2(origin.x + Picture * 0.66f, origin.y + Picture * 0.66f), Picture * 0.3f);
        }
        ImGui::Dummy(ImVec2(Picture, Picture));
        float used = 0;
        auto badge = [&](const std::string& text, unsigned fill) {
            const float w = ui::BadgeSize(text.c_str()).x;
            if (used > 0 && used + ui::space::XS + w <= Picture)
            {
                ImGui::SameLine(0, ui::space::XS);
                used += ui::space::XS + w;
            }
            else
                used = w;
            ui::Badge(text.c_str(), fill);
        };
        if (f.Kind() != collection::VariantKind::Base)
            badge(KindLabel(f.Kind()), collection::VariantKindRgb(f.Kind()));
        if (f.toy != nullptr && f.toy->element != collection::Element::None)
            badge(ElementName(f.toy->element), ui::color::Raised);
        if (card.Swapper())
            badge(Tr("Swapper"), ui::color::Raised);
        else if (f.toy != nullptr)
            badge(CategoryLabel(f.toy->category), ui::color::Raised);
        ImGui::EndGroup();

        // Right: where it is, what it can do, the level and the facts, Details.
        ImGui::SameLine(0, ui::space::L);
        ImGui::BeginGroup();
        std::string what = f.toy != nullptr ? f.variantInfo.name : Tf("Toy type {}", f.type);
        if (card.Swapper())
            what = Tf("Top: {} · Bottom: {}", WithoutHalf(f.variantInfo.name), WithoutHalf(card.second->variantInfo.name));
        if (what != Name(f)) // the line only when it says more (a nickname over the toy's name)
            ui::Text(ui::TextStyle::Body, what.c_str(), ui::color::InkSoft);
        if (card.Busy())
            ui::StatusPill(ui::Status::Warning, Tf("On the portal in {}: read-only", LiveGame(f)).c_str());
        else if (int seat = m_model.SeatOf(path); seat >= 0)
            ui::StatusPill(ui::Status::Info, Tf("On {} at the next start of {}", SeatName(size_t(seat)), ShortGameName(m_model.gameId)).c_str());

        const collection::Game* game = m_model.Game();
        const int seated = m_model.SeatOf(path);
        // 1. Put on the portal (of the next start), on a seat it fits.
        ui::ButtonOptions put;
        put.trailing = ui::Icon::ChevronDown;
        put.disabled = !m_model.CanEditSeats();
        if (ui::Button(Tr("Put on the portal"), put) || m_testMenu == 3)
        {
            ui::OpenMenu("##puton");
            m_testMenu = 0;
        }
        if (ui::BeginMenu("##puton"))
        {
            ui::Text(ui::TextStyle::Caption, Tf("At the next start of {}", FullGameName(m_model.gameId)).c_str(), ui::color::Dim, -1);
            for (size_t i = 0; game != nullptr && i < game->seats.size(); i++)
            {
                std::string text = SeatName(i);
                if (!m_model.seats[i].empty() && m_model.seats[i] != path)
                    if (const Figure* held = m_model.Find(m_model.seats[i]))
                        text += Tf(" (replaces {})", Name(*held));
                const bool fits = SeatAccepts(game->seats[i], f) && (f.toy == nullptr || collection::Catalogue::Get().Accepts(m_model.gameId, f.type));
                if (ui::MenuItem((text + "##seat" + std::to_string(i)).c_str(), { ui::Icon::None, seated == int(i), false, !fits }))
                    PutOn(i, path);
            }
            if (seated >= 0)
            {
                ui::MenuSeparator();
                if (ui::MenuItem(Tr("Take it off"), { ui::Icon::Cross }))
                    TakeOff(size_t(seated));
            }
            ui::EndMenu();
        }
        // 2. Edit: the name, and the stats of a figure that keeps them.
        ImGui::SameLine(0, ui::space::S);
        ui::ButtonOptions edit;
        edit.disabled = !writable || f.state == figures::AreaState::Damaged;
        if (ui::Button(Tr("Edit"), edit))
            OpenEdit(f);
        // 3. Favorite: shown first everywhere.
        ImGui::SameLine(0, ui::space::S);
        ui::ButtonOptions favourite;
        favourite.icon = ui::Icon::Star;
        if (ui::Button(f.favourite ? Tr("Favorite") : Tr("Add to favorites"), favourite))
        {
            collection::SetFavourite(path, !f.favourite);
            Reload();
        }
        // The rest.
        ImGui::SameLine(0, ui::space::S);
        ui::IconButtonOptions more;
        more.kind = ui::ButtonKind::Secondary;
        if (ui::IconButton("##figuremore", ui::Icon::More, Tr("More actions"), more) || m_testMenu == 2)
        {
            ui::OpenMenu("##figuremenu");
            m_testMenu = 0;
        }
        DrawFigureMenu(f, writable);
        if (!m_model.CanEditSeats())
            ui::Text(ui::TextStyle::Caption, m_model.RunningGame().empty() ? Tr("Not available for this game yet") : Tr("The game is running: use its Toy Box"), ui::color::Dim);
        // A swapper changes one half at a time ("Change the bottom": mixing stays possible); a half on its own looks for its partner.
        if (f.IsSwapTop() || f.IsSwapBottom())
        {
            ui::ButtonOptions change;
            change.size = ui::Size::Small;
            change.disabled = !writable;
            bool any = false;
            auto half = [&](const char* label, const std::string& keep) {
                if (any)
                    ImGui::SameLine(0, ui::space::XS);
                any = true;
                if (ui::Button(label, change))
                    m_changeKeep = keep;
            };
            if (f.IsSwapTop())
                half(Tr("Change the bottom"), f.path);
            if (card.Swapper())
                half(Tr("Change the top"), card.second->path);
            else if (f.IsSwapBottom())
                half(Tr("Change the top"), f.path);
        }
        // A figure held by a game: the lock can be broken when the game is really gone (a crash on another machine...).
        if (card.Busy())
        {
            ui::ButtonOptions release;
            release.kind = ui::ButtonKind::Danger;
            release.hold = true;
            release.tooltip = Tr(strings::ReleaseHint);
            if (ui::Button(Tr(strings::Release), release))
            {
                for (const Figure* half : card.Halves())
                    collection::BreakLock(half->path);
                Reload();
            }
        }
        ImGui::Dummy(ImVec2(0, ui::space::XXS));
        DrawFigureFacts(f);
        ui::ButtonOptions details;
        details.kind = ui::ButtonKind::Ghost;
        details.trailing = m_details ? ui::Icon::ChevronUp : ui::Icon::ChevronDown;
        if (ui::Button(Tr("Details"), details))
            m_details = !m_details;
        if (m_details)
            DrawDetails(f);
        ImGui::EndGroup();
        DrawConfirm(f);
    }

    // The sheet's menu: the wheel, duplicate, back up, restore, the folder; reset and delete are asked first.
    void CollectionPage::DrawFigureMenu(const Figure& f, bool writable)
    {
        if (!ui::BeginMenu("##figuremenu"))
            return;
        // "On the wheel": the in-game quick swap wheel (hold Tab or View), per game that reads the figure.
        for (const collection::Game& g : collection::Catalogue::Get().Games())
        {
            if (g.runtime.empty() || f.toy == nullptr || !collection::Catalogue::Get().Accepts(g.id, f.type) ||
                !(collection::HasProgress(f) || f.toy->category == collection::Category::CreationCrystal))
                continue;
            const bool on = collection::OnWheel(g.id, f.file);
            if (ui::MenuItem(Tf("On the wheel: {}", ShortGameName(g.id)).c_str(), { ui::Icon::None, on }))
            {
                if (!collection::SetOnWheel(g.id, f.file, !on))
                    Toast(Tr("The wheel is full: take a figure off it first"), ui::ToastKind::Error);
                Reload();
            }
        }
        ui::MenuSeparator();
        if (ui::MenuItem(Tr("Duplicate")))
        {
            std::string error;
            std::string copy = collection::Duplicate(f.path, &error);
            Toast(copy.empty() ? Tr("Couldn't duplicate it: ") + error : Tr("Duplicated: ") + fs::path(copy).filename().string(), copy.empty() ? ui::ToastKind::Error : ui::ToastKind::Success);
            Reload();
        }
        if (ui::MenuItem(Tr("Back up now"), { ui::Icon::None, false, false, !writable }))
        {
            std::string backup = collection::Backup(f.path);
            Toast(backup.empty() ? Tr("Couldn't back it up") : Tr("Backed up"), backup.empty() ? ui::ToastKind::Error : ui::ToastKind::Success);
            m_generation++; // Restore lists it
        }
        if (ui::MenuItem(Tr("Restore a backup..."), { ui::Icon::None, false, false, !writable || Backups(f.path).empty() }))
        {
            m_overlay = Overlay::Manage; // the backups of this figure, in Manage
            m_manageTab = 1;
            m_backupsOf = f.path;
        }
        if (ui::MenuItem(Tr("Show in folder"), { ui::Icon::Folder }))
        {
            std::string folder = collection::Folder();
            OpenFolder(std::filesystem::path(std::u8string(folder.begin(), folder.end())));
        }
        ui::MenuSeparator();
        if (collection::HasProgress(f) && ui::MenuItem(Tr("Reset to new..."), { ui::Icon::None, false, true, !writable }))
            m_confirm = "reset";
        if (ui::MenuItem(Tr("Delete..."), { ui::Icon::Trash, false, true, !writable }))
            m_confirm = "delete";
        ui::EndMenu();
    }

    // The question over the sheet before a reset or a delete: a danger dialog, the focus on Cancel.
    void CollectionPage::DrawConfirm(const Figure& f)
    {
        if (m_confirm.empty())
            return;
        const bool reset = m_confirm == "reset";
        bool open = true;
        const ui::ModalResult result = ui::ConfirmDialog("##confirmfigure", reset ? Tr("Reset to new") : Tr("Move to the trash"),
            (reset ? Tf("Reset {} to new? Level, gold and upgrades are lost.", Name(f)) : Tf("Move {} to the trash? It stays there for 30 days.", Name(f))).c_str(),
            reset ? Tr("Reset") : Tr("Move to the trash"), Tr("Cancel"), &open, ui::ModalKind::Danger);
        if (result == ui::ModalResult::None)
            return;
        m_confirm.clear();
        if (result != ui::ModalResult::Confirm)
            return;
        const std::string path = f.path;
        if (reset)
        {
            std::string error;
            const bool done = collection::ResetToNew(path, &error);
            Toast(done ? Tr("Reset to new") : Tr("Couldn't reset it: ") + error, !done ? ui::ToastKind::Error : ui::ToastKind::Success);
            Reload();
        }
        else
            DeleteFigure(path);
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
            Toast(Tr("Couldn't delete it: ") + error, ui::ToastKind::Error);
            return;
        }
        m_selected = next;
        m_figure.clear();
        Toast(Tr("Moved to the trash"), ui::ToastKind::Success);
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
            Toast(Tr("Couldn't change it: ") + error, ui::ToastKind::Error);
            return;
        }
        Reload();
        if (seat >= 0)
            m_model.SetSeat(size_t(seat), newTop);
        if (const Figure* now = m_model.Find(keep.path))
            m_figure = CardOf(*now).first->path;
        m_changeKeep.clear();
        Reload();
        Toast(Tr("Changed"), ui::ToastKind::Success);
    }

    // The list of the other halves to take: one row each, the ones in use or on a seat left out; a half that belongs to another
    // swapper says so (that swapper takes the old half in exchange).
    void CollectionPage::DrawChangeHalf()
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
        ui::Text(ui::TextStyle::Body, Tf("{}: pick the new {}", Name(*keep), wantBottom ? Tr("bottom") : Tr("top")).c_str(), ui::color::InkSoft);

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

        if (std::exchange(m_sheetTop, false))
            ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
        ui::BeginRoomyChild("##changehalf", ImVec2(0, OverlayBody(3 * ui::height::Main, 2 * ui::height::Main, 6 * ui::height::Main)), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        ImGui::SetScrollX(0.0f);
        constexpr float Art = ui::height::Large - 2 * ui::space::XXS;
        const Figure* chosen = nullptr;
        for (const Figure* half : rows)
        {
            collection::Card card = CardOf(*half);
            const std::string title = half->label.empty() ? WithoutHalf(half->DisplayName()) : half->label;
            const std::string note = card.Swapper() ? Tf("Now with {}", WithoutHalf((half->IsSwapTop() ? card.second : card.first)->DisplayName())) : std::string(Tr("On its own"));
            ui::ListRowOptions options;
            options.subtitle = note.c_str();
            options.imageWidth = Art;
            options.image = [&](ImDrawList* draw, ImVec2 at, ImVec2) { DrawIcon(draw, half->toy, half->Kind(), at, Art, false); };
            ImGui::PushID(half->path.c_str());
            if (ui::ListRow(title.c_str(), options))
                chosen = half;
            ImGui::PopID();
        }
        if (rows.empty())
            ui::EmptyState(ui::Icon::Info, wantBottom ? Tr("No other bottom to take.") : Tr("No other top to take."));
        ui::EndRoomyChild();
        if (chosen != nullptr)
        {
            Figure keepNow = *keep, replacement = *chosen; // the call reloads the collection
            ChangeHalf(keepNow, replacement);
            return;
        }
        if (OverlayActions(Tr("Cancel")) == ui::ModalResult::Cancel)
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
        ui::SectionHeader(Tr("Upgrades"), Tf("{} of {}", collection::BoughtCount(bits, table), table.upgrades.size()).c_str());
        ui::ButtonOptions small;
        small.size = ui::Size::Small;
        if (ui::Button(Tr("All"), small))
            setBits(collection::AllUpgrades(bits, table));
        ImGui::SameLine(0, ui::space::XS);
        if (ui::Button(Tr("None"), small))
            setBits(collection::NoUpgrades(bits));
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        const float columnW = (ImGui::GetContentRegionAvail().x - 2 * ui::space::S) / 3;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        bool chosen = (bits >> collection::UpgradePathChosenBit & 1) != 0, second = (bits >> collection::UpgradePathSecondBit & 1) != 0;
        const std::string titles[3] = { Tr("Basic"), table.pathNames[0], table.pathNames[1] };
        ImDrawList* draw = ImGui::GetWindowDrawList();
        for (int column = 0; column < 3; column++)
        {
            bool lit = column == 0 || (chosen && second == (column == 2));
            PutText(draw, ImVec2(origin.x + column * (columnW + ui::space::S), origin.y), ui::TextStyle::Body, true, lit ? ui::color::Info : ui::color::Dim,
                titles[column], columnW);
        }
        int rows[3] = { 0, 0, 0 };
        float bottom = origin.y;
        std::string hint;
        const float header = ui::type::Body + ui::space::XS;
        for (size_t i = 0; i < table.upgrades.size(); i++)
        {
            const collection::Upgrade& upgrade = table.upgrades[i];
            using collection::UpgradePath;
            int column = upgrade.path == UpgradePath::First ? 1 : upgrade.path == UpgradePath::Second ? 2 : 0;
            const ImVec2 at(origin.x + column * (columnW + ui::space::S), origin.y + header + rows[column]++ * ui::height::Normal);
            ImGui::SetCursorScreenPos(at);
            bool bought = collection::UpgradeBought(bits, upgrade);
            const bool greyed = collection::UpgradeGreyed(bits, upgrade);
            const std::string price = Tf("{} gold", ui::FormatNumber(upgrade.price));
            ui::PushText(ui::TextStyle::Caption);
            const float priceW = ImGui::CalcTextSize(price.c_str()).x;
            ui::PopText();
            ui::PushText(ui::TextStyle::Body);
            const std::string name = ui::Ellipsize(collection::UpgradeName(upgrade.name, language).c_str(), columnW - ui::height::Mark - 2 * ui::space::S - priceW);
            ui::PopText();
            if (greyed)
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ui::DisabledAlpha); // the other path's: it still toggles
            if (ui::Checkbox((name + "##upgrade" + std::to_string(i)).c_str(), &bought))
                setBits(collection::ToggleUpgrade(bits, upgrade));
            if (ImGui::IsItemHovered() || ImGui::IsItemFocused())
                hint = collection::UpgradeLine(upgrade.description, language);
            PutText(draw, ImVec2(at.x + columnW - priceW, at.y + (ui::height::Mark - ui::type::Caption) / 2), ui::TextStyle::Caption, false,
                bought ? ui::color::Info : ui::color::Dim, price, priceW);
            if (greyed)
                ImGui::PopStyleVar();
            bottom = std::max(bottom, at.y + ui::height::Normal);
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom));
        ImGui::Dummy(ImVec2(columnW, 0));
        ui::Text(ui::TextStyle::Caption, hint.empty() ? " " : hint.c_str(), ui::color::InkSoft);
    }

    // Edit: the name the lists show, and for a figure that keeps them, its level, experience, gold, hero points, hat and
    // the nickname the game shows.
    void CollectionPage::DrawEditStats(const Figure& f)
    {
        bool stats = collection::HasProgress(f) && f.state != figures::AreaState::Damaged;
        ui::Text(ui::TextStyle::Body, (f.variantInfo.name != Name(f) ? Name(f) + " · " + f.variantInfo.name : Name(f)).c_str(), ui::color::InkSoft);
        // The fields scroll above the Save row (the upgrades list makes the sheet taller than a small window).
        if (std::exchange(m_sheetTop, false))
            ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
        ui::BeginRoomyChild("##editbody", ImVec2(0, OverlayBody(4 * ui::height::Main, 3 * ui::height::Main, 8 * ui::height::Main)), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        ImGui::SetScrollX(0.0f);
        if (ui::BeginSettings("##edit"))
        {
            ui::SettingRow(Tr("Name"), Tr("Empty: the toy's name"));
            ui::TextFieldOptions nameOptions;
            nameOptions.hint = f.variantInfo.name.c_str();
            ui::TextField("##label", m_label, sizeof(m_label), nameOptions);
            if (stats)
            {
                // The limits of the portal's game: Spyro's Adventure edits its own part (up to level 10, its hat in the first
                // area), Giants the first two parts (up to 15) and its hat, Swap Force all three (up to 20); the rest of the figure stays as it is.
                const int cap = figures::LevelCap(m_model.gameId);
                int level = figures::LevelForExperience(figures::ExperienceFor(m_edit, cap), cap);
                ui::SettingRow(Tf("Level in {}", ShortGameName(m_model.gameId)).c_str(), Tf("1 to {}", cap).c_str());
                if (ui::Slider("##level", &level, 1, cap))
                    figures::SetExperienceFor(m_edit, cap, figures::LevelExperience[std::clamp(level, 1, cap) - 1]);
                ui::TextFieldOptions digits;
                digits.flags = ImGuiInputTextFlags_CallbackCharFilter;
                digits.callback = DigitsOnly;
                const int experienceMost = int(figures::LevelExperience[cap - 1]);
                ui::SettingRow(Tr("Experience"), Tf("0 to {}", ui::FormatNumber(experienceMost)).c_str());
                char experience[12];
                snprintf(experience, sizeof experience, "%u", unsigned(figures::ExperienceFor(m_edit, cap)));
                if (ui::TextField("##xp", experience, sizeof experience, digits))
                    figures::SetExperienceFor(m_edit, cap, uint32_t(NumberFromText(experience, experienceMost)));
                // Giants and later: the hat picker, by the game's names (the catalogue's "hats").
                const collection::Game* game = m_model.Game();
                if (game != nullptr && !game->hats.empty())
                {
                    ui::SettingRow(Tr("Hat"), Tr("What it wears in the game"));
                    if (ui::BeginSelect("##hat", m_edit.hat == 0 ? Tr("No hat") : HatText(m_edit.hat).c_str()))
                    {
                        if (ui::SelectOption(Tr("No hat"), m_edit.hat == 0))
                            m_edit.hat = 0;
                        for (const auto& [number, name] : game->hats)
                            if (ui::SelectOption(fmt::format("{}##hat{}", name, number).c_str(), m_edit.hat == number))
                                m_edit.hat = number;
                        ui::EndSelect();
                    }
                }
                ui::SettingRow(Tr("Gold"), Tf("0 to {}", ui::FormatNumber(figures::MaxGold)).c_str());
                char gold[8];
                snprintf(gold, sizeof gold, "%u", unsigned(m_edit.gold));
                if (ui::TextField("##gold", gold, sizeof gold, digits))
                    m_edit.gold = GoldFromText(gold);
                constexpr int MaxHeroPoints = 100;
                ui::SettingRow(Tr("Hero points"), Tf("0 to {}", MaxHeroPoints).c_str());
                char hero[8];
                snprintf(hero, sizeof hero, "%d", int(m_edit.heroPoints));
                if (ui::TextField("##hero", hero, sizeof hero, digits))
                    m_edit.heroPoints = uint16_t(NumberFromText(hero, MaxHeroPoints));
                ui::SettingRow(Tr("Nickname"), Tr("16 characters at most"));
                ui::TextField("##nick", m_nickname, sizeof(m_nickname));
            }
            ui::EndSettings();
        }
        if (stats)
            DrawUpgradeEditor(f);
        ui::EndRoomyChild();
        ImGui::Dummy(ImVec2(0, ui::space::XXS));
        const ui::ModalResult result = OverlayActions(Tr("Cancel"), Tr("Save"));
        if (result == ui::ModalResult::Cancel)
            m_editing = false;
        else if (result == ui::ModalResult::Confirm)
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
                Toast(Tr("Saved"), ui::ToastKind::Success);
                m_editing = false;
                Reload();
            }
            else
                Toast(Tr("Couldn't save it: ") + (error.empty() ? std::string(Tr("the collection's index can't be written")) : error), ui::ToastKind::Error);
        }
    }
}

// The component gallery (SKY_LAUNCHER_TEST_GALLERY=1 with SKY_LAUNCHER_SHOTS): every component of components/ in every
// state, one page per family, at 1x and 2x, in the language of the run (SKY_LAUNCHER_LANGUAGE). It is the components'
// visual test and the designer's review sheet (docs/launcher_ui.md, "Gallery"). The sample labels are the launcher's own
// translated strings; the captions naming components and states are for developers and stay in English.
#include "launcher/ui/launcher_app.h"
#include <fmt/format.h>
#include <imgui_internal.h> // ImHashStr: the combo's popup id, to show it open

namespace launcher_ui
{
    namespace
    {
        struct Page
        {
            const char* name;  // the shot's
            const char* title; // the page's heading
        };
        constexpr Page Pages[] = {
            { "buttons", "Buttons" },
            { "main-action", "Main action: Spyro's Adventure" },
            { "main-action-games", "Main action: each game" },
            { "selection", "Tabs, segmented, select, slider" },
            { "toggles", "Toggle, checkbox, radio" },
            { "fields-lists", "Fields, cards, lists" },
            { "feedback", "Badges, chips, status, progress" },
            { "toasts-keys", "Toasts, key caps, pad legend" },
            { "modal", "Modal" },
            { "modal-danger", "Modal: danger" },
            { "menu-tooltip", "Menu, tooltip" },
            { "select-open", "Select, open" },
        };
        constexpr int PageCount = int(std::size(Pages));

        struct StateCase
        {
            const char* name;
            ui::State state;
            bool disabled = false;
            bool loading = false;
        };
        const StateCase States[] = {
            { "Normal", {} },
            { "Hover", { .hovered = true } },
            { "Focus", { .focused = true } },
            { "Pressed", { .pressed = true } },
            { "Disabled", {}, true },
            { "Loading", {}, false, true },
        };

        // A caption over a cell: what the cell shows.
        void Caption(const char* text)
        {
            const std::string shown = text;
            ui::Text(ui::TextStyle::Caption, shown.c_str(), ui::color::Dim, -1);
        }

        // A grid: `columns` cells `cellW` wide from the cursor, rows `rowH` high.
        struct Grid
        {
            ImVec2 origin;
            float cellW, rowH, labelW;
            int columns;
            Grid(int columns, float rowH, float labelW = 120)
                : origin(ImGui::GetCursorScreenPos()), cellW((ImGui::GetContentRegionAvail().x - labelW) / float(columns)), rowH(rowH), labelW(labelW), columns(columns) {}
            void Header(const char* const* names)
            {
                for (int i = 0; i < columns; i++)
                {
                    ImGui::SetCursorScreenPos(ImVec2(origin.x + labelW + cellW * float(i), origin.y));
                    Caption(names[i]);
                }
                origin.y += ui::type::Caption + ui::space::S;
            }
            void RowLabel(int row, const char* name)
            {
                ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rowH * float(row) + ui::space::S));
                Caption(name);
            }
            void At(int row, int column) { ImGui::SetCursorScreenPos(ImVec2(origin.x + labelW + cellW * float(column), origin.y + rowH * float(row))); }
            void End(int rows) { ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rowH * float(rows))); ImGui::Dummy(ImVec2(0, 0)); }
        };

        void Preview(const StateCase& c) { ui::PreviewState(c.state); }

        // A seat's tag as the collection's cards show it (P1).
        std::string SeatTagSample() { return std::string(1, 'P') + "1"; }
    }

    void App::DrawGallery()
    {
        const ImVec2 size = ImGui::GetIO().DisplaySize;
        ImGui::GetBackgroundDrawList()->AddRectFilledMultiColor(ImVec2(0, 0), size, Rgb(PageTop), Rgb(PageTop), Rgb(PageBottom), Rgb(PageBottom));
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::space::XL, ui::space::L));
        ImGui::Begin("##gallery", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();
        const int page = std::clamp(m_galleryPage, 0, PageCount - 1);
        const std::string title = Pages[page].title, note = fmt::format("Arkeyan Launcher components  |  {}  |  {:g}x  |  page {} of {}",
            i18n::Current(), m_uiScale, page + 1, PageCount);
        ui::Text(ui::TextStyle::Title, title.c_str(), ui::color::Ink, -1);
        Caption(note.c_str());
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        const char* stateNames[std::size(States)];
        for (size_t i = 0; i < std::size(States); i++)
            stateNames[i] = States[i].name;

        switch (page)
        {
        case 0: // Buttons: each kind in each state
        {
            Grid grid(int(std::size(States)), ui::height::Normal + ui::space::S);
            grid.Header(stateNames);
            struct Row { const char* name; ui::ButtonKind kind; const char* label; ui::Icon icon; bool hold; };
            const Row rows[] = {
                { "Primary", ui::ButtonKind::Primary, Tr("INSTALL"), ui::Icon::None, false },
                { "Secondary", ui::ButtonKind::Secondary, Tr("Retry"), ui::Icon::None, false },
                { "Danger", ui::ButtonKind::Danger, Tr("Remove"), ui::Icon::None, false },
                { "Ghost", ui::ButtonKind::Ghost, Tr("Back"), ui::Icon::None, false },
                { "With icon", ui::ButtonKind::Secondary, Tr("Open"), ui::Icon::Folder, false },
                { "Hold", ui::ButtonKind::Danger, Tr("Restore"), ui::Icon::None, true },
            };
            int r = 0;
            for (const Row& row : rows)
            {
                grid.RowLabel(r, row.name);
                for (size_t c = 0; c < std::size(States); c++)
                {
                    grid.At(r, int(c));
                    ui::ButtonOptions options;
                    options.kind = row.kind;
                    options.icon = row.icon;
                    options.hold = row.hold;
                    options.disabled = States[c].disabled;
                    options.loading = States[c].loading;
                    ui::State state = States[c].state;
                    if (row.hold && state.pressed)
                        state.holdFill = 0.6f; // 60 % into its hold
                    ImGui::PushID(r * 16 + int(c));
                    ui::PreviewState(state);
                    ui::Button(fmt::format("{}##b", row.label).c_str(), options);
                    ImGui::PopID();
                }
                r++;
            }
            r++; // room for the hold's hint under its row
            // Pills and icon buttons: the same states but loading.
            grid.RowLabel(r, "Pill");
            for (size_t c = 0; c + 1 < std::size(States); c++)
            {
                grid.At(r, int(c));
                ImGui::PushID(200 + int(c));
                Preview(States[c]);
                ui::PillOptions options;
                options.trailing = ui::Icon::ChevronDown;
                options.disabled = States[c].disabled;
                ui::Pill(Tr("Filters"), options);
                ImGui::PopID();
            }
            grid.At(r, 5);
            ui::PillOptions selected;
            selected.selected = true;
            selected.icon = ui::Icon::Star;
            ui::Pill(Tr("Favourite"), selected);
            r++;
            grid.RowLabel(r, "IconButton");
            for (size_t c = 0; c + 1 < std::size(States); c++)
            {
                grid.At(r, int(c));
                ImGui::PushID(300 + int(c));
                Preview(States[c]);
                ui::IconButtonOptions options;
                options.disabled = States[c].disabled;
                options.kind = c % 2 == 0 ? ui::ButtonKind::Ghost : ui::ButtonKind::Secondary;
                ui::IconButton("##gear", ui::Icon::Gear, c == 1 ? Tr("Settings") : nullptr, options);
                ImGui::SameLine(0, ui::space::XS);
                ui::IconButton("##more", ui::Icon::More, nullptr, options);
                ImGui::PopID();
            }
            r++;
            grid.RowLabel(r, "Sizes: small, normal, large");
            grid.At(r, 2);
            ui::ButtonOptions small;
            small.size = ui::Size::Small;
            ui::Button(fmt::format("{}##s", Tr("Close")).c_str(), small);
            grid.At(r, 3);
            ui::ButtonOptions normal;
            ui::Button(fmt::format("{}##n", Tr("Close")).c_str(), normal);
            grid.At(r, 4);
            ui::ButtonOptions large;
            large.size = ui::Size::Large;
            large.kind = ui::ButtonKind::Primary;
            large.icon = ui::Icon::Download;
            ui::Button(Tr("UPDATE"), large);
            grid.End(r + 1);
            break;
        }
        case 1: // The main action, Spyro's Adventure, every state
        case 2: // The other games: their colours and focus ring
        {
            static const char* const Games[] = { "ssa", "giants", "swapforce" };
            struct Case { const char* name; ui::State state; bool enabled; float progress; bool indeterminate; const char* label; };
            const Case all[] = {
                { "Normal", {}, true, -1, false, "INSTALL" },
                { "Hover", { .hovered = true }, true, -1, false, "INSTALL" },
                { "Focus", { .focused = true }, true, -1, false, "PLAY" },
                { "Pressed", { .pressed = true }, true, -1, false, "PLAY" },
                { "Disabled", {}, false, -1, false, "UPDATE" },
                { "Busy 46 %", {}, false, 0.46f, false, "INSTALLING..." },
                { "Busy, no end known", {}, false, -1, true, "CHECKING..." },
            };
            const int caseCount = page == 1 ? int(std::size(all)) : 3;
            const int pick[] = { 0, 2, 5 }; // the other games: normal, focus, busy
            const float cellW = ui::height::MainWidth + ui::height::MainGear + ui::space::XL, rowH = ui::height::Main + ui::space::XL;
            ImVec2 origin = ImGui::GetCursorScreenPos();
            int cell = 0;
            for (int g = page == 1 ? 0 : 1; g < (page == 1 ? 1 : 3); g++)
                for (int k = 0; k < caseCount; k++, cell++)
                {
                    const Case& c = all[page == 1 ? k : pick[k]];
                    const int column = cell % 2, row = cell / 2;
                    ImVec2 at(origin.x + cellW * float(column), origin.y + rowH * float(row));
                    ImGui::SetCursorScreenPos(at);
                    const std::string caption = page == 1 ? c.name : fmt::format("{}: {}", GameName(Games[g], GameNameForm::Short), c.name);
                    Caption(caption.c_str());
                    ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + ui::type::Caption + ui::space::XS));
                    ImGui::PushID(cell);
                    ui::MainActionOptions options;
                    options.label = Tr(c.label);
                    options.enabled = c.enabled;
                    options.progress = c.progress;
                    options.indeterminate = c.indeterminate;
                    options.style = &StyleOf(Games[g]);
                    const unsigned accent = ui::Ctx().accent;
                    ui::Ctx().accent = StyleOf(Games[g]).glow; // the focus ring in the game's colour, as on its page
                    ui::PreviewState(c.state);
                    ui::MainAction(options);
                    ui::Ctx().accent = accent;
                    ImGui::PopID();
                }
            ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rowH * 4));
            ImGui::Dummy(ImVec2(0, 0));
            break;
        }
        case 3: // Selection controls
        {
            const char* const tabs[] = { Tr("Video"), Tr("Keyboard"), Tr("Language"), Tr("Disc"), Tr("Saves"), Tr("Game") };
            static int tab = 1, tabPad = 2, segment = 0, segment2 = 1, level = 12;
            Caption("Tabs (keyboard), the first one focused");
            ui::TabsOptions noBumpers;
            ui::PreviewState({ .focused = true });
            ui::Tabs("##tabs1", tabs, &tab, noBumpers);
            Caption("Tabs with a pad: LB / RB at the ends");
            const ui::Glyphs glyphs = ui::Ctx().glyphs;
            ui::Ctx().glyphs = ui::Glyphs::Xbox;
            ui::Tabs("##tabs2", tabs, &tabPad);
            ui::Ctx().glyphs = glyphs;
            ImGui::Dummy(ImVec2(0, ui::space::XS));

            const char* const fps[] = { ScaleChoices[1].label, ScaleChoices[2].label, ScaleChoices[3].label };
            Grid grid(5, ui::height::Normal + ui::space::L);
            const char* const names[] = { "Normal", "Hover", "Focus", "Pressed", "Disabled" };
            grid.Header(names);
            grid.RowLabel(0, "Segmented");
            for (int c = 0; c < 5; c++)
            {
                grid.At(0, c);
                ImGui::PushID(c);
                Preview(States[c]);
                ui::Segmented("##fps", fps, c == 2 ? &segment2 : &segment, grid.cellW - ui::space::S, c == 4);
                ImGui::PopID();
            }
            grid.RowLabel(1, "Select");
            for (int c = 0; c < 5; c++)
            {
                grid.At(1, c);
                ImGui::PushID(10 + c);
                Preview(States[c]);
                ui::SelectOptions options;
                options.width = grid.cellW - ui::space::S;
                options.disabled = c == 4;
                if (ui::BeginSelect("##lang", Tr("System default"), options))
                    ui::EndSelect();
                ImGui::PopID();
            }
            grid.RowLabel(2, "Slider");
            for (int c = 0; c < 5; c++)
            {
                grid.At(2, c);
                ImGui::PushID(400 + c);
                Preview(States[c]);
                ui::SliderOptions options;
                options.width = grid.cellW - ui::space::M;
                options.disabled = c == 4;
                ui::Slider("##level", &level, 1, 20, options);
                ImGui::PopID();
            }
            grid.End(3);
            break;
        }
        case 4: // Toggle, checkbox, radio: off and on, in each state
        {
            Grid grid(5, ui::height::Small + ui::space::L);
            const char* const names[] = { "Normal", "Hover", "Focus", "Pressed", "Disabled" };
            grid.Header(names);
            for (int on = 0; on < 2; on++)
            {
                grid.RowLabel(0 + on, on ? "Toggle on" : "Toggle off");
                grid.RowLabel(2 + on, on ? "Checkbox on" : "Checkbox off");
                grid.RowLabel(4 + on, on ? "Radio on" : "Radio off");
                for (int c = 0; c < 5; c++)
                {
                    bool value = on != 0;
                    ImGui::PushID(100 * (on + 1) + c);
                    grid.At(0 + on, c);
                    Preview(States[c]);
                    ui::Toggle(Tr("Check at startup"), &value, c == 4);
                    grid.At(2 + on, c);
                    Preview(States[c]);
                    ui::Checkbox(Tr("Fullscreen"), &value, c == 4);
                    grid.At(4 + on, c);
                    Preview(States[c]);
                    ui::Radio(Tr("Automatic"), on != 0, c == 4);
                    ImGui::PopID();
                }
            }
            grid.End(6);
            Caption("Settings rows: the label and its hint left, the control in its column");
            static bool fullscreen = true;
            if (ui::BeginSettings("##settings"))
            {
                ui::SettingRow(Tr("Fullscreen"));
                ui::Toggle("##fullscreen", &fullscreen);
                ui::SettingRow(Tr("Frame rate limit"), Tr("Made for 30 FPS; 60 is experimental."));
                ui::SelectOptions select;
                if (ui::BeginSelect("##fps", Tr(FpsChoices[0].label), select))
                    ui::EndSelect();
                ui::EndSettings();
            }
            break;
        }
        case 5: // Fields, cards, list rows, section headers
        {
            static char empty[64] = "", filled[64] = "Spyro", search[64] = "", searched[64] = "Gill";
            Grid grid(4, ui::height::Normal + ui::space::L);
            const char* const names[] = { "Empty, its hint", "Filled", "Focus", "Editing / disabled" };
            grid.Header(names);
            const float w = grid.cellW - ui::space::M;
            grid.RowLabel(0, "TextField");
            ui::TextFieldOptions hint;
            hint.hint = Tr("Name (optional)");
            hint.width = w;
            grid.At(0, 0);
            ui::TextField("##f1", empty, sizeof empty, hint);
            grid.At(0, 1);
            ui::TextField("##f2", filled, sizeof filled, hint);
            grid.At(0, 2);
            ui::PreviewState({ .focused = true });
            ui::TextField("##f3", filled, sizeof filled, hint);
            grid.At(0, 3);
            ui::PreviewState({ .pressed = true });
            ui::TextField("##f4", filled, sizeof filled, hint);
            grid.RowLabel(1, "SearchField");
            grid.At(1, 0);
            ui::SearchField("##s1", search, sizeof search, Tr("Search the catalogue"), w);
            grid.At(1, 1);
            ui::SearchField("##s2", searched, sizeof searched, Tr("Search the catalogue"), w);
            grid.At(1, 2);
            ui::PreviewState({ .focused = true });
            ui::SearchField("##s3", searched, sizeof searched, Tr("Search the catalogue"), w);
            grid.At(1, 3);
            ui::TextFieldOptions off = hint;
            off.disabled = true;
            ui::TextField("##f5", filled, sizeof filled, off);
            grid.End(2);

            // Cards side by side: a tile, a page's panel, an inset area; list rows and a section header inside.
            const float cardW = (ImGui::GetContentRegionAvail().x - 2 * ui::space::L) / 3, cardH = 360;
            ImVec2 row = ImGui::GetCursorScreenPos();
            ui::CardOptions tile;
            ui::BeginCard("##tile", ImVec2(cardW, cardH), tile);
            Caption("Card (tile): ListRow states");
            ui::ListRowOptions plain;
            ui::ListRow(Tr("Language"), plain);
            ui::PreviewState({ .hovered = true });
            ui::ListRow(Tr("Video"), plain);
            ui::PreviewState({ .focused = true });
            ui::ListRow(Tr("Keyboard"), plain);
            ui::ListRowOptions selected;
            selected.selected = true;
            ui::ListRow(Tr("Saves"), selected);
            ui::ListRowOptions disabled;
            disabled.disabled = true;
            ui::ListRow(Tr("Disc"), disabled);
            ui::EndCard();
            ImGui::SetCursorScreenPos(ImVec2(row.x + cardW + ui::space::L, row.y));
            ui::CardOptions panel;
            panel.panel = true;
            ui::BeginCard("##panel", ImVec2(cardW, cardH), panel);
            Caption("Card (panel): rows with more");
            ui::ListRowOptions rich;
            rich.subtitle = Tr("Ready to play");
            const std::string size = ui::FormatSize(8738846720ull);
            rich.trailing = size.c_str();
            rich.icon = ui::Icon::Disc;
            rich.chevron = true;
            ui::ListRow(GameName("ssa"), rich);
            ui::ListRowOptions folder;
            folder.icon = ui::Icon::Folder;
            folder.chevron = true;
            ui::ListRow(Tr("Show in folder"), folder);
            // `leading`: room for a picture the caller draws (a figure's).
            ui::ListRowOptions picture;
            picture.subtitle = Tr("Water");
            picture.leading = ui::height::Large - 2 * ui::space::XXS + ui::space::S;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ui::ListRow(GameName("giants", GameNameForm::Short), picture);
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + ui::space::S + 24, at.y + ui::height::Large / 2), 24, ui::Col(ui::color::AccentFill));
            ui::SectionHeader(Tr("Language"), Tr("Only contacts github.com."));
            ui::EndCard();
            ImGui::SetCursorScreenPos(ImVec2(row.x + 2 * (cardW + ui::space::L), row.y));
            ui::CardOptions inset;
            inset.inset = true;
            ui::BeginCard("##inset", ImVec2(cardW, cardH), inset);
            Caption("Card (inset): EmptyState");
            ui::EmptyState(ui::Icon::Search, Tr("No figure matches"), Tr("Filters"));
            ui::EndCard();
            break;
        }
        case 6: // Badges, chips, status, progress
        {
            Caption("Badge: element colours, a rare tag, a seat, Dark");
            for (unsigned element : Elements)
            {
                ui::Badge(Tr("Water"), element);
                ImGui::SameLine(0, ui::space::XS);
            }
            ui::Badge(Tr("Legendary"), 0xF2B21B);
            ImGui::SameLine(0, ui::space::XS);
            const std::string seat = SeatTagSample();
            ui::Badge(seat.c_str(), Accent);
            ImGui::SameLine(0, ui::space::XS);
            ui::Badge(Tr("Dark"), 0x1A1A22);
            ImGui::Dummy(ImVec2(0, ui::space::XS));

            Caption("Chip: filter off, hover, focus, on (accent, an element), disabled; sort ascending, descending, focus");
            const ui::State chipStates[] = { {}, { .hovered = true }, { .focused = true }, {}, {}, {} };
            for (int i = 0; i < 6; i++)
            {
                ui::ChipOptions options;
                options.selected = i == 3 || i == 4;
                options.color = i == 4 ? Elements[3] : 0;
                options.disabled = i == 5;
                ImGui::PushID(i);
                ui::PreviewState(chipStates[i]);
                ui::Chip(Tr("Fire"), options);
                ImGui::PopID();
                ImGui::SameLine(0, ui::space::XS);
            }
            ImGui::SameLine(0, ui::space::L);
            for (int i = 0; i < 3; i++)
            {
                ui::ChipOptions sort;
                sort.kind = ui::ChipKind::Sort;
                sort.direction = i == 1 ? -1 : 1;
                ImGui::PushID(10 + i);
                if (i == 2)
                    ui::PreviewState({ .focused = true });
                ui::Chip(Tr("Element"), sort);
                ImGui::PopID();
                ImGui::SameLine(0, ui::space::XS);
            }
            ImGui::NewLine();
            ImGui::Dummy(ImVec2(0, ui::space::XS));

            Caption("StatusPill: never a colour alone");
            const std::pair<ui::Status, const char*> statuses[] = { { ui::Status::Success, Tr("Ready to play") },
                { ui::Status::Info, Tr("Ready to install") }, { ui::Status::Busy, Tr("Checking your disc") },
                { ui::Status::Warning, Tr("Needs your disc") }, { ui::Status::Error, Tr("Disc not found") }, { ui::Status::Neutral, Tr("Open") } };
            for (const auto& [status, text] : statuses)
            {
                ui::StatusPill(status, text);
                ImGui::SameLine(0, ui::space::XS);
            }
            ImGui::NewLine();
            Caption("The status shapes alone (the sidebar's dot)");
            ImVec2 at = ImGui::GetCursorScreenPos();
            for (int i = 0; i < 6; i++)
                ui::DrawStatusIcon(ImGui::GetWindowDrawList(), ui::Status(i), ImVec2(at.x + 10 + 32 * float(i), at.y + 10), 14);
            ImGui::Dummy(ImVec2(0, 20));
            ImGui::Dummy(ImVec2(0, ui::space::XS));

            Caption("ProgressBar 0 %, 46 %, 100 % (with its percentage); IndeterminateBar; Spinner");
            const float barW = (ImGui::GetContentRegionAvail().x - 3 * ui::space::L) / 4;
            ui::ProgressBar(0, barW, true);
            ImGui::SameLine(0, ui::space::L);
            ui::ProgressBar(0.46f, barW, true);
            ImGui::SameLine(0, ui::space::L);
            ui::ProgressBar(1, barW, true);
            ImGui::SameLine(0, ui::space::L);
            ImGui::BeginGroup();
            ui::IndeterminateBar(barW - 40);
            ImGui::EndGroup();
            ImGui::SameLine(0, ui::space::S);
            ui::Spinner();
            break;
        }
        case 7: // Toasts, key caps, the pad legend
        {
            Caption("KeyCap: each pad family's buttons, then keyboard keys");
            const std::pair<ui::Glyphs, const char*> families[] = { { ui::Glyphs::Xbox, "Xbox" }, { ui::Glyphs::PlayStation, "PlayStation" },
                { ui::Glyphs::Nintendo, "Nintendo" } };
            for (const auto& [family, name] : families)
            {
                ImVec2 row = ImGui::GetCursorScreenPos();
                Caption(name);
                ImGui::SetCursorScreenPos(ImVec2(row.x + 110, row.y));
                for (int b = 0; b < 8; b++)
                {
                    ui::KeyCap(ui::PadButton(b), family);
                    ImGui::SameLine(0, ui::space::XS);
                }
                ImGui::NewLine();
            }
            {
                ImVec2 row = ImGui::GetCursorScreenPos();
                Caption("Keyboard");
                ImGui::SetCursorScreenPos(ImVec2(row.x + 110, row.y));
                for (const char* key : { Tr("Return"), Tr("Escape"), "Tab", Tr("Space"), "F6", "F7", "F8" })
                {
                    ui::KeyCap(key);
                    ImGui::SameLine(0, ui::space::XS);
                }
                ImGui::NewLine();
            }
            ImGui::Dummy(ImVec2(0, ui::space::XS));
            Caption("PadLegend: the bottom hint bar, in each family and on the keyboard (a hint the keyboard lacks is left out)");
            const ui::PadHint hints[] = { { ui::PadButton::South, Tr("Return"), Tr("Open") }, { ui::PadButton::East, Tr("Escape"), Tr("Back") },
                { ui::PadButton::West, nullptr, Tr("Edit") }, { ui::PadButton::North, "Tab", Tr("Filters") },
                { ui::PadButton::Start, nullptr, Tr("Settings") } };
            const float legendW = ImGui::GetContentRegionAvail().x * 0.62f;
            for (ui::Glyphs family : { ui::Glyphs::Xbox, ui::Glyphs::PlayStation, ui::Glyphs::Nintendo, ui::Glyphs::Keyboard })
            {
                ui::PadLegend(hints, family, legendW);
                ImGui::Dummy(ImVec2(0, ui::space::XXS));
            }
            // The toasts, at the window's bottom right: info, success, an error with the focus (its key closes it).
            if (m_toasts.Items().empty())
            {
                Toast(Tr("Log copied"), ui::ToastKind::Info);
                Toast(Tr("Game installed: press PLAY"), ui::ToastKind::Success);
                Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
            }
            m_toasts.PreviewFocus(true);
            m_toasts.Draw(0, 300);
            break;
        }
        case 8: // The modal shell
        case 9: // A danger confirmation
        {
            static bool modal = true, danger = true;
            Caption("Behind: the page, under the scrim");
            ui::ButtonOptions behind;
            ui::Button(Tr("Show in folder"), behind);
            if (page == 8)
            {
                modal = true;
                if (ui::BeginModal("##gallerymodal", Tr("Report a bug"), &modal, ui::ModalSize::Medium))
                {
                    ui::Text(ui::TextStyle::Body, Tr("A launcher for the native PC ports of Skylanders. No game code or art: it all comes from your disc."),
                        ui::color::InkSoft);
                    static bool tick = true;
                    ui::Checkbox(Tr("Fullscreen"), &tick);
                    ui::ModalActions(&modal, Tr("OK"), Tr("Cancel"));
                    ui::EndModal();
                }
            }
            else
            {
                danger = true;
                ui::ConfirmDialog("##gallerydanger", Tr("Uninstall everything"), Tr("Hold to delete all of this."), Tr("Uninstall everything"),
                    Tr("Cancel"), &danger, ui::ModalKind::Danger);
            }
            break;
        }
        case 10: // A menu under its pill, a tooltip
        {
            ui::PillOptions more;
            more.icon = ui::Icon::More;
            ui::Pill(Tr("Edit"), more);
            if (!ImGui::IsPopupOpen("##gallerymenu"))
                ui::OpenMenu("##gallerymenu");
            if (ui::BeginMenu("##gallerymenu"))
            {
                ui::MenuItem(Tr("Edit"), { ui::Icon::None, false });
                ui::MenuItem(Tr("Favourite"), { ui::Icon::Star, true });
                ui::MenuItem(Tr("Show in folder"), { ui::Icon::Folder });
                ui::MenuSeparator();
                ui::MenuItem(Tr("Remove"), { ui::Icon::Trash, false, true });
                ui::MenuItem(Tr("Restore"), { ui::Icon::None, false, false, true });
                ui::EndMenu();
            }
            ImGui::SameLine(0, 360);
            ImGui::BeginGroup();
            Caption("Tooltip: under its control, inside the window");
            ui::IconButtonOptions gear;
            gear.kind = ui::ButtonKind::Secondary;
            ui::PreviewState({ .hovered = true });
            ui::IconButton("##tipgear", ui::Icon::Gear, Tf("{} settings", GameName("giants")).c_str(), gear);
            ImGui::EndGroup();
            break;
        }
        case 11: // An open select
        {
            Caption("Select, open: the chosen row has a check");
            ui::SelectOptions options;
            options.width = 360;
            // Opened as a click would: the combo's popup id is ImGui's ("##ComboPopup" under the combo's id).
            const ImGuiID popup = ImHashStr("##ComboPopup", 0, ImGui::GetID("##galleryselect"));
            if (!ImGui::IsPopupOpen(popup, 0))
                ImGui::OpenPopup(popup);
            if (ui::BeginSelect("##galleryselect", Tr("Automatic"), options))
            {
                for (const Choice& choice : GlyphChoices)
                    ui::SelectOption(Tr(choice.label), std::string_view(choice.value) == "auto");
                ui::EndSelect();
            }
            break;
        }
        }
        ImGui::End();
        if (page != 7)
            m_toasts.Clear();
    }

    void App::TestGalleryScript(uint64_t now)
    {
        // Every page at 1x, then at 2x; each shot once the page has settled.
        static const float Scales[] = { 1, 2 };
        const int page = m_testStep % PageCount, pass = m_testStep / PageCount;
        if (pass >= int(std::size(Scales)))
        {
            m_running = false;
            return;
        }
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        if (page == 0)
        {
            m_testUiScale = m_uiScale = Scales[pass];
            SDL_SetWindowSize(m_window, int(WindowW * m_uiScale), int(WindowH * m_uiScale));
        }
        m_galleryPage = page;
        m_testShot = fmt::format("gallery-{}-{}x-{:02}-{}", i18n::Current(), int(Scales[pass]), page + 1, Pages[page].name);
        m_testAt = now + (page == 0 ? 1200 : 600);
    }
}

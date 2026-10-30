# Launcher UI Guide

The launcher has its own Skylanders look, designed by dorydev: dark greys, one light-blue accent, Bungee titles over Outfit
text, each game's own colours on its main action, chunky rounded buttons. This guide is the look's **system**: the tokens
(every size and colour, by role), the components drawn from them, the consistency rules, the gallery that shows them, and
the map of what each screen still has to move over (the migration, "phase B").

**The rule: screens use components only.** A screen lays out components (`src/launcher/ui/components/`) and reads tokens
(`src/launcher/ui/theme/tokens.h`). It never sizes a widget by hand, never writes a colour, a radius, a font size or a
spacing of its own, never draws a button, a field, a dialog or a progress bar with the draw list. What a screen needs and no
component has becomes a component (or an option of one) first, with its states drawn and a place in the gallery.

## Structure
- `src/launcher/ui/theme/tokens.h` - the tokens (below). `launcher_theme.h` beside it - the palette (dorydev's colours),
  each game's `GameStyle`, and the ImGui style (`LauncherTheme::Apply`); its old size constants stay until the screens no
  longer use them (phase B removes each one as it goes unused).
- `src/launcher/ui/components/` - the components: `core` (context, states, text styles, colour helpers, focus ring, icons),
  `buttons`, `inputs`, `layout`, `feedback`, `format` (values in the player's language); `components.h` includes them all.
- `src/launcher/game_names.h` - every game's full and short name (`GameName`).
- `src/launcher/ui/gallery.cpp` - the component gallery (`SKY_LAUNCHER_TEST_GALLERY`).
- `src/main.cpp` - picks the game and disc, shows the launcher, starts the runtime.
- `src/launcher/` - the launcher: `launcher.{h,cpp}` (disc checks, settings file), `launcher_games.cpp` (the game table),
  `game_install.{h,cpp}` (installing the game with the kit's SkyBuild, `docs/install.md`), `ui/`.
- `src/launcher/game_settings.{h,cpp}`, `shortcut.{h,cpp}`, `ui/game_settings_page.cpp` - a game's own settings: its per-game video copy,
  delete data and uninstall (no UI), the desktop shortcut files (no UI, one builder per system), and the page (below).
- `src/assets/` - the game's art for the window (`launcher_assets`): a thin layer over `src/shared/art/` (see "Disc art").
- `src/launcher/ui/launcher_app.h` - the window's class (`App`) and helpers shared by the page files. Its widget helpers
  (`PillButton`, `BigButton`, `HoldButton`, `Toggle`, `BeginCard`, `Chip`, `Badge`, `ProgressBar`, `FocusRing`, `LevelSlider`,
  `DrawTabBar`, `SettingRow`, `DrawMainAction`) are now thin wrappers over the components, kept so the screens build until
  phase B calls the components directly; then they go.
- `src/launcher/ui/launcher_ui.{h,cpp}` - the window (Dear ImGui on SDL_Renderer): backdrop, sidebar, the Home, Play and Settings
  pages, disc verification and the screenshot tests.
- `src/launcher/install_checklist.{h,cpp}`, `ui/install_page.cpp` - the install card (docs/install.md);
  `game_run.{h,cpp}`, `ui/support_page.cpp` - game logs, the crash dialog, the portal test (docs/support.md);
  `updater.{h,cpp}`, `ui/update_page.cpp` - updates, the banner and About (docs/updates.md);
  `shared/mini_json.{h,cpp}` - the JSON reader for GitHub's answer.
- `src/launcher/ui/collection_*.{h,cpp}` - the collection page on the shared library: the portal at the next start, the bar, the shelf, the figure sheet, Add a figure, Manage (`docs/collection_ui.md`).
- `src/launcher/ui/game_look.cpp` - the game page in each game's own look: the main action's options (`DrawMainAction`), the logo's
  arrival (`LogoDrop`, `DrawHeroLogo`), the disc's pictures as textures (`LoadArt`, `CachedArt`), `SystemReducesMotion`.
- `src/shared/art/` - SSARecomp's disc picture readers and per-game provider (`game_art`), copied by its
  `tools/sync_skylauncher.sh`: Xenos textures, SSA's `strm`/APKF, Giants' IGA/IGZ.
- `src/shared/` - what isn't the launcher's alone: the disc reader (`vfs`), the platform layer, `stb_impl.cpp`, the embedded fonts
  and the figure engine.
- `src/shared/collection/` and `src/shared/figures/` - SSARecomp's collection library (copied by `tools/sync_collection.sh`:
  tag, crypto, catalogue, folder, locks, backups) and the launcher's `figure_icons` (generated icons and their `IconStyle`).
- `lib/` - third-party code (SDL, Dear ImGui, XenonRecomp as submodules; stb and miniz vendored).
- `resources/fonts/` - licensed fonts (Bungee, Outfit) with their OFL licences; they are also built into the executable.
- `docs/settings.md` - the settings file the launcher writes and the game reads.

## Tokens
All in `tokens.h`, namespace `ui`. Sizes are launcher points (the window scales them, "Size on high-DPI screens").

**Type: five styles** (`ui::TextStyle`, `ui::Text`, `ui::PushText`). Caption, Body and Label are Outfit; Title and Display are
Bungee, which shows every word in capitals.

| Style | Size | Font | For |
|---|---|---|---|
| `Caption` | 14 | Outfit (SemiBold on badges, chips, key caps) | hints, status lines, badges, a card's meta: the smallest text there is |
| `Body` | 17 | Outfit; buttons in SemiBold | sentences, button labels, fields, menus, list rows |
| `Label` | 20 | Outfit SemiBold | what leads a row: a game in the sidebar, tabs, a big button's label |
| `Title` | 24 | Bungee | section and dialog titles, the main action's label |
| `Display` | 34 | Bungee | page titles |

The old sizes map onto them: 12, 13 and 14 (`CaptionSize`, `SmallSize - 1`, `SmallSize`, `VersionSize`) are Caption; 16
and 17 (`SidebarSmallSize`, `TextSize`) Body (the sidebar's status: Caption); 19 and 20 (`SidebarTextSize`) Label; 22, 26 and 28
(`TitleSize`, `TitleSize + 4`, `HeadlineSize - 6`, the main action's 40 % of 64) Title; 34 and 48 (`HeadlineSize`, `HeroSize`)
Display. A text that must fit a width shrinks with `ui::FitSize` from its style's size, never from a number of its own.

**Spacing: 4-point steps** (`ui::space`). `XXS` 4 (an icon and its text), `XS` 8 (inside a group), `S` 12 (between controls
of a row or list), `M` 16 (between groups in a card), `L` 24 (a card's padding, between cards; was `Pad`), `XL` 32 (page
margins, before a section, between tabs; was `TabGap` 34), `XXL` 48 (an empty state's air).

**Radii: three, by role** (`ui::radius`). `Control` 10 (buttons, fields, selects, menus, list rows; was `RadiusSmall`),
`Card` 14 (tiles and small cards, the main action, toasts; was `Radius`), `Panel` 22 (a page's big card, dialogs; was
`RadiusLarge`). Chips, pills, badges, toggles, progress bars and icon buttons have round ends (height / 2).

**Heights** (`ui::height`). `Small` 32 (chips, pills, compact buttons, the toggle, a select's rows), `Normal` 40 (buttons,
fields, selects, tabs, segmented controls, one-line list rows), `Large` 56 (list rows with a subtitle, the sidebar's rows,
settings rows, big buttons; was `GameRowH`), `Main` 64 with `MainWidth` 300 and `MainGear` 64 (the main action; were
`PlayButtonH/W`, `PlayGearW`). `Mark` 22 (a checkbox, a radio, a slider's knob), `SettingControl` 320 (a settings row's control column). `ui::width::Page` 940 is a settings page's column.

**Lines** (`ui::stroke`). `Hairline` 1 (card edges, dividers), `Edge` 1.5 (what shows a state: a toggle's track, a danger
button, a field being edited), `Selection` 3 (the line under the selected tab, the bar of a selected row; was `TabUnderlineH`).

**Icons** (`ui::icon`). One family, **Lucide** (ISC, https://lucide.dev): outlined, round ends, its stroke 1/12 of the icon's
size. `Small` 16 on a line of Caption or Body text (a state, a pill, a chip, a select's chevron, a menu's check), `Normal` 20
(buttons, list rows, toasts), `Large` 32 (an empty state's or a drop zone's picture, the main action's gear); no other size.
`ui::DrawIcon(draw, Icon, centre, size, color)` centres Lucide's 24-point grid on `centre`, on whole pixels; `ui::DrawSpinner`
turns Lucide's loader. The icons come from `resources/fonts/Lucide.ttf`, Lucide's icon font cut down to the icons used and
built into the executable (`cmake/embed_fonts.cmake`); to add one, put its Lucide name in `tools/lucide_subset.py`'s list, run
`python3 tools/lucide_subset.py <the lucide-static npm package, untarred>` (it rewrites the font and
`components/lucide.h`), then map it in `DrawIcon` (`components/core.cpp`). Nothing is drawn by hand but the GitHub mark (Lucide
has no brand marks).

**Text lines** (`ui::CapMiddle`, `ui::CapTop`, `ui::Baseline`). What is centred is the capitals: a line of text beside an
icon or in a control has its capitals' middle on the icon's centre or the control's middle; two sizes on one line share a
baseline (a name and its note, a label and its pad button); lines stacked in a row (a title and its subtitle, a label and its
hint) are centred from the first line's capitals to the last one's baseline.

**Focus** (`ui::focus`). One ring around every focusable control: `Width` 3, `Offset` 3 (the gap), in `Context::accent`:
the shown game's colour on its pages (Spyro's Adventure's blue, Giants' gold, Swap Force's green: `GameStyle::glow`), the
launcher's blue elsewhere. It shows only with the keyboard or the pad: the pointer moving, pressing or scrolling hides it
(`App::HandleEvent`), the next key or button that moves the focus shows it. Never on what is not pressed (the sidebar's
title is a link for the pointer only, not a stop). A page's first stop is its main action: Home, the card of the game
shown last; the game page, the main action; a settings page, its selected tab.

**Colours by role** (`ui::color`, values from `launcher_theme.h`).

| Role | Token | Value | Use |
|---|---|---|---|
| Surfaces | `Inset`, `Surface`, `Raised` (+ `Hover`, `Pressed`) | 1C1C21, 26262C, 3A3A42 | fields; cards; buttons and menus |
| On the art | `OverArt` (+ `Hover`, `Pressed`), `OverArtEdge` | 2C2C34, 50505E | buttons on the game's art and the band |
| Lines | `Hairline`, `Edge` | 4A4A55, 7A7A88 | decoration; what shows a state (3:1 on the surfaces) |
| Text | `Ink`, `InkSoft`, `Dim` | F4F4F6, D0D0D8, B8B8C4 | body; secondary; hints and disabled labels |
| Accent | `AccentFill` (+ `Hover`, `Pressed`), `OnAccent` | 6BBAFF, 8ECBFF, 2F86E0; 0A1B33 | primary buttons, on states, selections; its ink |
| Meaning | `Info`, `Success`, `Warning`, `Danger` | 6BBAFF, 48D17A, FFC65C, FF817A | status icons and words, toast rings, danger edges |
| Danger fill | `DangerFill` (+ `Hover`, `Pressed`) | 6B4345 | a danger button (Danger mixed 32 % into the surface) |
| Selection | `Selected` | F08A12 | the bar of a selected row (the sidebar's orange) |
| Scrim | `Scrim` at `elevation::ScrimAlpha` 150 | 0B0F2E | behind a dialog |

**Elevation** (`ui::elevation`): `Flat` (on the page), `Card` (Surface at 235 alpha, a 1.5-point hairline), `Raised`
(Raised at 245, a hairline, a soft shadow: menus, tooltips, toasts), `Overlay` (Surface at 250, over the scrim: dialogs).

**Motion** (`ui::motion`, ms): `Fast` 120 (hover, press, a toggle's knob), `Normal` 200, `Slow` 300 (the logo's drop), `Spin`
900 (a spinner's turn, an indeterminate bar's sweep), `HoldHint` 2500 (how long "Hold to confirm" stays after a short tap).
Every animation stops with reduced motion (`Context::reduceMotion`, "The logo's arrival"): spinners stand still, the
indeterminate bar becomes still dashes.

**Disabled**: one look, everything at `DisabledAlpha` 0.45 (`ImGuiStyle::DisabledAlpha` too).

### Contrast
WCAG AA, measured on every surface a colour sits on (page 1E1E24 to 32323C, inset, surface, raised, band 3A3A44):

| Colour | On page / inset / surface | On raised / band | On raised hover |
|---|---|---|---|
| `Ink` | 11.5 to 15.4 | 10.2 | 8.4 |
| `InkSoft` | 8.3 to 11.1 | 7.3 | 6.0 |
| `Dim` | 6.5 to 8.6 | 5.7 | 4.7 |
| `Accent` / `Info` | 6.1 to 8.2 | 5.4 | 4.4 (not for text there) |
| `Success` | 6.4 to 8.6 | 5.7 | 4.7 |
| `Warning` | 8.1 to 10.9 | 7.2 | 5.9 |
| `Danger` | 5.2 to 7.0 | 4.6 | 3.8 (not for text there) |
| `Edge` (a state's line) | 3.0 to 4.0 | 2.7 | - |
| Giants' / Swap Force's focus | 7.1 to 11.3 | 6.3 / 7.5 | 5.2 / 6.1 |

Text needs 4.5:1, large text and the parts that show a control's state 3:1: all pass where they are used. `OnAccent` on
the accent is 8.3:1, `Ink` on `DangerFill` 7.6:1. A badge's ink is chosen for contrast on its fill (`ui::DrawBadge`).

## Components
Every component: one function (or a Begin/End pair, or a small class), sized from the tokens only, every state drawn
(normal, hover, focus, pressed, disabled, loading where it applies, in that order of precedence, the focus ring over
them), reachable with the keyboard and the pad (ImGui's navigation; `ui::Hit` is the pressable area, ImGui's own square
cursor hidden). A label is translated by the caller (`Tr`); "Label##id" gives an id without changing the text. The state
model is `ui::State` read by `ui::ItemState`; the gallery forces one with `ui::PreviewState`. `ui::Ctx()` is set by the
window each frame (`App::Frame`): the fonts, the focus colour, the pad family (`Glyphs`), reduced motion, the time.

### Button
| | |
|---|---|
| When | Any action. `Primary`: the one action a page or dialog is for (one in view). `Secondary`: every other. `Danger`: what deletes, resets or overwrites (always `hold`, or inside a danger dialog). `Ghost`: a quiet action in a toolbar or a row. |
| API | `bool ui::Button(label, ButtonOptions{kind, size, icon, trailing, width, disabled, loading, hold, overArt, tooltip})`, `ui::ButtonWidth(label, options)`. `size`: Small 32, Normal 40, Large 56 (Label text). `width` 0: the label's; > 0: at least; < 0: the available width. `trailing` `ChevronDown` for one that opens a list. `overArt`: the band's greys, on the game's art. |
| States | Normal; hover (lighter); focus (ring); pressed (darker, the label 1 point lower); disabled (0.45 alpha); loading (a spinner before the label, not pressable). Hold: a timer icon in place of the icon; held, a sweep fills it (`HoldConfirm`, one second; Space, Enter and A hold it too; leaving or letting go cancels); its hint "Hold to confirm" (or `tooltip`) shows under it on hover, on focus and for 2.5 s after a tap too short. |

### MainAction
| | |
|---|---|
| When | The game page's big split button only: PLAY, INSTALL, UPDATE, CHOOSE DISC, and the game's settings in its gear segment. |
| API | `MainActionResult ui::MainAction(MainActionOptions{label, enabled, progress, indeterminate, style, gearTooltip, takeFocus, showFocus})` at the cursor; `result.hit` is `Main`, `Settings` or `None`. `style` is the game's `GameStyle`. `App::DrawMainAction` calls it. |
| States | Each half on its own: hover and focus 10 % lighter, pressed 10 % darker; disabled greyed with a faded label (the gear lights again under the pointer); busy with a known end: dark, tinted up to `progress`, a bar along the bottom, the label ending in the percentage; `indeterminate`: a band sweeping along that bar. Focus: one ring around the whole button in the game's colour. ("Each game's look".) |

### Pill
| | |
|---|---|
| When | A small, fully round action: in a toolbar, on the game's art (the controller, the portal, the play time and achievements), a menu's opener ("Filters"). `selected`: a mode that is on. |
| API | `bool ui::Pill(label, PillOptions{icon, trailing, prefix, iconColor, selected, disabled, overArt, width, tooltip})`, `ui::PillWidth(label, options)`; height `Small`. `prefix`: words before the icon, a dot XS apart from both ("12 h 34 · trophy 6/38"); `iconColor`: the icon in a state's colour (`ColorOf`). |
| States | Normal, hover, focus, pressed, disabled; selected (the accent fill). |

### IconButton
| | |
|---|---|
| When | An icon alone: a close cross, a gear, "more" (three dots), a seat's X. Always with its tooltip: it is the icon's only words. |
| API | `bool ui::IconButton(id, Icon, tooltip, IconButtonOptions{kind, size, selected, disabled, overArt})`; square, round. |
| States | As Button (Ghost has no fill until pointed at); the tooltip on hover and focus. |

### Tabs
| | |
|---|---|
| When | The pages of a settings screen (two to six words). |
| API | `TabChange ui::Tabs(id, labels, &current, TabsOptions{bumpers, focusSelected, showFocus})`: `None`, `Click` or `Bumper`. LB / RB change tab; with a pad in use their key caps show at the row's ends. Height `Normal`, `space::XL` between tabs. |
| States | The selected tab bold `Ink` with the accent line (`stroke::Selection`); others `Dim`, `Ink` under the pointer with a hairline; focus ring. |

### Segmented
| | |
|---|---|
| When | Two to four exclusive short choices that fit side by side (x1 / x2 / x3, Grid / List). More, or long labels: a Select. |
| API | `bool ui::Segmented(id, labels, &current, width = 0, disabled)`; one track, `Normal` high; `width` 0: each segment as wide as the widest label. |
| States | The chosen segment in the accent; hover and pressed on the others; focus ring on one segment; disabled. A label too long is cut with "...". |

### Select (combo)
| | |
|---|---|
| When | One choice among many (languages, controllers, scale). |
| API | `if (ui::BeginSelect(id, preview, SelectOptions{width, disabled})) { ui::SelectOption(label, selected, disabled)...; ui::EndSelect(); }`; the value is cut with "..." before the chevron; an option's "label##id" shows its label only. |
| States | Closed: the inset box with the value and a chevron; hover, focus ring, disabled. Open: the accent edge, the chevron up, a list of `Small` rows, a check on the chosen row (not colour alone), the keyboard and pad start on it. |

### Toggle
| | |
|---|---|
| When | A setting that is on or off and acts at once. In a settings row give it "##id": the row's label is its label. |
| API | `bool ui::Toggle(label, &value, disabled)`: the switch (56 by 32), then its label. |
| States | Off: the inset track with its `Edge` (3:1); on: the accent with the dark knob; the knob slides (`motion::Fast`); hover lighter; pressed, a larger knob; focus ring around the track; disabled. |

### Checkbox
| | |
|---|---|
| When | One of several independent options, or an option of an action ("Also delete my saves"). A setting that acts at once is a Toggle. |
| API | `bool ui::Checkbox(label, &value, disabled)`; the box `Mark` (22), its label after it; the whole row is pressable. |
| States | Off: the inset box with its `Edge`; on: the accent box with a check; hover; focus ring; disabled. |

### Radio
| | |
|---|---|
| When | One choice among two to five shown at once. |
| API | `bool ui::Radio(label, selected, disabled)`: true when chosen. |
| States | Off: a ring in `Edge`; on: an accent ring and dot; hover; focus ring; disabled. |

### Slider
| | |
|---|---|
| When | A number in a short range where the position means something (a figure's level). |
| API | `bool ui::Slider(id, &value, min, max, SliderOptions{width, disabled, showValue})`; the arrows and the D-pad step it when focused. |
| States | Track and fill, a round knob; hover and pressed lighter knob; focus ring; disabled; the value after it. |

### TextField and SearchField
| | |
|---|---|
| When | Text the player types: a name, a nickname, gold. SearchField: filtering a list. |
| API | `bool ui::TextField(id, buffer, size, TextFieldOptions{hint, width, disabled, flags, callback})`; `bool ui::SearchField(id, buffer, size, hint, width)`: a magnifier, a cross that clears it, Escape clears it. Height `Normal`. |
| States | Empty with its hint in `Dim`; filled; hover; focus ring (keyboard / pad, not editing); editing: the accent edge; disabled. |

### Card
| | |
|---|---|
| When | A group of content on a page. `panel`: a page's big card (Settings); a tile otherwise; `inset`: a sunken area inside a card. |
| API | `ui::BeginCard(id, size, CardOptions{panel, inset, padding, flags})` ... `ui::EndCard()`; `ImGuiChildFlags_AutoResizeY` for one as tall as its content. |
| States | None of its own (its content has them). |

### Roomy child (a scrolling area)
| | |
|---|---|
| When | Every scrolling area that is not a card: the page columns, the shelf, Add a figure's grid, the dialogs' scrolling bodies (Edit, Change the bottom, Manage), the save backups. A plain child window clips at its content's edge and cuts the focus rings, hover boxes and borders there. |
| API | `ui::BeginRoomyChild(id, size, flags, windowFlags, bleed = focus::Reach)` ... `ui::EndRoomyChild()`: the clip rect reaches `bleed` past the content on every side, the padding puts the content back where the cursor was; `size` is the content's (BeginChild's rule); what follows is laid out as after a plain child of that size. The page column (`App::BeginPageColumn`) uses `space::M` (a title's button raised onto its capitals). Its scrollbar runs the room's height: a panel beside it (Add a figure's versions) is as tall. |
| States | None. |

### ListRow
| | |
|---|---|
| When | One row of a list or a menu-like page: a backup, a trashed figure, a setting that opens a page, a game. |
| API | `bool ui::ListRow(title, ListRowOptions{subtitle, trailing, icon, chevron, selected, disabled, width, image, imageWidth, note, status, statusIcon, leadWidth, progress, progressColor, dim})`; `Normal` high, `Large` with a subtitle; the whole row is pressable. `image` draws a picture at the row's left, `imageWidth` wide and the row's height less `XXS` above and below (a game's logo, a figure's). `leadWidth`: the leading column (the icon or the image centred in it), one per list, so every row's title starts on one edge. `status`: the subtitle's state, its icon (`statusIcon`, else the status's) on the subtitle's line, at the title's left edge, centred on its capitals. One line: the title's capitals on the row's middle; two: the title and the subtitle centred as a block; `trailing` on the title's baseline. |
| States | Hover, pressed, focus ring, disabled; selected: the raised grey and the orange bar (as the sidebar). Title and subtitle are cut with "..." before the trailing part. |

### Settings rows
| | |
|---|---|
| When | Every setting: the one settings idiom (Video, Controls, Launcher, the game tabs). |
| API | `if (ui::BeginSettings(id)) { ui::SettingRow(label, hint, note); <its control>; ...; ui::EndSettings(); }`; the control column is `SettingControl` (320) wide, rows at least `Large`, a hairline between them. The label is drawn once the control is, centred on it (one line: its capitals on the control's middle; with a hint: the two as a block); a hint on two lines moves the control down to the block's middle. `note`: a small word after the label, on its baseline (a key's pad button). The hint is drawn, not an item (a table lowers a row's text items to its fields' baseline). **One alignment rule: every control ends on the column's right edge.** A Select, a field or a slider fills the column; a Button, Pill, IconButton, Toggle, Checkbox, Segmented, KeyButton or KeyCap that starts the column's line moves there by itself (`ui::SettingControl`, which they call); `ui::AlignRight(width)` does it for a group of buttons; `BeginSettings(id, controlWidth)` widens the control column for a row of buttons that needs it (never narrower than 320). The table doesn't clip its columns (`ImGuiTableFlags_NoClip`): a control at a column's edge keeps its whole focus ring. |
| States | Those of its controls. |

### SectionHeader
| | |
|---|---|
| When | A section's title inside a card or a page. |
| API | `ui::SectionHeader(title, subtitle = nullptr, divider = true)`: a hairline above (not on the first), the Title text, a Caption under it. |
| States | None. |

### Modal (dialog)
| | |
|---|---|
| When | Every dialog: a question, a sheet, What's new, the problem report. One shell; nothing draws its own. |
| API | `if (ui::BeginModal(id, title, &open, ModalSize::{Small 480, Medium 680, Large 880})) { body; ui::ModalActions(&open, confirm, cancel, ModalKind::{Normal, Danger}); ui::EndModal(); }`; `ui::ConfirmDialog(id, title, body, confirm, cancel, &open, kind)` for a whole question. |
| States | The scrim; the title with a close cross (IconButton, "Close"); actions bottom right, Cancel then the confirm button (Primary, or Danger); the focus starts on the confirm button, on Cancel in a danger dialog; B and Escape close it (= Cancel) unless a field or list inside answers first. | The close cross sits on the title's capitals and is the pointer's only: B and Escape close a dialog, so the keyboard's and the pad's first stop is its content or its action.

### Menu (popover)
| | |
|---|---|
| When | A short list of actions under the item that opens it ("...", a figure's actions). |
| API | `ui::OpenMenu(id, MenuAnchor{above, alignRight})` after the opener (`above`: the menu over it, for one at the window's foot; `alignRight`: the menu's right edge on the opener's, for one at the window's right); `if (ui::BeginMenu(id)) { ui::MenuItem(label, MenuItemOptions{icon, checked, danger, disabled}); ui::MenuSeparator(); ...; ui::EndMenu(); }` |
| States | Rows `Small` high, hover / focus highlight, a check for a checked row, danger rows in `Danger`, disabled rows dim; a row closes the menu. |

### Tooltip
| | |
|---|---|
| When | A control's explanation: an icon's words, what a truncated name is. Never the only place of information a pad or touch player needs (a hold button shows its own hint). |
| API | `ui::Tooltip(text)` after the item; components with a `tooltip` option call it. `ui::Bubble(text, min, max)` draws one under any rectangle. |
| States | Under the item (above it when there is no room), kept inside the window; with the pointer after ImGui's delay, at once with the keyboard / pad focus. |

### EmptyState
| | |
|---|---|
| When | A list or page with nothing to show yet (no figure, no backup, no match). |
| API | `bool ui::EmptyState(icon, line, action = nullptr)`: an icon, one line, the primary action that fills it; true when pressed. |
| States | Those of its button. |

### Badge
| | |
|---|---|
| When | A word on a card that is not pressable: Legendary, Series 2, Dark, P1. |
| API | `ui::Badge(text, fill, ink = 0)` at the cursor; `ui::DrawBadge(draw, at, text, fill, ink = 0)` on a card; ink 0: chosen for contrast. Caption SemiBold, round ends. |
| States | None (a very dark fill gets a light edge). |

### Chip
| | |
|---|---|
| When | `Filter`: a filter that is on or off (an element, a game). `Sort`: the order of a list; never shown as a filter, never removable. |
| API | `bool ui::Chip(label, ChipOptions{kind, selected, color, direction, disabled})`; height `Small`. |
| States | Filter: round, off grey with a hairline, on in the accent or `color` with a check and a bold label; Sort: square-cornered, inset, an up or down arrow. Hover, focus ring, pressed, disabled. |

### StatusPill and status icons
| | |
|---|---|
| When | A state in a word: ready, needs a disc, checking, failed. Never a coloured dot alone. |
| API | `ui::StatusPill(Status::{Neutral, Info, Success, Warning, Error, Busy}, text, icon)` (`PillHeight` 24; `icon`: the state's own, else the status's); `ui::DrawStatusIcon(draw, status, [icon,] centre, size)` for the icon alone; `ColorOf`, `IconOf`. A game's state has its own icon (`IconOf(SlotState)`): a disc (needs a disc), a download (ready to install), an arrow in a circle (update), a pause; else its status's. |
| States | Each status has its colour and its icon: a dashed circle (not known yet), an i, a checked circle, a triangle, a crossed circle, the turning loader. |

### ProgressBar, IndeterminateBar, Spinner
| | |
|---|---|
| When | Progress with a known end (a copy, an install step); a wait without one (reading the disc's pictures, a check); a small wait in a row or a button. One progress look everywhere. |
| API | `ui::ProgressBar(fraction, width, showPercent)`, `ui::IndeterminateBar(width)`, `ui::Spinner(size)`; the bar is 8 points high, round ends; the percentage is `FormatPercent`. The spinner turns about the middle of the loader glyph's quad as drawn (`DrawSpinner`), not the point asked for: the icon font's glyphs sit up to a point and a half off their em square's middle, which made it wobble. |
| States | Determinate: track and fill. Indeterminate: a band sweeping; with reduced motion, still dashes (never read as a percentage). |

### Toasts
| | |
|---|---|
| When | A short message after an action or a job: done, copied, failed. |
| API | `App::Toast(text, ui::ToastKind::{Info, Success, Error})` (`Toast(text, true)` is an error); the class `ui::Toasts` (`Push`, `Draw(bottomInset, maxTextWidth)`, `HasFocus`, `NextWakeMs`, `Clear`). |
| States | Bottom right, above the band, newest at the bottom, at most 4. Info (an i, the accent ring) and Success (a check, the green ring) go by themselves (2.5 to 6 s); an Error (a crossed circle, the red ring) stays until closed: a click, or with the keyboard and pad in use it takes the focus (its ring, the A key cap in place of its cross) and A, B, Enter or Escape close it, the focus going back to the page. |

### KeyButton
| | |
|---|---|
| When | A key bound to an action, to press and change (a game's Keyboard tab). |
| API | `bool ui::KeyButton(id, key, KeyButtonOptions{capturing, conflict, tooltip})`, `ui::KeyButtonWidth(key, options)`: a key cap, `Normal` high and at least as wide, as wide as its name needs; its side shows 3 points under the face (the face is the item, what lines up with a row). |
| States | Hover, pressed (the face goes down onto its side), focus ring (around face and side); `capturing`: the accent, "..." (the next key is the new one, Escape cancels); `conflict`: a warning icon and a warning edge, the tooltip says another action has this key. |

### KeyCap and PadLegend
| | |
|---|---|
| When | KeyCap: a key or a pad button named in the window. PadLegend: the bottom hint bar of a page (A Open, B Back...), shown while a pad is in use. |
| API | `ui::KeyCap(PadButton::{South, East, West, North, LeftBumper, RightBumper, Start, Back}, glyphs)`, `ui::KeyCap("F6")`, `ui::DrawKeyCap`, `ui::PadButtonLabel`; `ui::PadLegend(hints, glyphs, width)` with `PadHint{button, key, label}` (`key` nullptr: not shown on the keyboard). |
| States | The pad's family: Xbox letters (LB, RB, Menu, View), PlayStation shapes (L1, R1, Options, Share), Nintendo letters by place (B at the bottom, A at the right); the keyboard's keys translated ("Entrée", "Échap"). |

## Consistency
Phase B applies these on every screen it touches.
- **Game names**: `GameName(id, GameNameForm::Full | Short)` (`game_names.h`) is the only source. The **full** name
  ("Skylanders: Giants") in titles, headers, Home's cards, messages, dialogs and file names; the **short** one ("Giants")
  where room is tight: the sidebar's rows, chips, filters, a select's rows, the coming-later list, a logo's stand-in. All
  the cards of a row use the same form at the same size (`ui::FitSize` over the longest). No screen builds a name ("Skylanders:
  " + ...) or strips one.
- **Casing**: sentence case in the source for every button, label, title and menu ("Show in folder", "Add a figure",
  "Launcher settings"). Capitals come only from the font (Bungee titles, the main action) or never. Each part of a line
  starts with a capital ("Top: Doom Stone · Bottom: Doom Stone").
- **Ellipsis**: three dots, "...", everywhere: in labels that open something ("Change disc..."), in busy states
  ("CHECKING..."), in cut text (`ui::Ellipsize`). Never the single character "…".
- **Formats**, one helper each (`components/format.h`), in the player's language: sizes `ui::FormatSize` ("8.1 GB", "8,1 Go"),
  percentages `ui::FormatPercent` ("46%", "46 %"), numbers `ui::FormatNumber` ("12,345", "12 345"), dates `ui::FormatDate`
  ("28 Oct 2026", "28 oct. 2026"), times `ui::FormatTime` ("15:05", 24 hours), when something happened `ui::FormatWhen`
  ("Today 15:05", "Yesterday 15:05", else the date), the collection's ISO times `ui::ParseIsoUtc`. Local time, never "UTC" in the window.
- **French punctuation**: a no-break space (U+00A0) before ":", "?", "!" and "%" and inside « »; the translation test fails on a
  plain space there. The decimal mark is a comma, the thousands separator a no-break space (the helpers; the font has no narrow one).
- **Numbers and units**: digits, a space and the unit ("6 GB", "10 min", "30 FPS"); sizes rounded as `FormatSize` does;
  durations in minutes; a count with its noun has two strings, singular and plural.
- **Hold buttons**: the label is the plain verb ("Delete", "Reset"); the hint ("Hold to confirm", or a sentence saying what
  goes) is the button's own. French hints say "Maintenez pour ...".
- **Writing** (below) still holds: short help lines, no path in running text, errors say what happened then what to do.

## Gallery
`SKY_LAUNCHER_TEST_GALLERY=1` with `SKY_LAUNCHER_SHOTS` (and the usual scratch `HOME`, `SKY_LAUNCHER_INI`, `SKY_COLLECTION`,
`SKY_LAUNCHER_NO_UPDATE_CHECK=1`, offscreen video) draws, in place of the window's pages, every component in every state,
twelve pages, first at 1x then at 2x, and quits:
`gallery-<lang>-<1x|2x>-<NN>-<page>.png`: 01 buttons (each kind by state, the hold's hint, pills, icon buttons, sizes), 02 the
main action (every state, Spyro's Adventure), 03 the main action in Giants' and Swap Force's colours (focus ring included),
04 tabs (keyboard, then with a pad's LB / RB), segmented, select, slider, 05 toggle, checkbox, radio, settings rows,
06 fields, cards, list rows, a section header, an empty state, 07 badges, chips, status pills and shapes, progress, 08 toasts
(the error with the focus), key caps of each pad family and the keyboard, the pad legend, 09 the modal, 10 a danger dialog,
11 a menu and a tooltip, 12 an open select. Run it with `SKY_LAUNCHER_LANGUAGE=en` and `=fr`. It is the components'
visual test (look at every page after a change to a component) and the designer's review sheet. The sample labels are the
launcher's own translated strings; the captions naming components and states are for developers and stay in English.

## Migration map (phase B)
Three parts, each one piece of work. For each screen: replace the hand-drawn widgets by the components, the literal sizes and
colours by tokens, then delete the App wrapper or the old theme constant once nothing uses it. Check with the gallery, the
screen's own screenshot test (en and fr, 1024x680 and 1600x1000) and `ArkeyanLauncherTest`.

**1. Home, the game page, the sidebar, install and update: DONE** (`launcher_ui.cpp` DrawSidebar, DrawHome, DrawPlay, DrawToast,
DrawQuitPrompt; `game_look.cpp`; `install_page.cpp`; `update_page.cpp` banner and What's new). What is left of the list below is
only what each line says was not possible: see "Done" at the end of this part.
- Sidebar: the rows (hand-drawn, `GameRowH`, `SidebarBarW`) become ListRows (`selected`: the orange bar), which needs an image
  slot for the game's logo or initial (`ListRowOptions` gains one); the status dot becomes `ui::DrawStatusIcon` with a
  `ui::Status` per `SlotState` (in `SlotStatus`); the title's sizes (`TitleSize + 4`, `VersionSize`, `SidebarSmallSize - 2`)
  become Title and Caption; Collection and Settings rows are ListRows with icons.
- Home: "LIBRARY" (`HeadlineSize`) is Display, sentence case; the cards (`CardRadius = RadiusLarge`, the hand-drawn shadow, the
  `IM_COL32` fade) use `radius::Panel` and the elevation tokens; their status dot `ui::DrawStatusIcon`; the coming-later chips
  (drawn by hand) are Badges or Pills; "Report a bug" is a Button (`overArt`); the GitHub mark an IconButton
  (needs an image icon).
- Play: the disc lines (`SmallSize`, `PushFont` by hand) are Text styles; the disc check's `ProgressBar(360, 8)` an
  IndeterminateBar once it reaches its end (the "frozen at 98 %" look); "Use it for {}" a Primary Button; "Change disc..." a
  Pill or Button (`overArt`); the controller picker (190 points, its own popup) a Pill opening a Menu; the portal line
  (`DrawPortalStatus`) a StatusPill; a PadLegend at the bottom of the content (A Play, Y Game settings, B Sidebar).
- Install card: its own bar is a ProgressBar, the step's arc a Spinner, the five PillButtons Buttons (Retry the Primary, first),
  the card `ui::BeginCard`, `fmt::format("{}%")` `ui::FormatPercent`, seven `PushFont` sizes Text styles.
- Update banner: a Card with Buttons; What's new (`BeginPopupModal`, 880 points, its own Close and Escape) `ui::BeginModal(Large)` +
  `ModalActions`.
- Quit prompt (`BeginPopupModal` 520): `ui::BeginModal(Small)` + `ModalActions(&open, "Quit", "Keep installing",
  ModalKind::Danger)`: the focus starts on Keep installing, B / Escape keep installing.
- Toasts: switched; every success passes `ToastKind::Success`, every error `ToastKind::Error`; the "Couldn't" guess in
  `App::Toast` is gone (2026-10-28).

**Done (part 1).** Sidebar rows are `ui::ListRow`s (`ListRowOptions` gained `image`, `note`, `status`, `progress`, `dim`): the game's
logo or initial in the image slot, its version as the note (left out when it would cut the name), `ui::DrawStatusIcon` through
`StatusOf(SlotState)` (Info: needs a disc, ready to install, update, interrupted; Busy: checking and every build step; Success:
ready; Error: every disc problem and a failed build; Neutral: not opened yet), the install's thin bar on the row's foot; the four
games not out yet are one "Coming later" row with their number (a click lists them, dim and not pressable); Collection and Settings are
rows with icons; the title is Title-size, fitted, and Caption beside it. Home: "Library" Display, cards on `radius::Panel` with the
elevation shadow and the Scrim fade, the status a `StatusPill`, "Coming later" Badges, "Report a bug" a Button and the GitHub mark an
`IconButton` with an `image`. Game page: one **disc block** card (file, "Change disc..." Button, a StatusPill for the disc, the
install or update note, "Play without updating" a Ghost Button, "Use it for {}" a Primary Button); the disc check's bar is an
`IndeterminateBar` once it passes 97 % (the main action sweeps too: `MainAction::indeterminate`); **first run** (no disc): a dashed
drop zone with the three steps and "Your disc is read on this computer only." (a click chooses, a drop reads); the controller is a
`Pill` opening a `Menu` above it (`OpenMenu(id, {.above = true})`); the portal line a `StatusPill`; a `PadLegend` (Play, Game settings, Sidebar)
in a strip under the band while a pad is in use (`App::PlayBandH`). Install card: `BeginCard`, one `ProgressBar` with its
percentage (the percentage is said once in the card), status icons per step, Retry (Primary) first then Copy log, Open logs folder
(`Icon::Folder`), Show details (Ghost); Cancel stays Secondary (it keeps the install going on a tap, no hold). Update banner:
Buttons (`overArt`), `ProgressBar` or `IndeterminateBar`; What's new `BeginModal(Large)` + `ModalActions`; quit prompt
`BeginModal(Small)` + `ModalActions(Danger)`. Install and update toasts are typed. The sidebar's percentage is `FormatPercent`.
`App::Toast(text, bool)` and its "Couldn't" guess are gone: every call says its `ToastKind` (2026-10-28).

**2. All settings pages: done in phase B** (`launcher_ui.cpp` DrawSettings, DrawControls, DrawLauncherTab, DrawLauncherLanguage; `game_settings_page.cpp`;
`game_data_page.cpp`; `support_page.cpp`; `uninstall_page.cpp`; `update_page.cpp` About and Updates)
- Page titles ("LAUNCHER SETTINGS", the game's name at `HeadlineSize`): Display, sentence case in the source.
- `DrawTabBar` calls `ui::Tabs` directly; every `Title(..., TitleSize, Text)` section a SectionHeader (dividers); every
  setting a settings row (`ui::BeginSettings` / `SettingRow`): today only Video uses rows, Keyboard, Disc, Saves, Game and the
  launcher's four tabs are free stacks with controls under Bungee headings.
- `App::Combo` / `BeginCombo` / `Option` become `ui::BeginSelect` / `SelectOption` / `EndSelect` (40 points, rows 32 with a
  check; today 35 and 26-30); toggles go in the rows' control column ("##id").
- Buttons: fixed widths (120, 140, 190, 200, 260, 320) become 0 (the label's) or the control column; "Create" (desktop shortcut)
  is Secondary, not Primary; Back is a Ghost Button with `ChevronLeft`; Reinstall, Clear caches, Delete saves and data, Remove
  game, Restore, Reset to defaults are Danger with `hold` (the wrapper already draws them so).
- Controls: the key-binding sentence ("F6 or Tab opens the Toy Box...") becomes KeyCaps in a row; Launcher: "Remove Arkeyan
  Launcher" its own danger section at the bottom; the data folder path a Caption and a Button with `Icon::Folder`; "Check now"
  `loading` while it checks.
- Game data: the copy's `ProgressBar(420, 10)` a ProgressBar with its percentage; the `Ok`-coloured result a StatusPill.
- Support: the problem report (`BeginPopupModal`, 880 by the window) a Modal (Large); logs keep `fonts.mono`.
- Uninstall: its list ListRows with the size as `trailing`; "Also delete my saves and my figures" a Checkbox (an option of
  the action, not a setting); Uninstall everything a Danger `hold` Button.

*As built.* `App::BeginSettingsPage` draws the head of both settings pages (the centred column, the Display title, a Ghost Back
button with `ChevronLeft` on a game's). Every setting is a settings row (`ui::BeginSettings`, `SettingRow`) with its control in
the column (`App::SettingSelect` for a choice kept in the settings file); what is an action rather than a setting (the disc,
game data, folders, saves, the game tab's buttons, the data folder, "Remove Arkeyan Launcher") is a `SectionHeader` with its
subtitle, then Buttons at their label's width. States are `StatusPill`s (verified, checking, wrong disc, copied, interrupted,
damaged, the portal test), never a coloured line. The Keyboard tab (2026-10-28, the owner's review): what to do in one
line ("Click a key to change it.", or which key is awaited), then the **movement** as a cross of four `KeyButton`s (up over
down, left and right beside it: the player's letters, Z Q S D on AZERTY, W A S D on QWERTY) titled "Movement" with the
stick after it, the **right stick's** as another cross when the game uses it (titled with its action, "Shake / Insert /
Swing"), a hairline, then a settings row per other action (its name, its pad button as the row's note, the key cap at the
column's right); a key being changed is the accent with "...", a key two actions share a warning icon and edge; one Danger
hold "Reset to defaults". The Toy Box keys (Launcher settings > Controls) are key caps in settings rows, two columns at the
row's right: the keyboard's ("F6 / Tab": either) and the pad's in the pad family's pictures ("View + Y": together; the pad in use
or chosen, else Xbox's), a dash where the pad has none (F7, F8). The Controller tab names an unchanged button's job "Default".
Save backups are ListRows (when, files, size) with a Danger hold "Restore" beside each; the update check's last time and the
backups' times use `FormatWhen`. The problem report is `ui::BeginModal(Large)` with `ModalActions` (Close, Save as...) and Copy
at the left. Uninstall's list is ListRows with the size as `trailing` (not focusable: nobody presses them), "Also delete my saves
and my figures" a Checkbox, Cancel then the Danger hold "Uninstall everything" at the right. Text is Caption, Body and Display
through `ui::Text`; the portal test's verdicts name the game with `GameName`. The shots are listed in "Screenshot test"
(`17-` to `19-` are Support, About and the report).

**3. The Collection, its sheets and dialogs** (`collection_page.cpp`, `collection_figure.cpp`, `collection_add.cpp`,
`collection_manage.cpp`) - **done (phase B, 2026-10-28)**. What was decided: the portal's game is a Select after "On the portal
for" (tooltip says what it is) over the seats, the count under the title (the owner's review, 2026-10-29); Sort is a Select beside Filters (it is not a filter: no chip, Clear all keeps
it) and Filters a Button opening a Menu with three Selects; the figure sheet, Add a figure and Manage are one `ui::Modal`
(Large) whose title is the view's, Edit and "Change the bottom" being views of the sheet (B steps back); Reset and Delete ask
in a danger `ConfirmDialog` (the hold is for the buttons in a list: Delete for good, Restore, Tidy, Mark as not in use); the
sheet's "Restore a backup..." opens Manage > Backups on that figure (the Menu has no submenu); Edit is a settings list; the
pad legend closes the page while a pad is in use. The list that follows is what was asked.
- Header: "COLLECTION" Display, sentence case; "+ Add a figure" a Primary Button with `Icon::Plus` (the "+" leaves the
  string); "..." an IconButton (`Icon::More`, tooltip "Manage") or a Pill "Manage"; the portal's game dropdown (a 28-point
  pill) a Select with short names.
- Seats: Cards with an IconButton (`Icon::Cross`) for the X; search: a SearchField (its hint ends in "…": three dots);
  Filters: a Pill with `ChevronDown` opening a Menu or a Card; filter tags Chips (`Filter`); "Sort: Level" a `Sort` Chip or a
  Select "Sort by" (it is not a filter).
- Shelf: badges are `ui::DrawBadge` already; stars `Icon::Star`; P1 / P2 Badges; "No figure matches" and the empty
  collection EmptyStates with their action; "Not for {}" a SectionHeader.
- Figure sheet (`BeginPopupModal` 680x520, Close in its footer): a Modal (Large) with its cross; Put on the portal, Edit,
  Favourite a Button row, "..." a Menu; the XP bar a ProgressBar; Details a SectionHeader; its dates (`ShortDate`, "UTC")
  `ui::FormatWhen(ui::ParseIsoUtc(...))`.
- Edit: `LevelSlider` is `ui::Slider`; name, gold, nickname TextFields; Reset and Delete Danger `hold` Buttons.
- Add a figure: the catalogue search a SearchField; its list ListRows (`selected`); versions Radios or ListRows; the Name field a
  TextField; Create and Close as `ModalActions` (Cancel then Create, bottom right; today Create left, Close far right);
  "owned x2" (11 points, green) a Badge; "ADD A FIGURE" sentence case.
- Manage: its tabs (drawn with Chips) Tabs or Segmented; trash rows show file names ("012 Zap.sky"): ListRows with the
  figure's name, `FormatWhen` and the buttons in `trailing` position; Put back a Button, Delete for good a Danger `hold`; the
  backups' chips a Select or ListRows; "Move {} to the trash" Danger, not Primary.

**Inconsistencies seen, for phase B to fix**
(Done in the settings part: 1 for "LAUNCHER SETTINGS", 4 for the save backups, 6, 7 for "Create", 9 and 10 on the settings screens, 12 for the problem report, 13. Done: the Keyboard tab's warning is an icon, not red alone.)
(Collection: every item that names it is done: casing, ellipsis, dates, hold wording, Primary, text sizes, raw colours, the
shells, the toast kinds.)
1. Casing: page titles written in capitals in the source ("LIBRARY", "COLLECTION", "LAUNCHER SETTINGS", "ADD A FIGURE",
   "EDIT", "MANAGE", "CHANGE THE BOTTOM", "CHANGE THE TOP") while section titles are sentence case; overline captions in
   capitals ("YOUR GAMES", "COMING LATER", "PORTAL AT THE NEXT START", "CONTROLLER", "DISC IMAGE:").
2. Ellipsis: "Name, element, game, level…" (the collection's search) is the only "…"; everything else is "...".
3. Percent: "Installing 46 %" in the sidebar (English with a space, `Tf("{} {} %")`) against "46%" on the install card
   (`fmt::format("{}%")`); use `FormatPercent`.
4. Dates: the figure sheet and the trash show "2026-10-28 15:05 UTC" (`ShortDate`); save backups "2026-10-26 09:30" built by
   hand in `game_settings_page.cpp`; use `FormatWhen` / `FormatDate`.
5. French hold wording: tooltips "Maintenez pour ..." and button labels "Maintenir pour supprimer", "Maintenir pour
   réinitialiser" (collection); labels become plain verbs ("Supprimer"), the hint says the rest.
6. Hold labels: plain verbs on the settings pages, "Hold to delete" / "Hold to reset" as labels in the collection.
7. Primary misused: "Create" (a rarely used desktop shortcut), "Move {} to the trash" and "Put on the portal" (with a chevron).
8. Sizes in sentences: "about {} min, {} GB" formats its number by hand; "Not enough free space: about 6 GB needed." has it
   in the string.
9. Text sizes off the scale: `SmallSize - 1`, `CaptionSize + 1`, `SidebarSmallSize - 2`, 19, `TitleSize + 4`, `TitleSize + 6`,
   `HeadlineSize - 6`, `VersionSize`, and fitted sizes from the card's width (about 60 `PushFont` call sites).
10. Raw colours: `IM_COL32` literals in `launcher_ui.cpp` (6), `collection_page.cpp` (2), `collection_figure.cpp` (3),
    `game_look.cpp` (2): shadows, fades, hairlines; they become tokens (elevation, `Scrim`, `Hairline`).
11. Status by colour alone: the sidebar's and Home's dots.
12. Four dialog shells (support 880, What's new 880, figure sheet 680x520, quit 520) and the collection's full-column overlays.
13. Two settings idioms (rows on Video, stacks elsewhere) and two toggle layouts (label after it, label in the row).
14. `BigButton` is no longer drawn anywhere (the main action is `ui::MainAction`): delete it with the wrappers.
15. Toast kinds: only "Game installed" is a success yet; the collection's errors are still found by their "Couldn't" prefix.
    (Done: every toast says its kind.)

## Layout
The launcher opens on Home, the menu page (the ARKEYAN LAUNCHER title goes back to it): empty for now, with "Report a bug" (a new
GitHub issue) and the GitHub link bottom right. The launcher's own language is in Settings > Launcher > Language
(`docs/translations.md`); the game's text and voice languages are in that game's settings (Language tab). The game shown last is selected in the sidebar (`[launcher] game=`); the other is one click away.
1. Left sidebar (grey, fixed width) - the seven games as full-width rows (Spyro's Adventure, Giants and Swap Force enabled, the others "Coming later",
   SuperChargers Racing included), then Collection and Settings at the bottom. Each enabled row shows its game's icon
   and its live state (every game's disc is checked at start-up, one after the other: "A game's states"); the
   selected game's row has the bar while its Play page is open. Rows are reachable with the keyboard and the pad like the others.
2. Play (no tabs) - the game's logo from the player's disc over the drawn sky (the disc's background art is not shown); a light band under it holds the controller button
   (current device, PLAY WITH: a menu of the others), the portal's state, the main button (CHOOSE DISC, INSTALL, then PLAY)
   and, on its right, **Game settings** (the band is described in full below). PLAY and
   INSTALL check that the image is still there first. Changing the disc moved to the game's settings (Disc tab); CHOOSE DISC
   stays the main button while there is no disc.
3. Collection - see below.
4. Settings (the gear) - **the launcher's own settings and what both games share**, in four tabs: Controls (device, portal, glyphs), Launcher (language slot, updates, data folder, **Remove Arkeyan Launcher**: "Uninstall everything", below), Support (report a problem, logs), About (version, credits).
   Clicking a game goes back to Play; clicking the gear again, or B / Escape, goes back to the page you came from.
5. Game settings (the Settings button on a game's Play page) - **that game's own settings**, six tabs (the sidebar keeps the
   game selected; B / Escape or **Back** return to its Play page):
   - Video: fullscreen, resolution scale, frame rate limit (30 or 60), skip intro movies, Original resolution (Swap Force only), VSync, texture filtering, widescreen,
     HD texture packs (and their folder), Advanced > dump textures. The game's own copy, `[<game>.video]` (docs/settings.md).
   - Keyboard: one row per action **the game uses**, named as the game names it with the pad button in brackets ("Attack 1 (X)",
     "1st Power / Accept (A)"), two rows a line: the key's button (click it, press a key; Escape cancels), red when another row of
     the game has the same key, and **Reset to defaults** (held) for this game's keys. The game's own copy, `[<game>.input]`; PLAY puts it
     in `[input]` (docs/settings.md, "Per-game keyboard keys", which also lists each game's rows and where their names come from).
   - Language: game text and game voices (`[<game>] language`, `voice_language`). Giants' disc has twelve languages for both (English,
     French, German, Spanish, Latin-American Spanish `es-419`, Italian, Dutch, Portuguese, Swedish, Norwegian, Danish, Finnish) and its
     voices default to **Same as text** (an empty `voice_language`); Spyro's Adventure has ten, its voices default to English; Swap Force's
     disc has English and French (USA, Australia) or Spanish and Italian (Europe).
     **The lists show only what the disc holds**: read from its file names at each disc check (`shared/art/disc_languages.h`) and kept
     in `[<game>] disc_text_languages` / `disc_voice_languages`. A saved language the disc lacks shows the list's first entry (the
     default); a game not checked since this was added shows today's full list.
   - Disc: the image's file name and state; **Choose... / Change...**, **Remove** (the launcher forgets it; the file is not
     touched), **Show in folder**. Disabled while a check or an install runs.
   - Saves: **Open saves folder** and the save backups with Restore (held, refused while the game runs).
   - Game: **Create** (under Desktop shortcut), the folders, the built game with **Reinstall** (held), **Clear caches**,
     **Delete saves and data** (held), **Remove game** (held), each with the size it frees and what it keeps
     (docs/install.md, "Delete data, uninstall").

**Why the split.** The gear used to hold game things (disc image, graphics, game languages, saves) that looked like the
launcher's. Now the gear is only what is the launcher's or is shared, and anything about one game is on that game's page, so
the two games can differ (30 fps for one, 60 for the other, another disc, deleting one's saves). **The controller, the button
glyphs and the portal stay in the launcher's settings** because the games read the same `[input]` (device, glyphs) and `[portal]`
sections and the same hardware: a game page would only show the same values twice. **The keyboard keys moved to the game pages**
(Keyboard tab) because the games name and use their buttons differently (Giants' X is "Attack 1", Spyro's Adventure's is "Use / Talk"):
`[<game>.input]` works like `[<game>.video]` (`game_keys.h`).

The launcher opens on **Home**, the menu page (the ARKEYAN LAUNCHER title goes back to it): the **library**, a card per
playable game (its painting from the disc behind its logo, its name and state; the game's colours before its disc was
ever read), then the games **coming later** as quiet chips in their colours; "Report a bug" (a new GitHub issue) and the
GitHub link bottom right. A card opens its game's page. The launcher's own language is in Settings > Launcher > Language
(`docs/translations.md`); the game's text and voice languages are in that game's settings ("Game text", "Game voices"). The game shown last is selected
in the sidebar (`[launcher] game=`); the other is one click away. At start-up its disc is checked first, then the other games'
discs, one after the other in the background (`App::CheckQueuedDiscs`: each as its first visit would, never two at once), so
every game shows its real state and art at once ("Update all" on Home's title line when two or more have an update).
1. **Left sidebar** (grey, fixed width).
   - The title, **ARKEYAN LAUNCHER**, with the launcher's version beside it on its baseline, small and dim (`v0.6.0`, from
     `LAUNCHER_VERSION`: `LauncherVersion()`), then "Native PC ports" (XXS under it) and, L below, YOUR GAMES; all on the
     rows' content edge. The title is Home's link for the pointer (a hand on it), not a keyboard or pad stop: no ring.
   - The three playable games as rows (Spyro's Adventure, Giants and Swap Force), then one "Coming later" row for the four others. An enabled game whose disc has been read (this session or an earlier one) shows **its logo**, the
     art with "Skylanders" on it, in the row's left column (`GameLogoW` x `GameLogoH`); before that, and for the games
     coming later (no disc, so never any game art), a neutral badge: the initial on the game's colour.
   - The name, then under it the game's state: **its icon and its words on one line** (`IconOf(SlotState)`, "A game's
     states", below), the icon at the name's left edge, centred on the words' capitals. The selected game's row has the
     orange bar while its Play page is open.
   - Every row has the same leading column (`GameLogoW` wide: the logo, the initial, the chevron of "Coming later", the
     icons of Collection and Settings centred in it): all the titles start on one edge.
   - Collection and Settings at the bottom (sentence case), rows with an icon, L from the window's foot.
   - Rows are reachable with the keyboard and the pad like the others.
2. **Play** (the game page, no tabs).
   - The backdrop is the game's own painting from the disc (SSA: the painted Skylands sky; Giants: the main menu's sky; Swap Force: the main menu's painted sky),
     cropped to fill the page and shaded (`DrawBackdrop`: darker at the top, light behind the logo, dark towards the band,
     a shadow along the sidebar) so the logo and white text read on any painting. Before any disc: the drawn night sky.
   - The logo, large and centred, with a soft shadow, **falls in** on each arrival (see "The logo's arrival"). Before the
     disc was ever read: the game's name.
   - At the top right, over the art (XL from the edges), the **play time and achievements** in one Pill (`overArt`):
     "12 h 34 · trophy 6/38" (`PillOptions::prefix`: the time before the trophy; a game never played shows the trophy and
     the count only), shown once the disc check cached the list; it opens the Achievements page; its tooltip says
     "Achievements" and when the game was last played. The room on its left is kept for a sound button to come.
   - At the hero's foot, centred like the logo and the band, the **disc block**: "DISC IMAGE:" and the image's file name (XXS
     apart) with "Change disc..." centred on the two once a disc is verified, then (S below) its state and the install note,
     XS apart. Nothing else: the play time, the achievements and the update's details left it (the owner's review,
     2026-10-29). (The small rounded app icon that sat here is gone: the owner's request, 2026-10-25.)
   - The band under it: **one row** on the main action's middle line, centred in the band, S between its parts: the
     controller picker (a Pill: a pad icon and the current device, as wide as its name up to `ControllerW`; PLAY WITH: a
     menu of the others, above it), the **portal** (a Pill as high: "Portal" with a green check when a real portal is
     plugged in, "No portal" with a dashed circle when not; as wide in both states, so the row never moves; its tooltip
     says the model or "not detected" and that the game then has its virtual portal; a click opens Launcher settings >
     Controls), then the **action button** in the game's colours ("Each game's look"): the main action (CHOOSE DISC,
     INSTALL, UPDATE, PLAY) and the **game's settings** (a gear) in its right segment. While an update is offered, under the
     action, centred on it: one muted Caption line, "0.8.2 · 41 MB · ~15 min" (the version on offer, the kit's download if
     one is needed, the time), then **Play without updating** (a Small Ghost Button); the row and those two lines are
     centred in the band together. PLAY and INSTALL check that the image is still there first. With a pad, the pad legend
     runs along the band's foot, edge to edge.
3. Collection - see below.
4. Settings (the gear) - Graphics, Controls (device, portal), Language, Game data, About. Clicking a game
   goes back to Play; clicking the gear again, or B / Escape, goes back to the page you came from. The game page's settings
   button opens it too, until each game has a settings page of its own (another branch).

## A game's states
One function decides: `App::SlotStatus(slot)` returns the game's `SlotState`, its line and the main button's label (its icon and colour: `IconOf` and `StatusOf`)
and whether it can be pressed. The sidebar, Home's cards and the Play page's main button all read it, so they cannot
disagree; the page's other lines (the disc's status, the install card, the game settings' Disc tab) say more about the same
`DiscState` / install state. In this order (the first that applies):
| State | Sidebar and Home line | Icon | Main button |
|---|---|---|---|
| the kit being downloaded (the first step of an install or an update) | `Downloading tools 42 %` | the turning loader, blue | INSTALLING... (off) |
| installing; its last step | `Installing 42 %`; `Finishing 97 %` | the turning loader, blue | INSTALLING... (off) |
| updating a build; its last step | `Updating 42 %`; `Finishing 97 %` | the turning loader, blue | UPDATING... (off) |
| the disc being checked; a remembered disc whose turn in the start-up checks has not come | Checking your disc | the turning loader, blue | CHECKING... (off) |
| no disc chosen (a game never shown reads this too, from `launcher.ini`) | Needs your disc | a disc, blue | CHOOSE DISC |
| the image moved or its drive gone | Disc not found | a crossed circle, red | CHOOSE DISC |
| the image can't be read | Can't read the disc | a crossed circle, red | CHOOSE DISC |
| another game's disc | Wrong disc | a crossed circle, red | CHOOSE DISC |
| this game, a release no build exists for | Unknown release | a crossed circle, red | CHOOSE DISC |
| a folder, not an Xbox 360 game, damaged | Not a game disc | a crossed circle, red | CHOOSE DISC |
| the last install failed (no build); the last update failed (a build) | Install failed; Update failed | a crossed circle, red | INSTALL; UPDATE (or PLAY) |
| the install was interrupted | Interrupted | a pause, blue | INSTALL, or PLAY if built |
| the copied game data is damaged, and no disc image to fall back on | Game data damaged | a crossed circle, red | CHOOSE DISC |
| a verified disc, no build yet | Ready to install | a download, blue | INSTALL |
| the release has a newer version of the game than the one built (the release's games line), not set aside with "Play without updating" | Update available | an arrow in a circle, blue | UPDATE |
| verified disc and built (or an own runtime) | Ready to play | a checked circle, green | PLAY |
| the game is running (started here, or found running: "A game running") | Running | a play triangle, green | RUNNING (off) |

While a game runs, every **other** game's main button is off and says so ("SPYRO'S ADVENTURE IS RUNNING", fitted to the button),
whatever its own state would offer (a busy one, checking or installing, keeps its progress). Its sidebar line is its own.

**UPDATE** (`docs/updates.md`): the main button reads UPDATE and, under it, one muted line says the version on offer, the
download and the time ("0.8.2 · 41 MB · ~15 min"; no size when the kit at hand is the right one); one click fetches the kit if
it is needed, rebuilds the game (SkyBuild is incremental) and PLAY is back. Under that line a small link,
**Play without updating**, plays what is built (offline, in a hurry): it sets the offer aside until the launcher is closed. The game's version is small, in the
secondary text colour, after its name in the sidebar ("Giants · 0.8.2"); nothing on the page moves for it. While UPDATE runs the progress card is titled
"UPDATING THE GAME" ("INSTALLING THE GAME" for a first install). No tools are ever downloaded without a click on INSTALL or UPDATE.

"Ready to play" needs both the build and a verified source: the disc image, **or the copied game data** ("Game data" below).
With no source or a refused one the game says what it needs, even if it was built (the game reads the disc image while it runs). The Play page's disc line says more
("Disc verified: 8.1 GB", the refusal's reason, "Drop your disc image here..."), the install card is shown while
`installView`, and the game settings' Disc tab shows the same `DiscState`. The window has one fixed title.

The percentage is the install card's overall fraction (`MakeInstallChecklist`), the same as the band's button. The text is
cut with "..." to the column (the sidebar is a fixed width at every window size) and the bar is drawn over the row's own
space, so nothing moves when the line changes.

### A game running
PLAY starts the game **beside the window, which stays open** (`GameSession`, `launcher/game_run.h`): one game at a time.
- **Started here**: the runtime is started without waiting for it, its output going straight into `skylauncher-game.log`
  (`docs/support.md`), so it runs on if the launcher is closed. The launcher watches the process (`SDL_WaitProcess`, every
  wake: at most 0.5 s) and goes back to normal when it exits; a non-zero exit code is a crash and shows the crash dialog
  (`ShowGameStopped`, over the window).
- **Found running**: a game this launcher did not start (one left running when the launcher was closed and opened again, or one
  started by hand) is seen by the marker each game keeps while it runs (`<saves>-backup/game.lock` with its process id:
  `save_backup.h`, `GameRunning`, which drops a crashed game's marker), looked at every second; its end is seen the same way.
- **No race**: PLAY asks `GameSession::Start`, which refuses while any game runs (looking at the markers at that moment too);
  the button is off from the next frame, and a double click, `--play` or a second PLAY start nothing ("Spyro's Adventure is
  running", an error toast).
- **Meanwhile**: the running game's Change disc..., UPDATE, Update all (its turn is skipped), Reinstall, Clear caches, Delete
  saves and data, Remove game and Restore (a save backup) are off; the other games' main buttons are off. Settings, the
  Collection (the running game's seats are read-only: its Toy Box has the portal) and the achievements stay open.
- PLAY returning the choice instead (the window closing, `main()` running the game and waiting) is what `RunLauncherWindow`
  does without `LauncherOptions::startGames`: the test windows and `SKY_LAUNCHER_TEST_PLAY`.
- Tests: `GameSessionChecks` (`tests/shell_test.cpp`: start, a double click and another game refused, exit codes, a marker
  found and gone, a game started by hand a moment ago, a launcher restarted while its game runs) and `RunningChecks`
  (`tests/launcher_test.cpp`, the window with `SKY_LAUNCHER_TEST_RUNNING`: shots `running-1-game`, `running-2-other`,
  `running-3-ended`).

`SKY_LAUNCHER_TEST_SIDEBAR=1` (with `SKY_LAUNCHER_SHOTS`, a `[launcher] kit=` that is the fake kit and both games' `iso=`;
`FAKE_SSABUILD_HOLD` keeps the build at 100 %) shoots and checks the line through an install and an update at 0, 50 and 100 %,
with the other game shown meanwhile, then a failed build: `ArkeyanLauncherTest` runs it (`SidebarChecks`).

## Achievements and play time

The games have Xbox 360 achievements; the launcher lists them per game and shows how long the game was played. Nothing is shipped: the list is
read from the player's own disc.

- **The list.** The disc check (`VerifyJob`) reads the title's XDBF in `default.xex` and caches it in `<Game>/launcher-cache/achievements/`:
  `list.tsv` (each achievement's id, gamerscore, picture id and hidden flag, and its name, unlocked text and locked text in en, fr, de, es, it,
  pt) and one `<picture id>.png` each. The cache holds the `default.xex`'s SHA-256: it is rewritten only when the disc's `default.xex`
  changes (older launchers' caches have no list: the next disc check makes it). Code: `src/shared/collection/achievements.{h,cpp}` (also
  the runtime's reader: `tools/sync_skylauncher.sh`); counts per disc are in SSARecomp's `docs/runtime/xbox360/xam.md` (SSA 38, Giants 45,
  Swap Force 39, 1000 G each). "Clear caches" removes it; the next disc check makes it again.
- **What was unlocked and the play time** are written by the game runtime, not the launcher (`achievements.txt`, `playtime.txt`:
  `docs/settings.md`, "Progress files"). `src/launcher/game_progress.{h,cpp}` joins them with the cached list in the launcher's language (English
  where a language has no text) and formats the play time line. The page reads them again every 1.5 s, so a game running beside the launcher shows
  its new unlocks.
- **The game page** (Play) shows them at its top right, over the art, in one Pill: "12 h 34 · trophy 6/38" (the play time is left
  out for a game never played); its tooltip says "Last played: today" (today, yesterday, "3 days ago", then the date). A click
  opens the page once the list is cached (Layout, Play).
- **The Achievements page** (`ui/achievements_page.cpp`; `Page::Achievements`, which the sidebar and Back treat like the game's settings: B or Back
  returns to the game page): a settings page's frame (`BeginSettingsPage`: the game's name, Back), one card: "Achievements" with the totals
  ("6 of 38 unlocked · 180 of 1000 G") under it, a ProgressBar, the play time, then one row per achievement in the game's order: its picture
  (48 points, `radius::Control`), its name over its description (the two centred on the row), its gamerscore on the name's baseline and, for
  an unlocked one, the date under it (accent colour, full picture).
  A locked one is greyed and shows the game's own text for it (the locked description, else the unlocked one); a hidden one still locked shows
  "Hidden achievement" and the game's locked text, as the game shows it (none of the three discs has one: `BuildProgress` is checked on
  made-up data). The rows are not pressed (no ring on them): up and down on the keyboard or the pad scroll the page.
- **Screenshots**: `SKY_LAUNCHER_TEST_ACHIEVEMENTS=1` (with `SKY_LAUNCHER_SHOTS`, `SKY_LAUNCHER_TEST_DROP=<disc>`, a scratch `SKYLANDERS_DATA`, `--game <id>`,
  `SKY_LAUNCHER_LANGUAGE=en|fr`) verifies the disc, writes six unlocks and 12 h 34 of play in the scratch folder, and shoots
  `achievements-1-game`, `achievements-2-list` and `achievements-3-end`.
- **Tests**: `ArkeyanLauncherTest` (`AchievementChecks`: rows, hidden ones, the play time texts in English and French; per game's disc in the
  "Disc pictures" part: the cache, the counts, the unlocks and the play time read back, French names differ).

## Game data (copy the disc's files, no disc image afterwards)
Game settings > Disc has two sections: **Disc image** (choose, change, remove, show in folder) and **Game data**, which copies the
disc's files (about the image's size, 8 GB) into `<data>/<Game>/gamedata/` so the disc image is no longer needed. The default is
unchanged: nothing is copied and the game reads the disc image. The files, the manifest and the checks are SSARecomp's
`game_data.{h,cpp}` (`docs/runtime/host/game_data.md` there); the job and the choice of source are `src/launcher/game_copy.{h,cpp}`;
the window is `ui/game_data_page.cpp` (+ `game_settings_page.cpp` for the tab). What the player sees, by the copy's state:

| Copy | Game data section | The game |
|---|---|---|
| none | **Copy files** (needs a verified disc image and no install running), how much room it takes | plays from the disc image |
| running | a bar "Copying the game data: 3.2 GB of 8.1 GB", **Stop**; the disc image's buttons are off | plays from the disc image (a copy is never read before it is complete) |
| stopped, failed or the launcher quit | "Interrupted: 3.2 GB of 8.1 GB copied", **Carry on copying**, **Remove the copy** | from the disc image; carrying on reads only the missing files |
| complete | "Copied: 8.1 GB, 1142 files", **Remove the copy** (held), Show in folder | **from the copy**: the Play page says "GAME DATA: Game data copied: 8.1 GB", the Disc image section says it is not needed, "Change disc..." is hidden, the state is "Ready to play" (or "Ready to install") **with no disc image at all** |
| damaged (a file is missing or changed since) | the first problem, **Repair the copy** (copies again, only what is wrong), **Remove the copy** | **falls back to the disc image** (the Play page says the copy is damaged); with no disc image the state is "Game data damaged" |

- The copy is made on a worker thread, file by file: each file is written to `.part`, read back and compared with the disc, then renamed. The
  room is checked first ("Not enough space: the copy needs 8.1 GB and this drive has 3.1 GB free.": nothing is written). It resumes
  after a stop, a failure or a quit. When it ends the game is checked again, from the copy.
- **At start** (`App::StartGameDisc`, `PickGameSource`): a complete copy is verified (`CheckGameSource` takes the folder: its `default.xex` must be a
  known release) and used; else the remembered image. The remembered image (`[<game>] iso=`) is never forgotten for a copy: removing the copy
  brings it back. A copy whose `default.xex` is refused counts as damaged. PLAY and INSTALL look at the copy again first (sizes only) and
  fall back to the disc image if it changed. PLAY passes the copy's folder to the game, which reads it too if it is given an image
  (`host/launcher.cpp`, `CopiedGameData`). INSTALL gives it to SkyBuild as `--iso`.
- **Uninstall keeps the copy** (like the disc image); only "Remove the copy" deletes it. Delete saves and data and Clear caches never touch it.
- `SKY_LAUNCHER_TEST_COPY=1` (with `SKY_LAUNCHER_SHOTS`, the fake kit, `[giants] iso=` set to a stand-in SSA disc) drives all of it in an offscreen window
  (`App::TestCopyScript`, run by `ArkeyanLauncherTest`): the wrong disc, not enough space, interrupted and carried on, played with no disc
  image, damaged and the fall back, repaired, removed, and Giants' own copy. `SKY_LAUNCHER_COPY_FREE_BYTES` fakes the room (tests only).

## The wrong disc, offered to the right game
When the check finds the other supported game's disc ("This disc image is Skylanders: Spyro's Adventure, not Skylanders: Giants.", status
`WrongGame`, `GameSourceCheck::otherGame`), the Play page and the Disc tab add one button, **Use it for <that game>** (`App::UseDiscForOtherGame`):
the window switches to that game, checks the image there, and remembers it as that game's disc once it is verified. The game it was chosen
for goes back to the disc it had (or none); a stale `iso=` that was this image is dropped. Refused with a toast while the other game installs,
copies or checks its own disc. A game that already plays from a copy only switches ("already plays from its copied game data").

## Each game's look
What belongs to the game on its page is drawn in **the game's own colours**, taken from its logo (`GameStyle` in
`launcher_theme.h`, `StyleOf(id)`): the action button, and the glow of its Home card.

| Game | Colours | Tokens |
|---|---|---|
| Spyro's Adventure | the logo's sky blue to deep blue, navy ink | `SpyrosAdventureStyle` |
| Giants | the logo's gold to orange, dark brown ink | `GiantsStyle` |
| Swap Force | the logo's lime to green, dark green ink | `SwapForceStyle` |
| any other | the launcher's own blue | |

**The action button** (`ui::MainAction`, called by `App::DrawMainAction(at, {label, enabled, progress})`, the owner's choice
of 2026-10-25, "flat and crisp"): one split button, `height::MainWidth` + `height::MainGear` by `height::Main` (300 + 64 by
64 points).
- Flat: a light gradient between the game's two colours (20 % to 58 % of the way), `radius::Card` corners, no outline, a soft
  shadow, a hairline of light along the top. The label in Bungee at `type::Title` (smaller if it would not fit), centred
  on its capitals and placed on whole pixels; its ink is white when that reads at 3:1 on the fill, else the game's dark
  ink (navy for SSA, brown for Giants, dark green for Swap Force), and it never changes under the pointer.
- The game's settings: a gear in the right segment, behind a dark and a light hairline; its tooltip "<game> settings".
- States, each half on its own: under the pointer 10 % lighter, pressed 10 % darker; **focused** (keyboard, pad): the
  focused half lit as under the pointer and one ring around the whole button in the game's colour (`GameStyle::glow`);
  **disabled**: greyed, a light faded label; **busy** (checking the disc, installing): a dark face tinted in the game's
  colour up to the progress, a 5-point bar in that colour along the bottom edge, the label ending in the percentage
  ("INSTALLING 42%", `FormatPercent`: "42 %" in French); `indeterminate`: a band sweeping along that bar.
- The band's group (the controller picker, 40 points, the button) is centred in the band on whole points; the picker is
  as tall as the button. At 100 % and 200 % and in the smallest window (1024x680) both share their top, bottom and
  centre to the pixel, and the margins left and right are equal.
- Everything else (the sidebar, the band, Settings, Collection) keeps the launcher's neutral look: the games' styles
  never mix.
- A new game: add its `GameStyle` and a line in `StyleOf`.

`SKY_LAUNCHER_TEST_BUTTONS=1` (with `SKY_LAUNCHER_SHOTS`, a `[launcher] kit=` that is the fake kit, and both games' `iso=`)
shoots the button's states: for each game `00-install`, `01-hover`, `02-pressed`, `03-focused`, `04-settings-hover`,
`05-disabled`, `06-installing`, `07-play`, `08-play-hover`, then `09-small` (1024x680), `10-scale150` and `11-scale200`,
named `<game>-<nn>-<state>.png`.

## The logo's arrival
On each arrival on a game's page (from another page, or switching games, or when the logo appears because the disc was
just read), the logo **falls in**: `LogoDropMs` (300 ms) from `LogoDropDistance` (44 points) above, eased out with a
short landing (`EaseOutBack`, most of the way in the first third), fading in over the first 40 %. Once per arrival; a page
already shown doesn't move. `App::IdleWait` draws every frame only while it falls.
**Reduced motion:** with the system's setting (macOS: Accessibility > Display > Reduce motion; Windows: Accessibility >
Visual effects > Animation effects off), or `[launcher] reduce_motion=1`, the logo is simply there. `reduce_motion=0`
always animates (`docs/settings.md`).

## Interaction rules
- **Focus.** Every control takes the keyboard and pad focus and shows it: the components draw the one ring (`ui::FocusRing`,
  "Tokens", Focus) in the shown game's colour on its pages; their pressable area is `ui::Hit` (an `InvisibleButton` with
  `ImGuiButtonFlags_EnableNav`, which ImGui 1.91+ needs, and ImGui's own square cursor hidden). The widgets still drawn by
  hand call `App::FocusRing`, which draws the same ring. The page's columns are `ImGuiChildFlags_NavFlattened`.
- **Pad and keyboard.** The sidebar and the page are separate windows: B / Escape on Play or Home moves the focus to the
  sidebar's current row; A on a row opens its page with the focus on its first control (`App::GoTo`, `TakeFocus`).
  B / Escape leave Settings, Game settings and Collection, close dialogs and menus, clear the collection's search; whatever is open
  answers it before the page does (`m_backTaken`). LB / RB change Settings and Game settings tab (`App::DrawTabBar`) and the seat the collection chooses for. The
  launcher starts with the focus in the sidebar. The ARKEYAN LAUNCHER title is not a stop (it is Home's link for the pointer):
  Home is the start page, and its games are the sidebar's rows.
- **Hold to confirm.** What deletes or overwrites is a red button held for a second (`App::HoldButton`, the state in
  `hold_confirm.h`): Reset and Delete (Figure page), Delete for good (trash), Mark as not in use (a stale lock), Restore
  (a save backup), Reset to defaults (a game's keyboard), Reinstall, Delete saves and data, Remove game. The fill shows the time; releasing, or the pointer
  leaving, cancels; it acts once per press; Space, Enter and the pad's A hold it too. The label is a plain verb, the
  instruction is its hint under it (`ui::Button` with `hold`: a timer icon on the button, "Hold to confirm" or the
  button's own sentence on hover, on keyboard and pad focus, and for 2.5 s after a tap too short, so a pad or touch player
  sees how it works). Buttons are told apart by their ImGui ID.
- **Quitting.** The window's close, Cmd+Q or Alt+F4 during an install asks: Keep installing (default) or Quit.
- **Toasts.** `ui::Toasts` (`App::Toast(text, ui::ToastKind)`): bottom right, stacked upwards (newest at the bottom, at most
  4), above the game page's band and Home's buttons, each at most 420 points of text wide. Info and Success go by themselves
  after 2.5 s, longer for a long message (up to 6 s); the same message again refreshes the one shown. An Error
  (`ToastKind::Error`; every call says its kind)
  stays until it is closed: a click, or, with the keyboard or pad in use, it takes the focus and A, B, Enter or Escape close it
  (the focus goes back to the page). Each kind has its icon and ring colour (an i, a check, a crossed circle). Texts are short,
  sober, no full stop; they are English in the code and go through the string table (`docs/translations.md`).
- **Idle.** The loop sleeps in `SDL_WaitEventTimeout` (`App::IdleWait`): every frame only while something moves (a disc
  check, an install, an update, a held button, a text cursor, the logo's fall) and for three frames after an input; 30 fps for the night
  sky's stars; else it wakes every 0.5 s to poll the jobs, the portal, the collection and the game running.
- **Copy.** New and changed sentences sit at the top of their file (`namespace strings`), ready for #4.

## Desktop shortcut
Game settings > Game > Desktop shortcut > **Create** (enabled once the disc is verified: the icon comes from it) puts a launcher for
that game on the Desktop. It starts Arkeyan Launcher as `ArkeyanLauncher --game <id> --play`: the window opens on that game's page, checks the
disc, and starts the game as soon as it is ready. If it can't (no disc, a failed check, the game not built yet) the window stays
open and says why, so the shortcut never leaves the player with nothing. Made again, it replaces the old one.
The icon is the built game's own, `game_icon.png` in its folder (the disc's title logo fitted in a transparent 512 px square, made by the kit's SkyBuild); a game built before that has none, and the shortcut falls back to the title icon decoded from the player's disc (`GameSlot::meta.iconPng`, memory only until the player asks). On Windows a 512 px PNG is halved to 256 px for the `.ico` (an entry is at most 256, `FitIcoPng`).

| System | File on the Desktop | Icon |
| --- | --- | --- |
| macOS | `Skylanders Giants.app`: `Contents/MacOS/launch` (a two-line `sh` script: `exec '<launcher>' --game giants --play`), `Info.plist` | `Contents/Resources/icon.icns`, the PNG as an `icp6` (64 px; the type follows the width) |
| Windows | `Skylanders Giants.lnk` (IShellLink: target ArkeyanLauncher.exe, arguments, working folder) | `<data>\Giants\shortcut.ico`, the PNG inside an .ico (Vista and later) |
| Linux | `skylanders-giants.desktop`, executable (`Exec="<launcher>" --game giants --play`, `Categories=Game;`) | `<data>/Giants/shortcut-icon.png` (some desktops ask "Allow launching" once) |

`<launcher>` is the running file (`SelfExecutable()`: the executable, or the `.AppImage` on Linux, not its temporary mount).
If Arkeyan Launcher is moved afterwards the shortcut needs making again. On macOS the first write to the Desktop may ask for permission
to access it. The Desktop is `SKY_LAUNCHER_DESKTOP` when set (tests: nothing writes the real one), else the system's folder.
Windows is not built or run here: its `.ico` bytes and every name and argument are tested on every system; the `.lnk` itself is
the one standard COM call (`#ifdef _WIN32`, checked to compile with MinGW) and needs a look on a real PC.

## Command line
`Arkeyan Launcher [--game <id>] [--play] [--help]`: `--game ssa|giants|swapforce` opens that game's page (else the game shown last),
`--play` starts it once its disc is checked and it is built. Anything else is ignored with a line on stderr.

## Collection
See `docs/collection_ui.md`: the page shows SSARecomp's shared collection (the game's folder, locks, favourites, backups
and six-game catalogue) with the Toy Box's model: the portal at the next start (the game's seats on top), the bar
(the search field and Filters: Element, Game, Type, Sort by; the tags), the shelf (every figure once, favourites first), the figure sheet (Put on the portal, Edit,
Favourite, `...`, Details), Add a figure and Manage. The icons are the shown game's portraits from its disc, else the
other game's, else the toy's family picture, else the generated ones (`figures::IconStyle`): "Disc art" below.

## Disc art
The launcher never ships Skylanders artwork. After a disc is verified, the check's worker thread calls the shared
provider (`src/shared/art/game_art.h`, the same code as the game's Toy Box; SSARecomp's `docs/runtime/host/game_art.md`):
`LauncherAssets::Provider::ExtractFromDisc` = `art::Extract(game, disc, titleIcon)`. One flow for every game; what
differs per game is the provider's table.

| Role | Spyro's Adventure | Giants | Swap Force | File |
| --- | --- | --- | --- | --- |
| Logo (game page, sidebar, Home) | `skylanderslogo` in `uiimage.str` (1024x512, DXT5) | composed from the title's sprite sheet (`level/Title.bld`, 1033x541, with the plate and the medallions) | the title picture of `defaultloadscreen.pak` (`juicestandard_title_00.igz`, 872x400, the 32-bit lossless one; the disc has no larger logo) | `<data>/<Game>/launcher-cache/logo.png` |
| Background (game page, Home) | `fullscreen_000` in `uiimage.str` (512x512, the painted sky) | `ui_levelfullscreen/mainmenu.png` in `permanent/global.arc` (1600x960) | the main menu's background of `juicedomain_frontend.pak` (1280x720) | `.../background.png` |
| Title icon | `default.xex` XDBF resource | the same | the same | `.../title_icon.png` |
| Toy portraits (collection) | `toy_<name>_hud` (128 px), `<name>_vs` (512 px), 4 items and 3 family icons (potion, compass, paw) of `uigameimage.str`: 81 | the collection screen's 123 portraits (256 px) | the toy collection's portraits and the collectible icons of `toycollectionimages.pak` and `collectibleicons.pak`: 209 | `<data>/<Game>/portraits/` |

- Already cached pictures are kept (a second check reads nothing); `manifest.ini` records the reader's version and what
  was found. Deleting the folders is always safe. The cache of older launchers (`SSA/launcher-cache/<title id>/`, for every
  game) is removed at the next check.
- The window loads them as textures through `App::CachedArt(game, role, maxWidth)` (also for games not opened this
  session: the sidebar and Home show any game whose disc was read once). Logos are cropped to their opaque part
  (`LoadArt`); art drawn much smaller than its size (the sidebar's logos, Home's cards) is averaged down on the CPU first,
  since the renderer has no mipmaps.
- **Textures at most 1024 px wide for the backdrop** (`BackdropArtW`): SDL's software renderer (the offscreen screenshot
  test, and the fallback when no GPU renderer opens) drops one triangle of an image wider than that (seen with Giants'
  1600 px painting).
- **A logo is never drawn above its own pixels** (`App::DrawLogo`: one texel per device pixel at most, with the display's
  scale taken in): a picture stretched past its size only blurs, and a game whose disc has a smaller logo (Swap Force's is
  872 px wide, SSA's 1024 and Giants' 1033) is not enlarged to match the others. In a large window the logo stops growing.
- Swapper cards show the swap screen's pictures of both halves, joined at the waist (`docs/collection_ui.md`); they are read from the
  disc with the other artworks (`artworks/`, the `halves` of the catalogue).
- The toys' pictures follow one rule (`collection::PortraitPath`): the shown game's, else the other game's, else the
  toy's family picture, else the generated icon (see `docs/collection_ui.md`).

## Legibility
- Outfit was loaded from its variable file at the font's default axis, **Thin (100)**, and "bold" was that thin text drawn twice.
  It is now `Outfit-Regular.ttf` and `Outfit-SemiBold.ttf`, static instances of the same OFL font (`fontTools.varLib.instancer`
  at wght 400 and 600); bold text (`AddBoldText`, `BoldText`, bold buttons) is SemiBold. Only `BolderPasses` (the chosen
  controller) still draws a second copy.
- Contrast, WCAG AA, measured on every text colour against every surface it sits on (the dark greys of the sidebar, panels,
  fields, the band, the raised buttons and the page gradient). Two tokens failed and changed, keeping their hues:
  `TextDim` 0x9C9CA8 -> 0xB8B8C4 (4.1:1 on the raised greys, now 4.5 to 8) and `Danger` 0xF85149 -> 0xFF817A (3.4:1, now 4.5 or
  more). Text, TextSecondary, Accent, Ok and the sidebar ink were already above 4.5 (5.4 to 15). Ink on the accent is 8.3.
- Sizes at 1x: the five text styles ("Tokens", Type): Caption 14 is the floor, Body 17, Label 20, Title 24, Display 34.
- Glyphs are rasterized at the screen's density with no extra oversampling (`OversampleH/V=1`, `PixelSnapH`): stems stay
  one pixel on a 1.5x or 2x display. FreeType (`imgui_freetype`, light hinting) isn't built in: it would be a new library,
  and with static weights the stb rasterizer is crisp enough; revisit if a display shows blur.

## Writing
The launcher's texts are short. A player reads them to decide or to know what to do, not to learn how it works.
1. A **help line is one short sentence**, one line at the default window size: what it does for the player. It leaves out
   how it works (checks, hashes, caches), what happens if you don't, and anything the label already says.
2. **No path in running text.** Where the place matters there is a "Show in folder" button (the existing one) instead.
3. **No help line** when the label is clear on its own.
4. **Errors and notifications** say what happened, then what to do: one short line each, no full stop in a toast.
5. **Numbers that help decide** (sizes, times) stay, rounded. The tone is the same; French keeps "vous" and is as short.
6. **Labels are two or three words**, and a title and its button do not repeat words ("Desktop shortcut" and **Create**,
   not "Create a desktop shortcut").
7. **Each part of a line starts with a capital**: "Top: Doom Stone · Bottom: Doom Stone" (French: "Haut : Doom Stone · Bas : Doom Stone").

Developer details (the runtime's name, an option's file, the SHA-256 a check uses) belong in these docs, not in the window.

## Uninstall everything
Settings > Launcher > **Remove Arkeyan Launcher** > **Uninstall everything...** opens, in the tab, the list of what goes (`ui/uninstall_page.cpp`;
what it removes and the safety rules are in `docs/install.md`, "Uninstall everything"):

- **What goes**: one row per built game, then Game data copies, Game tools, Caches and logs, Settings, Desktop shortcuts and Arkeyan Launcher,
  each with its size, and the **Total**. "Your disc images are never touched." sits under the title.
- **Also delete my saves and my figures** (a toggle, off by default) adds Saves and Figures to the list and to the Total; **Export first**
  beside it is the collection's export of every figure (a folder to choose). Under it: "Saves X, figures Y."
- What the launcher already knows will stay is said under the Total ("SGRecomp.app is not in its folder: it stays.").
- **Uninstall everything** is a hold button (a second; the existing pattern, `HoldConfirm`); **Cancel** goes back. Both are disabled while a game
  runs, installs or is being checked, and the reason is said in red above them.
- After it ran, the tab shows the result: "Everything is removed", or "Removed, except" with one line for each thing that stayed
  (a disc image in a game's folder, a game the player moved, a link, a file the system refused), the size freed, and where the launcher
  goes ("Arkeyan Launcher goes to the Trash when you quit." on macOS, "...deletes itself when you quit." elsewhere). **Quit** closes the window. Until
  then the window does nothing else: the sidebar and the tabs are off and the settings file is no longer written (`FreezeLauncherConfig`).

The screenshot test is `SKY_LAUNCHER_TEST_UNINSTALL=1` with `SKY_LAUNCHER_SHOTS` on a scratch data tree (`SKYLANDERS_DATA`, `SKY_LAUNCHER_DATA`,
`SKY_LAUNCHER_INI`, `SKY_COLLECTION`); `SKY_LAUNCHER_UNINSTALL_SELF` names a dummy bundle to stand for the launcher and, on macOS,
`SKY_LAUNCHER_UNINSTALL_TRASH` a scratch folder to stand for the Trash (without them a scratch run never removes the launcher).

## Size on high-DPI screens
Every size is in launcher points. `App::UpdateUiScale` picks how many window units a point is: `[launcher] ui_scale`, else
the display's scale beyond its pixel density (Windows at 200 %: 2; a Retina Mac: 2 / 2 = 1, its window units are
already points), capped so the smallest layout fits the display's usable area. ImGui gets the window size divided by
the scale and a framebuffer scale multiplied by it, so it draws and rasterizes the fonts at the screen's real density.
It follows `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED`; Ctrl (Cmd) + / - / 0 zoom and reset, saved in `ui_scale`.
The Windows executable declares per-monitor v2 DPI awareness in its manifest (`resources/windows/`).

## How to change the look
- Colours and sizes: the tokens (`ui/theme/tokens.h`), named by role; the palette's values in `launcher_theme.h`; ImGui's own
  widget colours in `LauncherTheme::Apply`. Each game's own colours (its main action, its focus ring) are its `GameStyle` there.
- A widget: change its component in `ui/components/`, then look at every gallery page (en and fr, 1x and 2x) and the screens'
  screenshot tests. A new widget is a new component (or an option of one), with every state, in the gallery.
- Fonts: put an OFL- or Apache-licensed font in `resources/fonts/` and load it in `App::LoadFonts`
  (`SKY_LAUNCHER_FONT_DIR` overrides the folder while trying fonts); `ui::Ctx().fonts` hands them to the components.

## Screenshot test
The launcher can drive itself without opening a visible window:
```sh
SDL_VIDEO_DRIVER=offscreen \
CFFIXED_USER_HOME="$PWD/private/launcher-home" \
HOME="$PWD/private/launcher-home" \
SKY_LAUNCHER_INI="$PWD/private/launcher-home/launcher.ini" \
SKY_LAUNCHER_SHOTS="$PWD/private/launcher-shots" \
SKY_LAUNCHER_TEST_DROP="/path/to/game.iso" \
SKY_LAUNCHER_FONT_DIR="$PWD/resources/fonts" \
./build/ArkeyanLauncher.app/Contents/MacOS/ArkeyanLauncher
```
It writes `01-no-iso.png`, `02-verified.png`, `03-graphics.png`, `04-controls.png`, `05-collection.png`, `06-home.png`,
`07-figure-sheet.png`, `08-new-figure.png`, `09-play-with.png` (the controller menu), `10-language.png`, `11-game-disc.png`,
`12-game-saves.png`, `13-game-data.png`, `14-launcher-settings.png`, `15-disc-gone.png`, `16-logo-drop.png`, `17-launcher-support.png`, `18-launcher-about.png` and `19-problem-report.png` (INSTALL on an image that moved; `03-` is the
game's Video tab, `04-` the launcher's Controls) (it creates a few figures in `SKY_COLLECTION` first: set it to a scratch folder). With `SKY_LAUNCHER_TEST_COLLECTION=1` it shows the collection's screens instead (`docs/collection_ui.md`).

`07-figure-sheet.png`, `08-new-figure.png`, `09-play-with.png` (the controller menu), `10-language.png`, `11-disc-gone.png`
(INSTALL on an image that moved) and `12-logo-drop.png` (the logo early in its fall) (it creates a few figures in `SKY_COLLECTION` first: set it to a scratch folder). With `SKY_LAUNCHER_TEST_COLLECTION=1` it shows the collection's screens instead (`docs/collection_ui.md`).
With `SKY_LAUNCHER_TEST_NAV=1` it sends keys as a player would (`40-` to `53-`): B to the sidebar, down past the three games and
Coming later, A into Collection, Y for the search, the D-pad to a seat, A to choose for it, down and A to put the first card there,
X for a card's sheet, B back, then down to Settings, A, RB to its Launcher tab, Escape. Each step's page is checked ("FAIL nav step";
a timeout stops a run that went astray); `ArkeyanLauncherTest` runs it (`NavChecks`).
With `SKY_LAUNCHER_TEST_INSTALL=1` and `SKY_LAUNCHER_KIT=$PWD/build/fake-kit` it shows the install screens instead
(`docs/install.md`). With `SKY_LAUNCHER_TEST_GALLERY=1` it shoots the component gallery ("Gallery").

## Other screenshot variables
`SKY_LAUNCHER_TEST_SCREENS=mouse` (or `pad`) with `SKY_LAUNCHER_TEST_DROP=<SSA disc>` shoots the three model screens,
`screen-home`, `screen-game` and `screen-keys` (Spyro's Adventure verified and built by hand, Giants opened with no disc,
Swap Force not opened); with `pad` each page's first stop has its ring and the pad legend shows. `SKY_LAUNCHER_LANGUAGE`,
`SKY_LAUNCHER_TEST_SIZE` and `SKY_LAUNCHER_TEST_SCALE=2` (a Retina screen's density, the same layout) set the rest
(`docs/polish_audit.md`, "Model screens").
`SKY_LAUNCHER_TEST_INPUT=pad` shoots any script as a pad player sees it: the pad's glyphs and legends, the focus on each page's (or tab's)
first stop with its ring.
`SKY_LAUNCHER_TEST_RUNNING=1` (with `SKY_LAUNCHER_RUNTIME` a stand-in game and `SKY_LAUNCHER_TEST_DROP` Spyro's Adventure's disc)
shoots one game at a time ("A game running"): `running-1-game`, `running-2-other`, `running-3-ended`.
`SKY_LAUNCHER_TEST_KEYS=1` shoots each game's Keyboard tab (`keys-ssa`, `keys-giants`, `keys-swapforce`; `SKY_LAUNCHER_LANGUAGE=fr` for the French ones).
`SKY_LAUNCHER_TEST_PAD=1` shoots each game's Controller tab with a few buttons given other jobs (`pad-ssa`, `pad-giants`, `pad-swapforce`)
and Swap Force's Game tab with Nightmare from the start on (`game-swapforce`).
`SKY_LAUNCHER_TEST_LOGO=1` shoots each game's page (`logo-ssa`, `logo-giants`, `logo-swapforce`) from the art already cached in
`SKYLANDERS_DATA`; `SKY_LAUNCHER_TEST_SIZE=<w>x<h>` sets the window (the default one, a large one).
`SKY_LAUNCHER_TEST_UPDATE=1` with `SKY_LAUNCHER_UPDATE_URL=file://<fake latest.json>` shows the update screens (`20-` to `24-`).
`ArkeyanLauncherTest --no-disc <absolute scratch dir>` runs only the checks that need no disc image (the unit parts; the build workflow runs it on
the three systems); the full run is `ArkeyanLauncherTest <disc> <scratch dir>`.
`ArkeyanLauncherTest` removes its scratch folder at the end; `SKY_LAUNCHER_TEST_KEEP=1` keeps it with its screenshots.
`SKY_LAUNCHER_TEST_INSTALL=real` runs the real kit. `private/shoot.sh <out dir> [VAR=value...]` is the wrapper used in this
repository's working copy (its own HOME, the offscreen driver).

## Notes
- Drag & drop ISO is supported through `SDL_EVENT_DROP_FILE`.
- Verification progress is saved to `launcher.ini`; the SSA title/icon come from the player's own XEX.
- The game catalog lives in `LauncherGameCatalog()`; SSA, Giants and Swap Force are enabled.
- **Switching games is in place.** Choosing the other game in the sidebar keeps the window: the pages draw that game's state
  (`App::SelectGame`). Each game the player opens has a `GameSlot` for the session: its disc and check, art and icon, install
  (progress, kit download, built game), save backups and collection page (the collection's game filter starts on that game).
  The first time a game is shown its remembered disc is checked on a worker thread (art extraction included), as at start; a
  game seen before is shown as it was left, instantly. Every slot's checks and installs are polled each frame (with that game's
  folders in place), so an install goes on while the other game is shown; closing the window asks first if any game is
  installing. The last game shown is written to `[launcher] game=` and opens at the next start. PLAY starts the shown game's
  own build (SSARecomp, SGRecomp...) with its settings (`PrepareGameLaunch`) beside the window ("A game running"). The settings
  pages (language, game data, reinstall) follow the shown game. Since 0.4 the game page arrives with its logo's fall and
  its own look ("Each game's look").
- `SKY_LAUNCHER_TEST_SWITCH=1` (with `SKY_LAUNCHER_SHOTS`, a `[launcher] kit=` that is the fake kit, and both games' `iso=` in
  the config) runs the switch script: SSA and Giants checked, an install of each with the fake SkyBuild (SSA shown while
  Giants builds), a switch back and forth with each state kept, and PLAY on each returning its game and build. It is run by
  `ArkeyanLauncherTest` when `SKY_LAUNCHER_TEST_GIANTS_ISO` is set.
- `SKY_LAUNCHER_TEST_SEATX=1` (with `SKY_LAUNCHER_SHOTS`, `SKY_COLLECTION`, `SKY_SAVE_DIR` on scratch paths) runs the seat X script
  (`App::TestSeatXScript`): the Collection page's four seat tiles (player 1, player 2, item, adventure) each emptied by the mouse on the X,
  by Delete and by the pad's X, checking after every try that only that seat is empty. It is run by `ArkeyanLauncherTest`.
- `ArkeyanLauncherTest` checks verification, settings (`tests/settings_test.cpp`: per-game video, the command line, the shortcut files,
  delete data and uninstall on scratch folders), font/text helpers, the disc texture decoder, the seat X and the in-place game switch
  (build it with `--target ArkeyanLauncherTest FakeSkyBuild`; set `SKY_LAUNCHER_TEST_GIANTS_ISO` for the switch, and
  `SKY_LAUNCHER_TEST_SWAPFORCE_ISO` for the Swap Force disc and its pictures). A swapper in the Collection is one card
  (`docs/collection_ui.md`, "Swappers").
- `launcher: ui scale` is logged once, then only when the scale changes (a display change, Ctrl +/-/0), not at every window event.

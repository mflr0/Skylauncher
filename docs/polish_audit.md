# Polish pass (0.6.2): clipped edges and small oddities

Every screen and state the screenshot tests reach was shot in English and French (the default window 1024x720, the
smallest 1024x680, a large 1600x1000; the main button also at 150 % and 200 % UI scale) and looked at zoomed. The design is
kept; only defects are listed. Shots: `SKY_LAUNCHER_SHOTS` with each `SKY_LAUNCHER_TEST_*` script (`docs/launcher_ui.md`,
"Screenshot test"); `ArkeyanLauncherTest` with `SKY_LAUNCHER_TEST_KEEP=1` keeps its own shots (install, update, sidebar,
switch, copy, seat X) in its scratch folder.

## Fixed (root causes)

| Where | Defect (before) | Fix |
|---|---|---|
| Collection (every shot, about 75 per language), also seats, the choose banner, the figure sheet's confirm bar, filter chips | The page column ended exactly at the content edge: the clip rect shaved the outer half of the right border and corners of the rightmost widgets ("..." button, Adventure seat, banner) | `App::BeginPageColumn`: the column reaches 4 points past each side (its padding gives the content the same width) |
| Settings (launcher and game), Collection | Scrolling content was cut flat 28 points above the window's bottom, over an empty band | The same column reaches over the page's bottom padding; `EndPageColumn` adds the padding back at the end of the content |
| Every primary pill (Add a figure, Keep installing, Retry, Resume, Create, Quit, Install details...) | ImGui's grey frame showed as a dark, jagged ring around the light-blue fill | `PillButton` draws a transparent frame for primary buttons |
| Keyboard tab, French | "Retour arrière" touched the key button's edges; "Déplacement haut (stick gauche)" wrapped to two lines and made uneven rows | Key buttons 118 wide in 124-point columns; the four movement rows are named "Haut", "Bas", "Gauche", "Droite" (stick gauche) |

## Fixed in the second round

| Where | Defect (before) | Fix |
|---|---|---|
| Toasts | Over the top-right buttons | Bottom right, stacked upwards, above the game page's band and Home's buttons |
| Main button, disabled | The gear segment stayed bright, its icon dark | The segment greys with the main half (lit again under the pointer), its icon fades like the label |
| Main button | The divider before the gear stopped short of the edges | It runs from edge to edge |
| Edit figure | The Level value sat under the grab; the right column was 8 points lower than the left; All / None were tiny pills | The value beside the slider; the second column starts at the first one's top; All / None as the other small buttons |
| Manage > Backups | Restore buttons at different x, text off the button's centre | One column (the widest label), text centred on the button |
| PLAY WITH | Wider than the picker, square hover row | As wide as the picker (names cut to the row, tooltip), "Automatic (gamepad first)" is now "Automatic", the row highlight uses the popup's corners |
| Seats | A long seat name ran into the X | The name is cut before the X; French "Objet" and "Voir ci-dessous" |
| Launcher settings | A long data-folder path broke inside a name | It breaks after a separator |
| Game settings | The Back button's right edge matched neither the card nor the scrollbar | It sits at the column's right edge |
| Collection | The choose banner's right border was still shaved (the shelf clipped it) | The shelf reaches 4 points past each side too |

## Fixed in the third round

| Where | Defect (before) | Fix |
|---|---|---|
| Sidebar rows (keyboard/pad focus) | Two rings (ImGui's and ours), flat at the window's left edge | ImGui's nav cursor is off in the sidebar; one rounded ring inside the row |
| Every combo | A square arrow cell glued to the rounded box | `App::BeginCombo`: the chevron inside the box (`ImGuiComboFlags_NoArrowButton`) |
| Every list row (combos, Versions, devices...) | Square hover/selected highlight | `App::Option` draws it under the row with the launcher's radius |
| Edit figure, Level | ImGui's slider: grab squeezed at the maximum, box corners cut | `App::LevelSlider`: round knob, one size along the track, focus ring, arrows when focused |
| Settings tabs | Game settings' tab bar 6 points lower than the launcher's | Same spacing under the heading; the tab focus ring has room around the label |
| Sidebar footer | At 1024x680 the footer touched the game list | A hairline midway between them when there is room (none when they nearly touch) |
| Video tab, French | Descriptions on two lines | "Made for 30 FPS; 60 is experimental." and "Auto shows more when wide; 16:9 adds bars." |
| Game page | "Change disc..." 40 points past the main button | Its right edge is the action button's |
| Install details | The log's top line half cut | The box holds a whole number of lines |
| Settings pages | The card and the Back button changed width with the scrollbar | The scrollbar is always shown |
| Keyboard tab | A long label wrapped inside its brackets; the heading sat 8 points above the Reset button | The brackets go on a second line; the heading is centred on the button |
| Misc | "..." 4 points past the seats' edge; Auto-open Toy Box cramped; Video "Show in folder" narrower than the dropdowns; French "Fonctionne dans" cut; edit fields narrower than the upgrade cards | Each aligned or spaced |

## Fixed in the fourth round

| Where | Defect (before) | Fix |
|---|---|---|
| Add a figure | The catalogue grid ran under the footer strip, its scrollbar sat in the gutter; nothing said when a search found nothing | The grid ends 8 points above the strip; the cards share the column's width, the scrollbar sits against the last card; "No figure matches" when nothing matches |
| Hold-to-confirm fill | Ended square at the leading edge | Rounded like the button, at least two radii wide (`App::HoldButton`) |
| Disc check and game data copy bars | ImGui's square fill, a dark stub for the track's end | `App::ProgressBar`: pill track and fill, a whole cap even when nearly empty |
| Toasts | The 2-point ring blurred at 1x | Whole-point box, ring inset so it covers whole pixels at 1x and 2x |
| "Dark" badge | The dark purple badge vanished on the dark card | A light hairline on badges darker than the cards (`App::Badge`) |
| Collection cards | The portrait ended flat at the bottom of some arts | The foot fades into the card |
| Tooltips and popups | Stuck to the window's right edge | `DisplaySafeAreaPadding` 12 points |
| Interrupted install page | A toast from the page before stayed over the card | Toasts are cleared when the page comes back |
| Install details | The kit's long English lines ran past the box (sideways scroll) | They wrap inside the box; the text is untouched |
| Figure sheet, Details | "created" shown in English in French | Translated (`created`, `imported`, `portal`) |
| Filters panel | The label column could touch the boxes in a longer language | The column fits the widest label |
| Screenshot test | "polar" search found nothing for Spyro's Adventure | It is run for Giants, where Polar Whirlwind exists; `56-add-no-match` shows the empty search |

## Left

Nothing in the launcher. The kit's own English log and error lines are shown as it writes them (wrapped, readable); their wording is the kit's, not ours.

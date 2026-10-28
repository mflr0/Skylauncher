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

## Model screens (2026-10-28): Home, the game page, the Keyboard tab

The owner's design review asked for "as clean as possible, at a professional level": dorydev's style on a strict grid. Three
screens were brought there first, for the owner to review before the rest: Home, the game page (with the sidebar and the
band) and a game's Keyboard tab. Shots: `SKY_LAUNCHER_TEST_SCREENS=mouse|pad` ("Screenshot test" in `launcher_ui.md`), in
English and French, 1024x720 and 1600x1000, with the mouse and with a pad, and at 2x (`SKY_LAUNCHER_TEST_SCALE=2`) for the
zoomed checks; edges and centres measured on the 2x shots (pixel rows and the ink's bounding boxes).

**Checklist** (each shot of the three screens, each item):

| # | Check | Result |
|---|---|---|
| 1 | Spacing on the 4-point steps, from the tokens (no literal gaps) | Pass: sidebar header (XXS, L, XS), Home (XS under the title, L to the cards, XL to Coming later, XS to its badges), the disc block (XXS head, S to the body, XS lines), the Keyboard tab (S under a cross's title, XS between keys, M to the hairline) |
| 2 | Even margins: a card's padding the same on its sides and foot | Pass: Home's cards L at the left and the foot (was M at the foot); the disc block M all round, centred in the page (was pinned left) |
| 3 | One left edge per list | Pass: every sidebar row's title at the same x (a `GameLogoW` leading column for logos, initials, the chevron and the icons); the header on the rows' content edge (was 4 points left of it) |
| 4 | Each row's content centred on one line | Pass: list rows' title and state as one block centred on the row (cap top to baseline, within half a point at 2x); settings rows' labels on their control's middle (they sat at the cell's top); key caps' letters on their face |
| 5 | Baselines shared on a line | Pass: the version after the title, a list row's note and trailing text, a settings row's note (the pad button) |
| 6 | An icon on its text's line | Pass: the state's icon centred on the capitals of its words (measured: same centre at 2x); pills, buttons, chips, menus the same |
| 7 | Nothing wraps inside a grid | Pass in both languages and sizes ("Agiter / Insérer / Balancer (stick droit)", "Retour arrière", "Changer de disque...") |
| 8 | One icon family, one look | Pass: Lucide everywhere (sizes 16, 20, 32); the hand-drawn icons and PlayStation's drawn shapes are gone; the GitHub mark stays (no Lucide brand marks) |
| 9 | Focus ring with a pad only, never on what is not pressed | Pass: none in the mouse shots; with a pad on Home's shown card, the game page's main action, the settings' selected tab; the sidebar title is no longer a stop (it had a ring drawn always) |
| 10 | The ring clear of its neighbours | Pass: a tab's ring keeps S of air at its sides (it touched the letters); the portal pill S under the main action (the ring touched it); Home's ring around the card (it was drawn around the card's state pill) |
| 11 | Fewer elements | Pass: the CONTROLLER caption is a pad icon in the picker; the Keyboard tab's repeated KEYBOARD title is gone (the tab says it); the four movement and four right-stick rows are two crosses |
| 12 | Text and states read the same in English and French | Pass |

**Left for the owner** (decisions, not defects): a game not opened yet this session shows "Open" with a dashed circle (its
disc is only checked when it is first shown; checking every disc at start would say more). The pad navigation script
(`SKY_LAUNCHER_TEST_NAV`) still counts two games in the sidebar (Swap Force's row came later): "down, down" lands on Swap
Force instead of Collection, and the run stops at its step 49, A on CHOOSE DISC opening the file chooser; to update with the
Collection's pass.


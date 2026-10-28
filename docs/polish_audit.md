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

## Left (checked again, small, not touched)

- Sidebar rows' focus ring is drawn inside on purpose and is flat at the window's left edge.
- ImGui's own widgets: the combo's arrow cell, the Level slider's grab at its maximum, square Selectable rows (Add a figure > Versions).
- The tab row sits 6 points lower on game settings than on the launcher's settings; at 1024x680 the sidebar's footer is 40 points under the game list.
- Two-line French descriptions in the Video tab; "Change disc..." is 40 points past the main button's edge.
- The install log's first line can be cut by the box's top; the kit's own error lines stay in English.

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

## Left (small, not touched)

- Toasts sit over the top-right buttons while shown and reach 20 points from the window's edge (Collection, Game tab).
- The disabled main button greys its label half only; the gear segment stays blue.
- The divider before the gear stops a few points short of the button's edges; the installing button's fill ends in a lighter band.
- Edit figure dialog: the Level slider's value sits on the grab; the two columns' rows are offset by a few points; All / None are very small pills.
- Add a figure: the selected Versions row is square-cornered; Manage > Backups: Restore buttons do not line up when a row has no level text.
- PLAY WITH popup is wider than its button; the hovered row is square inside the rounded popup.
- Focus rings on sidebar rows are cut by the window's left edge (they are drawn inside on purpose, `FocusRing(0, -2)`).
- French: "Objet magique" runs into the seat's X; two-line descriptions in the Video tab leave one-word lines.
- A long data-folder path breaks mid-word.

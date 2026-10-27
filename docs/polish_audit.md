# Polish audit (Skylauncher 0.3.1, `feat/polish`)

Every screen and interaction, with the mouse, the keyboard and a gamepad, against the bar of Steam, Battle.net, the
Xbox app and the Epic launcher. The design is dorydev's: nothing here redesigns it; the findings are interaction bugs,
states, copy, layout slips, robustness and performance.

How it was checked: the code of every screen (`src/launcher/ui/`), the launcher's own screenshot test
(`SSA_LAUNCHER_SHOTS`: the main, collection and install passes, at the minimum window size), `ArkeyanLauncherTest`, and an
idle CPU measure (the launcher alone on Home, offscreen, 6 s). Real displays at 150-200 % and Linux were not available:
their findings come from the code. While fixing, a keyboard and pad pass (`SSA_LAUNCHER_TEST_NAV`, keys sent to ImGui as
a player's) found N0, N10 and N11, a timing found P4's freeze, and a 300-figure collection found P6 and B1.

Severity: **P0** broken, **P1** clearly unprofessional, **P2** polish, **P3** nice to have. Effort: **S** under an hour,
**M** a few hours, **L** more. A dash is a check that found nothing to change.

## Counts

| Severity | Found | Fixed | Left |
|---|---|---|---|
| P0 | 2 | 2 | 0 |
| P1 | 20 | 20 | 0 |
| P2 | 24 | 21 | 3 (N9, F4, R7) |
| P3 | 7 | 1 | 6 (C7, X2, X3, P5, R8, R9) |

## 1. Hold-to-confirm buttons

| ID | Sev | Where | What is wrong | Why it matters | Fix | Effort | Status |
|---|---|---|---|---|---|---|---|
| H1 | P0 | `CollectionPage::HoldButton` (`collection_figure.cpp`), Manage > Trash | The hold is keyed by the button's label. In the trash every entry has a "Delete for good (hold)" button: the entries not held clear the shared state every frame, so the held one restarts at 0 each frame and **never completes** once the trash has two entries. | The only way to empty the trash does nothing, with no feedback. | Key the hold by the ImGui ID; one state machine (`HoldConfirm`) without ImGui, tested. | S || Fixed (`3950c9e`) |
| H2 | P1 | Same | Moving the pointer off the button while holding keeps filling it and acts. | Every hold-to-confirm cancels when the pointer leaves; that is the escape hatch. | Cancel when the pointer leaves; a new hold needs a new press. | S || Fixed (`3950c9e`) |
| H3 | P1 | Figure page, Trash, lock | Labels "Reset (hold)", "Delete (hold)", "Delete for good (hold)", "It is not in use any more (hold)". | Instructions in a label read as unfinished. | Plain verbs ("Reset", "Delete", "Delete for good", "Mark as not in use"); the instruction in the tooltip, which the keyboard and the pad's focus show too. | S || Fixed (`3950c9e`) |
| H4 | P2 | Same | The label is drawn twice (ImGui's, then a copy one offset to the right), and a third time over the fill; no focus ring. | Blurry text, no keyboard or pad focus. | Draw it once, bold, over the fill; the focus ring of N1. | S || Fixed (`3950c9e`) |
| H5 | P2 | Same | After acting, keeping the button down is safe (it acts once per press), but nothing guards a new hold that starts under a still-pressed key or button. | A held key must never act twice. | The state machine latches until every activation input is released. | S || Fixed (`3950c9e`) |
| H6 | - | Keyboard and pad | Space, Enter and the pad's A do hold the button (ImGui keeps a nav-activated button active while the key is down), but only H1-H5 stand in the way. | | Covered by the tests of H1. | - || Checked: `49-nav-delete-held` holds Delete with Space |

## 2. Focus and navigation

| ID | Sev | Where | What is wrong | Why it matters | Fix | Effort | Status |
|---|---|---|---|---|---|---|---|
| N0 | P0 | Every widget drawn over an `InvisibleButton` | ImGui 1.91+ leaves `InvisibleButton` out of navigation unless asked (`ImGuiButtonFlags_EnableNav`): **the main PLAY / INSTALL button**, the sidebar, toggles, chips, tabs, seats, cards and the controller picker could not be reached with the keyboard or a pad at all. Found by the navigation pass. | The pad and keyboard paths of the collection (A, X, Delete on a seat) never ran. | `ImGuiButtonFlags_EnableNav` on every one; the page's columns `NavFlattened`. | S | Fixed (`4e7c335`) |
| N1 | P1 | Sidebar rows, the main button, the controller picker, toggles, chips, Settings tabs, seats, cards, hold buttons, the install card's close | Drawn over `InvisibleButton`: they take keyboard and pad focus but show none (only ImGui's own buttons draw the nav cursor). | Keyboard and pad users can't see where they are. | One focus ring (`App::FocusRing`, the accent, as ImGui's own) on every custom widget. | M || Fixed (`e9c6e52`) |
| N2 | P1 | The whole window | The sidebar and the page are separate ImGui windows; the D-pad stays in the page. With a pad, Collection, Settings and Home can't be reached. | A launcher that takes the pad must be usable with it alone. | B / Escape on Play or Home moves the focus to the sidebar (on the current row); A on a row opens the page and puts the focus on its first control. The launcher starts with the focus in the sidebar. | M || Fixed (`4e7c335`) |
| N3 | P1 | Settings, Collection | Escape and B do nothing there. | Esc / B is "back" in every launcher. | Escape / B go back to the page you came from (Home or Play), unless a menu, a dialog, the search or a text field takes it first. | S || Fixed (`4e7c335`) |
| N4 | P1 | Collection > View (Y / F3) | The View menu is its own window and opens without focus: the pad can open it but not use it. | Half a feature for the pad. | Focus it when it opens. | S || Fixed (`4e7c335`) |
| N5 | P2 | Sidebar title | Home opens only with a click on the SKYLAUNCHER title (text, not a control). | Mouse-only. | A focusable button over the title. | S || Fixed (`e9c6e52`) |
| N6 | P1 | Report a problem | The pad's **A** closes the dialog (`IsKeyPressed(GamepadFaceDown)`): pressing A on Save as... or Copy also closes it. | The dialog can't be used with a pad. | B / Escape close it, as every other dialog. | S || Fixed (`5d7a69f`) |
| N7 | P2 | What's new | No Escape / B. | Every other dialog has it. | Escape / B close it. | S || Fixed (`5d7a69f`) |
| N8 | P2 | Settings | No LB / RB between tabs. | The pad convention (Xbox app, Steam). | LB / RB (and Ctrl+Tab) change tab. | S || Fixed (`4e7c335`), LB / RB only (Ctrl+Tab is ImGui's window switch) |
| N10 | P1 | Collection cards | A figure on the portal shows twice (On the portal, Recent) and both cards had its path as their ImGui ID: hover, focus and the ring landed on both, a click could answer for the other (ImGui's conflict highlight is off). | Wrong card acted on. | One ID per section. | S | Fixed (`cbd0e4a`) |
| N11 | P2 | Collection, D-pad | ImGui's navigation scores by distance: from the section chips Down lands on a seat, and Right from a seat finds no card. | The pad hops between the columns. | Left and right cross between the seats and the list explicitly. | S | Fixed (`4e7c335`) |
| N9 | P2 | Update banner | Its own window: the pad can't reach its buttons. | | Left: the banner is reachable with the mouse and the keyboard's Tab once clicked; a pad path needs the banner inside the page window (M). | M || Left |

## 3. Feedback

| ID | Sev | Where | What is wrong | Why it matters | Fix | Effort | Status |
|---|---|---|---|---|---|---|---|
| F1 | P2 | `App::DrawToast` | No width limit: a long toast ("Exported 12 figures to <a long path>", an error with its reason) runs off the left edge over the sidebar; every toast lasts 2.5 s whatever its length. | Errors must be readable. | At most 560 points wide, wrapped; 2.5 s plus the time to read the rest (up to 6 s). | S || Fixed (`6f016c7`) |
| F2 | P2 | `App::Toggle` | No hover state, no focus. | Every other control reacts. | Lighter border under the mouse, the focus ring. | S || Fixed (`3950c9e`) |
| F3 | - | Toasts | Checked: none fires per frame (each comes from one action, a finished job, or the dialog thread's answer, read once); #13's repeated toasts came from the hold button firing every second, fixed before this pass. | | Nothing to do. | - || - |
| F5 | P2 | Update banner | A message cut to fit ("The update failed: v99.0.0 has no Skylauncher-macOS.zip for this comp...") can't be read whole. | | The whole message in a tooltip. | S | Fixed (`028afa6`) |
| F4 | P2 | Disc check | The check of a disc can't be cancelled (Change disc is hidden while it runs). It takes a few seconds. | Long operations should be cancellable. | Left: `CheckGameSource` has no cancel; it is short. | M || Left |

## 4. Destructive actions

Rule after this pass: what can't be undone, or overwrites what the player has now, is held for a second; what goes to
the trash or keeps a backup is held too (Delete, Reset), so every destructive button behaves the same.

| ID | Sev | Where | What is wrong | Why it matters | Fix | Effort | Status |
|---|---|---|---|---|---|---|---|
| D1 | P1 | Settings > Game data > Save backups | Restore acts on one click (it keeps the current saves first). The figure Restore asks first. | Same kind of action, two behaviours. | Hold to restore. | S || Fixed (`6f016c7`) |
| D2 | P2 | Settings > Controls > Keyboard | Reset to defaults wipes the player's bindings on one click, with no undo. | | Hold to reset. | S || Fixed (`6f016c7`) |
| D3 | P2 | Settings > Game data | Reinstall the game starts a 15-minute rebuild on one click. | | Hold to reinstall. | S || Fixed (`6f016c7`) |
| D4 | P1 | Closing the window | Closing the launcher (the window's close, Cmd+Q, Alt+F4) during an install kills the build without a word. | Fifteen minutes lost to a stray click. | Ask: "The game is still installing" with Keep installing (default) / Quit; it resumes from its step next time. | S || Fixed (`6f016c7`) |

## 5. Copy

| ID | Sev | Where | What is wrong | Fix | Effort | Status |
|---|---|---|---|---|---|---|
| C1 | P1 | Play, main button | "Upload Iso": nothing is uploaded, and the casing is neither the title's nor a sentence's. | "Choose disc" (the band's Bungee draws it in capitals, as INSTALL and PLAY). | S || Fixed (`6384adf`) |
| C2 | P1 | Home and Play, top right | #10: an "English" menu sets the **game's** text language, the launcher being English only; the owner couldn't tell what it was for. | Recommended and done: the menu leaves the top right (kept for the launcher's own language, #4); Settings > Language names its rows "Game text" and "Game voices". | S || Fixed (`6384adf`) |
| C3 | P2 | Settings > Controls | "0 gamepad(s) connected." | "No gamepad connected" / "1 gamepad connected" / "2 gamepads connected". | S || Fixed (`6f016c7`) |
| C4 | P2 | Portal status tooltip | "the emulated one (Toy Box)" where every other screen says "virtual portal". | "the virtual portal". | S || Fixed (`6f016c7`) |
| C5 | P2 | Settings > Game data | A button reads just "Figures". | "Open figures folder", next to "Open data folder". | S || Fixed (`6f016c7`) |
| C6 | P2 | Figure page | "On the portal in Spyro's Adventure (pid 12345)": a process id is jargon. | Drop the pid. | S || Fixed (`3950c9e`) |
| C7 | P3 | Settings > Game data, Game runtime | "runtime= in the [launcher] section of <file>": settings-file jargon for players. | Left: aimed at people who set up their own build. | - || Left |

No exclamation marks were found. Casing: titles in Bungee capitals, everything else in sentence case; the only
exception was C1.

## 6. Layout

| ID | Sev | Where | What is wrong | Why it matters | Fix | Effort | Status |
|---|---|---|---|---|---|---|---|
| L1 | P1 | Play > Controller > PLAY WITH (the owner's screenshot) | The radio circles are centred on the row while ImGui puts the label at its top, so they sit low and touch it; the label is indented with spaces; the rows have ImGui's 12-point gaps between them, the heading another spacing; the menu slices through the disc text behind it with nothing marking its edge. | | Rows of one height, the circle and the label centred on it with an 10-point gap; the heading on the labels' column; no gap between rows; an opaque surface with a border. | S || Fixed (`6384adf`) |
| L2 | P1 | Play, minimum window (1024 points) | The controller picker starts 3 points under the sidebar. | Overlap at the default minimum size. | The main button moves right just enough to keep the picker a margin from the sidebar. | S || Fixed (`6384adf`) |
| L3 | P1 | Play | The portal line sits just above the band: with the two-line install hint (narrow window) the two touch. | | The portal line moves under the main button, inside the band. | S || Fixed (`6384adf`) |
| L4 | P2 | Settings > Controls | The "Test the portal" hint is a smaller font aligned to the button's top. | | Centre it on the button. | S || Fixed (`6f016c7`) |
| L5 | - | Every popup, combo, radio, checkbox | Audited after L1: the Settings combos open under their field and are opaque with a border; the seat menu, Put on..., Restore... and the game combos too. The controller menu was the only custom one. | | Nothing else to change. | - || - |
| L6 | - | DPI 100-200 %, resize, full screen | Every size is in points and the minimum window scales with the UI (`UpdateUiScale`), so the layouts are the same at every scale; the pages centre in a wide window (Settings 940, Collection 1180). | | Real displays: #3. | - || - |

## 7. Platform conventions

| ID | Sev | What | Status |
|---|---|---|---|
| X1 | - | macOS: SDL's menu gives Cmd+Q (Quit) and Cmd+W (Close), both reach the launcher as a quit request; D4 asks first during an install. Windows: Alt+F4 the same. | OK |
| X2 | P3 | macOS: the app menu's "Preferences..." (Cmd+,) is SDL's and does nothing. | Left (SDL owns the menu). |
| X3 | P3 | No key for the launcher's own full screen (F11 / Ctrl+Cmd+F): it is `[launcher] fullscreen` only. | Left. |

## 8. Performance

| ID | Sev | Where | What is wrong | Why it matters | Fix | Effort | Status |
|---|---|---|---|---|---|---|---|
| P1 | P1 | `App::Run` | The loop redraws as fast as it can (the display's refresh with VSync) even when nothing changes: **70 % of a core** idle offscreen (`private/idle.sh`). | Laptops heat and drain doing nothing. | Draw on input, then sleep: `SDL_WaitEventTimeout`, 0.5 s while idle (the portal, the collection and jobs are still polled), every frame only while something moves (a check, an install, an update, a held button, a text cursor), 30 fps for the stars. | M || Fixed (`066d4ff`): 71 % -> 3 % |
| P2 | P1 | Collection > Manage, Figure page | Every frame Manage re-reads and **decrypts every dump** (`UntouchedStarters` lists the collection) and parses `collection.json` twice (`Trash`); its Backups tab lists every figure's backups and reads each backup; the Figure page lists its backups, and its Restore menu reads every backup, every frame. | Disk work on the UI thread at 60-120 Hz; stutters on a big collection or a slow disk. | Read once per change (the page's generation) and cache. | S || Fixed (`bb739d1`) |
| P3 | P2 | `App::PollInstall` | The old installer's process is checked on frames whose clock falls in `% 1000 < 20` ms: with P1's idle loop it could never run. | Resume could stay disabled. | A timer. | S || Fixed (`066d4ff`) |
| P4 | P1 | `App::FinishVerify` | The disc's art and the figure portraits are decoded on the UI thread after the check: the window freezes **4.3 s** (measured, empty cache) at 98 %. | A frozen window reads as a crash. | The check's worker decodes them into its own `LauncherAssets::Provider` ("Reading the game's pictures"). | S | Fixed (`8ca1327`) || Fixed (`8ca1327`) |
| P6 | P2 | Collection, 300 figures | Every card was drawn every frame, scrolled out of view or not. | | Cards out of view keep their place and input, skip their drawing. | S | Fixed (`1ba1a25`) |
| B1 | P3 | Manage > Backups | Empty, the tab shows only its explanation. | | "No backups yet." | S | Fixed (`1ba1a25`) |
| P5 | P3 | File dialogs | The window isn't redrawn while a file dialog is open (the dialogs are waited for in a loop). | | Left. | M || Left |

## 9. Accessibility

| ID | Sev | What | Status |
|---|---|---|---|
| A1 | - | Contrast was measured on every text colour and surface before (docs/launcher_ui.md, Legibility): all at 4.5:1 or more. New this pass: the portal line on the band (TextDim on BottomBar) is 5.8:1; the hold buttons' text on their red fill is above 7:1. | OK |
| A2 | - | Sizes: 17 body, 14 small, 13 badges, 12 floor. | OK |

## 10. Robustness and the known issues

| ID | Sev | Where | What is wrong | Fix | Effort | Status |
|---|---|---|---|---|---|---|
| R1 | P1 | PLAY, INSTALL (#9 S-14) | A disc image moved or deleted after its check: INSTALL fails later with an exit code, PLAY starts the game on a missing file. | Check the file first; if it's gone, the disc state shows why and the button offers Choose disc. | S || Fixed (`6384adf`) |
| R2 | P1 | `UpdateUiScale` (#9 S-13) | `ui_scale=nan` gives a NaN scale (garbage layout, window size). | Not finite: automatic. | S || Fixed (`6f016c7`) |
| R3 | P2 | Edit stats (#9 S-11) | Save writes back the stats read when the editor opened: a game that played the figure meanwhile loses its play time and the rest. | Re-read on Save and apply only the fields that were changed. | S || Fixed (`635a277`) |
| R4 | P2 | Rename, Add a figure (#9 S-17) | A name of spaces hides the figure's name. | Trim; empty means the toy's name. | S || Fixed (`635a277`) |
| R5 | P2 | Edit stats (#9 S-16) | The nickname buffer (48 bytes) is shorter than 16 three-byte characters. | 64 bytes. The UTF-8 validation is the library's (SSARecomp). | S || Fixed (`635a277`) |
| R6 | - | #13 | Verified: deleting a figure goes back to the list on the next figure, one toast, no grey page (`30-after-delete`, `CollectionChecks`). | - | Fixed before this pass |
| R7 | P2 | #9 S-04 to S-10, S-12, S-18 to S-23 | Library or model findings (paths, locks, backups by name, atomic writes, export overwrite, imports). | Left: they belong to the collection library, which is SSARecomp's (`tools/sync_collection.sh`). | L || Left |
| R8 | P3 | #9 S-29 | A game kit update rebuilds the installed game without asking. | Left: a product choice for the owner. | - || Left |
| R9 | P3 | #9 S-15, S-26 | Experience and hero points take `+ - * / .` (clamped, harmless); `resolution_scale=999` shows Auto. | Left. | S || Left |

dorydev's design issues #1, #2, #5 and #6 are hers (the figure page's button weights, the seats' art, the trash's
names, the banner and dialogs' look, placeholder art, glyph hints); this pass fixes only their interaction bugs (H, N,
L above). #3 (real displays) needs hardware. #4 (French) comes next: the new and changed strings of this pass sit at
the top of their screen's file (`namespace strings`), ready for extraction.

## What is left, and why

- **N9** (P2): the update banner is its own window, out of the pad's reach; the mouse and keyboard reach it. A pad path
  means drawing the banner inside the page window (M), best done with #2, the banner's design.
- **F4** (P2): a disc check can't be cancelled; it takes seconds, and `CheckGameSource` has no cancel point.
- **R7** (P2): #9's library findings (S-04 to S-10, S-12, S-18 to S-23) are in SSARecomp's collection library, synced
  here by `tools/sync_collection.sh`: they are fixed there, then synced.
- **P3s**: C7 (the runtime= line is for people who build their own), X2 (SDL owns the macOS menu), X3 (the launcher's
  own full screen key), P5 (no redraw behind a file dialog), R8 (a product choice for the owner), R9 (harmless).

## Screenshots

`private/polish-shots/before/` and `private/polish-shots/after/` (main, collection and install passes): git-ignored.

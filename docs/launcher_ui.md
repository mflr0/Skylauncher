# Launcher UI Guide

The launcher is a Skylanders-flavoured take on the Minecraft Launcher. Keep the look modular so it
is easy to iterate without touching the game runtime.

## Structure
- `src/launcher/theme/launcher_theme.h` - every colour, size, radius and font size, plus the ImGui style (`Apply`). Change the look here first.
- `src/main.cpp` - picks the game and disc, shows the launcher, starts the runtime.
- `src/launcher/` - the launcher: `launcher.{h,cpp}` (disc checks, settings file), `launcher_games.cpp` (the game table),
  `game_install.{h,cpp}` (installing the game with the kit's SkyBuild, `docs/install.md`),
  `ui/`, `theme/`.
- `src/assets/` - the art read from the player's disc (`launcher_assets`).
- `src/launcher/game_settings.{h,cpp}`, `shortcut.{h,cpp}`, `ui/game_settings_page.cpp` - a game's own settings: its per-game video copy,
  delete data and uninstall (no UI), the desktop shortcut files (no UI, one builder per system), and the page (below).

- `src/assets/` - the game's art for the window (`launcher_assets`): a thin layer over `src/shared/art/` (see "Disc art").
- `src/launcher/ui/launcher_app.h` - the window's class (`App`) and helpers shared by the page files.
- `src/launcher/ui/launcher_ui.{h,cpp}` - the window (Dear ImGui on SDL_Renderer): backdrop, sidebar, the Home, Play and Settings
  pages (the launcher's Settings, the tab bar), the launcher's widgets (`BigButton`, `PillButton`, `Toggle`, `BeginCard`, `Chip`), disc verification and the screenshot test.
- `src/launcher/install_checklist.{h,cpp}`, `ui/install_page.cpp` - the install card (docs/install.md);
  `game_run.{h,cpp}`, `ui/support_page.cpp` - game logs, the crash dialog, the portal test (docs/support.md);
  `updater.{h,cpp}`, `ui/update_page.cpp` - updates, the banner and About (docs/updates.md);
  `shared/mini_json.{h,cpp}` - the JSON reader for GitHub's answer.
- `src/launcher/ui/collection_*.{h,cpp}` - the collection page on the shared library: the portal at the next start, the bar, the shelf, the figure sheet, Add a figure, Manage (`docs/collection_ui.md`).
- `src/launcher/ui/game_look.cpp` - the game page in each game's own look: the action button (`DrawMainAction`), the logo's
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

## Layout
The launcher opens on Home, the menu page (the ARKEYAN LAUNCHER title goes back to it): empty for now, with "Report a bug" (a new
GitHub issue) and the GitHub link bottom right. The launcher's own language is in Settings > Launcher > Language
(`docs/translations.md`); the game's text and voice languages are in that game's settings (Language tab). The game shown last is selected in the sidebar (`[launcher] game=`); the other is one click away.
1. Left sidebar (grey, fixed width) - the seven games as full-width rows (Spyro's Adventure, Giants and Swap Force enabled, the others "Coming later",
   SuperChargers Racing included), then COLLECTION and the Settings gear at the bottom. Each enabled row shows its game's icon
   and its live state ("Open" until the game is first shown, then the states in "A game's states"); the
   selected game's row has the bar while its Play page is open. Rows are reachable with the keyboard and the pad like the others.
2. Play (no tabs) - the game's logo from the player's disc over the drawn sky (the disc's background art is not shown); a light band under it holds the controller button
   (current device, PLAY WITH: a menu of the others), the main button (CHOOSE DISC, INSTALL, then PLAY) with the portal's
   line under it and, on the right, **Game settings** (a plain button for now; its style is the art direction's). PLAY and
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
in the sidebar (`[launcher] game=`); the other is one click away.
1. **Left sidebar** (grey, fixed width).
   - The title, **ARKEYAN LAUNCHER**, with the launcher's version beside it, small and dim (`v0.6.0`, from
     `LAUNCHER_VERSION`: `LauncherVersion()`), then "Native PC ports" and YOUR GAMES.
   - The seven games as full-width rows (Spyro's Adventure, Giants and Swap Force enabled, the others "Coming later", SuperChargers
     Racing included). An enabled game whose disc has been read (this session or an earlier one) shows **its logo**, the
     art with "Skylanders" on it, in the row's left column (`GameLogoW` x `GameLogoH`); before that, and for the games
     coming later (no disc, so never any game art), a neutral badge: the initial on the game's colour.
   - The name (shrunk to fit), then the game's state with a dot ("A game's states", below). The selected game's row has
     the orange bar while its Play page is open.
   - COLLECTION and the Settings gear at the bottom, their icons in the logos' column and their labels in the names'.
   - Rows are reachable with the keyboard and the pad like the others.
2. **Play** (the game page, no tabs).
   - The backdrop is the game's own painting from the disc (SSA: the painted Skylands sky; Giants: the main menu's sky; Swap Force: the main menu's painted sky),
     cropped to fill the page and shaded (`DrawBackdrop`: darker at the top, light behind the logo, dark towards the band,
     a shadow along the sidebar) so the logo and white text read on any painting. Before any disc: the drawn night sky.
   - The logo, large and centred, with a soft shadow, **falls in** on each arrival (see "The logo's arrival"). Before the
     disc was ever read: the game's name.
   - Bottom left of the hero, the disc's lines: the image's file name, "DISC IMAGE:" and its state, the install note;
     "Change disc..." at the right of these lines once a disc is verified. (The small rounded app icon that sat here is
     gone: the owner's request, 2026-10-25.)
   - The band under it, one group centred in it: the controller button (current device; PLAY WITH: a menu of the others),
     then the **action button** in the game's colours ("Each game's look"): the main action (CHOOSE DISC, INSTALL, then
     PLAY) and the **game's settings** (a gear) in its right segment, with the portal's line under it. PLAY and INSTALL
     check that the image is still there first.
3. Collection - see below.
4. Settings (the gear) - Graphics, Controls (device, portal), Language, Game data, About. Clicking a game
   goes back to Play; clicking the gear again, or B / Escape, goes back to the page you came from. The game page's settings
   button opens it too, until each game has a settings page of its own (another branch).

## A game's states
One function decides: `App::SlotStatus(slot)` returns the game's `SlotState`, its line, its dot and the main button's label
and whether it can be pressed. The sidebar, Home's cards and the Play page's main button all read it, so they cannot
disagree; the page's other lines (the disc's status, the install card, the game settings' Disc tab) say more about the same
`DiscState` / install state. In this order (the first that applies):
| State | Sidebar and Home line | Dot | Main button |
|---|---|---|---|
| never shown this session | Open | blue | CHOOSE DISC |
| the kit being downloaded (the first step of an install or an update) | `Downloading tools 42 %` | blue | INSTALLING... (off) |
| installing; its last step | `Installing 42 %`; `Finishing 97 %` | blue | INSTALLING... (off) |
| updating a build; its last step | `Updating 42 %`; `Finishing 97 %` | blue | UPDATING... (off) |
| the disc being checked | Checking your disc | blue | CHECKING... (off) |
| no disc chosen | Needs your disc | blue | CHOOSE DISC |
| the image moved or its drive gone | Disc not found | red | CHOOSE DISC |
| the image can't be read | Can't read the disc | red | CHOOSE DISC |
| another game's disc | Wrong disc | red | CHOOSE DISC |
| this game, a release no build exists for | Unknown release | red | CHOOSE DISC |
| a folder, not an Xbox 360 game, damaged | Not a game disc | red | CHOOSE DISC |
| the last install failed (no build); the last update failed (a build) | Install failed; Update failed | red | INSTALL; UPDATE (or PLAY) |
| the install was interrupted | Interrupted | blue | INSTALL, or PLAY if built |
| the copied game data is damaged, and no disc image to fall back on | Game data damaged | red | CHOOSE DISC |
| a verified disc, no build yet | Ready to install | blue | INSTALL |
| the release has a newer version of the game than the one built (the release's games line), not set aside with "Play without updating" | Update available | blue | UPDATE |
| verified disc and built (or an own runtime) | Ready to play | green | PLAY |

**UPDATE** (`docs/updates.md`): the page says the version built and the one on offer ("Update 0.8.0 to 0.8.1: no download, up to
about 15 min.") and the main button reads UPDATE; one click fetches the kit if it is
needed, rebuilds the game (SkyBuild is incremental) and PLAY is back. Under that line, left-aligned with it, a small link,
**Play without updating**, plays what is built (offline, in a hurry): it sets the offer aside until the launcher is closed. The bottom
band holds only the portal line under the button, as for PLAY. The game's version is small, in the
secondary text colour, after its name in the sidebar ("Giants · 0.8.2"); nothing on the page moves for it. While UPDATE runs the progress card is titled
"UPDATING THE GAME" ("INSTALLING THE GAME" for a first install). No tools are ever downloaded without a click on INSTALL or UPDATE.

"Ready to play" needs both the build and a verified source: the disc image, **or the copied game data** ("Game data" below).
With no source or a refused one the game says what it needs, even if it was built (the game reads the disc image while it runs). The Play page's disc line says more
("Disc verified: 8.1 GB", the refusal's reason, "Drop your disc image here..."), the install card is shown while
`installView`, and the game settings' Disc tab shows the same `DiscState`. The window has one fixed title.

The percentage is the install card's overall fraction (`MakeInstallChecklist`), the same as the band's button. The text is
cut with "..." to the column (the sidebar is a fixed width at every window size) and the bar is drawn over the row's own
space, so nothing moves when the line changes. "Playing" is never shown: PLAY closes the launcher until the game ends.

`SKY_LAUNCHER_TEST_SIDEBAR=1` (with `SKY_LAUNCHER_SHOTS`, a `[launcher] kit=` that is the fake kit and both games' `iso=`;
`FAKE_SSABUILD_HOLD` keeps the build at 100 %) shoots and checks the line through an install and an update at 0, 50 and 100 %,
with the other game shown meanwhile, then a failed build: `ArkeyanLauncherTest` runs it (`SidebarChecks`).

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

**The action button** (`App::DrawMainAction(at, {label, enabled, progress})`, the owner's choice of 2026-10-25, "flat and
crisp"): one split button, `PlayButtonW` + `PlayGearW` by `PlayButtonH` (300 + 64 by 64 points).
- Flat: a light gradient between the game's two colours (20 % to 58 % of the way), `Radius` corners, no outline, a soft
  shadow, a hairline of light along the top. The label in Bungee, 40 % of the height (smaller if it would not fit), centred
  on its capitals and placed on whole pixels; its ink is white when that reads at 3:1 on the fill, else the game's dark
  ink (navy for SSA, brown for Giants, dark green for Swap Force), and it never changes under the pointer.
- The game's settings: a gear in the right segment, behind a dark and a light hairline; its tooltip "<game> settings".
- States, each half on its own: under the pointer 10 % lighter, pressed 10 % darker; **focused** (keyboard, pad): the
  focused half lit as under the pointer and one accent ring around the whole button (ImGui's own ring hidden there);
  **disabled**: greyed, a light faded label; **busy** (checking the disc, installing): a dark face tinted in the game's
  colour up to the progress, a 5-point bar in that colour along the bottom edge, the label ending in the percentage
  ("INSTALLING 42%").
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
- **Focus.** Every control takes the keyboard and pad focus and shows it: ImGui's buttons draw it, the widgets drawn by
  hand over an `InvisibleButton` call `App::FocusRing` (the accent ring) and pass `ImGuiButtonFlags_EnableNav` (ImGui
  1.91+ leaves invisible buttons out of navigation otherwise). The page's columns are `ImGuiChildFlags_NavFlattened`.
- **Pad and keyboard.** The sidebar and the page are separate windows: B / Escape on Play or Home moves the focus to the
  sidebar's current row; A on a row opens its page with the focus on its first control (`App::GoTo`, `TakeFocus`).
  B / Escape leave Settings, Game settings and Collection, close dialogs and menus, clear the collection's search; whatever is open
  answers it before the page does (`m_backTaken`). LB / RB change Settings and Game settings tab (`App::DrawTabBar`) and the seat the collection chooses for. The
  launcher starts with the focus in the sidebar.
- **Hold to confirm.** What deletes or overwrites is a red button held for a second (`App::HoldButton`, the state in
  `hold_confirm.h`): Reset and Delete (Figure page), Delete for good (trash), Mark as not in use (a stale lock), Restore
  (a save backup), Reset to defaults (a game's keyboard), Reinstall, Delete saves and data, Remove game. The fill shows the time; releasing, or the pointer
  leaving, cancels; it acts once per press; Space, Enter and the pad's A hold it too. The label is a plain verb, the
  instruction is the tooltip (shown on keyboard and pad focus too). Buttons are told apart by their ImGui ID.
- **Quitting.** The window's close, Cmd+Q or Alt+F4 during an install asks: Keep installing (default) or Quit.
- **Toasts.** Top right, stacked downwards (newest at the bottom, at most 4), each at most 420 points wide, 20 points from the window's top and right edges, over the page. 2.5 s, longer for a long message (up to 6 s); the same message again refreshes the one shown. A failure (`Toast(text, true)`, or a text starting "Couldn't") stays until it is closed, with a cross; a click on any message closes it and does not reach what is under it. Texts are short, sober, no full stop; they are English in the code and go through the string table (`docs/translations.md`). An error is a `Toast(text, true)` (a text starting "Couldn't" is one too, as the collection page's English toasts still are).
- **Idle.** The loop sleeps in `SDL_WaitEventTimeout` (`App::IdleWait`): every frame only while something moves (a disc
  check, an install, an update, a held button, a text cursor, the logo's fall) and for three frames after an input; 30 fps for the night
  sky's stars; else it wakes every 0.5 s to poll the jobs, the portal and the collection.
- **Copy.** New and changed sentences sit at the top of their file (`namespace strings`), ready for #4.

## Desktop shortcut
Game settings > Game > Desktop shortcut > **Create** (enabled once the disc is verified: the icon comes from it) puts a launcher for
that game on the Desktop. It starts Arkeyan Launcher as `ArkeyanLauncher --game <id> --play`: the window opens on that game's page, checks the
disc, and starts the game as soon as it is ready. If it can't (no disc, a failed check, the game not built yet) the window stays
open and says why, so the shortcut never leaves the player with nothing. Made again, it replaces the old one.
The icon is the game's title icon decoded from the player's disc (`GameSlot::meta.iconPng`, memory only until the player asks).

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
- Sizes at 1x: 17 body, 14 small (hints, status), `CaptionSize` 12 as the floor, titles 22 and up.
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
- Colours: edit the constants in `launcher_theme.h`; ImGui's own widget colours are set in `LauncherTheme::Apply`. Each
  game's own colours (its action button) are its `GameStyle` there.
- Spacing and sizes: `SidebarW`, `Pad`, `Radius*`, `PlayButtonW/H`, `PlayGearW` and the font sizes in the same file.
- Fonts: put an OFL- or Apache-licensed font in `resources/fonts/` and load it in `App::LoadFonts`
  (`SKY_LAUNCHER_FONT_DIR` overrides the folder while trying fonts).
- Widgets: add them next to `BigButton` and `Toggle` in `launcher_ui.cpp`, taking every colour from the theme.

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
`12-game-saves.png`, `13-game-data.png`, `14-launcher-settings.png` and `15-disc-gone.png` (INSTALL on an image that moved; `03-` is the
game's Video tab, `04-` the launcher's Controls) (it creates a few figures in `SKY_COLLECTION` first: set it to a scratch folder). With `SKY_LAUNCHER_TEST_COLLECTION=1` it shows the collection's screens instead (`docs/collection_ui.md`).

`07-figure-sheet.png`, `08-new-figure.png`, `09-play-with.png` (the controller menu), `10-language.png`, `11-disc-gone.png`
(INSTALL on an image that moved) and `12-logo-drop.png` (the logo early in its fall) (it creates a few figures in `SKY_COLLECTION` first: set it to a scratch folder). With `SKY_LAUNCHER_TEST_COLLECTION=1` it shows the collection's screens instead (`docs/collection_ui.md`).
With `SKY_LAUNCHER_TEST_NAV=1` it sends keys as a player would (`40-` to `53-`): B to the sidebar, A into Collection, Y for
the search, the D-pad to a seat, A to choose for it, down and A to put the first card there, X for a card's sheet, B back,
RB in Settings.
With `SKY_LAUNCHER_TEST_INSTALL=1` and `SKY_LAUNCHER_KIT=$PWD/build/fake-kit` it shows the install screens instead
(`docs/install.md`).

## Other screenshot variables
`SKY_LAUNCHER_TEST_KEYS=1` shoots each game's Keyboard tab (`keys-ssa`, `keys-giants`, `keys-swapforce`; `SKY_LAUNCHER_LANGUAGE=fr` for the French ones).
`SKY_LAUNCHER_TEST_LOGO=1` shoots each game's page (`logo-ssa`, `logo-giants`, `logo-swapforce`) from the art already cached in
`SKYLANDERS_DATA`; `SKY_LAUNCHER_TEST_SIZE=<w>x<h>` sets the window (the default one, a large one).
`SKY_LAUNCHER_TEST_UPDATE=1` with `SKY_LAUNCHER_UPDATE_URL=file://<fake latest.json>` shows the update screens (`20-` to `24-`).
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
  installing. The last game shown is written to `[launcher] game=` and opens at the next start. PLAY returns the shown game
  and its own build, and `main()` runs that game's runtime (SSARecomp or SGRecomp) with its language settings. The settings
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

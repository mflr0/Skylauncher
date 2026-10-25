# Launcher UI Guide

The launcher is a Skylanders-flavoured take on the Minecraft Launcher. Keep the look modular so it
is easy to iterate without touching the game runtime.

## Structure
- `src/launcher/theme/launcher_theme.h` - every colour, size, radius and font size, plus the ImGui style (`Apply`). Change the look here first.
- `src/main.cpp` - picks the game and disc, shows the launcher, starts the runtime.
- `src/launcher/` - the launcher: `launcher.{h,cpp}` (disc checks, settings file), `launcher_games.cpp` (the game table),
  `game_install.{h,cpp}` (installing the game with the kit's SSABuild, `docs/install.md`),
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
- `src/launcher/ui/collection_*.{h,cpp}` - the collection page on the shared library: seats, sections, View, Figure page, Add a figure, Manage (`docs/collection_ui.md`).
- `src/launcher/ui/game_look.cpp` - the game page in each game's own look: `GameButton`, `GameSettingsButton`, the logo's
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
The launcher opens on Home, the menu page (the SKYLAUNCHER title goes back to it): empty for now, with "Report a bug" (a new
GitHub issue) and the GitHub link bottom right. The top right is kept for the launcher's own language (#4): its slot is
Settings > Launcher > Language (English only, disabled, for now); the game's text and voice languages are in that game's
settings (Language tab). The game shown last is selected in the sidebar (`[launcher] game=`); the other is one click away.
1. Left sidebar (grey, fixed width) - the seven games as full-width rows (Spyro's Adventure and Giants enabled, the others "Coming later",
   SuperChargers Racing included), then COLLECTION and the Settings gear at the bottom. Each enabled row shows its game's icon
   and disc state ("Open" until the game is first shown, then "Checking your disc", "Needs your disc" or "Ready to play"); the
   selected game's row has the bar while its Play page is open. Rows are reachable with the keyboard and the pad like the others.
2. Play (no tabs) - the game's logo from the player's disc over the drawn sky (the disc's background art is not shown); a light band under it holds the controller button
   (current device, PLAY WITH: a menu of the others), the main button (CHOOSE DISC, INSTALL, then PLAY) with the portal's
   line under it and, on the right, **Game settings** (a plain button for now; its style is the art direction's). PLAY and
   INSTALL check that the image is still there first. Changing the disc moved to the game's settings (Disc tab); CHOOSE DISC
   stays the main button while there is no disc.
3. Collection - see below.
4. Settings (the gear) - **the launcher's own settings and what both games share**, in four tabs: Controls (device, portal, glyphs,
   keyboard bindings), Launcher (language slot, updates, data folder), Support (report a problem, logs), About (version, credits).
   Clicking a game goes back to Play; clicking the gear again, or B / Escape, goes back to the page you came from.
5. Game settings (the Settings button on a game's Play page) - **that game's own settings**, five tabs (the sidebar keeps the
   game selected; B / Escape or **Back** return to its Play page):
   - Video: fullscreen, resolution scale, frame rate limit (30 or 60), skip intro movies, VSync, texture filtering, widescreen,
     HD texture packs (and their folder), Advanced > dump textures. The game's own copy, `[<game>.video]` (docs/settings.md).
   - Language: game text and game voices (`[<game>] language`, `voice_language`).
   - Disc: the image's path and state; **Choose / Change disc...**, **Remove disc** (the launcher forgets it; the file is not
     touched), **Show in folder**. Disabled while a check or an install runs.
   - Saves: **Open saves folder** and the save backups with Restore (held, refused while the game runs).
   - Game: **Create a desktop shortcut**, the folders, the built game with **Reinstall the game** (held), **Clear caches**,
     **Delete saves and data** (held), **Uninstall the game** (held), each with the size it frees and what it keeps
     (docs/install.md, "Delete data, uninstall").

**Why the split.** The gear used to hold game things (disc image, graphics, game languages, saves) that looked like the
launcher's. Now the gear is only what is the launcher's or is shared, and anything about one game is on that game's page, so
the two games can differ (30 fps for one, 60 for the other, another disc, deleting one's saves). **The controller, the keys,
the button glyphs and the portal stay in the launcher's settings** because both games read the same `[input]` and `[portal]`
sections and the same hardware: a game page would only show the same values twice. If the games ever need different keys, the
Controls tab moves to the game pages the way Video did (`GameVideoKeys` shows how).

The launcher opens on **Home**, the menu page (the SKYLAUNCHER title goes back to it): the **library**, a card per
playable game (its painting from the disc behind its logo, its name and state; the game's colours before its disc was
ever read), then the games **coming later** as quiet chips in their colours; "Report a bug" (a new GitHub issue) and the
GitHub link bottom right. A card opens its game's page. The top right is kept for the launcher's own language (#4): the
game's text and voice languages are in Settings > Language ("Game text", "Game voices"). The game shown last is selected
in the sidebar (`[launcher] game=`); the other is one click away.
1. **Left sidebar** (grey, fixed width).
   - The title, **SKYLAUNCHER**, with the launcher's version beside it, small and dim (`v0.3.1`, from
     `SKYLAUNCHER_VERSION`: `LauncherVersion()`), then "Native PC ports" and YOUR GAMES.
   - The seven games as full-width rows (Spyro's Adventure and Giants enabled, the others "Coming later", SuperChargers
     Racing included). An enabled game whose disc has been read (this session or an earlier one) shows **its logo**, the
     art with "Skylanders" on it, in the row's left column (`GameLogoW` x `GameLogoH`); before that, and for the games
     coming later (no disc, so never any game art), a neutral badge: the initial on the game's colour.
   - The name (shrunk to fit), then the disc's state with a dot ("Open" until the game is first shown, then "Checking your
     disc", "Needs your disc" or "Ready to play"). The selected game's row has the orange bar while its Play page is open.
   - COLLECTION and the Settings gear at the bottom, their icons in the logos' column and their labels in the names'.
   - Rows are reachable with the keyboard and the pad like the others.
2. **Play** (the game page, no tabs).
   - The backdrop is the game's own painting from the disc (SSA: the painted Skylands sky; Giants: the main menu's sky),
     cropped to fill the page and shaded (`DrawBackdrop`: darker at the top, light behind the logo, dark towards the band,
     a shadow along the sidebar) so the logo and white text read on any painting. Before any disc: the drawn night sky.
   - The logo, large and centred, with a soft shadow, **falls in** on each arrival (see "The logo's arrival"). Before the
     disc was ever read: the game's name.
   - Bottom left of the hero, the disc's lines: the image's file name, "DISC IMAGE:" and its state, the install note;
     "Change disc..." at the right of these lines once a disc is verified. (The small rounded app icon that sat here is
     gone: the owner's request, 2026-10-25.)
   - The band under it: the controller button (current device; PLAY WITH: a menu of the others), then the **main button**
     (CHOOSE DISC, INSTALL, then PLAY) and the **game's settings button** (a gear) beside it, both in the game's own look
     ("Each game's look"), with the portal's line under them. PLAY and INSTALL check that the image is still there first.
3. Collection - see below.
4. Settings (the gear) - Graphics, Controls (device, portal, keyboard bindings), Language, Game data, About. Clicking a game
   goes back to Play; clicking the gear again, or B / Escape, goes back to the page you came from. The game page's settings
   button opens it too, until each game has a settings page of its own (another branch).

## Each game's look
What belongs to the game on its page is drawn in **the game's own art direction**, taken from its menus and logo
(`GameStyle` in `launcher_theme.h`, `StyleOf(id)`):

| Game | Look | Tokens |
|---|---|---|
| Spyro's Adventure | the logo's sky-blue-to-white letters in a navy outline: a soft stone bevel, a gradient from sky blue to deep blue, a light upper part, a darker lip | `SpyrosAdventureStyle` |
| Giants | the menus' glossy "gel" buttons (the yes/no buttons of `global.bld`): a bright top half, a hard split at the middle, a darker bottom half, a glint, a dark rim; in the logo's gold and orange | `GiantsStyle` |
| any other | the launcher's own blue | |

- `App::GameButton(label, size, style)`: the main button. A 3 px dark rim, the face (`gel` or bevel), the label in the
  title font as the logos letter it (white with a dark rim), a glow in the game's colour under the pointer, a 2 px sink
  when pressed, dimmed when disabled.
- `App::GameSettingsButton(id, size)`: **the game's settings entry**, a square button with a gear in the shown game's
  style (its tooltip: "<game> settings"). It is the one to call for the per-game settings page: it only draws and
  returns the click.
- Everything else (the sidebar, the band, Settings, Collection) keeps the launcher's neutral look: the games' styles
  never mix.
- A new game: add its `GameStyle` and a line in `StyleOf`.

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
  answers it before the page does (`m_backTaken`). LB / RB change Settings and Game settings tab (`App::DrawTabBar`) and the collection's section. The
  launcher starts with the focus in the sidebar.
- **Hold to confirm.** What deletes or overwrites is a red button held for a second (`App::HoldButton`, the state in
  `hold_confirm.h`): Reset and Delete (Figure page), Delete for good (trash), Mark as not in use (a stale lock), Restore
  (a save backup), Reset to defaults (keyboard), Reinstall the game, Delete saves and data, Uninstall the game. The fill shows the time; releasing, or the pointer
  leaving, cancels; it acts once per press; Space, Enter and the pad's A hold it too. The label is a plain verb, the
  instruction is the tooltip (shown on keyboard and pad focus too). Buttons are told apart by their ImGui ID.
- **Quitting.** The window's close, Cmd+Q or Alt+F4 during an install asks: Keep installing (default) or Quit.
- **Toasts.** One at a time, bottom right, at most 560 points wide; 2.5 s, longer for a long message (up to 6 s).
- **Idle.** The loop sleeps in `SDL_WaitEventTimeout` (`App::IdleWait`): every frame only while something moves (a disc
  check, an install, an update, a held button, a text cursor, the logo's fall) and for three frames after an input; 30 fps for the night
  sky's stars; else it wakes every 0.5 s to poll the jobs, the portal and the collection.
- **Copy.** New and changed sentences sit at the top of their file (`namespace strings`), ready for #4.

## Desktop shortcut
Game settings > Game > **Create a desktop shortcut** (enabled once the disc is verified: the icon comes from it) puts a launcher for
that game on the Desktop. It starts Skylauncher as `Skylauncher --game <id> --play`: the window opens on that game's page, checks the
disc, and starts the game as soon as it is ready. If it can't (no disc, a failed check, the game not built yet) the window stays
open and says why, so the shortcut never leaves the player with nothing. Made again, it replaces the old one.
The icon is the game's title icon decoded from the player's disc (`GameSlot::meta.iconPng`, memory only until the player asks).

| System | File on the Desktop | Icon |
| --- | --- | --- |
| macOS | `Skylanders Giants.app`: `Contents/MacOS/launch` (a two-line `sh` script: `exec '<Skylauncher>' --game giants --play`), `Info.plist` | `Contents/Resources/icon.icns`, the PNG as an `icp6` (64 px; the type follows the width) |
| Windows | `Skylanders Giants.lnk` (IShellLink: target Skylauncher.exe, arguments, working folder) | `<data>\Giants\shortcut.ico`, the PNG inside an .ico (Vista and later) |
| Linux | `skylanders-giants.desktop`, executable (`Exec="<Skylauncher>" --game giants --play`, `Categories=Game;`) | `<data>/Giants/shortcut-icon.png` (some desktops ask "Allow launching" once) |

`<Skylauncher>` is the running file (`SelfExecutable()`: the executable, or the `.AppImage` on Linux, not its temporary mount).
If Skylauncher is moved afterwards the shortcut needs making again. On macOS the first write to the Desktop may ask for permission
to access it. The Desktop is `SKYLAUNCHER_DESKTOP` when set (tests: nothing writes the real one), else the system's folder.
Windows is not built or run here: its `.ico` bytes and every name and argument are tested on every system; the `.lnk` itself is
the one standard COM call (`#ifdef _WIN32`, checked to compile with MinGW) and needs a look on a real PC.

## Command line
`Skylauncher [--game <id>] [--play] [--help]`: `--game ssa|giants` opens that game's page (else the game shown last),
`--play` starts it once its disc is checked and it is built. Anything else is ignored with a line on stderr.

## Collection
See `docs/collection_ui.md`: the page shows SSARecomp's shared collection (the game's folder, locks, favourites, backups
and six-game catalogue) with the Toy Box's organisation: the seats of the next session, My Skylanders in sections, the
View menu, the Figure page, Add a figure and Manage. The icons are the shown game's portraits from its disc, else the other
game's, else the generated ones (`figures::IconStyle`): "Disc art" below.

## Disc art
The launcher never ships Skylanders artwork. After a disc is verified, the check's worker thread calls the shared
provider (`src/shared/art/game_art.h`, the same code as the game's Toy Box; SSARecomp's `docs/runtime/host/game_art.md`):
`LauncherAssets::Provider::ExtractFromDisc` = `art::Extract(game, disc, titleIcon)`. One flow for every game; what
differs per game is the provider's table.

| Role | Spyro's Adventure | Giants | File |
| --- | --- | --- | --- |
| Logo (game page, sidebar, Home) | `skylanderslogo` in `uiimage.str` (1024x512, DXT5) | composed from the title's sprite sheet (`level/Title.bld`, 987x424) | `<data>/<Game>/launcher-cache/logo.png` |
| Background (game page, Home) | `fullscreen_000` in `uiimage.str` (512x512, the painted sky) | `ui_levelfullscreen/mainmenu.png` in `permanent/global.arc` (1600x960) | `.../background.png` |
| Title icon | `default.xex` XDBF resource | the same | `.../title_icon.png` |
| Toy portraits (collection) | `toy_<name>_hud` (128 px), `<name>_vs` (512 px) of `uigameimage.str`: 78 | the collection screen's 123 portraits (256 px) | `<data>/<Game>/portraits/` |

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
- The toys' pictures follow one rule (`collection::PortraitPath`): the shown game's, else the other game's, else the
  generated icon (see `docs/collection_ui.md`).

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

## Size on high-DPI screens
Every size is in launcher points. `App::UpdateUiScale` picks how many window units a point is: `[launcher] ui_scale`, else
the display's scale beyond its pixel density (Windows at 200 %: 2; a Retina Mac: 2 / 2 = 1, its window units are
already points), capped so the smallest layout fits the display's usable area. ImGui gets the window size divided by
the scale and a framebuffer scale multiplied by it, so it draws and rasterizes the fonts at the screen's real density.
It follows `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED`; Ctrl (Cmd) + / - / 0 zoom and reset, saved in `ui_scale`.
The Windows executable declares per-monitor v2 DPI awareness in its manifest (`resources/windows/`).

## How to change the look
- Colours: edit the constants in `launcher_theme.h`; ImGui's own widget colours are set in `LauncherTheme::Apply`. Each
  game's own look (its main and settings buttons) is its `GameStyle` there.
- Spacing and sizes: `SidebarW`, `Pad`, `Radius*`, `PlayButtonW/H` and the font sizes in the same file.
- Fonts: put an OFL- or Apache-licensed font in `resources/fonts/` and load it in `App::LoadFonts`
  (`SSA_LAUNCHER_FONT_DIR` overrides the folder while trying fonts).
- Widgets: add them next to `BigButton` and `Toggle` in `launcher_ui.cpp`, taking every colour from the theme.

## Screenshot test
The launcher can drive itself without opening a visible window:
```sh
SDL_VIDEO_DRIVER=offscreen \
CFFIXED_USER_HOME="$PWD/private/launcher-home" \
HOME="$PWD/private/launcher-home" \
SSA_LAUNCHER_INI="$PWD/private/launcher-home/launcher.ini" \
SSA_LAUNCHER_SHOTS="$PWD/private/launcher-shots" \
SSA_LAUNCHER_TEST_DROP="/path/to/game.iso" \
SSA_LAUNCHER_FONT_DIR="$PWD/resources/fonts" \
./build/Skylauncher.app/Contents/MacOS/Skylauncher
```
It writes `01-no-iso.png`, `02-verified.png`, `03-graphics.png`, `04-controls.png`, `05-collection.png`, `06-home.png`,
`07-figure-sheet.png`, `08-new-figure.png`, `09-play-with.png` (the controller menu), `10-language.png`, `11-game-disc.png`,
`12-game-saves.png`, `13-game-data.png`, `14-launcher-settings.png` and `15-disc-gone.png` (INSTALL on an image that moved; `03-` is the
game's Video tab, `04-` the launcher's Controls) (it creates a few figures in `SSA_COLLECTION` first: set it to a scratch folder). With `SSA_LAUNCHER_TEST_COLLECTION=1` it shows the collection's screens instead (`docs/collection_ui.md`).

`07-figure-sheet.png`, `08-new-figure.png`, `09-play-with.png` (the controller menu), `10-language.png`, `11-disc-gone.png`
(INSTALL on an image that moved) and `12-logo-drop.png` (the logo early in its fall) (it creates a few figures in `SSA_COLLECTION` first: set it to a scratch folder). With `SSA_LAUNCHER_TEST_COLLECTION=1` it shows the collection's screens instead (`docs/collection_ui.md`).
With `SSA_LAUNCHER_TEST_NAV=1` it sends keys as a player would (`40-` to `52-`): B to the sidebar, A into Collection, Y for
View, the D-pad to a seat and a card, X for its Figure page, Space held on Delete, B back, RB in Settings.
With `SSA_LAUNCHER_TEST_INSTALL=1` and `SKYLAUNCHER_KIT=$PWD/build/fake-kit` it shows the install screens instead
(`docs/install.md`).

## Other screenshot variables
`SSA_LAUNCHER_TEST_UPDATE=1` with `SKYLAUNCHER_UPDATE_URL=file://<fake latest.json>` shows the update screens (`20-` to `24-`).
`SSA_LAUNCHER_TEST_INSTALL=real` runs the real kit. `private/shoot.sh <out dir> [VAR=value...]` is the wrapper used in this
repository's working copy (its own HOME, the offscreen driver).

## Notes
- Drag & drop ISO is supported through `SDL_EVENT_DROP_FILE`.
- Verification progress is saved to `launcher.ini`; the SSA title/icon come from the player's own XEX.
- The game catalog lives in `LauncherGameCatalog()`; SSA and Giants are enabled.
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
- `SSA_LAUNCHER_TEST_SWITCH=1` (with `SSA_LAUNCHER_SHOTS`, a `[launcher] kit=` that is the fake kit, and both games' `iso=` in
  the config) runs the switch script: SSA and Giants checked, an install of each with the fake SSABuild (SSA shown while
  Giants builds), a switch back and forth with each state kept, and PLAY on each returning its game and build. It is run by
  `SkylauncherTest` when `SKYLAUNCHER_TEST_GIANTS_ISO` is set.
- `SSA_LAUNCHER_TEST_SEATX=1` (with `SSA_LAUNCHER_SHOTS`, `SSA_COLLECTION`, `SSA_SAVE_DIR` on scratch paths) runs the seat X script
  (`App::TestSeatXScript`): the Collection page's four seats (player 1, player 2, item, adventure) each emptied by the mouse on the X,
  by Delete and by the pad's X, checking after every try that only that seat is empty. It is run by `SkylauncherTest`.
- `SkylauncherTest` checks verification, settings (`tests/settings_test.cpp`: per-game video, the command line, the shortcut files,
  delete data and uninstall on scratch folders), font/text helpers, the disc texture decoder, the seat X and the in-place game switch
  (build it with `--target SkylauncherTest FakeSSABuild`; set `SKYLAUNCHER_TEST_GIANTS_ISO` for the switch).
- `launcher: ui scale` is logged once, then only when the scale changes (a display change, Ctrl +/-/0), not at every window event.

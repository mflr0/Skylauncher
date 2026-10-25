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
- `src/launcher/ui/launcher_app.h` - the window's class (`App`) and helpers shared by the page files.
- `src/launcher/ui/launcher_ui.{h,cpp}` - the window (Dear ImGui on SDL_Renderer): backdrop, sidebar, the Home, Play and Settings
  pages, the launcher's widgets (`BigButton`, `PillButton`, `Toggle`, `BeginCard`, `Chip`), disc verification and the screenshot test.
- `src/launcher/install_checklist.{h,cpp}`, `ui/install_page.cpp` - the install card (docs/install.md);
  `game_run.{h,cpp}`, `ui/support_page.cpp` - game logs, the crash dialog, the portal test (docs/support.md);
  `updater.{h,cpp}`, `ui/update_page.cpp` - updates, the banner and About (docs/updates.md);
  `shared/mini_json.{h,cpp}` - the JSON reader for GitHub's answer.
- `src/launcher/ui/collection_*.{h,cpp}` - the collection page on the shared library: seats, sections, View, Figure page, Add a figure, Manage (`docs/collection_ui.md`).
- `src/assets/launcher_assets.{h,cpp}` - reads frontend art from the player's disc, decodes it and caches it.
- `src/shared/` - what isn't the launcher's alone: the disc reader (`vfs`), the platform layer, `stb_impl.cpp`, the embedded fonts
  and the figure engine.
- `src/shared/collection/` and `src/shared/figures/` - SSARecomp's collection library (copied by `tools/sync_collection.sh`:
  tag, crypto, catalogue, folder, locks, backups) and the launcher's `figure_icons` (generated icons and their `IconStyle`).
- `lib/` - third-party code (SDL, Dear ImGui, XenonRecomp as submodules; stb and miniz vendored).
- `resources/fonts/` - licensed fonts (Bungee, Outfit) with their OFL licences; they are also built into the executable.
- `docs/settings.md` - the settings file the launcher writes and the game reads.

## Layout
The launcher opens on Home, the menu page (the SKYLAUNCHER title goes back to it): empty for now, with "Report a bug" (a new
GitHub issue) and the GitHub link bottom right. The top right is kept for the launcher's own language (#4): the game's text
and voice languages are in Settings > Language ("Game text", "Game voices"). No game is selected until the player clicks
one in the sidebar.
1. Left sidebar (grey, fixed width) - the seven games as full-width rows (Spyro's Adventure enabled, the others "Coming later",
   SuperChargers Racing included), then COLLECTION and the Settings gear at the bottom.
2. Play (no tabs) - the game's logo from the player's disc over the drawn sky (the disc's background art is not shown); a light band under it holds the controller button
   (current device, PLAY WITH: a menu of the others), the main button (CHOOSE DISC, INSTALL, then PLAY) with the portal's
   line under it and, on the right, "Change disc...". PLAY and INSTALL check that the image is still there first.
3. Collection - see below.
4. Settings (the gear) - Graphics, Controls (device, portal, keyboard bindings), Language, Game data, About. Clicking a game
   goes back to Play; clicking the gear again, or B / Escape, goes back to the page you came from.

## Interaction rules
- **Focus.** Every control takes the keyboard and pad focus and shows it: ImGui's buttons draw it, the widgets drawn by
  hand over an `InvisibleButton` call `App::FocusRing` (the accent ring) and pass `ImGuiButtonFlags_EnableNav` (ImGui
  1.91+ leaves invisible buttons out of navigation otherwise). The page's columns are `ImGuiChildFlags_NavFlattened`.
- **Pad and keyboard.** The sidebar and the page are separate windows: B / Escape on Play or Home moves the focus to the
  sidebar's current row; A on a row opens its page with the focus on its first control (`App::GoTo`, `TakeFocus`).
  B / Escape leave Settings and Collection, close dialogs and menus, clear the collection's search; whatever is open
  answers it before the page does (`m_backTaken`). LB / RB change Settings tab and the collection's section. The
  launcher starts with the focus in the sidebar.
- **Hold to confirm.** What deletes or overwrites is a red button held for a second (`App::HoldButton`, the state in
  `hold_confirm.h`): Reset and Delete (Figure page), Delete for good (trash), Mark as not in use (a stale lock), Restore
  (a save backup), Reset to defaults (keyboard), Reinstall the game. The fill shows the time; releasing, or the pointer
  leaving, cancels; it acts once per press; Space, Enter and the pad's A hold it too. The label is a plain verb, the
  instruction is the tooltip (shown on keyboard and pad focus too). Buttons are told apart by their ImGui ID.
- **Quitting.** The window's close, Cmd+Q or Alt+F4 during an install asks: Keep installing (default) or Quit.
- **Toasts.** One at a time, bottom right, at most 560 points wide; 2.5 s, longer for a long message (up to 6 s).
- **Idle.** The loop sleeps in `SDL_WaitEventTimeout` (`App::IdleWait`): every frame only while something moves (a disc
  check, an install, an update, a held button, a text cursor) and for three frames after an input; 30 fps for the night
  sky's stars; else it wakes every 0.5 s to poll the jobs, the portal and the collection.
- **Copy.** New and changed sentences sit at the top of their file (`namespace strings`), ready for #4.

## Collection
See `docs/collection_ui.md`: the page shows SSARecomp's shared collection (the game's folder, locks, favourites, backups
and six-game catalogue) with the Toy Box's organisation: the seats of the next session, My Skylanders in sections, the
View menu, the Figure page, Add a figure and Manage. The icons are the disc's portraits when cached, else the generated
ones (`figures::IconStyle`).

## Disc art
The launcher never ships Skylanders artwork. After an ISO is verified it:
1. reads the title icon from `default.xex` (XDBF image resource);
2. reads the STRM containers it needs (`ui/eng/startmenu.str`, `uiimage.str`), inflates the zlib stream at offset `0x14`;
3. finds APKF packages by their original asset path;
4. decodes the BC3/DXT5 payload at `APKF+0x1000` after undoing the Xbox 360 32x32-block tiling;
5. caches PNGs in `SDL_GetPrefPath("", "SSARecomp")/launcher-cache/<titleid>/`.

Current role mapping:
| Role | Disc asset | Size |
| --- | --- | --- |
| `logo.png` | `...\startmenu\skylanderslogo.dds` | 1024x512 |
| `hero_bg.png` | `fullscreen_000` in `uiimage.str` (the painted Skylands sky) | 512x512 (the Play page's backdrop, replacing the drawn sky and stars; the other pages use a plain gradient) |
| `title_icon.png` | `default.xex` XDBF resource | as stored |

`manifest.ini` in the cache records the title ID and where each role came from. The cached files are
local only and are ignored by git; deleting the cache is always safe.

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
- Colours: edit the constants in `launcher_theme.h`; ImGui's own widget colours are set in `LauncherTheme::Apply`.
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
`07-figure-sheet.png`, `08-new-figure.png`, `09-play-with.png` (the controller menu), `10-language.png` and `11-disc-gone.png`
(INSTALL on an image that moved) (it creates a few figures in `SSA_COLLECTION` first: set it to a scratch folder). With `SSA_LAUNCHER_TEST_COLLECTION=1` it shows the collection's screens instead (`docs/collection_ui.md`).
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
- The game catalog lives in `LauncherGameCatalog()`; only SSA is enabled for now.
- `SkylauncherTest` checks verification, settings, font/text helpers and the disc texture decoder.

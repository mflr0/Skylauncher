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
- `src/launcher/ui/collection_page.cpp` - the collection: grid, filters, figure sheet, figure creator, import, portal.
- `src/assets/launcher_assets.{h,cpp}` - reads frontend art from the player's disc, decodes it and caches it.
- `src/shared/` - what isn't the launcher's alone: the disc reader (`vfs`), the platform layer, `stb_impl.cpp`, the embedded fonts
  and the figure engine.
- `src/shared/figures/` - the figure engine: `figure_catalog` (every toy: game, category, variant, element, portraits),
  `figure_file` (the tag dump: header, encryption, progress), `figure_collection` (the folder: import, create, backups, locks),
  `figure_icons` (generated icons and their `IconStyle`).
- `lib/` - third-party code (SDL, Dear ImGui, XenonRecomp as submodules; stb and miniz vendored).
- `resources/fonts/` - licensed fonts (Bungee, Outfit) with their OFL licences; they are also built into the executable.
- `docs/settings.md` - the settings file the launcher writes and the game reads.

## Layout
The launcher opens on Home, the menu page (the SKYLAUNCHER title goes back to it): empty for now, with "Report a bug" (a new
GitHub issue) and the GitHub link bottom right. A language menu (not wired yet) sits top right on Home and Play. No game is
selected until the player clicks one in the sidebar.
1. Left sidebar (grey, fixed width) - the seven games as full-width rows (Spyro's Adventure enabled, the others "Coming later",
   SuperChargers Racing included), then COLLECTION and the Settings gear at the bottom.
2. Play (no tabs) - the game's logo from the player's disc over the drawn sky (the disc's background art is not shown); a light band under it holds the controller button
   (current device, a menu of the others), the main button (CHOOSE ISO, then PLAY once the disc is verified) and, on the right,
   the disc name with "Change disc...".
3. Collection - see below.
4. Settings (the gear) - Graphics, Controls (device, portal, keyboard bindings), Language, Game data, About. Clicking a game
   goes back to Play.

## Collection
- The grid: every figure file with its icon, name (the nickname when it has one), level and gold, and badges for the variant
  (Legendary, Dark...), favourite, "starts on the portal" and "in use" (a running game holds it: see docs/settings.md).
- Filters: category chips with counts (Skylanders, Giants, Swap Force, Trap Masters, Minis, Traps, Vehicles, Magic items,
  Adventure packs, Imaginators, Creation crystals), "Playable in" a game, favourites, and a search over names, nicknames
  and file names.
- The portal bar: virtual or real portal (`[portal] mode`) and the figures that start on it (`[portal] place`).
- The figure sheet (click a figure): its picture, what it is, the games it plays in, its progress read from the figure (level,
  experience, gold, nickname, hat, upgrades, play time), and rename, favourite, on the portal at start, back up, restore,
  reset and delete (with confirmation; a backup is kept before every change). Nothing can be changed while a game uses it.
- + New figure: a category, a figure, its variant; the figure engine writes a new, never played figure.
- Import... (or drop files on the window): .sky, .bin and .dmp dumps join the collection after a check.
- Icons: the disc's portraits (`toy_<name>_hud` and `<name>_vs` in `uigameimage.str`, cached in `launcher-cache/<titleid>/figures/`)
  once a disc is verified; otherwise a generated icon (shape by category, element colour, variant badge, initials). Its
  look is `figures::IconStyle`.
- Only Spyro's Adventure's toys are in the catalogue; the other games' categories are there, empty until their toys are added
  from a verified source. Later: give the game's Toy Box the same look, since it shows the same collection.

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
SSA_LAUNCHER_SHOTS="$PWD/private/launcher-shots" \
SSA_LAUNCHER_TEST_DROP="/path/to/game.iso" \
SSA_LAUNCHER_FONT_DIR="$PWD/resources/fonts" \
./build/Skylauncher.app/Contents/MacOS/Skylauncher
```
It writes `01-no-iso.png`, `02-verified.png`, `03-graphics.png`, `04-controls.png`, `05-collection.png`, `06-home.png`,
`07-figure-sheet.png` and `08-new-figure.png` (it creates a few figures in its own settings folder first).
With `SSA_LAUNCHER_TEST_INSTALL=1` and `SKYLAUNCHER_KIT=$PWD/build/fake-kit` it shows the install screens instead
(`docs/install.md`).

## Notes
- Drag & drop ISO is supported through `SDL_EVENT_DROP_FILE`.
- Verification progress is saved to `launcher.ini`; the SSA title/icon come from the player's own XEX.
- The game catalog lives in `LauncherGameCatalog()`; only SSA is enabled for now.
- `SkylauncherTest` checks verification, settings, font/text helpers and the disc texture decoder.

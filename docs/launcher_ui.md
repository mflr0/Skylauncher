# Launcher UI Guide

The launcher is a Skylanders-flavoured take on the Minecraft Launcher. Keep the look modular so it
is easy to iterate without touching the game runtime.

## Structure
- `src/launcher_theme.h` - every colour, size, radius and font size, plus the ImGui style (`Apply`). Change the look here first.
- `src/launcher_ui.{h,cpp}` - the window (Dear ImGui on SDL_Renderer): backdrop, sidebar, the Play and Settings pages
  (My Skylanders is a Settings tab), the launcher's widgets (`BigButton`, `PillButton`, `Toggle`, `BeginCard`), disc verification and the
  screenshot test.
- `src/launcher_assets.{h,cpp}` - reads frontend art from the player's disc, decodes it and caches it.
- `src/figures/figure_catalog.{h,cpp}` - every toy the game knows: name, element, portrait name.
- `resources/fonts/` - licensed fonts (Bungee, Outfit) with their OFL licences; they are also built into the executable.
- `docs/settings.md` - the settings file the launcher writes and the game reads.

## Layout
The launcher opens on Home, the menu page (the SKYLAUNCHER title goes back to it): empty for now.
A language menu (not wired yet) sits top right on every page but Settings. No game is selected until
the player clicks one in the sidebar.
1. Left sidebar (grey, fixed width) - the six games as full-width rows (Spyro's Adventure enabled, the others "Coming later"), and the Settings gear at the bottom.
2. Play (no tabs) - the game's logo from the player's disc over the drawn sky (the disc's background art is not shown); a light band under it holds the controller button
   (current device, a menu of the others), the main button (CHOOSE ISO, then PLAY once the disc is verified) and, on the right,
   the disc name with "Change disc...".
3. Settings (the gear) - Graphics, Controls (device, portal, keyboard bindings), Language, My Skylanders (figures from the
   figures folder; click one to start with it on the portal), Game data, About. Clicking a game goes back to Play.

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
| `hero_bg.png` | `fullscreen_000` in `uiimage.str` (the painted Skylands sky) | 512x512 (the window's backdrop, replacing the drawn sky and stars) |
| `title_icon.png` | `default.xex` XDBF resource | as stored |

`manifest.ini` in the cache records the title ID and where each role came from. The cached files are
local only and are ignored by git; deleting the cache is always safe.

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
It writes `01-no-iso.png`, `02-verified.png`, `03-graphics.png`, `04-controls.png`, `05-skylanders.png` and `06-home.png`.

## Notes
- Drag & drop ISO is supported through `SDL_EVENT_DROP_FILE`.
- Verification progress is saved to `launcher.ini`; the SSA title/icon come from the player's own XEX.
- The game catalog lives in `LauncherGameCatalog()`; only SSA is enabled for now.
- `SkylauncherTest` checks verification, settings, font/text helpers and the disc texture decoder.

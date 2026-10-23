# Launcher UI Guide

The launcher is a Skylanders-flavoured take on the Minecraft Launcher. Keep the look modular so it
is easy to iterate without touching the game runtime.

## Structure
- `src/launcher_theme.h` - every colour, size, radius and font setting. Change the look here first.
- `src/widgets/launcher_widgets.{h,cpp}` - reusable buttons, sidebar entries, panels and progress bars.
- `src/launcher_ui.{h,cpp}` - layout, screens, input, verification flow and screenshot test.
- `src/launcher_ui_draw.{h,cpp}` - SDL drawing primitives, font atlas, PNG loading and background.
- `src/launcher_assets.{h,cpp}` - reads frontend art from the player's disc, decodes it and caches it.
- `resources/fonts/` - licensed fonts (Bungee, Outfit) with their OFL licences.

## Layout (Minecraft-like)
1. Left sidebar - game list (SSA enabled, five greyed "coming later"), Settings at the bottom.
2. Hero area - selected game title, icon from the XDBF, real disc art after verification, drop zone.
3. Bottom bar - large PLAY button, ISO status and change-image action.
4. Settings overlay - text/voice language, fullscreen, resolution scale, game data and quit.

## Disc art
The launcher never ships Skylanders artwork. After an ISO is verified it:
1. reads the title icon from `default.xex` (XDBF image resource);
2. reads `ui/eng/startmenu.str`, inflates the zlib stream at offset `0x14`;
3. finds APKF packages by their original asset path;
4. decodes the BC3/DXT5 payload at `APKF+0x1000` after undoing the Xbox 360 32x32-block tiling;
5. caches PNGs in `SDL_GetPrefPath("", "SSARecomp")/launcher-cache/<titleid>/`.

Current role mapping:
| Role | Disc asset | Size |
| --- | --- | --- |
| `logo.png` | `...\startmenu\skylanderslogo.dds` | 1024x512 |
| `hero_bg.png` | `...\pausemenu\godraysdark.dds` | 512x512 |
| `title_icon.png` | `default.xex` XDBF resource | as stored |

`manifest.ini` in the cache records the title ID and where each role came from. The cached files are
local only and are ignored by git; deleting the cache is always safe.

## How to change the look
- Colours: edit the `LauncherTheme` namespace in `launcher_theme.h`.
- Spacing/sizes: change constants such as `SidebarW`, `BottomBarH`, `BtnPlayH`, `Radius*`.
- Fonts: add an OFL/Apache-compatible font under `resources/fonts/`, then update
  `SSA_LAUNCHER_FONT_DIR` / the font loading in `launcher_ui_draw.cpp`.
- Widgets: add methods to `LauncherWidgets`; keep all colours coming from the theme.

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
It writes `01-no-iso.png`, `02-verified.png` and `03-settings.png` at 1280x800.

## Notes
- Drag & drop ISO is supported through `SDL_EVENT_DROP_FILE`.
- Verification progress is saved to `launcher.ini`; the SSA title/icon come from the player's own XEX.
- The game catalog lives in `LauncherGameCatalog()`; only SSA is enabled for now.
- `SkylauncherTest` checks verification, settings, font/text helpers and the disc texture decoder.

# Skylauncher

A launcher for the native PC port of *Skylanders: Spyro's Adventure* (the SSARecomp runtime). Drop your own disc
image on the window, it gets checked, and the PLAY button starts the game with it. The layout follows the Minecraft
Launcher; the style is Skylanders-inspired.

Skylauncher contains no game code and no game art. Everything that comes from the game (title, icon, logo,
backgrounds) is read at run time from the disc image you provide, then cached on your machine only.

## What it does

- A sidebar with the console Skylanders games (and SuperChargers Racing). Spyro's Adventure is the only one enabled for now.
- Drag and drop of an `.iso`, or a Browse button. The image is checked: Xbox 360 file system, `default.xex`
  present and readable, and a release the runtime supports (SHA-256 of `default.xex`).
- Once the disc is verified, the game's own name, icon and logo are shown.
- Settings: graphics (fullscreen, resolution scale x1 to x10, frame rate limit, VSync, texture filtering), controls
  (which gamepad or the keyboard, every key remappable, Portal of Power type), text and voice language, game data.
- Collection: your figures with their portraits from the game, filters and search, each figure's stats, rename, backups,
  reset, new figures, import of your own dumps, and which figures start on the portal.
- The game runtime reads the same settings file; `docs/settings.md` lists every key and which ones it applies yet.
- The choice is saved in `launcher.ini`, shared with the game runtime. Its location is the SDL preferences folder
  of the app `SSARecomp`: `~/Library/Application Support/SSARecomp/` on macOS, `%APPDATA%\SSARecomp\` on Windows,
  `~/.local/share/SSARecomp/` on Linux.

## Download

Every push to `main` is built by GitHub Actions and published on the
[Releases page](https://github.com/mflr0/Skylauncher/releases):

- Windows: `Skylauncher.exe`, a single file.
- Linux: `Skylauncher-x86_64.AppImage`. Make it executable (`chmod +x`) and run it.
- macOS: `Skylauncher-macOS.dmg` (drag the app to Applications) or `Skylauncher-macOS.zip` (the `.app` itself),
  for Apple Silicon and Intel Macs. The app is not notarized: the first time, right-click it and choose Open, or
  allow it in System Settings > Privacy & Security.

## Build

Requirements: CMake 3.20+, Ninja (or another generator), a C++20 compiler, git.

```sh
git clone --recursive https://github.com/mflr0/Skylauncher.git
cd Skylauncher
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build Skylauncher
```

If you cloned without `--recursive`, run `git submodule update --init --recursive` first.
Tested on macOS (Apple Silicon). Windows and Linux builds are expected to work but have not been tested yet.

## Run

The launcher starts the game runtime, which is a separate program. It looks for it in this order:

1. the `SKYLAUNCHER_RUNTIME` environment variable;
2. `runtime=` in the `[launcher]` section of `launcher.ini`;
3. `SSARecomp` (`SSARecomp.exe` on Windows, or `SSARecomp.app` on macOS) next to the launcher.

If none is found, the launcher tells you where to set the path.

## Changing the look

The interface is drawn with Dear ImGui. All colours, sizes and fonts are in `src/launcher/theme/launcher_theme.h`; the screens and the
launcher's own widgets (big buttons, toggles, cards) are in `src/launcher/ui/launcher_ui.cpp`.
`docs/launcher_ui.md` explains the structure, how disc art is extracted and mapped to roles, and how to run the
screenshot test, which renders every screen to PNG files without opening a window.

## Tests

`SkylauncherTest` runs the disc checks, settings, text helpers and texture decoder against your own image:

```sh
ninja -C build SkylauncherTest
./build/SkylauncherTest /path/to/your/game.iso /tmp/skylauncher-test
```

## Licence

MIT, see `LICENSE`. Third-party components and their licences are listed in `THIRD_PARTY_NOTICES.md`.
Skylanders is a trademark of Activision. This project is not affiliated with or endorsed by Activision; you need
your own copy of the game.

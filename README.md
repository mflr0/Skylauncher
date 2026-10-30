# Arkeyan Launcher

A launcher for the native PC ports of *Skylanders: Spyro's Adventure*, *Skylanders: Giants* and *Skylanders: Swap Force*. You give it your own disc
image, it builds the game on your computer, and you play. (Formerly Skylauncher: it updates itself to this version.)

Arkeyan Launcher contains no game code and no game art. Everything shown from the game is read from your own disc and stays
on your computer.

| Game | Status |
|---|---|
| Spyro's Adventure | ![code recompiled: 100% (31k functions)](https://img.shields.io/badge/code%20recompiled-100%25%20%2831k%20functions%29-blue) ![Mac: playable](https://img.shields.io/badge/Mac-playable-brightgreen) ![Windows: playable](https://img.shields.io/badge/Windows-playable-brightgreen) ![Linux: container only](https://img.shields.io/badge/Linux-container%20only-yellow) |
| Giants | ![code recompiled: 100% (23k functions)](https://img.shields.io/badge/code%20recompiled-100%25%20%2823k%20functions%29-blue) ![Mac: playable](https://img.shields.io/badge/Mac-playable-brightgreen) ![Windows: Wine only](https://img.shields.io/badge/Windows-Wine%20only-yellow) ![Linux: container only](https://img.shields.io/badge/Linux-container%20only-yellow) |
| Swap Force | ![code recompiled: 100% (81k functions)](https://img.shields.io/badge/code%20recompiled-100%25%20%2881k%20functions%29-blue) ![Mac: playable](https://img.shields.io/badge/Mac-playable-brightgreen) ![Windows: Wine only](https://img.shields.io/badge/Windows-Wine%20only-yellow) ![Linux: container only](https://img.shields.io/badge/Linux-container%20only-yellow) |

## The games

| Game | Mac | Windows | Linux | Played so far |
|---|---|---|---|---|
| Spyro's Adventure | ✅ | ✅ | ⚠️ tested in a container only | The story and the Heroic Challenges; a complete playthrough is not confirmed yet |
| Giants | ✅ | ⚠️ tested in Wine only | ⚠️ tested in a container only | Chapter 1, and 78 of its 91 levels in an automatic tour (the other 13 are challenge levels the tour cannot start) |
| Swap Force | ✅ new | ⚠️ tested in Wine only | ⚠️ tested in a container only | The tutorial, the first area of the story and the two-player arenas |

Finished a game, or played one on Windows or Linux? Please say so in an
[issue](https://github.com/mflr0/Skylauncher/issues): it is how this table gets filled.

**Testers wanted.** Above all on a real Windows or Linux PC (and the Steam Deck), and with a real Portal of Power. Play a
game as far as you like and tell us in an issue what worked and what did not (Settings > Support > Copy game log helps).

## What you need

- Your own copy of *Skylanders: Spyro's Adventure*, *Skylanders: Giants* or *Skylanders: Swap Force* for Xbox 360, as a disc image (`.iso`).
- Windows 10 or 11 (64-bit), a Mac with Apple Silicon, or a Linux PC (below).
- Apple Silicon Mac required (M1 or later).
- An internet connection the first time: INSTALL downloads the game tools (about 30 to 40 MB) and the compiler.
- **Linux:** an x86_64 PC with a Vulkan driver (Mesa or NVIDIA), about 6 GB free, and a distribution from 2022 or later
  (glibc 2.34+: Ubuntu 22.04+, Fedora, Arch; SteamOS on the Steam Deck should work but has not been tested yet). No compiler or root is needed: INSTALL
  downloads zig (55 MB) as the compiler. A real portal needs a udev rule once, the only step that needs `sudo` (on
  the Steam Deck, set a password with `passwd` first):

  ```sh
  echo 'SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1430", ATTRS{idProduct}=="0150", MODE="0660", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/70-skylanders-portal.rules
  sudo udevadm control --reload-rules && sudo udevadm trigger
  ```

  Then unplug the portal and plug it in again.

## Supported discs

The launcher checks your disc's `default.xex`, so only these editions work (names as in the Redump database):

| Game | Disc |
|---|---|
| Spyro's Adventure | Skylanders - Spyro's Adventure (USA, Europe) (En,Fr,De,Es,It,Nl,Sv,No,Da,Fi) |
| Giants | Skylanders - Giants (USA, Europe) (En,Fr,De,Es,It,Nl,Pt,Sv,No,Da,Fi) |
| Swap Force | Skylanders - Swap Force (USA, Australia) (En,Fr), or Skylanders - Swap Force (Europe) (Es,It) |

Another edition is refused with a message. If you have one, tell us in an issue.

## Get started

1. Download Arkeyan Launcher from the [Releases page](https://github.com/mflr0/Skylauncher/releases):
   - **Windows:** `ArkeyanLauncher.exe`. If SmartScreen warns you, choose More info, then Run anyway.
   - **Mac:** `ArkeyanLauncher-macOS.dmg`. The app is not notarized: the first time, macOS blocks it; go to System Settings > Privacy & Security and choose Open Anyway (macOS 14 and earlier: right-click the app and choose Open).
   - **Linux:** `ArkeyanLauncher-x86_64.AppImage`, after `chmod +x`.
2. Drop your `.iso` on the window, or click the button to choose it. The disc is checked.
3. Click **INSTALL**. The game is built from your disc. It takes 10 to 20 minutes depending on the game, and happens
   once.
4. Click **PLAY**.

## Controls

Any gamepad works: Xbox, PlayStation, Switch and others. You can also play on the keyboard, and change every key in
Settings > Controls.

The launcher shows live whether a real Portal of Power is plugged in ("Portal: <model>" or "Portal: not detected"), next
to PLAY and on the Collection page.

| Action | Gamepad | Keyboard |
|---|---|---|
| Move | Left stick | W A S D |
| Camera | Right stick | I J K L |
| A | A | Space |
| B | B | X |
| X | X | C |
| Y | Y | V |
| Pause | Start | Enter |
| Back | Back / View | Backspace (Escape also goes back in menus) |
| LB / RB | LB / RB | Q / E |
| LT / RT | LT / RT | 1 / 3 |
| D-pad | D-pad | Arrow keys |

On-screen buttons can show Xbox 360, Xbox One / Series, PlayStation, Nintendo or keyboard glyphs (Settings > Controls).

### Figures and the Toy Box

You don't need a real Portal of Power. The game has a virtual portal, and your figures live in the **Toy Box**.

| | Gamepad | Keyboard / mouse |
|---|---|---|
| Open the Toy Box | View + Y | F6, or tap Tab |
| Quick swap wheel | Hold View | Hold Tab |
| Choose / back | A / B | Enter / Escape |
| Options of a figure | X | Right click |
| View menu (sort, group) | Y | F3 |
| Previous / next section | LB / RB | Page Up / Page Down |
| Take a figure off a seat | X, then Take off | Delete |

The **Collection** page of the launcher shows the same figures: their stats, backups, the trash, and the seats for the
next game. You can also create figures from the catalogue there.

To use a real portal (Wii, PlayStation, Xbox 360 or Xbox One), plug it in and choose it in Settings > Controls >
Portal of Power. Then click **Test the portal**.

## Settings

- **Resolution scale:** Auto picks x1 on integrated graphics and x2 on dedicated cards. If the game is slow, use x1.
- **Frame rate:** 30 is the original. 60 is experimental.
- The launcher and the games share one settings file, `launcher.ini`.
- **Language:** the launcher is available in English and French (Settings, Launcher tab).
- **Your saves are safe:** each save is written in one go, and the previous version is kept before it is replaced. To go
  back to one, open the game's settings (the gear next to PLAY) > Saves > Restore.

## If something goes wrong

- Settings (the gear) > Support > **Copy game log** copies the log so you can send it to us. **Open logs folder** shows the
  files.
- Everything is in one "Skylanders" folder: `%APPDATA%\Skylanders\` on Windows, `~/Library/Application Support/Skylanders/`
  on Mac, `~/.local/share/Skylanders/` on Linux. The logs are in its `logs/` folder and in each game's folder.
- If the game stops with an error, a window offers Copy log and Report.
- Updates (Arkeyan Launcher's and each game's) are looked for at every start; an offline start says nothing. Every download is checked against the release's SHA-256.

## Build from source

You need CMake 3.20 or later, Ninja, a C++20 compiler (Clang) and git.

```sh
git clone --recursive https://github.com/mflr0/Skylauncher.git
cd Skylauncher
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build ArkeyanLauncher
```

The developer docs are in `docs/`: `launcher_ui.md` covers the screens and theme, `install.md` the install,
`collection_ui.md` the figures, `updates.md` the updater and releases, and `support.md` the logs.

## Known issues

- On a real Windows PC only Spyro's Adventure has been played; the other games were run in Wine. On Linux every game was
  run in a container, none on a real PC yet.
- Swap Force is new: not every mode has been played through yet.
- The 60 fps option is experimental.
- The Mac app is not notarized, and Windows SmartScreen may warn about the launcher (see Get started).

## How it works

The ports are made by static recompilation: the game's PowerPC code is translated to C++ and its shaders to Metal,
Vulkan and Direct3D 12 shaders, on your computer, from your disc. A runtime written for these games replaces the Xbox 360
(memory, threads, files, graphics, audio, controllers, the Portal of Power).

These ports are the author's own, written from scratch with XenonRecomp and XenosRecomp and a runtime made for these
games. They are not a fork, do not use ReXGlue, and take no code from other Skylanders recompilation projects, even where
an executable has the same name as one of theirs (SSARecomp).

Every change is checked by automatic tests: each game is built from its disc, started and played for a few minutes by a
script (frame rate, sound, the picture compared with a reference, saves, figures), and scripted tours load the games'
levels one by one. Before a release, the Windows and Linux builds of every game are run too, in Wine and in a container.

The reverse engineering is the author's own work; AI tools assist with the reimplementation and run these tests.

## Credits and license

Made by mflr0, designed by dorydev.

The ports are built with [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) and
[XenosRecomp](https://github.com/hedge-dev/XenosRecomp) (hedge-dev), [plume](https://github.com/renderbag/plume)
(renderbag) and SDL 3, and adapt parts of [Xenia](https://github.com/xenia-project/xenia). Thank you to all of them.

MIT license, see `LICENSE`. The third-party components are listed in `THIRD_PARTY_NOTICES.md`.

Skylanders is a trademark of Activision. This project is not affiliated with or endorsed by Activision. You need your
own copy of the game.

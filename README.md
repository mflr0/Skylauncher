# Skylauncher

A launcher for the native PC ports of *Skylanders: Spyro's Adventure*, *Skylanders: Giants* and *Skylanders: Swap Force*. You give it your own disc
image, it builds the game on your computer, and you play.

Skylauncher contains no game code and no game art. Everything shown from the game is read from your own disc and stays
on your computer.

## What you need

- Your own copy of *Skylanders: Spyro's Adventure*, *Skylanders: Giants* or *Skylanders: Swap Force* for Xbox 360, as a disc image (`.iso`).
- Windows 10 or 11 (64-bit), a Mac with Apple Silicon, or a Linux PC (below).
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

## Get started

1. Download Skylauncher from the [Releases page](https://github.com/mflr0/Skylauncher/releases):
   - **Windows:** `Skylauncher.exe`. If SmartScreen warns you, choose More info, then Run anyway.
   - **Mac:** `Skylauncher-macOS.dmg`. The app is not notarized: the first time, right-click it and choose Open.
   - **Linux:** `Skylauncher-x86_64.AppImage`, after `chmod +x`.
2. Drop your `.iso` on the window, or click the button to choose it. The disc is checked.
3. Click **INSTALL**. The game is built from your disc. It takes about 10 minutes for Spyro's Adventure and 15 for
   Giants, and happens once.
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

## If something goes wrong

- Settings (the gear) > Support > **Copy game log** copies the log so you can send it to us. **Open logs folder** shows the
  files.
- Everything is in one "Skylanders" folder: `%APPDATA%\Skylanders\` on Windows, `~/Library/Application Support/Skylanders/`
  on Mac, `~/.local/share/Skylanders/` on Linux. The logs are in its `logs/` and `SSA/` folders.
- If the game stops with an error, a window offers Copy log and Report.
- Updates (Skylauncher's and each game's) are looked for at every start; an offline start says nothing. Every download is checked against the release's SHA-256.

## Build from source

You need CMake 3.20 or later, Ninja, a C++20 compiler (Clang) and git.

```sh
git clone --recursive https://github.com/mflr0/Skylauncher.git
cd Skylauncher
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build Skylauncher
```

The developer docs are in `docs/`: `launcher_ui.md` covers the screens and theme, `install.md` the install,
`collection_ui.md` the figures, `updates.md` the updater and releases, and `support.md` the logs.

## Credits and licence

Designed by dorydev.

MIT licence, see `LICENSE`. The third-party components are listed in `THIRD_PARTY_NOTICES.md`.

Skylanders is a trademark of Activision. This project is not affiliated with or endorsed by Activision. You need your
own copy of the game.

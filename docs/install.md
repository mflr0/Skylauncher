# Installing the game

Skylauncher contains no game code. The game is recompiled and built on the player's own machine, from their own disc,
by `SSABuild`, the build driver of the SSARecomp **player kit** (its contract is `docs/ssabuild.md` in SSARecomp).
The launcher finds the kit, runs it, shows its progress and then starts the game it built.

## The flow

1. The player drops the ISO; the launcher verifies it (`CheckGameSource`) and keeps the SHA-256 of its `default.xex`.
2. No game built for that disc, and no runtime set up by hand (below): **INSTALL** replaces PLAY, with one line
   saying the game is built from the disc, about 10 minutes and 6 GB, and internet is needed once for the compiler
   (`InstallMinutes`, `InstallGigabytes` in `game_install.h`).
3. With no kit (below), INSTALL first downloads it: the checklist starts with **Downloading the game tools** (a bar in MB,
   retry with **Retry**; a failure says "The game tools couldn't be downloaded"). Linux (from 0.3.1) gets
   `ssa-kit-linux-x64.zip` the same way: its SSABuild downloads zig as the compiler, no root needed. See `docs/updates.md`
   for the release it comes from.
   Then INSTALL runs `SSABuild --iso <iso> --out <game folder> --kit <kit>`. The progress card replaces the logo
   (`ui/install_page.cpp`, its model is `install_checklist.{h,cpp}`):
   - a fixed **checklist** of eight steps: Preparing the compiler, Reading the disc, Translating the game code, Translating
     the shaders, Compiling the game, Linking the game, Adding the movie shaders, Finishing (the second shaders / compile /
     link pass SSABuild runs after the movie shaders). Each is waiting, running (a spinner, its own bar from `@progress`
     and its time), done (a tick and how long it took) or failed. An unknown `@step` gets a row of its own;
   - one **overall bar** with a percentage (each step weighted by its usual share: compiling is half), the elapsed time and
     a **time left** that appears once the current step has some progress (its own pace for the rest of it, the pace of the
     steps that really ran for the next ones; "up to date" steps say nothing about the machine);
   - **one line** under the steps with SSABuild's last `@log` of the step: it replaces itself, it is never a log;
   - **Show details**: the log in a scrollable monospace panel, the current step above it;
   - on **failure**: a human headline ("Couldn't download the compiler. Check your connection."), SSABuild's
     `@error`, "Retry carries on from this step", and **Copy log**, **Open logs folder**, **Retry**;
   - **across a restart**: `[install.<sha256>] state=running|failed` and `pid=` are kept in `launcher.ini` while an install
     runs or after it failed. Next start, with the disc verified, the card comes back with the steps read from
     `install.log` ("Installation interrupted", **Resume**; Resume waits for an SSABuild that is still running).
4. `@done <path>` and exit code 0: the path is saved for that disc and PLAY appears. On Windows SDL cannot report the
   exit code of a process started without a console window, so there `@done` alone means success.
5. When the kit changes (another `build=` in its `kit.txt`), the Play page offers **Update the game**; the game's settings
   (Settings button on its Play page) > Game has **Reinstall** (held a second), **Remove game** and
   **Game files**. SSABuild is incremental, so a reinstall is quick when little changed.

Cancel stops only the process the launcher started: SIGTERM then SIGKILL after 3 s on macOS and Linux; on Windows
SSABuild runs in a job object, so the compilers it started stop with it (also when the launcher is closed).
SSABuild's output is also written to `install.log` in the game folder. `SKYLAUNCHER_BUILD_JOBS=N` passes `--jobs N`.

## Two games: Spyro's Adventure and Giants (kit 0.5.0)

The sidebar lists both games as playable. Each has its own folder in the data folder (`SSA/`, `Giants/`), its own remembered
disc (`[ssa] iso=`, `[giants] iso=`), its own built game and its own install log; **one kit builds both** (`kit.txt`
`games=ssa,giants`).

- Clicking the other game switches the window to it in place, with that game's state kept in a slot (docs/launcher_ui.md); the
  choice is kept as `[launcher] game=` so the launcher starts on the game last shown. Code: `App::SelectGame` and `PollSlots`,
  `RunLauncherWindow(..., played)`, `SetActiveGame()` in `game_install.cpp` (the folder helpers answer for the active game, which
  the launcher sets to each slot's game while it polls that slot).
- The disc check is per game: the table in `src/launcher/launcher_games.cpp` (title ID, SHA-256 of `default.xex`). A Giants
  disc dropped on Spyro's Adventure says it is another game, and the other way round. The name and icon on the game page come
  from the disc's own XDBF, as for SSA.
- INSTALL runs the same SSABuild with the disc; **SSABuild finds the game itself** from `default.xex`'s SHA-256
  (workshop `docs/release/launcher/kit_contract.md`, "Several games in one kit"). The built game is `SSARecomp` for Spyro's
  Adventure and **`SGRecomp`** (`SGRecomp.app`, `SGRecomp.exe`) for Giants, `GameInfo::runtime` in the game table. Both log to
  `SSARecomp.log` in their own folder.
- The Giants install is about 15 minutes (4443 shaders to translate instead of 1118): `InstallMinutesFor()`. Measured with kit 0.5.0 on an M1 Pro: SSA 193 s, Giants 563 s (the second shader pass is incremental since 0.5.0).
- The shared figure collection serves both games (the catalogue already holds all six games' toys).
- Verify: `SkylauncherTest <spyro iso> <scratch>` checks the table and folders; with `SKYLAUNCHER_TEST_GIANTS_ISO=<giants iso>` it
  also checks that each disc is accepted by its own game and refused by the other. End to end: see "Testing without the kit".

## Where things are

| What | Where |
| --- | --- |
| The kit | `SKYLAUNCHER_KIT`, else `[launcher] kit=` in `launcher.ini`, else next to the launcher (next to `Skylauncher.app` on macOS): `ssa-kit/`, `kit/`, or a folder whose name starts with `ssa-kit` (an unzipped `ssa-kit-windows-x64`), else the newest `kit/<release tag>/` Skylauncher downloaded into its data folder. It must hold `SSABuild` (`SSABuild.exe`). |
| The kit's version | `build=` (else `version=`) in the kit's `kit.txt`, else SSABuild's size and date |
| The built game | `<data>/SSA` (see below). Never next to the ISO. |
| What was built | `[install.<sha256>]` in `launcher.ini`: `runtime=` (the `@done` path), `kit=` (the kit version it was built with) |

Which runtime PLAY starts: `SKYLAUNCHER_RUNTIME`, else the game built for this disc, else `[launcher] runtime=`, else
`SSARecomp` next to the launcher. A runtime set up by hand (the last two) also hides INSTALL.

### One data folder, "Skylanders"

The launcher, the game and SSABuild keep everything under one root per OS, chosen by the shared `datadir` helper
(`src/shared/collection/datadir.{h,cpp}`, copied from SSARecomp by `tools/sync_collection.sh`):

| OS | Root |
| --- | --- |
| Windows | `%APPDATA%\Skylanders` (everything, the built game and the compiler too) |
| macOS | `~/Library/Application Support/Skylanders` |
| Linux | `$XDG_DATA_HOME/Skylanders`, by default `~/.local/share/Skylanders` |

| Inside the root | What |
| --- | --- |
| `Collection/` | the figures, shared by every game |
| `launcher.ini` | the settings shared by the launcher and the games |
| `kit/<release tag>/` | the kits the launcher downloaded |
| `toolchains/` | the compiler SSABuild downloads (Windows) |
| `logs/` | `skylauncher-game.log` (+ `.prev.log`) and `migration.log` |
| `SSA/` | the built game (`SSARecomp.app` / `SSARecomp.exe`, `install.log` + `install.prev.log`, `install-logs/`), and its data: `saves/`, `saves-backup/`, `pipelines.bin`, `portal.txt`, `figures/`, `portraits/`, `launcher-cache/`, `SSARecomp.log` (+ `.prev.log`). Giants has its own folder, `Giants/`, with the same layout (`SGRecomp.app` / `SGRecomp.exe`). |

`SKYLANDERS_DATA` replaces the root (tests, portable use). `SKYLAUNCHER_DATA` still replaces the launcher's part of it
(the game folder, the kits, the logs). The launcher settings (the gear) > Launcher > **Show in folder** opens the root, and each game's settings > Game > Folders > **Game files** opens its own folder.

**Save backups.** The game's settings > Saves lists the backups the game keeps of its saves (`SSA/saves-backup/<time>/`, see
SSARecomp's `docs/settings.md`): date, time and size, newest first. **Restore** (held a second) first keeps the saves the player has now
as a backup of its own, then copies the chosen one back byte for byte. It is refused while the game runs: the game holds
`SSA/saves-backup/game.lock` with its process id while it plays, and a marker a crash left behind is dropped once that
process is gone. The code is the shared library's `save_backup.{h,cpp}` (`src/shared/collection/`, synced from
SSARecomp by `tools/sync_collection.sh`), so the game and the launcher agree on the folder, the names and the rotation
(the last 10, plus one per day for 7 days).

**Moving from the older layout.** Before 0.3.1 the data was in `<roaming>/SSARecomp/` (and its nested `SSARecomp/`),
in `Skylauncher/` (`games/<sha256>/`, `kit/`; `%LOCALAPPDATA%\Skylauncher` on Windows) and, on Windows,
`%LOCALAPPDATA%\SSARecomp\toolchains`. The first start of the new launcher **or** the new game moves them (`datadir::MigrateOnce`):
entries are renamed one by one; nothing is overwritten or deleted (a taken name keeps the moved entry as
`<name>.old-<time>`); a failed rename is copied, checked, then deleted, else left for the next start; an old folder is
removed only once empty; `launcher.ini` paths into a moved folder are rewritten (the ISO's stays); everything is logged
in `logs/migration.log`. The full table and rules: SSARecomp's `docs/settings.md`. A run with `SKYLANDERS_DATA`,
`SKYLAUNCHER_DATA`, `SSA_LAUNCHER_INI`, `SSA_SETTINGS`, `SSA_SAVE_DIR` or `SSA_COLLECTION` set never migrates.

**Older builds.** A game built by a kit up to 0.3.0 still uses the older folders, which the migration emptied. Installs
from 0.3.1 on leave `layout.txt` in the game folder. When PLAY starts a game under `SSA/` without it, the launcher points
it at the new places through variables that build already reads: `SSA_LAUNCHER_INI` (the root's `launcher.ini`),
`SSA_SAVE_DIR` (`SSA/saves`; that build also keeps `pipelines.bin` and `portal.txt` there), `SSA_LOG` and
`SSA_PORTRAITS`. A `launcher-cache` it makes again in the old folder is dropped at the next start. "Update the game"
rebuilds it with the new kit and ends this.

### Delete data, uninstall

Each game's settings > **Game** tab (the Settings button on the game's Play page) has the buttons below. They act on that game
only, in its folder (`<root>/SSA` or `<root>/Giants`); the shared `Collection/`, the other game, the kits and the launcher's
`launcher.ini` are never touched. All of them are **refused while the game runs** (`saves-backup/game.lock`, the same marker
Restore uses) and **disabled while the game installs**. The sizes shown next to them are measured when the tab opens.

| Button | Goes | Stays |
| --- | --- | --- |
| **Clear caches** (plain button) | `pipelines.bin` (shader cache), `launcher-cache/` (the disc's art), `portraits/`, the game's `*.log` | everything else: the saves too |
| **Delete saves and data** (held a second) | the saves (`saves/`, or `SSA_SAVE_DIR`), `saves-backup/`, `portal.txt` (the seats of the next session), and the caches above | the built game, the disc image, the settings in `launcher.ini`, `textures/`, `figures/`, the shared figure collection |
| **Remove game** (held a second) | the built game (`SSARecomp.app` or `.exe`, its libraries and shaders), `install.log` and `install-logs/`, `pipelines.bin`, the game's logs; and `[install.<sha256>]` in `launcher.ini`, so the Play page offers INSTALL again | the saves and their backups, `portal.txt`, `textures/`, `figures/`, `launcher-cache/`, `portraits/`, the disc image, the settings |
| **Reinstall** (held a second) | nothing is deleted: SSABuild builds again (incremental) | all of it |

After an uninstall the disc is still known, so INSTALL builds the game again and the saves are where they were. To remove
everything the launcher keeps for a game, uninstall, then delete saves and data, then delete the game's folder by hand.
**Remove** (Disc tab) only makes the launcher forget the image (`[<game>] iso=` goes): the file is not touched.
The copied game data (`<Game>/gamedata/`, "Copy files" in the Disc tab, `docs/launcher_ui.md` "Game data") is kept by **Uninstall** and
untouched by the two delete buttons, like the disc image: only **Remove the copy** (Disc tab) deletes it, and the disc image comes back.
The code is `src/launcher/game_settings.{h,cpp}` (no UI; `SkylauncherTest` runs it on scratch folders, `tests/settings_test.cpp`).

### Housekeeping

After a **successful** install: `ssabuild-work/` (2-3 GB) is deleted and its `logs/` kept as `install-logs/` (an update
therefore rebuilds everything); the kits in `kit/` older than the one that built the game are deleted; a compiler archive
(`toolchains/*.zip`) whose extracted copy is ready is deleted. Each log keeps one previous run (`.prev.log`). The art
cache `<Game>/launcher-cache` holds three pictures (logo, background, title icon; `docs/launcher_ui.md`, "Disc art") and
needs no trimming; the per-title folders of older launchers in it are removed at the next disc check.

## Linux

The kit's zip keeps the Unix modes of `SSABuild` and its `bin/` programs, and `InstallKitZip` restores them, so the
AppImage runs the downloaded SSABuild directly. The game is built in the data folder's `SSA/` (`~/.local/share/Skylanders/SSA/SSARecomp`); PLAY starts it and
keeps its output in `skylauncher-game.log`, as on the other systems. SSABuild caches its compiler (zig) in the data folder's
`toolchains/` (`~/.local/share/Skylanders/toolchains`).

The real portal needs read and write access to its `/dev/hidraw*` node, which a udev rule gives the logged-in user:

```sh
echo 'SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1430", ATTRS{idProduct}=="0150", MODE="0660", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/70-skylanders-portal.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

then unplug and replug the portal. Without it the game's log has `[portal] hid: Linux needs this udev rule ...`.

Tested in containers only (amd64 under Rosetta, Mesa's lavapipe, offscreen): a fake release over `file://`
(`SKYLAUNCHER_UPDATE_URL`) with the real kit, INSTALL to PLAY with `SSA_LAUNCHER_TEST_INSTALL=real` and
`SSA_LAUNCHER_TEST_PLAY=1` (PLAY once the install has finished). Not yet on a Linux desktop or a Steam Deck.

## Code

- `src/launcher/game_install.{h,cpp}`: finding the kit and the runtime, the game folder, `InstallProgress::Apply` (one
  stdout line), `ReadInstallLog`, `GameInstaller` (the process and the thread reading its output).
- `src/launcher/install_checklist.{h,cpp}`: the checklist, the overall fraction, the time left, the failure headline.
- `src/launcher/ui/install_page.cpp`: `App::StartInstall`, `PollInstall`, `RefreshInstall`, `DrawInstall`; the INSTALL button
  stays in `DrawPlay` (`launcher_ui.cpp`).

## Testing without the kit

`tests/fake_ssabuild.c` prints the same events with delays (target `FakeSSABuild`, built as `<build>/fake-kit/SSABuild`
with a `kit.txt`). `FAKE_SSABUILD_FAIL=<step id>` makes it fail half way through that step, `FAKE_SSABUILD_DELAY` sets
the pause between events (0.05 s).

- `SkylauncherTest` parses events, then runs the fake kit through `GameInstaller` (a failure, a success, a cancel), and
  checks the checklist model on event streams (`tests/shell_test.cpp`).
- The fake kit prints SSABuild's real step ids, titles and order, including the movie pass and the second pass.
- The screenshot test with `SSA_LAUNCHER_TEST_INSTALL=1` and `SKYLAUNCHER_KIT=<build>/fake-kit` writes
  `10-install.png` (INSTALL), `11-installing.png`, `12-installing-compile.png`, `13-install-details.png`,
  `14-install-failed.png`, `15-installed.png` (PLAY after a retry), `16-game-data.png` and `17-install-interrupted.png`.
  `SSA_LAUNCHER_TEST_INSTALL=real` with the real kit takes a shot every `SSA_LAUNCHER_TEST_EVERY` seconds (default 120).
- Try it by hand: `ninja -C build FakeSSABuild`, then start the launcher with `SKYLAUNCHER_KIT=$PWD/build/fake-kit`.

## Windows build from a Mac

```sh
cmake -S . -B build-win64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-w64-mingw32-clang.cmake \
    -DLLVM_MINGW_ROOT=<llvm-mingw release> -DCMAKE_BUILD_TYPE=Release
ninja -C build-win64 Skylauncher
```

`build-win64/Skylauncher.exe` is a single file (fonts built in; only system DLLs and the UCRT, which Windows 10 and
11 include). CI still builds Windows with clang-cl.

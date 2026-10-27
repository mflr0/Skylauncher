# Installing the game

Arkeyan Launcher contains no game code. The game is recompiled and built on the player's own machine, from their own disc,
by `SkyBuild`, the build driver of the SSARecomp **player kit** (its contract is `docs/skybuild.md` in SSARecomp).
The launcher finds the kit, runs it, shows its progress and then starts the game it built.

## The flow

1. The player drops the ISO; the launcher verifies it (`CheckGameSource`) and keeps the SHA-256 of its `default.xex`.
2. No game built for that disc, and no runtime set up by hand (below): **INSTALL** replaces PLAY, with one line
   saying the game is built from the disc, about 10 minutes and 6 GB, and internet is needed once for the compiler
   (`InstallMinutes`, `InstallGigabytes` in `game_install.h`).
3. With no kit (below), a kit from an older release than the launcher's, or a kit that does not build this disc's game
   (`games=` in its `kit.txt`; a kit without it builds Spyro's Adventure only), INSTALL first downloads the kit of the
   launcher's own release: the checklist starts with **Downloading the game tools** (a bar in MB,
   retry with **Retry**; a failure says "The game tools couldn't be downloaded"). SkyBuild never runs on a disc its kit
   cannot build. A kit the player chose (`SKY_LAUNCHER_KIT`, `[launcher] kit=`) is never replaced: if it lacks the game, INSTALL
   says "These game tools can't build <game>" and stops. Linux (from 0.3.1) gets
   `skylanders-kit-linux-x64.zip` the same way: its SkyBuild downloads zig as the compiler, no root needed. See `docs/updates.md`
   for the release it comes from.
   Then INSTALL runs `SkyBuild --iso <iso> --out <game folder> --kit <kit>`. The progress card replaces the logo
   (`ui/install_page.cpp`, its model is `install_checklist.{h,cpp}`):
   - a fixed **checklist** of eight steps: Preparing the compiler, Reading the disc, Translating the game code, Translating
     the shaders, Compiling the game, Linking the game, Adding the movie shaders, Finishing (the second shaders / compile /
     link pass SkyBuild runs after the movie shaders). Each is waiting, running (a spinner, its own bar from `@progress`
     and its time), done (a tick and how long it took) or failed. An unknown `@step` gets a row of its own;
   - one **overall bar** with a percentage (each step weighted by its usual share: compiling is half), the elapsed time and
     a **time left** that appears once the current step has some progress (its own pace for the rest of it, the pace of the
     steps that really ran for the next ones; "up to date" steps say nothing about the machine);
   - **one line** under the steps with SkyBuild's last `@log` of the step: it replaces itself, it is never a log;
   - **Show details**: the log in a scrollable monospace panel, the current step above it;
   - on **failure**: a human headline ("Couldn't download the compiler. Check your connection."), SkyBuild's
     `@error`, "Retry carries on from this step", and **Copy log**, **Open logs folder**, **Retry**;
   - **across a restart**: `[install.<sha256>] state=running|failed` and `pid=` are kept in `launcher.ini` while an install
     runs or after it failed. Next start, with the disc verified, the card comes back with the steps read from
     `install.log` ("Installation interrupted", **Resume**; Resume waits for an SkyBuild that is still running).
4. `@done <path>` and exit code 0: the path is saved for that disc and PLAY appears. On Windows SDL cannot report the
   exit code of a process started without a console window, so there `@done` alone means success.
5. When the release publishes a newer version of the game than the one built (`games.json`, `docs/updates.md`), the Play page's button is **UPDATE**, with a small link **Play without updating** under it; the game's settings
   (Settings button on its Play page) > Game has **Reinstall** (held a second), **Remove game** and
   **Game files**. SkyBuild is incremental, so a reinstall is quick when little changed.

Cancel stops only the process the launcher started: SIGTERM then SIGKILL after 3 s on macOS and Linux; on Windows
SkyBuild runs in a job object, so the compilers it started stop with it (also when the launcher is closed).
SkyBuild's output is also written to `install.log` in the game folder. `SKY_LAUNCHER_BUILD_JOBS=N` passes `--jobs N`.

## Three games: Spyro's Adventure, Giants and Swap Force (kit 0.5.0; Swap Force since the kit that lists it)

The sidebar lists the three games as playable. Each has its own folder in the data folder (`SSA/`, `Giants/`, `SwapForce/`,
the name `datadir::GameFolderName` gives), its own remembered disc (`[ssa] iso=`, `[giants] iso=`, `[swapforce] iso=`), its own
built game and its own install log; **one kit builds them all** (`kit.txt` `games=ssa,giants,swapforce`; a kit that does not
list Swap Force makes the launcher fetch the right one first, as for Giants).

- Clicking the other game switches the window to it in place, with that game's state kept in a slot (docs/launcher_ui.md); the
  choice is kept as `[launcher] game=` so the launcher starts on the game last shown. Code: `App::SelectGame` and `PollSlots`,
  `RunLauncherWindow(..., played)`, `SetActiveGame()` in `game_install.cpp` (the folder helpers answer for the active game, which
  the launcher sets to each slot's game while it polls that slot).
- The disc check is per game: the table in `src/launcher/launcher_games.cpp` (title ID, SHA-256 of `default.xex`). A Giants
  disc dropped on Spyro's Adventure says it is another game, and the other way round. The name and icon on the game page come
  from the disc's own XDBF, as for SSA.
- INSTALL runs the same SkyBuild with the disc; **SkyBuild finds the game itself** from `default.xex`'s SHA-256
  (workshop `docs/release/launcher/kit_contract.md`, "Several games in one kit"). The built game is `SSARecomp` for Spyro's
  Adventure, **`SGRecomp`** (`SGRecomp.app`, `SGRecomp.exe`) for Giants and **`SSFRecomp`** for Swap Force, `GameInfo::runtime` in
  the game table. Each logs to `SSARecomp.log` in its own folder.
- The Giants install is about 15 minutes (4443 shaders to translate instead of 1118): `InstallMinutesFor()`. Measured with kit 0.5.0 on an M1 Pro: SSA 193 s, Giants 563 s (the second shader pass is incremental since 0.5.0).
- The shared figure collection serves all three games (the catalogue already holds all six games' toys).
- Swap Force's disc is "Skylanders - Swap Force (USA, Australia) (En,Fr)": English and French (its Language tab lists the
  languages the disc holds). The install estimate shown is the generic one (`InstallMinutes`); only Giants has its own figure.
- Verify: `ArkeyanLauncherTest <spyro iso> <scratch>` checks the table and folders; with `SKY_LAUNCHER_TEST_GIANTS_ISO=<giants iso>` it
  also checks that each disc is accepted by its own game and refused by the other; `SKY_LAUNCHER_TEST_SWAPFORCE_ISO=<swap force iso>`
  adds the Swap Force disc (accepted by its game, refused by the others, its pictures and portraits read into `SwapForce/`). End to end: see "Testing without the kit".

## Where things are

| What | Where |
| --- | --- |
| The kit | `SKY_LAUNCHER_KIT`, else `[launcher] kit=` in `launcher.ini`, else next to the launcher (next to the `.app` on macOS): `skylanders-kit/`, `ssa-kit/`, `kit/`, or a folder whose name starts with `skylanders-kit` or `ssa-kit` (an unzipped `skylanders-kit-windows-x64`), else the newest `kit/<release tag>/` Arkeyan Launcher downloaded into its data folder. A downloaded kit from a newer release than the one next to the launcher (by its `kit.txt` `version=`), or the only one that builds the game, wins over it. It must hold `SkyBuild` (`SkyBuild.exe`), or `SSABuild` (`SSABuild.exe`) in a kit made for 0.4.x launchers. |
| The kit's version | `build=` (else `version=`) in the kit's `kit.txt`, else SkyBuild's size and date |
| What the kit builds, its release | `games=` in `kit.txt` (none: `ssa`); its release is the `kit/<tag>/` folder it was downloaded to, else `version=` |
| The built game | `<data>/SSA` (see below). Never next to the ISO. |
| What was built | `[install.<sha256>]` in `launcher.ini`: `runtime=` (the `@done` path), `kit=` (the kit version it was built with) |

Which runtime PLAY starts: `SKY_LAUNCHER_RUNTIME`, else the game built for this disc, else `[launcher] runtime=`, else
`SSARecomp` next to the launcher. A runtime set up by hand (the last two) also hides INSTALL.

### One data folder, "Skylanders"

The launcher, the game and SkyBuild keep everything under one root per OS, chosen by the shared `datadir` helper
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
| `toolchains/` | the compiler SkyBuild downloads (Windows) |
| `logs/` | `skylauncher-game.log` (+ `.prev.log`) and `migration.log` |
| `SSA/` | the built game (`SSARecomp.app` / `SSARecomp.exe`, `install.log` + `install.prev.log`, `install-logs/`), and its data: `saves/`, `saves-backup/`, `pipelines.bin`, `portal.txt`, `figures/`, `portraits/`, `launcher-cache/`, `SSARecomp.log` (+ `.prev.log`). Giants has its own folder, `Giants/`, with the same layout (`SGRecomp.app` / `SGRecomp.exe`). |

**Environment variables.** The launcher reads `SKY_LAUNCHER_<X>` (its own: `SKY_LAUNCHER_DATA`, `SKY_LAUNCHER_INI`, `SKY_LAUNCHER_KIT`...)
and `SKY_<X>` (shared with the games and SkyBuild: `SKY_COLLECTION`, `SKY_SAVE_DIR`, `SKY_SETTINGS`...), then the names of earlier
versions (`SKYLAUNCHER_<X>`, `SSA_LAUNCHER_<X>`, `SSA_<X>`), so every old script keeps working; a variable that is set wins over the
older names (`src/shared/env.h`). A game the launcher starts gets both names of each variable that is set (`CreateGameEnvironment`: the launcher's own environment is not changed):
a game built by an older kit reads only the `SSA_` names. `SKYLANDERS_DATA` never changed.

`SKYLANDERS_DATA` replaces the root (tests, portable use). `SKY_LAUNCHER_DATA` still replaces the launcher's part of it
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
`SKY_LAUNCHER_DATA`, `SKY_LAUNCHER_INI`, `SKY_SETTINGS`, `SKY_SAVE_DIR` or `SKY_COLLECTION` set never migrates.

**Older builds.** A game built by a kit up to 0.3.0 still uses the older folders, which the migration emptied. Installs
from 0.3.1 on leave `layout.txt` in the game folder. When PLAY starts a game under `SSA/` without it, the launcher points
it at the new places through variables that build already reads: `SKY_LAUNCHER_INI` (the root's `launcher.ini`),
`SKY_SAVE_DIR` (`SSA/saves`; that build also keeps `pipelines.bin` and `portal.txt` there), `SKY_LOG` and
`SKY_PORTRAITS`. A `launcher-cache` it makes again in the old folder is dropped at the next start. UPDATE
rebuilds it with the new kit and ends this.

### Delete data, uninstall

Each game's settings > **Game** tab (the Settings button on the game's Play page) has the buttons below. They act on that game
only, in its folder (`<root>/SSA`, `<root>/Giants` or `<root>/SwapForce`); the shared `Collection/`, the other game, the kits and the launcher's
`launcher.ini` are never touched. All of them are **refused while the game runs** (`saves-backup/game.lock`, the same marker
Restore uses) and **disabled while the game installs**. The sizes shown next to them are measured when the tab opens.

| Button | Goes | Stays |
| --- | --- | --- |
| **Clear caches** (plain button) | `pipelines.bin` (shader cache), `launcher-cache/` (the disc's art), `portraits/`, the game's `*.log` | everything else: the saves too |
| **Delete saves and data** (held a second) | the saves (`saves/`, or `SKY_SAVE_DIR`), `saves-backup/`, `portal.txt` (the seats of the next session), and the caches above | the built game, the disc image, the settings in `launcher.ini`, `textures/`, `figures/`, the shared figure collection |
| **Remove game** (held a second) | the built game (`SSARecomp.app` or `.exe`, its libraries and shaders), `install.log` and `install-logs/`, `pipelines.bin`, the game's logs; and `[install.<sha256>]` in `launcher.ini`, so the Play page offers INSTALL again | the saves and their backups, `portal.txt`, `textures/`, `figures/`, `launcher-cache/`, `portraits/`, the disc image, the settings |
| **Reinstall** (held a second) | nothing is deleted: SkyBuild builds again (incremental) | all of it |

After an uninstall the disc is still known, so INSTALL builds the game again and the saves are where they were. To remove
everything the launcher keeps for a game, uninstall, then delete saves and data, then delete the game's folder by hand; to remove everything of every game
and the launcher, see "Uninstall everything" below.
**Remove** (Disc tab) only makes the launcher forget the image (`[<game>] iso=` goes): the file is not touched.
The copied game data (`<Game>/gamedata/`, "Copy files" in the Disc tab, `docs/launcher_ui.md` "Game data") is kept by **Uninstall** and
untouched by the two delete buttons, like the disc image: only **Remove the copy** (Disc tab) deletes it, and the disc image comes back.
The code is `src/launcher/game_settings.{h,cpp}` (no UI; `ArkeyanLauncherTest` runs it on scratch folders, `tests/settings_test.cpp`).

### Uninstall everything

Settings (the gear) > Launcher > **Remove Arkeyan Launcher** > **Uninstall everything...** (`docs/launcher_ui.md`, "Uninstall everything") removes what
Arkeyan Launcher and the games put on the machine, after a list with sizes and a held button. The code is `src/launcher/uninstall.{h,cpp}` (no UI:
a **plan** that only measures, then the run), the window is `ui/uninstall_page.cpp`.

| Goes | What it is |
| --- | --- |
| Games | each game's folder (`SSA/`, `Giants/`, `SwapForce/`) but the player's data below: the built game, its install logs, `layout.txt`, the shortcut icon |
| Game data copies | `<Game>/gamedata/` |
| Game tools | `kit/` and `toolchains/` |
| Caches and logs | `pipelines.bin`, `launcher-cache/`, `portraits/`, the games' `*.log`, `logs/` |
| Settings | `launcher.ini` (the remembered discs go with it) |
| Desktop shortcuts | the `<Game>.app`, `.lnk` or `.desktop` the launcher made, only if it holds `--game` |
| Arkeyan Launcher | the `.app` (macOS), the `.exe` (Windows), the AppImage (Linux), and the `.old` an update left beside it |
| **Only with the box "Also delete my saves and my figures"** | the saves (`saves/`, `saves-backup/`, `portal.txt`), the games' `figures/` and `textures/`, and the collection: the figures (`.sky`, `.bin`, `.dmp`), `.lock`, `collection.json`, `backups/` and `trash/` |

**What is never removed**: the disc images (a path `[<game>] iso=` remembers, and any `.iso` / `.xiso` file, wherever it is, even in a game's folder:
the folder stays around it and the result says so); a game the player moved out of its folder (the settings' `[install.*] runtime=` is outside the games'
folders: said, not touched); other files in the data folder or in the figure folder (said as "Other files in the figures folder stay"); the
older-layout folders of before 0.3.1, which the first start moved (`datadir::MigrateOnce`); the data folder itself unless it is empty at the end.

**Safety, because it deletes** (all in `uninstall.cpp`, and checked again at run time, so a plan cannot be forged):
- Targets come only from the `Environment`: the data root (`datadir::Root()`), Arkeyan Launcher's data folder (`SKY_LAUNCHER_DATA`), each game's folder,
  the shared figure folder, the settings file, the Desktop shortcuts and the launcher. Nothing is searched for.
- A path must be absolute, without `..`, not the filesystem root nor a folder straight under it, not the home folder nor a folder that holds it, not a
  link, and strictly inside the data root or the launcher's data folder (links resolved). The figure folder and the settings file may be
  elsewhere only because the environment names them (`SKY_COLLECTION`, `SKY_LAUNCHER_INI`). A data folder that fails refuses everything.
  Saves kept elsewhere (`SKY_SAVE_DIR`) are not ours: left, and said.
- Inside a game's folder a link goes as a link and is never followed; the figure folder is cleaned by name only (its links stay).
- A game that runs (`saves-backup/game.lock`) or a figure on a running portal stops everything; so does an install or a disc check in the window.

**The launcher itself** is the last step, done after the window has quit by a small helper the launcher starts (`StartScheduled`; the command is
`ScheduleCommand`, pure and tested): it waits for the launcher's process to end (a minute at most), then
- **macOS**: `/bin/sh` moves the `.app` (and its `.old`) into `~/.Trash`, with a free name ("Arkeyan Launcher 1.app") when one is taken. The `.app` must be a
  bundle (`Contents/MacOS`) whose folder is writable (the same rule as updates; run from a disk image it cannot be moved: the result says to delete it by hand);
- **Windows**: a hidden `powershell.exe` (`-EncodedCommand`, so no quoting) waits for the process, then deletes the `.exe` and its `.old`, retrying for 20 s
  (an exe cannot delete itself while it runs);
- **Linux**: `/bin/sh` deletes the AppImage (`$APPIMAGE`) and its `.old`. A launcher started another way (a build folder) has no path to remove: the result
  says to delete it by hand.
A launcher path that is not of this system's kind (not a `.app`, not an `.exe`), a link, a disc image, inside the data folder or in a system folder
(Linux) is refused: `PlanLauncherRemoval`. When the rest could not run (a game plays) the helper is not started.

**Tests** (`tests/uninstall_test.cpp`, in `ArkeyanLauncherTest`; `SKY_LAUNCHER_TEST_UNINSTALL_ONLY=1` runs only them): scratch trees with each system's
layout (the paths are pure; the data folder of each system, the shortcut names); the plan with exact sizes and nothing touched; the run without and with
the tick; disc images, links, the player's own files kept; unsafe roots (`/`, a folder under it, home, a folder holding it, relative, `..`), a forged plan,
a moved game, a game running; the launcher removal's refusals per system; the commands (Windows decoded back from its encoding); and the macOS and Linux
helper scripts **run for real** on a dummy bundle or file copied into the scratch folder, with a scratch Trash, on a process that is already gone and on one
that is still running. The real launcher, the real Trash and the real data folders are never named: `ThisMachine` ignores the real launcher while any of
`SKYLANDERS_DATA`, `SKY_LAUNCHER_DATA`, `SKY_LAUNCHER_INI` or `SKY_COLLECTION` is set, and takes a dummy only from `SKY_LAUNCHER_UNINSTALL_SELF` (on
macOS together with `SKY_LAUNCHER_UNINSTALL_TRASH`).

### Housekeeping

After a **successful** install: `ssabuild-work/` (2-3 GB) is deleted and its `logs/` kept as `install-logs/` (an update
therefore rebuilds everything); the kits in `kit/` older than the one that built the game are deleted; a compiler archive
(`toolchains/*.zip`) whose extracted copy is ready is deleted. Each log keeps one previous run (`.prev.log`). The art
cache `<Game>/launcher-cache` holds three pictures (logo, background, title icon; `docs/launcher_ui.md`, "Disc art") and
needs no trimming; the per-title folders of older launchers in it are removed at the next disc check.

## Linux

The kit's zip keeps the Unix modes of `SkyBuild` and its `bin/` programs, and `InstallKitZip` restores them, so the
AppImage runs the downloaded SkyBuild directly. The game is built in the data folder's `SSA/` (`~/.local/share/Skylanders/SSA/SSARecomp`); PLAY starts it and
keeps its output in `skylauncher-game.log`, as on the other systems. SkyBuild caches its compiler (zig) in the data folder's
`toolchains/` (`~/.local/share/Skylanders/toolchains`).

The real portal needs read and write access to its `/dev/hidraw*` node, which a udev rule gives the logged-in user:

```sh
echo 'SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1430", ATTRS{idProduct}=="0150", MODE="0660", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/70-skylanders-portal.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

then unplug and replug the portal. Without it the game's log has `[portal] hid: Linux needs this udev rule ...`.

Tested in containers only (amd64 under Rosetta, Mesa's lavapipe, offscreen): a fake release over `file://`
(`SKY_LAUNCHER_UPDATE_URL`) with the real kit, INSTALL to PLAY with `SKY_LAUNCHER_TEST_INSTALL=real` and
`SKY_LAUNCHER_TEST_PLAY=1` (PLAY once the install has finished). Not yet on a Linux desktop or a Steam Deck.

## Code

- `src/launcher/game_install.{h,cpp}`: finding the kit and the runtime, the game folder, `InstallProgress::Apply` (one
  stdout line), `ReadInstallLog`, `GameInstaller` (the process and the thread reading its output).
- `src/launcher/install_checklist.{h,cpp}`: the checklist, the overall fraction, the time left, the failure headline.
- `src/launcher/ui/install_page.cpp`: `App::StartInstall`, `PollInstall`, `RefreshInstall`, `DrawInstall`; the INSTALL button
  stays in `DrawPlay` (`launcher_ui.cpp`).

## Testing without the kit

`tests/fake_skybuild.c` prints the same events with delays (target `FakeSkyBuild`, built as `<build>/fake-kit/SkyBuild`
with a `kit.txt`). `FAKE_SSABUILD_FAIL=<step id>` makes it fail half way through that step, `FAKE_SSABUILD_DELAY` sets
the pause between events (0.05 s).

- `ArkeyanLauncherTest` parses events, then runs the fake kit through `GameInstaller` (a failure, a success, a cancel), and
  checks the checklist model on event streams (`tests/shell_test.cpp`).
- The fake kit prints SkyBuild's real step ids, titles and order, including the movie pass and the second pass.
- The screenshot test with `SKY_LAUNCHER_TEST_INSTALL=1` and `SKY_LAUNCHER_KIT=<build>/fake-kit` writes
  `10-install.png` (INSTALL), `11-installing.png`, `12-installing-compile.png`, `13-install-details.png`,
  `14-install-failed.png`, `15-installed.png` (PLAY after a retry), `16-game-data.png` and `17-install-interrupted.png`.
  `SKY_LAUNCHER_TEST_INSTALL=real` with the real kit takes a shot every `SKY_LAUNCHER_TEST_EVERY` seconds (default 120).
- Try it by hand: `ninja -C build FakeSkyBuild`, then start the launcher with `SKY_LAUNCHER_KIT=$PWD/build/fake-kit`.

## Windows build from a Mac

```sh
cmake -S . -B build-win64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-w64-mingw32-clang.cmake \
    -DLLVM_MINGW_ROOT=<llvm-mingw release> -DCMAKE_BUILD_TYPE=Release
ninja -C build-win64 Arkeyan Launcher
```

`build-win64/ArkeyanLauncher.exe` is a single file (fonts built in; only system DLLs and the UCRT, which Windows 10 and
11 include). CI still builds Windows with clang-cl.

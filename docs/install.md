# Installing the game

Skylauncher contains no game code. The game is recompiled and built on the player's own machine, from their own disc,
by `SSABuild`, the build driver of the SSARecomp **player kit** (its contract is `docs/ssabuild.md` in SSARecomp).
The launcher finds the kit, runs it, shows its progress and then starts the game it built.

## The flow

1. The player drops the ISO; the launcher verifies it (`CheckGameSource`) and keeps the SHA-256 of its `default.xex`.
2. No game built for that disc, and no runtime set up by hand (below): **INSTALL** replaces PLAY, with one line
   saying the game is built from the disc, about 15 minutes and 6 GB, and internet is needed once for the compiler
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
   - on **failure**: a human headline ("The compiler couldn't be downloaded. Check the internet connection."), SSABuild's
     `@error`, "What was already built is kept", and **Copy log**, **Open logs folder**, **Retry**;
   - **across a restart**: `[install.<sha256>] state=running|failed` and `pid=` are kept in `launcher.ini` while an install
     runs or after it failed. Next start, with the disc verified, the card comes back with the steps read from
     `install.log` ("Installation interrupted", **Resume**; Resume waits for an SSABuild that is still running).
4. `@done <path>` and exit code 0: the path is saved for that disc and PLAY appears. On Windows SDL cannot report the
   exit code of a process started without a console window, so there `@done` alone means success.
5. When the kit changes (another `build=` in its `kit.txt`), the Play page offers **Update the game**; Settings >
   Game data has **Reinstall the game** and **Open the game folder**. SSABuild is incremental, so these are quick when
   little changed.

Cancel stops only the process the launcher started: SIGTERM then SIGKILL after 3 s on macOS and Linux; on Windows
SSABuild runs in a job object, so the compilers it started stop with it (also when the launcher is closed).
SSABuild's output is also written to `install.log` in the game folder. `SKYLAUNCHER_BUILD_JOBS=N` passes `--jobs N`.

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
| `SSA/` | the built game (`SSARecomp.app` / `SSARecomp.exe`, `install.log` + `install.prev.log`, `install-logs/`), and its data: `saves/`, `pipelines.bin`, `portal.txt`, `figures/`, `portraits/`, `launcher-cache/`, `SSARecomp.log` (+ `.prev.log`). Another game (Giants) gets its own folder. |

`SKYLANDERS_DATA` replaces the root (tests, portable use). `SKYLAUNCHER_DATA` still replaces the launcher's part of it
(the game folder, the kits, the logs). Settings > Game data > **Open data folder** opens the root.

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

### Housekeeping

After a **successful** install: `ssabuild-work/` (2-3 GB) is deleted and its `logs/` kept as `install-logs/` (an update
therefore rebuilds everything); the kits in `kit/` older than the one that built the game are deleted; a compiler archive
(`toolchains/*.zip`) whose extracted copy is ready is deleted. Each log keeps one previous run (`.prev.log`), and the art
cache `SSA/launcher-cache` is trimmed to 256 MB (oldest files first) when a game starts.

## Linux

The kit's zip keeps the Unix modes of `SSABuild` and its `bin/` programs, and `InstallKitZip` restores them, so the
AppImage runs the downloaded SSABuild directly. The game is built as `games/<sha256>/SSARecomp`; PLAY starts it and
keeps its output in `skylauncher-game.log`, as on the other systems. SSABuild caches its compiler (zig) in
`~/.cache/SSARecomp/toolchains`.

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

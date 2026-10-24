# Installing the game

Skylauncher contains no game code. The game is recompiled and built on the player's own machine, from their own disc,
by `SSABuild`, the build driver of the SSARecomp **player kit** (its contract is `docs/ssabuild.md` in SSARecomp).
The launcher finds the kit, runs it, shows its progress and then starts the game it built.

## The flow

1. The player drops the ISO; the launcher verifies it (`CheckGameSource`) and keeps the SHA-256 of its `default.xex`.
2. No game built for that disc, and no runtime set up by hand (below): **INSTALL** replaces PLAY, with one line
   saying the game is built from the disc, about 15 minutes and 6 GB, and internet is needed once for the compiler
   (`InstallMinutes`, `InstallGigabytes` in `game_install.h`).
3. INSTALL runs `SSABuild --iso <iso> --out <game folder> --kit <kit>`. The progress card replaces the logo
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
| The kit | `SKYLAUNCHER_KIT`, else `[launcher] kit=` in `launcher.ini`, else next to the launcher (next to `Skylauncher.app` on macOS): `ssa-kit/`, `kit/`, or a folder whose name starts with `ssa-kit` (an unzipped `ssa-kit-windows-x64`). It must hold `SSABuild` (`SSABuild.exe`). |
| The kit's version | `build=` (else `version=`) in the kit's `kit.txt`, else SSABuild's size and date |
| The built game | `%LOCALAPPDATA%\Skylauncher\games\<sha256>` on Windows, `~/Library/Application Support/Skylauncher/games/<sha256>` on macOS, `~/.local/share/Skylauncher/games/<sha256>` on Linux. Never next to the ISO. |
| What was built | `[install.<sha256>]` in `launcher.ini`: `runtime=` (the `@done` path), `kit=` (the kit version it was built with) |

Which runtime PLAY starts: `SKYLAUNCHER_RUNTIME`, else the game built for this disc, else `[launcher] runtime=`, else
`SSARecomp` next to the launcher. A runtime set up by hand (the last two) also hides INSTALL.

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

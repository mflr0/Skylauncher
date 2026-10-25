# Support: logs, the crash dialog, the portal test

## Where the logs are

In the "Skylanders" data folder (`%APPDATA%\Skylanders\` on Windows, `~/Library/Application Support/Skylanders/` on macOS,
`~/.local/share/Skylanders/` on Linux; docs/install.md):

| File | Written by | What |
| --- | --- | --- |
| `SSA/SSARecomp.log` (`.prev.log`: the run before) | the game runtime | its own log: version, OS, CPU, GPU, settings, `[perf]` |
| `logs/skylauncher-game.log` (`.prev.log`) | the launcher | the runtime's stdout and stderr, from its first byte: early crashes, a missing DLL's message |
| `SSA/install.log` (`.prev.log`), `SSA/install-logs/` | the launcher, SSABuild | what SSABuild printed during the install, and its own step logs kept after the build (docs/install.md) |
| `logs/migration.log` | the first start after an update | what was moved from the older folders |

Settings > Game data > Support: **Report a problem** (one text file with the last logs, the settings and the versions;
see below), **Copy game log** (the end of `SSARecomp.log`, then of `skylauncher-game.log`, each under a heading) and
**Open logs folder** (`logs/`). `SKYLAUNCHER_LOG_DIR` replaces both log folders (tests).

## Report a problem

**Report a problem** builds one text file and shows it in a window before anything else (Close, Escape or the pad's B
close it): the header has the launcher,
kit and game build versions, the OS and the GPU line from the last game log (plus the disc image path), then the files,
each under a `===== name =====` heading:

- `launcher.ini`;
- `logs/migration.log`;
- `SSA/SSARecomp.log` and `SSA/SSARecomp.prev.log`;
- `logs/skylauncher-game.log`;
- `SSA/install.log`, `SSA/install.prev.log` and every file in `SSA/install-logs/`.

Missing files are named as empty or not found, never skipped silently. Before it is shown the text is redacted
(`src/launcher/report.cpp`): the account name becomes `<user>`, the computer name `<computer>`, the home folder and
`/Users/<name>`, `C:\Users\<name>`, `C:/Users/<name>` and `/home/<name>` become `<home>`, and the folder holding the
disc image becomes `<iso folder>`. The replacement is case-insensitive, whole names only, one pass; everything else is
kept byte for byte. The window has **Save as...** (the extension is `.txt`, the name
`Skylauncher-report-<yyyymmdd-hhmmss>.txt`) and **Copy**; nothing is uploaded. Unit tests for the redaction and the
report are in `tests/shell_test.cpp` (`Report: ...`).

## The launcher stays while the game runs

`src/main.cpp` calls `RunGame` (`launcher/game_run.cpp`): the runtime starts with its output piped to
`skylauncher-game.log`, and the launcher (no window) waits for it. On Windows the process is started without a console
window and its exit code is read from a handle of our own, since SDL reports none for such processes.

If the runtime exits with a non-zero code a dialog says **The game stopped unexpectedly**, with its exit code and the logs
folder, and three buttons: **Report** (copies the log, opens a new GitHub issue with a template; paste the log into it),
**Copy log**, **Close**. Nothing is sent by the launcher.

## Portal status

Under PLAY and under COLLECTION's title the launcher shows "Portal: <model>" (for example "Portal: Portal of Power") or
"Portal: not detected"; the tooltip says that without a real portal the game uses the virtual portal (its Toy Box) and points to
"Test the portal" below. It is read with SDL's HID API (`SDL_hid_enumerate(0x1430, 0)`, Activision's vendor id) by a
worker thread (`src/launcher/portal_status.{h,cpp}`) that scans once at start, then only when `SDL_hid_device_change_count()`
changes, at most every 2 s, so the UI never waits. Models by product id: `0150` Portal of Power, `1F17` Xbox 360 portal
(only where the OS exposes it as HID), any other Activision id "Skylanders portal (1430:XXXX)". If SDL's HID can't start,
nothing is shown. Checks: `PortalChecks` in `tests/launcher_test.cpp`, with fake enumerators.

## Test the portal

Settings > Controls > Portal of Power. The button runs `<runtime> --portal-probe` (the game, so it needs to be installed)
and reads its stdout, at most 15 s:

```
{"kind":"hid","vid":"1430","pid":"0150","product":"...","opened":true,"status":"ok"}
{"done":true,"count":1}
```

Transport lines (`{"transport":"usb","available":false,"status":"error: ..."}`) become a note under the result. One flat JSON object per line (`kind` `hid` or `usb`, `opened`, `status` `ok` or `error: <why>`; `name` is read as `product`
too), then the `done` line; anything else on stdout is ignored. The card lists each portal ("Found" / "Found, not usable"
with the reason) and says what the game will do given the mode: real portal, virtual portal, or waiting for one. No
portal: "No portal found: the game will use the virtual portal". Each portal line of a newer runtime also carries `id`, `model`, `verdict` and `verdict_reason` (the portal's answer to the game's
reset command: "Model: Spyro's Adventure portal (Xbox 360) (id 01 28)", then "Spyro's Adventure accepts this portal." or, in red, why
the game would reject or warn about it; the contract is the runtime's `docs/portal_probe.md`). A runtime without `--portal-probe` exits with code 2: "This
version of the game doesn't have the portal test yet". Parser and tests: `ParsePortalProbe`, `tests/shell_test.cpp`.

Portal mode is `[portal] mode` = `auto`, `emulated` (Virtual) or `real`; the launcher passes it as `SSA_PORTAL`. Older
`hid` and `usb` values still work and show as "Real portal".

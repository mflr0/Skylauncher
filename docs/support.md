# Support: logs, the crash dialog, the portal test

## Where the logs are

All in the folder of `launcher.ini` (`%APPDATA%\SSARecomp\` on Windows, `~/Library/Application Support/SSARecomp/` on macOS,
`~/.local/share/SSARecomp/` on Linux):

| File | Written by | What |
| --- | --- | --- |
| `SSARecomp.log` (`.prev.log`: the run before) | the game runtime | its own log: version, OS, CPU, GPU, settings, `[perf]` |
| `skylauncher-game.log` (`.prev.log`) | the launcher | the runtime's stdout and stderr, from its first byte: early crashes, a missing DLL's message |
| `<game folder>/install.log` | the launcher | what SSABuild printed during the install (docs/install.md) |

Settings > Game data > Support: **Copy game log** (the end of `SSARecomp.log`, then of `skylauncher-game.log`, each under a
heading) and **Open logs folder**. `SKYLAUNCHER_LOG_DIR` replaces the folder (tests).

## The launcher stays while the game runs

`src/main.cpp` calls `RunGame` (`launcher/game_run.cpp`): the runtime starts with its output piped to
`skylauncher-game.log`, and the launcher (no window) waits for it. On Windows the process is started without a console
window and its exit code is read from a handle of our own, since SDL reports none for such processes.

If the runtime exits with a non-zero code a dialog says **The game stopped unexpectedly**, with its exit code and the logs
folder, and three buttons: **Report** (copies the log, opens a new GitHub issue with a template; paste the log into it),
**Copy log**, **Close**. Nothing is sent by the launcher.

## Test the portal

Settings > Controls > Portal of Power. The button runs `<runtime> --portal-probe` (the game, so it needs to be installed)
and reads its stdout, at most 15 s:

```
{"kind":"hid","vid":"1430","pid":"0150","product":"...","opened":true,"status":"ok"}
{"done":true,"count":1}
```

One flat JSON object per line (`kind` `hid` or `usb`, `opened`, `status` `ok` or `error: <why>`; `name` is read as `product`
too), then the `done` line; anything else on stdout is ignored. The card lists each portal ("Found" / "Found, not usable"
with the reason) and says what the game will do given the mode: real portal, virtual portal, or waiting for one. No
portal: "No portal found: the game will use the virtual portal". A runtime without `--portal-probe` exits with code 2: "This
version of the game doesn't have the portal test yet". Parser and tests: `ParsePortalProbe`, `tests/shell_test.cpp`.

Portal mode is `[portal] mode` = `auto`, `emulated` (Virtual) or `real`; the launcher passes it as `SSA_PORTAL`. Older
`hid` and `usb` values still work and show as "Real portal".

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

Settings > Game data > Support: **Copy game log** (the end of `SSARecomp.log`, then of `skylauncher-game.log`, each under a
heading) and **Open logs folder** (`logs/`). `SKYLAUNCHER_LOG_DIR` replaces both log folders (tests).

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

Transport lines (`{"transport":"usb","available":false,"status":"error: ..."}`) become a note under the result. One flat JSON object per line (`kind` `hid` or `usb`, `opened`, `status` `ok` or `error: <why>`; `name` is read as `product`
too), then the `done` line; anything else on stdout is ignored. The card lists each portal ("Found" / "Found, not usable"
with the reason) and says what the game will do given the mode: real portal, virtual portal, or waiting for one. No
portal: "No portal found: the game will use the virtual portal". Each portal line of a newer runtime also carries `id`, `model`, `verdict` and `verdict_reason` (the portal's answer to the game's
reset command: "Model: Spyro's Adventure portal (Xbox 360) (id 01 28)", then "Spyro's Adventure accepts this portal." or, in red, why
the game would reject or warn about it; the contract is the runtime's `docs/portal_probe.md`). A runtime without `--portal-probe` exits with code 2: "This
version of the game doesn't have the portal test yet". Parser and tests: `ParsePortalProbe`, `tests/shell_test.cpp`.

Portal mode is `[portal] mode` = `auto`, `emulated` (Virtual) or `real`; the launcher passes it as `SSA_PORTAL`. Older
`hid` and `usb` values still work and show as "Real portal".

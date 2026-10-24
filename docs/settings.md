# Settings file

The launcher and the game runtime share one settings file, `launcher.ini`, in the SDL preferences folder of the app
`SSARecomp` (`~/Library/Application Support/SSARecomp/` on macOS, `%APPDATA%\SSARecomp\` on Windows,
`~/.local/share/SSARecomp/` on Linux). Lines are `[section]`, `key=value` or `# comment`.

"Runtime" says whether the game runtime applies the key (checked against SSARecomp's `docs/settings.md`; the game's own log is in `docs/support.md`). Keys it does not know are kept and ignored.

| Section | Key | Values | Meaning | Runtime |
| --- | --- | --- | --- | --- |
| `launcher` | `fullscreen` | `0`, `1` | The launcher window itself | n/a |
| `launcher` | `ui_scale` | `auto`, `0.75` to `3` | The launcher's size: `auto` follows the display's scale (Windows 150 %, 200 %...); Ctrl + / Ctrl - / Ctrl 0 change it | n/a |
| `launcher` | `runtime` | path | The game runtime to start, if not next to the launcher | n/a |
| `launcher` | `kit` | folder | The player kit that installs the game, if not next to the launcher (`docs/install.md`) | n/a |
| `update` | `auto_check` | `0`, `1` | Look for a new Skylauncher once a day at start (default 1; `docs/updates.md`) | n/a |
| `update` | `last_check` | epoch seconds | When the last check ran | n/a |
| `update` | `repo` | `owner/name` | Where Skylauncher's releases are (default `mflr0/Skylauncher`) | n/a |
| `update` | `kit_repo` | `owner/name` | Where the player kit's releases are; empty: no kit updates | n/a |
| `update` | `kit_tag` | text | The release of the installed kit, written after a kit update | n/a |
| `install.<sha256>` | `runtime` | path | The game the launcher built for the disc whose `default.xex` has this SHA-256 | n/a |
| `install.<sha256>` | `kit` | text | The kit version it was built with; another kit offers "Update the game" | n/a |
| `install.<sha256>` | `state`, `pid` | `running`, `failed`; process id | An install that runs or stopped: shown again after a restart | n/a |
| `ssa` | `iso` | path | The verified disc image | yes |
| `ssa` | `language` | `en` `fr` `de` `es` `it` `nl` `sv` `no` `da` `fi` | Text language | yes |
| `ssa` | `voice_language` | same codes | Voice language | yes |
| `video` | `fullscreen` | `0`, `1` | Play in fullscreen | yes |
| `video` | `resolution_scale` | `auto` (default), `1` to `10` | 3D rendered at this multiple of 1120x704; `auto` picks x1 on integrated GPUs | yes |
| `video` | `fps_limit` | `30`, `60` | Frame rate cap (the game is made for 30; 60 is experimental). Other values the runtime reads as 60 | yes |
| `video` | `vsync` | `0`, `1` | Wait for the display's refresh | yes |
| `video` | `anisotropy` | `1`, `2`, `4`, `8`, `16` | Texture filtering | yes |
| `input` | `device` | `auto`, `keyboard`, `gamepad` | What the game listens to | yes |
| `input` | `gamepad` | SDL GUID | The gamepad chosen when `device=gamepad` | yes |
| `input` | `key_<action>` | SDL key name (`Space`, `Return`, `A`...) | Keyboard binding | yes |
| `input` | `glyphs` | `auto` (default), `xbox360`, `xboxone`, `playstation`, `nintendo`, `keyboard` (`xbox` is read as Xbox One / Series) | The button pictures the game shows | yes |
| `portal` | `mode` | `auto` (default), `emulated` (Virtual), `real` (`hid` and `usb` still work) | Portal of Power backend | yes (`SSA_PORTAL`) |
| `portal` | `place` | figure file names, comma separated | Figures put on the emulated portal at start | yes (`SSA_PORTAL_PLACE`) |
| `collection` | `favourites` | figure file names, comma separated | The collection's favourites | launcher only |

Keyboard actions: `a`, `b`, `x`, `y`, `start`, `back`, `lb`, `rb`, `lt`, `rt`, `left_up`, `left_down`, `left_left`,
`left_right` (movement), `right_up`, `right_down`, `right_left`, `right_right` (camera), `dpad_up`, `dpad_down`,
`dpad_left`, `dpad_right`. Defaults: Space, X, C, V, Return, Backspace, Q, E, 1, 3, W, S, A, D, I, K, J, L and the arrow keys.

## Figures

The collection and the game share the `figures` folder next to `launcher.ini`. A figure is a 1 KB tag dump (`.sky`,
`.bin` or `.dmp`); `src/shared/figures/figure_file.h` describes the layout the launcher reads and writes.

- Before every change (rename, reset, restore, delete) the launcher copies the figure to
  `figures/backups/<figure name>/<date>.sky`.
- While a figure is on the game's portal, the runtime should keep a lock next to it: `figures/<file>.lock`, holding the
  runtime's process id as text, removed when the figure leaves the portal or the game exits. The collection shows such a
  figure as "in use" and changes nothing in it. A lock whose process isn't running is ignored, so a crash never leaves a
  figure stuck. The runtime doesn't write these locks yet.

When it starts the game, the launcher also passes `portal` and the languages through the environment variables in
brackets above, which the runtime already reads.

# Settings file

The launcher and the game runtime share one settings file, `launcher.ini`, in the SDL preferences folder of the app
`SSARecomp` (`~/Library/Application Support/SSARecomp/` on macOS, `%APPDATA%\SSARecomp\` on Windows,
`~/.local/share/SSARecomp/` on Linux). Lines are `[section]`, `key=value` or `# comment`.

"Runtime" says whether the game runtime applies the key yet. Keys it does not know are kept and ignored.

| Section | Key | Values | Meaning | Runtime |
| --- | --- | --- | --- | --- |
| `launcher` | `fullscreen` | `0`, `1` | The launcher window itself | n/a |
| `launcher` | `ui_scale` | `auto`, `0.75` to `3` | The launcher's size: `auto` follows the display's scale (Windows 150 %, 200 %...); Ctrl + / Ctrl - / Ctrl 0 change it | n/a |
| `launcher` | `runtime` | path | The game runtime to start, if not next to the launcher | n/a |
| `launcher` | `kit` | folder | The player kit that installs the game, if not next to the launcher (`docs/install.md`) | n/a |
| `install.<sha256>` | `runtime` | path | The game the launcher built for the disc whose `default.xex` has this SHA-256 | n/a |
| `install.<sha256>` | `kit` | text | The kit version it was built with; another kit offers "Update the game" | n/a |
| `ssa` | `iso` | path | The verified disc image | yes |
| `ssa` | `language` | `en` `fr` `de` `es` `it` `nl` `sv` `no` `da` `fi` | Text language | yes |
| `ssa` | `voice_language` | same codes | Voice language | yes |
| `video` | `fullscreen` | `0`, `1` | Play in fullscreen | pending |
| `video` | `resolution_scale` | `1` to `10` | 3D rendered at this multiple of 1120x704 | pending |
| `video` | `fps_limit` | `30`, `60`, `120`, `144`, `240`, `0` (none) | Frame rate cap (the game is made for 30) | pending |
| `video` | `vsync` | `0`, `1` | Wait for the display's refresh | pending |
| `video` | `anisotropy` | `1`, `2`, `4`, `8`, `16` | Texture filtering | pending |
| `input` | `device` | `auto`, `keyboard`, `gamepad` | What the game listens to | pending |
| `input` | `gamepad` | SDL GUID | The gamepad chosen when `device=gamepad` | pending |
| `input` | `key_<action>` | SDL key name (`Space`, `Return`, `A`...) | Keyboard binding | pending |
| `portal` | `mode` | `auto`, `emulated`, `hid`, `usb` | Portal of Power backend | yes (`SSA_PORTAL`) |
| `portal` | `place` | figure file names, comma separated | Figures put on the emulated portal at start | yes (`SSA_PORTAL_PLACE`) |
| `collection` | `favourites` | figure file names, comma separated | Old favourites; moved once into the shared collection's `collection.json` | read once (see `collection_ui.md`) |
| `collection` | `moved` | `1` | Set once the old favourites and `[portal] place` were moved | launcher only |

Keyboard actions: `a`, `b`, `x`, `y`, `start`, `back`, `lb`, `rb`, `lt`, `rt`, `left_up`, `left_down`, `left_left`,
`left_right` (movement), `right_up`, `right_down`, `right_left`, `right_right` (camera), `dpad_up`, `dpad_down`,
`dpad_left`, `dpad_right`. Defaults: Space, X, C, V, Return, Backspace, Q, E, 1, 3, W, S, A, D, I, K, J, L and the arrow keys.

## Figures

The launcher and the game share one collection folder (`collection::Folder()`: `Skylanders/Collection` in the user's data
folder, or `SSA_COLLECTION`), with `collection.json` (labels, favourites, views, trash), `<dump>.lock` (JSON written by
the game while a figure is on its portal), `backups/` and `trash/`. `docs/collection_ui.md` describes the page; the
format is SSARecomp's `docs/collection.md`. The old `figures` folder next to `launcher.ini` is copied in once, never
changed.

- The seats of the next session are `portal.txt` in the game's save folder (`SSA_SAVE_DIR`, else its preference
  folder): the launcher writes it, the game reads it at start. `[portal] place` is no longer written (it would override
  the session file through `SSA_PORTAL_PLACE`); `[portal] mode` still goes through `SSA_PORTAL`.
- Before every change the library backs the figure up (newest 10 kept); a figure whose lock belongs to another live
  process is never written.

When it starts the game, the launcher also passes `portal` and the languages through the environment variables in
brackets above, which the runtime already reads.

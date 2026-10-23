# Settings file

The launcher and the game runtime share one settings file, `launcher.ini`, in the SDL preferences folder of the app
`SSARecomp` (`~/Library/Application Support/SSARecomp/` on macOS, `%APPDATA%\SSARecomp\` on Windows,
`~/.local/share/SSARecomp/` on Linux). Lines are `[section]`, `key=value` or `# comment`.

"Runtime" says whether the game runtime applies the key yet. Keys it does not know are kept and ignored.

| Section | Key | Values | Meaning | Runtime |
| --- | --- | --- | --- | --- |
| `launcher` | `fullscreen` | `0`, `1` | The launcher window itself | n/a |
| `launcher` | `runtime` | path | The game runtime to start, if not next to the launcher | n/a |
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

Keyboard actions: `a`, `b`, `x`, `y`, `start`, `back`, `lb`, `rb`, `lt`, `rt`, `left_up`, `left_down`, `left_left`,
`left_right` (movement), `right_up`, `right_down`, `right_left`, `right_right` (camera), `dpad_up`, `dpad_down`,
`dpad_left`, `dpad_right`. Defaults: Space, X, C, V, Return, Backspace, Q, E, 1, 3, W, S, A, D, I, K, J, L and the arrow keys.

When it starts the game, the launcher also passes `portal` and the languages through the environment variables in
brackets above, which the runtime already reads.

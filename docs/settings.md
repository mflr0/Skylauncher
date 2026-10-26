# Settings file

The launcher and the game runtime share one settings file, `launcher.ini`, at the root of the "Skylanders" data folder
(`~/Library/Application Support/Skylanders/` on macOS, `%APPDATA%\Skylanders\` on Windows, `~/.local/share/Skylanders/` on
Linux; docs/install.md). Lines are `[section]`, `key=value` or `# comment`.

"Runtime" says whether the game runtime applies the key (checked against SSARecomp's `docs/settings.md`; the game's own log is in `docs/support.md`). Keys it does not know are kept and ignored.

| Section | Key | Values | Meaning | Runtime |
| --- | --- | --- | --- | --- |
| `launcher` | `fullscreen` | `0`, `1` | The launcher window itself | n/a |
| `launcher` | `ui_scale` | `auto`, `0.75` to `3` | The launcher's size: `auto` follows the display's scale (Windows 150 %, 200 %...); Ctrl + / Ctrl - / Ctrl 0 change it | n/a |
| `launcher` | `language` | empty (the system's language), `game`, or a code (`en`, `fr`...) | The language of the launcher's own text (`docs/translations.md`): `game` follows the shown game's `language`; a code that has no file in `resources/lang` shows English. The environment variable `SKYLAUNCHER_LANGUAGE` overrides it (tests, screenshots) | n/a |
| `launcher` | `reduce_motion` | `auto`, `0`, `1` | The logo's fall on the game page (`docs/launcher_ui.md`): `auto` follows the system (macOS Reduce motion, Windows animation effects), `1` never animates, `0` always does | n/a |
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
| `video` | `fullscreen` | `0`, `1` | Play in fullscreen | yes (the key is per game: see "Per-game video" below) |
| `video` | `resolution_scale` | `auto` (default), `1` to `10` | 3D rendered at this multiple of 1120x704; `auto` picks x1 on integrated GPUs | yes |
| `video` | `fps_limit` | `30`, `60` | Frame rate cap (the game is made for 30; 60 is experimental). Other values the runtime reads as 60 | yes |
| `video` | `skip_intro_movies` | `0`, `1` | Skip the start-up logos (legal, ATVI, XPEC, Toys for Bob); the story movies still play | yes |
| `video` | `vsync` | `0`, `1` | Wait for the display's refresh | yes |
| `video` | `anisotropy` | `1`, `2`, `4`, `8`, `16` | Texture filtering | yes |
| `video` | `aspect` | `auto` (default), `16:9` | Widescreen: `auto` shows more to the sides on a window wider than 16:9, `16:9` keeps the game's picture with bars | yes |
| `video` | `texture_packs` | `1` (default), `0` | Use the HD textures of the game's `textures/` folder (<game> settings > Video > Show in folder) | yes |
| `video` | `texture_dump` | `0` (default), `1` | Write the game's textures as DDS files, for pack makers (<game> settings > Video > Advanced) | yes |
| `input` | `device` | `auto`, `keyboard`, `gamepad` | What the game listens to | yes |
| `input` | `gamepad` | SDL GUID | The gamepad chosen when `device=gamepad` | yes |
| `input` | `key_<action>` | SDL key name (`Space`, `Return`, `A`...) | Keyboard binding | yes |
| `input` | `glyphs` | `auto` (default), `xbox360`, `xboxone`, `playstation`, `nintendo`, `keyboard` (`xbox` is read as Xbox One / Series) | The button pictures the game shows | yes |
| `portal` | `mode` | `auto` (default), `emulated` (Virtual), `real` (`hid` and `usb` still work) | Portal of Power backend | yes (`SSA_PORTAL`) |
| `portal` | `toybox_auto_open` | `1` (default), `0` | The Toy Box opens by itself when the game asks for a figure | no: the game reads `launcher.ini` itself (Settings > Controls > Portal of Power) |
| `portal` | `place` | figure file names, comma separated | Figures put on the emulated portal at start | yes (`SSA_PORTAL_PLACE`) |
| `collection` | `favourites` | figure file names, comma separated | Old favourites; moved once into the shared collection's `collection.json` | read once (see `collection_ui.md`) |
| `collection` | `moved` | `1` | Set once the old favourites and `[portal] place` were moved | launcher only |

## Per-game video

The runtime reads one `[video]` section, but the two games do not have to share it. Each game keeps its own copy of the
nine keys that belong to a game (`fullscreen`, `resolution_scale`, `fps_limit`, `skip_intro_movies`, `vsync`,
`anisotropy`, `aspect`, `texture_packs`, `texture_dump`) in **`[ssa.video]`** and **`[giants.video]`**:

| Section | Key | Meaning |
| --- | --- | --- |
| `<game>.video` | the nine `video` keys above | The game's own video settings. The game's settings page (Video tab) edits this copy. |

- The first time, a game's copy starts from the `[video]` the player already had (`SeedGameVideo`).
- **PLAY** puts the copy of the game that starts into `[video]` (keys it lacks are dropped, so the runtime's defaults apply):
  the runtime needs no change (`ApplyGameVideo`).
- When the game ends, what is in `[video]` (the player may have changed it in the game's own Settings menu) becomes that
  game's copy again (`StoreGameVideo`), so the other game never sees it.
- What both games share stays in its own section: `[input]` (device, keys, glyphs), `[portal]`, and the launcher's
  `[launcher]` and `[update]`. They are in the gear's Settings, not on a game page.

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

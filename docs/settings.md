# Settings file

The launcher and the game runtime share one settings file, `launcher.ini`, at the root of the "Skylanders" data folder
(`~/Library/Application Support/Skylanders/` on macOS, `%APPDATA%\Skylanders\` on Windows, `~/.local/share/Skylanders/` on
Linux; docs/install.md). Lines are `[section]`, `key=value` or `# comment`.

"Runtime" says whether the game runtime applies the key (checked against SSARecomp's `docs/settings.md`; the game's own log is in `docs/support.md`). Keys it does not know are kept and ignored.

| Section | Key | Values | Meaning | Runtime |
| --- | --- | --- | --- | --- |
| `launcher` | `fullscreen` | `0`, `1` | The launcher window itself | n/a |
| `launcher` | `ui_scale` | `auto`, `0.75` to `3` | The launcher's size: `auto` follows the display's scale (Windows 150 %, 200 %...); Ctrl + / Ctrl - / Ctrl 0 change it | n/a |
| `launcher` | `language` | empty (the system's language), or a code (`en`, `fr`...) | The language of the launcher's own text, never the game's (`docs/translations.md`): a code that has no file in `resources/lang` shows English. An old `game` (follow the shown game) is read as empty and erased at the next save. The environment variable `SKY_LAUNCHER_LANGUAGE` overrides it (tests, screenshots) | n/a |
| `launcher` | `reduce_motion` | `auto`, `0`, `1` | The logo's fall on the game page (`docs/launcher_ui.md`): `auto` follows the system (macOS Reduce motion, Windows animation effects), `1` never animates, `0` always does | n/a |
| `launcher` | `runtime` | path | The game runtime to start, if not next to the launcher | n/a |
| `launcher` | `kit` | folder | The player kit that installs the game, if not next to the launcher (`docs/install.md`) | n/a |
| `update` | `auto_check` | `0`, `1` | Look for a new Arkeyan Launcher and for game updates at every start (default 1; `docs/updates.md`) | n/a |
| `update` | `last_check` | epoch seconds | When the last check ran | n/a |
| `update` | `repo` | `owner/name` | Where Arkeyan Launcher's releases are (default `mflr0/Skylauncher`) | n/a |
| `update` | `kit_repo` | `owner/name` | Where the player kit's releases, and the games' versions in their notes, are; empty: no game updates | n/a |
| `update` | `kit_tag` | text | The release of the installed kit, written after a kit download | n/a |
| `update` | `kit_fetched` | release tag | The release whose kit INSTALL or UPDATE fetched: not asked again for it (docs/updates.md) | n/a |
| `install.<sha256>` | `runtime` | path | The game the launcher built for the disc whose `default.xex` has this SHA-256 | n/a |
| `install.<sha256>` | `kit` | text | The kit version it was built with | n/a |
| `install.<sha256>` | `version` | `0.8.2` | The game's version when it was built (from the kit's `kit.txt`); UPDATE is offered when the release's games line has a higher one | n/a |
| `install.<sha256>` | `state`, `pid` | `running`, `failed`; process id | An install that runs or stopped: shown again after a restart | n/a |
| `ssa` | `iso` | path | The verified disc image | yes |
| `ssa` | `language` | `en` `fr` `de` `es` `it` `nl` `sv` `no` `da` `fi` | Text language | yes |
| `ssa` | `voice_language` | same codes | Voice language | yes |
| `giants` | `language`, `voice_language` | `en` `fr` `de` `es` `es-419` `it` `nl` `pt` `sv` `no` `da` `fi` | Text and voice language (the disc's twelve); `voice_language` empty or absent: the voices follow the text | yes |
| `swapforce` | `language`, `voice_language` | `en` `fr` (USA, Australia disc) or `es` `it` (Europe disc) | Text and voice language (the disc's two) | yes |
| each game | `disc_text_languages`, `disc_voice_languages` | codes joined by commas | Written by the launcher at each disc check: the languages the disc holds, which the Language tab lists (absent: the full list). Not for editing | no |
| `video` | `fullscreen` | `0`, `1` | Play in fullscreen | yes (the key is per game: see "Per-game video" below) |
| `video` | `resolution_scale` | `auto` (default), `1` to `10` | 3D rendered at this multiple of 1120x704; `auto` picks x1 on integrated GPUs | yes |
| `video` | `fps_limit` | `30`, `60` | Frame rate cap (the game is made for 30; 60 is experimental; a computer that cannot hold 60 gets a steady 30). Other values the runtime reads as 60 | yes |
| `video` | `skip_intro_movies` | `0`, `1` | Skip the start-up logos (legal, ATVI, XPEC, Toys for Bob); the story movies still play | yes |
| `video` | `vsync` | `0`, `1` | Wait for the display's refresh | yes |
| `video` | `anisotropy` | `1`, `2`, `4`, `8`, `16` | Texture filtering | yes |
| `video` | `aspect` | `auto` (default), `16:9` | Widescreen: `auto` shows more to the sides on a window wider than 16:9, `16:9` keeps the game's picture with bars | yes |
| `video` | `texture_packs` | `1` (default), `0` | Use the HD textures of the game's `textures/` folder (<game> settings > Video > Show in folder) | yes |
| `video` | `texture_dump` | `0` (default), `1` | Write the game's textures as DDS files, for pack makers (<game> settings > Video > Advanced) | yes |
| `video` | `original_resolution` | `0` (default), `1` | Swap Force only: keep the console's 3D image (1040x620) stretched to the screen instead of the full frame (<game> settings > Video > Original resolution) | yes |
| `input` | `device` | `auto`, `keyboard`, `gamepad` | What the game listens to | yes |
| `input` | `gamepad` | SDL GUID | The gamepad chosen when `device=gamepad` | yes |
| `input` | `key_<action>` | SDL key name (`Space`, `Return`, `A`...) | Keyboard binding | yes |
| `input` | `glyphs` | `auto` (default), `xbox360`, `xboxone`, `playstation`, `nintendo`, `keyboard` (`xbox` is read as Xbox One / Series) | The button pictures the game shows | yes |
| `portal` | `mode` | `auto` (default), `emulated` (Virtual), `real` (`hid` and `usb` still work) | Portal of Power backend | yes (`SKY_PORTAL`) |
| `portal` | `toybox_auto_open` | `1` (default), `0` | The Toy Box opens by itself when the game asks for a figure | no: the game reads `launcher.ini` itself (Settings > Controls > Portal of Power) |
| `portal` | `place` | figure file names, comma separated | Figures put on the emulated portal at start | yes (`SKY_PORTAL_PLACE`) |
| `collection` | `favourites` | figure file names, comma separated | Old favourites; moved once into the shared collection's `collection.json` | read once (see `collection_ui.md`) |
| `collection` | `moved` | `1` | Set once the old favourites and `[portal] place` were moved | launcher only |

Removing the settings: Settings > Launcher > Uninstall everything deletes `launcher.ini` with everything else (`docs/install.md`, "Uninstall everything");
the launcher then writes no settings until it is started again.

## Per-game video

The runtime reads one `[video]` section, but the games do not have to share it. Each game keeps its own copy of the
ten keys that belong to a game (`fullscreen`, `resolution_scale`, `fps_limit`, `skip_intro_movies`, `vsync`,
`anisotropy`, `aspect`, `texture_packs`, `texture_dump`, `original_resolution`) in **`[ssa.video]`**, **`[giants.video]`** and **`[swapforce.video]`**:

| Section | Key | Meaning |
| --- | --- | --- |
| `<game>.video` | the ten `video` keys above | The game's own video settings. The game's settings page (Video tab) edits this copy. |

- The first time, a game's copy starts from the `[video]` the player already had (`SeedGameVideo`).
- **PLAY** puts the copy of the game that starts into `[video]` (keys it lacks are dropped, so the runtime's defaults apply):
  the runtime needs no change (`ApplyGameVideo`).
- The runtime also reads the game's own `[<game>.video]` after `[video]` and lets it win for every key it has, so a game
  started without PLAY (its own icon) gets what its page shows too. Two options a player turns on, `skip_intro_movies`
  and `fps_limit=60`, travel this way; `tests/settings_test.cpp` follows them from the page's write to the file the game
  parses. With 60 FPS on, a computer that cannot hold 60 gets a steady 30 FPS (the game says "30 FPS: 60 not held" once
  per run) and goes back to 60 when it holds.
- A game's copy that is still empty (a page only looked at it) is seeded from the `[video]` the player already had, like a
  missing one (`SeedGameVideo`).
- When the game ends, what is in `[video]` (the player may have changed it in the game's own Settings menu) becomes that
  game's copy again (`StoreGameVideo`), so the other game never sees it.
- What the games share stays in its own section: `[input]` (device, glyphs), `[portal]`, and the launcher's
  `[launcher]` and `[update]`. They are in the gear's Settings, not on a game page. The keyboard keys are per game, below.

## Per-game keyboard keys

The runtime reads one `[input]` section (`key_<action>=<SDL key name>`), but the keys are each game's own: **`[ssa.input]`**,
**`[giants.input]`** and **`[swapforce.input]`** hold a game's `key_<action>` lines, edited on the game's settings page
(Keyboard tab). Only the keys are per game: `device`, `gamepad` and `glyphs` stay in `[input]`.

| Section | Key | Meaning |
| --- | --- | --- |
| `<game>.input` | `key_<action>` | The key (an SDL key name) the game's copy gives to an action. Absent: the game's default key. |
| `<game>.input` | `seeded` | `1` once the copy has been made from `[input]` (an empty copy is not written to the file, so this says it was). |

- The first time (`SeedGameInput`, when the launcher starts), every game's copy starts from the `key_` lines the player
  already had in `[input]`, so nobody loses a binding. It never happens again for that game.
- **PLAY** puts the game's keys in `[input]` (`ApplyGameInput`): the keys it has set; for the actions on its page, its default
  where none is set; the other `key_` lines follow the copy, or are dropped so the runtime's own default applies. The runtime
  needs no change. Nothing is stored back (the runtime never changes keys).
- **Reset to defaults** empties the game's copy (every `key_` line of it); the defaults then apply at PLAY.
- Two rows of one game on the same key are marked (a conflict is only possible inside a game).

Keyboard actions the runtime knows: `a`, `b`, `x`, `y`, `start`, `back`, `lb`, `rb`, `lt`, `rt`, `left_up`, `left_down`,
`left_left`, `left_right` (movement), `right_up`, `right_down`, `right_left`, `right_right` (right stick: Spyro's Adventure's Shake / Insert / Swing), `dpad_up`, `dpad_down`,
`dpad_left`, `dpad_right`. Defaults (all three games): A Space, B X, X C, Y V, Start Return, Back Backspace, W S A D for the
left stick, I K J L for the right stick, the arrow keys for the D-pad.

### What each game's Keyboard tab lists (`src/launcher/game_keys.cpp`)

A row names what the key does **in that game, in the game's own words**, with the pad button in brackets ("Attack 1 (X)";
the sticks in the player's language: "(stick gauche)"). Only the actions the game uses have a row; the other actions keep
whatever the copy was seeded with (the runtime's default otherwise). The names are English; the French is the disc's own
wording (`resources/lang/fr.txt`); the other launcher languages show English until translated. A key's name is shown in the
player's language where the launcher has one ("Espace", "Entrée", "Retour arrière", "Échap", "Flèche haut"); the stored value
stays SDL's English name. The sources are each game's disc, read in the running game; nothing from it is in the repository (the
names are short words, written in the table).

Verified [V] (2026-10-28) by running each game (keyboard device, `SKY_PRESS`, an emulated portal with a figure) and reading its
own Controls screen (pause menu: Options > Controls in Spyro's Adventure and Swap Force, Settings > Controls in Giants), in
English and, for Spyro's Adventure, French. The Back button was pressed in play in all three.

| Game | Rows (pad button) | Proof |
| --- | --- | --- |
| Spyro's Adventure | Move up, down, left, right (L stick); **1st Power / Accept** (A), **2nd Power** (X), **Use / Talk** (Y), **Special Power / Back** (B), **Skylander Info** (Back), **Pause** (Start); **Shake / Insert / Swing** up, down, left, right (R stick; the last three rows read "Same") | The Controls screen: A "1st Power/Accept", X "2nd Power", Y "Use/Talk", B "Special Power/Back", Back "Skylander Info", Start "Pause", left stick "Move Character/Menu Navigation", D-pad "Menu Navigation", right stick "Shake/Insert/Swing" (French "1er pouvoir/Accepter", "2e pouvoir", "Utiliser/Parler", "Pouvoir spécial/Retour", "Infos du Skylander", "Agiter/Insérer/Balancer"). Back in play opens the "Skylander Info" screen. **No camera**: the screen has no camera label |
| Giants | Move up, down, left, right (L stick); **Attack 1 / Accept** (A), **Attack 2** (X), **Action** (Y), **Attack 3 / Back** (B), **Skylander Info** (Back), **Pause** (Start) | The Controls screen (Settings > Controls): A "Attack 1", X "Attack 2", Y "Action", B "Attack 3", Back "Skylander Info", Start "Pause", left stick "Move"; "Accept" and "Back" from the menus' legend (A "Accept", B "Back"). Back in play opens "Skylander Info" (Stats, Hats, Upgrades, Quests, Manage). No right stick on the screen: no camera rows |
| Swap Force | Move up, down, left, right (L stick); **Jump** (A), **Attack 1** (X), **Attack 2** (B), **Attack 3** (Y), **Attack 2** (RB), **Attack 3** (LB), **Interact** (LT), **Interact** (RT), **Skylanders Stats** (Back), **Pause** (Start) | The Controls screen (Options > Controls): A "Jump", X "Attack 1", B "Attack 2", Y "Attack 3", the right shoulder "Attack 2", the left shoulder "Attack 3", both triggers "Interact", Back "Skylanders Stats", Start "Pause", left stick "Move" (French "Sauter", "Attaque 1/2/3", "Interagir", "Stats Skylanders"). Back in play opens the Skylander screen (Stats, Hats, Upgrades, Quests). The pause legend shows B as "Back" and Y as "Leaderboard". No right stick on the screen: no camera rows |

**Not settled [?]**: the Controls screens are the proof for every row, and the Back button was pressed in play; the other
buttons were not seen doing their action. In particular: the shoulders and triggers of Swap Force (`SKY_PRESS` has no `lb`,
`rb`, `lt`, `rt`, so they could not be pressed; the diagram is the proof); the direction each way of Spyro's Adventure's right
stick ("Shake/Insert/Swing") takes (`SKY_PRESS` has no right stick); and whether Swap Force or Giants moves a camera with the
right stick at all (their screens show none, so there are no rows; the seeded keys stay in `[input]`). Giants' French "Infos
Skylander" and Spyro's Adventure's "Infos du Skylander" share one English key ("Skylander Info"): the launcher shows the second.
On the French pad names, the Back button reads "Retour" (the Xbox 360 French name), as the action "Retour" of the B button.

## Figures

The launcher and the game share one collection folder (`collection::Folder()`: `Skylanders/Collection` in the user's data
folder, or `SKY_COLLECTION`), with `collection.json` (labels, favourites, views, trash), `<dump>.lock` (JSON written by
the game while a figure is on its portal), `backups/` and `trash/`. `docs/collection_ui.md` describes the page; the
format is SSARecomp's `docs/collection.md`. The old `figures` folder next to `launcher.ini` is copied in once, never
changed.

- The seats of the next session are `portal.txt` in the game's save folder (`SKY_SAVE_DIR`, else its preference
  folder): the launcher writes it, the game reads it at start. `[portal] place` is no longer written (it would override
  the session file through `SKY_PORTAL_PLACE`); `[portal] mode` still goes through `SKY_PORTAL`.
- Before every change the library backs the figure up (newest 10 kept); a figure whose lock belongs to another live
  process is never written.

When it starts the game, the launcher also passes `portal` and the languages through the environment variables in
brackets above, which the runtime already reads.

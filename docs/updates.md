# Updates

Arkeyan Launcher updates itself from GitHub releases (`src/launcher/updater.{h,cpp}`, the window side in `ui/update_page.cpp`).
No telemetry: the only request is `GET https://api.github.com/repos/<repo>/releases/latest`, then the asset.

## Flow

1. **Check**: at **every** start (the launcher's release and the games' versions, one request each, `[update] last_check` keeps
   the time), unless `[update] auto_check=0`; and on demand in Settings > Launcher > Check now. `SKY_LAUNCHER_NO_UPDATE_CHECK`
   turns the start-up check off. **Offline, or any failure, is silent**: nothing is shown and nothing is asked again until the
   next start.
2. The tag (`vX.Y.Z`) is compared, as semver, with the built version (`LAUNCHER_VERSION`, the CMake cache variable the CI
   sets from the tag or run number, shown in About). Pre-releases and tags that aren't versions are never offered.
3. A **banner** above the page (it pushes the page down 56 px, nothing overlaps): "Update available: vX.Y.Z", **Update now**,
   **What's new** (the release's notes, as written, and its page), **Later** (until the next start).
4. **Update now**: the asset for this OS is downloaded next to the running program (`.skylauncher-update/`), its SHA-256 is
   checked against the release's `SHA256SUMS` asset, then it is swapped in. A release **without** `SHA256SUMS`, or a hash
   that doesn't match, is refused and deleted: nothing is installed.
5. The banner then offers **Restart now**.

| OS | Asset | Swap |
| --- | --- | --- |
| Windows | `ArkeyanLauncher.exe` | the running exe is renamed to `<its name>.old`, the new one moved into place, the old one deleted at the next start |
| macOS | `ArkeyanLauncher-macOS.zip` (the `.app`) | the `.app` is replaced if its folder is writable (`.old` kept until the next start); otherwise `ArkeyanLauncher-macOS.dmg` is downloaded, verified and opened |
| Linux | `ArkeyanLauncher-x86_64.AppImage` | the AppImage (`$APPIMAGE`) is replaced the same way; not an AppImage: the release page is opened |

The program is replaced **where it is, under the name the player gave it**: an install called `Skylauncher.exe`,
`Skylauncher.app` or `Skylauncher-x86_64.AppImage` (made by a launcher before 0.6.0) keeps that name after the update, and
shows "Arkeyan Launcher" everywhere it draws its name. On macOS the new bundle is found in the zip whatever its name.

If the second move fails the first is undone (`ReplaceInPlace`), so a failed update leaves the program as it was. A copy that
isn't installed (a build folder) opens the release page instead.

### Two sets of names (0.6.0)

Launchers before 0.6.0 ask for `Skylauncher.exe`, `Skylauncher-macOS.zip` (holding a bundle named `Skylauncher.app`),
`Skylauncher-macOS.dmg`, `Skylauncher-x86_64.AppImage` and `ssa-kit-<platform>.zip`; the updater of the old launcher is part of
the old program and cannot be changed. So each release carries **both sets, the same files under two names**, all in
`SHA256SUMS`: the new names (`ArkeyanLauncher*`, `skylanders-kit-<platform>.zip`) and the old ones. A 0.6.0 and later launcher
asks for the new name and falls back to the old one (`LauncherAssetIn`, `KitAssetIn`), so it also updates from a release that
has only one of the two. The old `Skylauncher-macOS.zip` holds `Skylauncher.app` (the same program; `Contents/MacOS/Skylauncher`
points to `ArkeyanLauncher`, so that a desktop shortcut made by an old launcher, which runs that path, keeps working).
`tools/package_macos.sh` makes both bundles. The old names stay published until the 0.4.x launchers are gone; drop them from
`.github/workflows/build.yml` and `tools/release_kit.sh` then.

A kit root holds `SkyBuild` (and a copy named `SSABuild` for 0.4.x launchers); the launcher accepts either, finds the kit folder
named `skylanders-kit*`, `ssa-kit*` or `kit`, and keeps reading the builder's older files (`ssabuild-work/`, `ssabuild-ready.txt`).

### Desktop shortcuts

A shortcut made by a launcher before 0.6.0 keeps working after the update. At every start the launcher rewrites the shortcuts of
its games that still name the old program (macOS `.app` script, Linux `.desktop` file; the icon and the rest are kept) so that they
start this executable and carry the new name (`RefreshShortcuts`). A Windows `.lnk` points at the exe file, whose name an update
keeps, so it needs nothing.

HTTPS is `curl` (`curl.exe` ships with Windows 10 and 11, as SkyBuild already relies on it; macOS and most Linux have it):
`--proto =https --tlsv1.2`, a 15 s connect timeout, no redirect to plain http. No new library.

## The game kit

The kit is an asset of the launcher's own release: `skylanders-kit-windows-x64.zip`, `skylanders-kit-macos-arm64.zip` and
`skylanders-kit-linux-x64.zip` (also published as `ssa-kit-<platform>.zip`, above), listed in the release's `SHA256SUMS`. `[update] kit_repo=owner/name` says where (default `mflr0/Skylauncher`).

- **First install**: with no kit, INSTALL starts with "Downloading the game tools" (docs/install.md): the release tagged
  `v<launcher version>`, else the latest one; the zip is checked against that release's `SHA256SUMS` (a mismatch or a missing line
  refuses it), then unpacked into `kit/<release tag>/` in the data folder.
- **Updates** are per game, below. `[update] kit_tag` is the release of the last kit downloaded, else the kit's `version=` is the
  release it comes from. `[update] repo=` changes where the launcher itself is looked up.

## Game versions and UPDATE

Each game has its own version, `X.Y.Z`, by completeness (1.0 = the game at 100 %; each fix bumps the last number). The workshop
keeps it in one place per game (`version=` in the game's `game.txt`), the **kit** carries it (`kit.txt`: `game_version.<id>=0.8.2`,
and `config/<id>/game.txt`), and the **release** says it in a manifest asset, **`games.json`**:

```
{"games":[{"id":"giants","version":"0.8.2","kit":"v0.4.3","notes":"What changed, in a line."}, ...]}
```

`id` is the game's id, `version` the latest one, `kit` the tag of the release whose kit builds it, `notes` plain text (may be
empty). `tools/release_kit.sh` writes it from the kits' `kit.txt` (the kits must agree), takes the notes from the file named by
`GAME_NOTES` (`<id>=<text>` lines), uploads it with the kits and lists it in `SHA256SUMS`; the launcher checks it like every
asset. A release without `games.json` offers no game update.

- The version of a **built** game is recorded when it is built (`[install.<sha256>] version=`, from the kit that built it). The
  game's page shows it ("Giants 0.8.2"); before the build it shows the kit's.
- When the release's version is higher than the built one (a build that recorded none counts as older), the game's main button
  reads **UPDATE** and the sidebar says "Update available". The page says the versions, the download (the kit's size, or "no
  download" when the kit in use is already the right one) and the time, then the notes. One click fetches the kit of the release
  `kit` names if the kit in use is older, then rebuilds the game (SkyBuild is incremental); PLAY is back at the end.
- Under UPDATE, **Play without updating** plays what is built (offline, in a hurry) and sets the offer aside until the launcher is
  closed.
- **Nothing is downloaded in the background**: tools are fetched only by a click on INSTALL or UPDATE, with the install card
  showing it. Only the launcher's own update is offered at start (the banner above), as before.

## The kit follows the launcher

A launcher never builds with a kit older than its own release, and never starts SkyBuild on a disc its kit cannot build.
(Without this a launcher that updated itself kept the kit next to it, and an old SkyBuild refused the Giants disc.)

- **Before INSTALL or UPDATE** (never in the background), a missing kit, one from an older release than the launcher's (or than
  the one `games.json` names for the game), or one whose `games=` lacks the disc's game makes the checklist start with "Downloading
  the game tools": the release's kit is fetched, verified as above, into `kit/<tag>/`, then the game is built with it. It is not
  asked again for the same release (`[update] kit_fetched=<tag>`).
- **Which release a kit comes from**: the `kit/<tag>/` folder of a downloaded kit, else `version=` in `kit.txt`; a kit that says
  nothing is left alone. A kit next to the launcher that is older than a downloaded one, or cannot build the game while the
  downloaded one can, is not used. The kit the player chose with `SKY_LAUNCHER_KIT` or `[launcher] kit=` is never replaced.
- With no published release for the launcher's tag the latest release is used (a build of the development branch), and that is
  then the best there is: `kit_fetched` stops it asking again.

## Releasing

1. Bump `LAUNCHER_VERSION` in `CMakeLists.txt` and commit.
2. Tag the commit `vX.Y.Z` and push the tag.
3. Wait for the CI: it publishes the release with the launcher assets and `SHA256SUMS`.
4. `GAME_NOTES=<notes file> tools/release_kit.sh vX.Y.Z skylanders-kit-windows-x64.zip skylanders-kit-macos-arm64.zip` (needs `gh`):
   uploads each kit zip under both names (`skylanders-kit-<platform>.zip` and `ssa-kit-<platform>.zip`) and `games.json` (made from
   the kits' `kit.txt`) to that release, downloads its `SHA256SUMS`, replaces or appends the kit and manifest lines and uploads it
   again with `--clobber`.

A kit-only update is a new release the same way (a new tag, the launcher unchanged in behaviour).

## What the CI must publish

The release job of `.github/workflows/build.yml` attaches `ArkeyanLauncher.exe`, `ArkeyanLauncher-x86_64.AppImage`,
`ArkeyanLauncher-macOS.dmg`, `ArkeyanLauncher-macOS.zip`, the same four as `Skylauncher.exe`, `Skylauncher-x86_64.AppImage`,
`Skylauncher-macOS.dmg`, `Skylauncher-macOS.zip` (the zip and the disk image hold `Skylauncher.app`), and **`SHA256SUMS`**
(`sha256sum` format, one line per asset, written by the job; the kits and `games.json` are added by `tools/release_kit.sh`). Only a pushed tag `vX.Y.Z` publishes, and it must match `LAUNCHER_VERSION` in `CMakeLists.txt`: bump that first,
then tag the commit. Branch builds publish nothing.

## Testing

`ArkeyanLauncherTest` builds fake releases in a scratch folder (`latest.json`, assets, `SHA256SUMS`, served as `file://` through
`SKY_LAUNCHER_UPDATE_URL`): version compare, the JSON, a good download, a damaged one, no `SHA256SUMS`, the swap and its
rollback, zip path escapes, the two name sets (new, old, both), the kit install (`SkyBuild` or `SSABuild`), the kit download (a fake release with a kit zip: good, bad hash, missing line, fallback to the latest), the games.json manifest (read, wrong hash, absent) and the manager. "The kit follows the launcher" has its own checks (`ArkeyanLauncherTest`; `SKY_LAUNCHER_TEST_KIT_ONLY=1` runs only them; `SKY_LAUNCHER_TEST_LANGUAGE=fr` takes the screenshots in French, the text checks then fail):
`kit.txt` `games=`, the release a kit comes from, a kit next to the launcher against a downloaded one
(`SKY_LAUNCHER_BASE_PATH` stands in for the folder the launcher is in), then the window against a fake release (a kit next to the
launcher from an older release, replaced before INSTALL builds; a kit without `giants` and a Giants disc, the right kit first; at
the game updates: a fake release whose `games.json` bumps Giants only: UPDATE for Giants and PLAY for the other, no kit downloaded
in the background, Play without updating, UPDATE fetching the kit and rebuilding Giants only; and the same offline: silent). `SKY_LAUNCHER_TEST_UPDATE=1` with `SKY_LAUNCHER_UPDATE_URL` on a
fake `latest.json` takes the banner, What's new, About and failure screenshots.

# Updates

Skylauncher updates itself from GitHub releases (`src/launcher/updater.{h,cpp}`, the window side in `ui/update_page.cpp`).
No telemetry: the only request is `GET https://api.github.com/repos/<repo>/releases/latest`, then the asset.

## Flow

1. **Check**: at start, at most once a day (`[update] last_check`, epoch seconds), unless `[update] auto_check=0`; and on
   demand in Settings > Launcher > Check for updates. `SKYLAUNCHER_NO_UPDATE_CHECK` turns the start-up check off.
2. The tag (`vX.Y.Z`) is compared, as semver, with the built version (`SKYLAUNCHER_VERSION`, the CMake cache variable the CI
   sets from the tag or run number, shown in About). Pre-releases and tags that aren't versions are never offered.
3. A **banner** above the page (it pushes the page down 56 px, nothing overlaps): "Update available: vX.Y.Z", **Update now**,
   **What's new** (the release's notes, as written, and its page), **Later** (until the next start).
4. **Update now**: the asset for this OS is downloaded next to the running program (`.skylauncher-update/`), its SHA-256 is
   checked against the release's `SHA256SUMS` asset, then it is swapped in. A release **without** `SHA256SUMS`, or a hash
   that doesn't match, is refused and deleted: nothing is installed.
5. The banner then offers **Restart now**.

| OS | Asset | Swap |
| --- | --- | --- |
| Windows | `Skylauncher.exe` | the running exe is renamed to `Skylauncher.exe.old`, the new one moved into place, the old one deleted at the next start |
| macOS | `Skylauncher-macOS.zip` (the `.app`) | the `.app` is replaced if its folder is writable (`.old` kept until the next start); otherwise `Skylauncher-macOS.dmg` is downloaded, verified and opened |
| Linux | `Skylauncher-x86_64.AppImage` | the AppImage (`$APPIMAGE`) is replaced the same way; not an AppImage: the release page is opened |

If the second move fails the first is undone (`ReplaceInPlace`), so a failed update leaves the program as it was. A copy that
isn't installed (a build folder) opens the release page instead.

HTTPS is `curl` (`curl.exe` ships with Windows 10 and 11, as SSABuild already relies on it; macOS and most Linux have it):
`--proto =https --tlsv1.2`, a 15 s connect timeout, no redirect to plain http. No new library.

## The game kit

The kit is an asset of the launcher's own release: `ssa-kit-windows-x64.zip`, `ssa-kit-macos-arm64.zip` and, since 0.3.1,
`ssa-kit-linux-x64.zip`, listed in the release's `SHA256SUMS`. `[update] kit_repo=owner/name` says where (default `mflr0/Skylauncher`).

- **First install**: with no kit, INSTALL starts with "Downloading the game tools" (docs/install.md): the release tagged
  `v<launcher version>`, else the latest one; the zip is checked against that release's `SHA256SUMS` (a mismatch or a missing line
  refuses it), then unpacked into `kit/<release tag>/` in the data folder.
- **Updates**: the installed kit's release is `[update] kit_tag` (written after a download or an update), else `version=` in its
  `kit.txt`. A newer tag shows "A new version of the game is available" with **Update the game**: the zip is verified, unpacked
  into `kit/<tag>/` (or over the kit next to the launcher, if that is the one in use) and, a disc being verified, the install
  runs again; SSABuild is incremental. `[update] repo=` changes where the launcher itself is looked up.

## Releasing

1. Bump `SKYLAUNCHER_VERSION` in `CMakeLists.txt` and commit.
2. Tag the commit `vX.Y.Z` and push the tag.
3. Wait for the CI: it publishes the release with the launcher assets and `SHA256SUMS`.
4. `tools/release_kit.sh vX.Y.Z ssa-kit-windows-x64.zip ssa-kit-macos-arm64.zip` (needs `gh`): uploads the kit zips to that
   release, downloads its `SHA256SUMS`, replaces or appends the kit lines and uploads it again with `--clobber`.

A kit-only update is a new release the same way (a new tag, the launcher unchanged in behaviour).

## What the CI must publish

The release job of `.github/workflows/build.yml` attaches `Skylauncher.exe`, `Skylauncher-x86_64.AppImage`,
`Skylauncher-macOS.dmg`, `Skylauncher-macOS.zip` and **`SHA256SUMS`** (`sha256sum` format, one line per asset, written by the
job). Only a pushed tag `vX.Y.Z` publishes, and it must match `SKYLAUNCHER_VERSION` in `CMakeLists.txt`: bump that first,
then tag the commit. Branch builds publish nothing.

## Testing

`SkylauncherTest` builds fake releases in a scratch folder (`latest.json`, assets, `SHA256SUMS`, served as `file://` through
`SKYLAUNCHER_UPDATE_URL`): version compare, the JSON, a good download, a damaged one, no `SHA256SUMS`, the swap and its
rollback, zip path escapes, the kit install, the kit download (a fake release with a kit zip: good, bad hash, missing line, fallback to the latest) and the manager. `SSA_LAUNCHER_TEST_UPDATE=1` with `SKYLAUNCHER_UPDATE_URL` on a
fake `latest.json` takes the banner, What's new, About and failure screenshots.

#!/usr/bin/env bash
# Adds the game kit zips to an existing release, lists them in its SHA256SUMS and puts the games' versions in its notes
# (docs/updates.md).
#   tools/release_kit.sh <tag> <kit zip>...      e.g. tools/release_kit.sh v0.6.1 skylanders-kit-windows-x64.zip skylanders-kit-macos-arm64.zip
# The zips must be named as the launcher looks for them: skylanders-kit-windows-x64.zip, skylanders-kit-macos-arm64.zip,
# skylanders-kit-linux-x64.zip.
# The versions go in one hidden line at the end of the release's notes, "<!-- games: ssa=0.9.1 giants=0.8.1 -->": one
# id=version per game of the kits, the version from the kit's kit.txt (game_version.<id>). The launcher reads it from the
# release it already fetches; the kit that builds those versions is this release's own. An earlier games line is replaced.
# All the kits given must agree on the versions.
# DRY_RUN=1 prints the new notes and the files that would be uploaded, and changes nothing.
# Needs the GitHub CLI (gh), logged in with write access to the repository (SKY_LAUNCHER_REPO, else SKYLAUNCHER_REPO, default mflr0/Skylauncher).
set -euo pipefail
[[ $# -ge 2 ]] || { echo "usage: $0 <tag> <kit zip>..." >&2; exit 2; }
tag="$1"; shift
repo="${SKY_LAUNCHER_REPO:-${SKYLAUNCHER_REPO:-mflr0/Skylauncher}}"
for zip in "$@"; do
    [[ -f "$zip" ]] || { echo "no such file: $zip" >&2; exit 2; }
    [[ "$(basename "$zip")" == skylanders-kit-*.zip ]] || { echo "not a kit zip (skylanders-kit-<os>-<arch>.zip): $zip" >&2; exit 2; }
done
if command -v sha256sum >/dev/null; then hash() { sha256sum "$1" | cut -d' ' -f1; }; else hash() { shasum -a 256 "$1" | cut -d' ' -f1; }; fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
# One line "<id>=<version>" per game from each kit's kit.txt; the kits must say the same.
for zip in "$@"; do
    kit_txt="$(unzip -p "$zip" '*/kit.txt')" || { echo "no kit.txt in $zip" >&2; exit 2; }
    printf '%s\n' "$kit_txt" | sed -n 's/^game_version\.\([a-z0-9]*\)=\(.*\)$/\1=\2/p' | sort > "$work/versions.$(basename "$zip")"
done
first=""
for zip in "$@"; do
    f="$work/versions.$(basename "$zip")"
    [[ -s "$f" ]] || { echo "$zip has no game_version.<id> lines in kit.txt: build it with the current tools/make_kit.sh" >&2; exit 2; }
    if [[ -z "$first" ]]; then first="$f"; else cmp -s "$first" "$f" || { echo "the kits disagree on the game versions" >&2; exit 2; }; fi
done
line="<!-- games: $(paste -sd' ' "$first") -->"

# The release's notes without an earlier games line, then the new one after a blank line.
gh release view "$tag" --repo "$repo" --json body --jq .body | tr -d '\r' | grep -v -E '^<!-- games:.*-->$' > "$work/body" || true
while [[ -s "$work/body" && -z "$(tail -n 1 "$work/body")" ]]; do sed -i.bak '$d' "$work/body"; rm -f "$work/body.bak"; done
{ [[ -s "$work/body" ]] && { cat "$work/body"; echo; }; echo "$line"; } > "$work/notes"

if [[ -n "${DRY_RUN:-}" ]]; then
    echo "DRY RUN: nothing is changed on $repo $tag. New notes:"
    echo "-----"
    cat "$work/notes"
    echo "-----"
    echo "Would upload to $tag (kits, then SHA256SUMS with one line per kit):"
    printf '  %s\n' "${@##*/}"
    exit 0
fi

gh release download "$tag" --repo "$repo" --pattern SHA256SUMS --dir "$work" \
    || { echo "release $tag has no SHA256SUMS: wait for the CI to publish it" >&2; exit 1; }

# Replace or append one line per kit zip, keeping the others as they are.
for zip in "$@"; do
    name="$(basename "$zip")"
    grep -v -E "^[0-9a-fA-F]{64} [ *]${name}\$" "$work/SHA256SUMS" > "$work/next" || true
    echo "$(hash "$zip")  $name" >> "$work/next"
    mv "$work/next" "$work/SHA256SUMS"
done

gh release upload "$tag" --repo "$repo" --clobber "$@" "$work/SHA256SUMS"
gh release edit "$tag" --repo "$repo" --notes-file "$work/notes"
echo "Uploaded to $tag:"
printf '  %s\n' "${@##*/}"
echo "Notes end with: $line"

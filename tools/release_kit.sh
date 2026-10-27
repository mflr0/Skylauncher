#!/usr/bin/env bash
# Adds the game kit zips to an existing release and lists them in its SHA256SUMS (docs/updates.md).
#   tools/release_kit.sh <tag> <kit zip>...      e.g. tools/release_kit.sh v0.6.0 skylanders-kit-windows-x64.zip skylanders-kit-macos-arm64.zip
# The zips must be named as the launcher looks for them: skylanders-kit-windows-x64.zip, skylanders-kit-macos-arm64.zip,
# skylanders-kit-linux-x64.zip. Each is uploaded twice, as skylanders-kit-<platform>.zip and as ssa-kit-<platform>.zip (the
# same file): launchers before 0.6.0 ask for the second name.
# It also writes and uploads games.json, the manifest the launcher reads to offer UPDATE per game (docs/updates.md): one entry
# {id, version, kit, notes} per game of the kits, the version from the kit's kit.txt (game_version.<id>), `kit` this tag, and the
# notes from the file named by GAME_NOTES ("<id>=<text>" lines; none: empty). All the kits given must agree on the versions.
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
# The same file under the older name, in a folder of its own: what gets uploaded is "$@" plus these.
kits=()
for zip in "$@"; do
    name="$(basename "$zip")"
    kits+=("$zip")
    mkdir -p "$work/older"
    cp "$zip" "$work/older/ssa-kit-${name#skylanders-kit-}"
    kits+=("$work/older/ssa-kit-${name#skylanders-kit-}")
done
# games.json: one line "<id> <version>" per game from each kit's kit.txt; the kits must say the same.
for zip in "$@"; do
    kit_txt="$(unzip -p "$zip" '*/kit.txt')" || { echo "no kit.txt in $zip" >&2; exit 2; }
    printf '%s\n' "$kit_txt" | sed -n 's/^game_version\.\([a-z0-9]*\)=\(.*\)$/\1 \2/p' | sort > "$work/versions.$(basename "$zip")"
done
first=""
for zip in "$@"; do
    f="$work/versions.$(basename "$zip")"
    [[ -s "$f" ]] || { echo "$zip has no game_version.<id> lines in kit.txt: build it with the current tools/make_kit.sh" >&2; exit 2; }
    if [[ -z "$first" ]]; then first="$f"; else cmp -s "$first" "$f" || { echo "the kits disagree on the game versions" >&2; exit 2; }; fi
done
json_escape() { sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'; }
{
    printf '{"games":['
    sep=""
    while read -r id version; do
        notes=""
        [[ -n "${GAME_NOTES:-}" && -f "${GAME_NOTES:-}" ]] && notes="$(sed -n "s/^$id=//p" "$GAME_NOTES" | head -n 1 | json_escape)"
        printf '%s{"id":"%s","version":"%s","kit":"%s","notes":"%s"}' "$sep" "$id" "$version" "$tag" "$notes"
        sep=","
    done < "$first"
    printf ']}\n'
} > "$work/games.json"
cat "$work/games.json"
gh release download "$tag" --repo "$repo" --pattern SHA256SUMS --dir "$work" \
    || { echo "release $tag has no SHA256SUMS: wait for the CI to publish it" >&2; exit 1; }

# Replace or append one line per kit zip, keeping the others as they are.
for zip in "${kits[@]}"; do
    name="$(basename "$zip")"
    grep -v -E "^[0-9a-fA-F]{64} [ *]${name}\$" "$work/SHA256SUMS" > "$work/next" || true
    echo "$(hash "$zip")  $name" >> "$work/next"
    mv "$work/next" "$work/SHA256SUMS"
done
grep -v -E "^[0-9a-fA-F]{64} [ *]games.json\$" "$work/SHA256SUMS" > "$work/next" || true
echo "$(hash "$work/games.json")  games.json" >> "$work/next"
mv "$work/next" "$work/SHA256SUMS"

gh release upload "$tag" --repo "$repo" --clobber "${kits[@]}" "$work/games.json" "$work/SHA256SUMS"
echo "Uploaded to $tag:"
printf '  %s\n' "${kits[@]##*/}"

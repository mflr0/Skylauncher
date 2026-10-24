#!/usr/bin/env bash
# Adds the game kit zips to an existing release and lists them in its SHA256SUMS (docs/updates.md).
#   tools/release_kit.sh <tag> <kit zip>...      e.g. tools/release_kit.sh v0.3.0 ssa-kit-windows-x64.zip ssa-kit-macos-arm64.zip
# The zips must be named as the launcher looks for them: ssa-kit-windows-x64.zip, ssa-kit-macos-arm64.zip.
# Needs the GitHub CLI (gh), logged in with write access to the repository (SKYLAUNCHER_REPO, default mflr0/Skylauncher).
set -euo pipefail
[[ $# -ge 2 ]] || { echo "usage: $0 <tag> <kit zip>..." >&2; exit 2; }
tag="$1"; shift
repo="${SKYLAUNCHER_REPO:-mflr0/Skylauncher}"
for zip in "$@"; do
    [[ -f "$zip" ]] || { echo "no such file: $zip" >&2; exit 2; }
    [[ "$(basename "$zip")" == ssa-kit-*.zip ]] || { echo "not a kit zip (ssa-kit-<os>-<arch>.zip): $zip" >&2; exit 2; }
done
if command -v sha256sum >/dev/null; then hash() { sha256sum "$1" | cut -d' ' -f1; }; else hash() { shasum -a 256 "$1" | cut -d' ' -f1; }; fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
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
echo "Uploaded to $tag:"
printf '  %s\n' "$@"

#!/usr/bin/env bash
# Copies SSARecomp's figure and collection library into this checkout (docs/collection_ui.md), or with --check only
# lists the files that differ. The SSARecomp copies are the reference (they have the tests): change them there.
# Same files as SSARecomp's tools/sync_skylauncher.sh, into Skylauncher's layout (src/shared/).
#   tools/sync_collection.sh [--check] <ssa-recomp dir>
set -euo pipefail
check=0
[[ "${1:-}" == "--check" ]] && { check=1; shift; }
[[ $# -eq 1 && -d "$1/SSARecomp/host/collection" ]] || { echo "usage: $0 [--check] <ssa-recomp dir>" >&2; exit 2; }
from="$(cd "$1" && pwd)"
here="$(cd "$(dirname "$0")/.." && pwd)"

pairs=()
for f in figure_catalog figure_crypto figure_data figure_tag; do
    pairs+=("SSARecomp/host/figures/$f.h:src/shared/figures/$f.h" "SSARecomp/host/figures/$f.cpp:src/shared/figures/$f.cpp")
done
for f in json catalogue collection icons datadir; do
    pairs+=("SSARecomp/host/collection/$f.h:src/shared/collection/$f.h" "SSARecomp/host/collection/$f.cpp:src/shared/collection/$f.cpp")
done
pairs+=("resources/collection/catalogue.json:resources/collection/catalogue.json" "cmake/embed_file.cmake:cmake/embed_file.cmake")

status=0
for pair in "${pairs[@]}"; do
    source="$from/${pair%%:*}" target="$here/${pair#*:}"
    cmp -s "$source" "$target" && continue
    if [[ $check -eq 1 ]]; then
        echo "differs: ${pair#*:}"
        status=1
    else
        mkdir -p "$(dirname "$target")"
        cp "$source" "$target"
        echo "copied: ${pair#*:}"
    fi
done
exit $status

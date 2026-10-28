#!/usr/bin/env bash
# Packages the built app (the CI's macOS job, or by hand to try it):
#   tools/package_macos.sh <ArkeyanLauncher.app built by cmake> <icon.svg> <output dir>
# Writes, in the output dir, the release assets (docs/updates.md):
#   ArkeyanLauncher-macOS.zip / .dmg      holding "Arkeyan Launcher.app"
# The icon is made from the svg (rsvg-convert, iconutil); the app is signed ad hoc once complete.
set -euo pipefail
[[ $# -eq 3 && -d "$1" && -f "$2" ]] || { echo "usage: $0 <ArkeyanLauncher.app> <icon.svg> <output dir>" >&2; exit 2; }
built="$(cd "$1" && pwd)"; svg="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
mkdir -p "$3"; out="$(cd "$3" && pwd)"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

mkdir "$work/icon.iconset"
for size in 16 32 128 256 512; do
    rsvg-convert -w $size -h $size "$svg" -o "$work/icon.iconset/icon_${size}x${size}.png"
    rsvg-convert -w $((size * 2)) -h $((size * 2)) "$svg" -o "$work/icon.iconset/icon_${size}x${size}@2x.png"
done
iconutil -c icns "$work/icon.iconset" -o "$work/Skylauncher.icns"

# make_app <bundle name>
make_app() {
    local app="$work/$1/$1.app"
    mkdir -p "$work/$1"
    ditto "$built" "$app"
    cp "$work/Skylauncher.icns" "$app/Contents/Resources/Skylauncher.icns"
    local plist="$app/Contents/Info.plist"
    /usr/libexec/PlistBuddy -c "Set :CFBundleIconFile Skylauncher" "$plist" 2>/dev/null || /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string Skylauncher" "$plist"
    /usr/libexec/PlistBuddy -c "Set :CFBundleDisplayName 'Arkeyan Launcher'" "$plist" 2>/dev/null || /usr/libexec/PlistBuddy -c "Add :CFBundleDisplayName string 'Arkeyan Launcher'" "$plist"
    /usr/libexec/PlistBuddy -c "Set :CFBundleName 'Arkeyan Launcher'" "$plist" 2>/dev/null || /usr/libexec/PlistBuddy -c "Add :CFBundleName string 'Arkeyan Launcher'" "$plist"
    codesign --force --deep --sign - "$app"   # ad hoc: not notarized, see the README
    echo "$app"
}

# package <bundle name> <asset prefix> <volume name>
package() {
    local app; app="$(make_app "$1")"
    rm -f "$out/$2-macOS.zip" "$out/$2-macOS.dmg"
    ditto -c -k --norsrc --noextattr --keepParent "$app" "$out/$2-macOS.zip"
    mkdir -p "$work/dmg-$1"
    cp -R "$app" "$work/dmg-$1/"
    ln -s /Applications "$work/dmg-$1/Applications"
    hdiutil create -volname "$3" -srcfolder "$work/dmg-$1" -ov -format UDZO "$out/$2-macOS.dmg" > /dev/null
}

package "Arkeyan Launcher" ArkeyanLauncher "Arkeyan Launcher"
ls -l "$out"

#!/bin/sh
# Makes "Molten Metal Engine.app" in the build folder: an application that starts the editor
# built next to it, with the engine's icon in the Finder and in the Dock. Double-click it, or
# drag it to the Dock. Run after every build of the editor (CMake does it).
# usage: make_editor_app.sh <the molten program> <icon.png> <version> <where the .app goes>
program="$1"; icon="$2"; version="$3"; app="$4"
contents="$app/Contents"
mkdir -p "$contents/MacOS" "$contents/Resources" || exit 1

# The application's program is a link to the editor itself: one editor, never a stale copy.
ln -sfn "$program" "$contents/MacOS/molten"

# The icon, in every size the Finder and the Dock ask for, made again when the picture changes.
icns="$contents/Resources/Molten.icns"
if [ -f "$icon" ] && { [ ! -f "$icns" ] || [ "$icon" -nt "$icns" ]; }; then
    work="$(mktemp -d)"
    set="$work/Molten.iconset"
    mkdir -p "$set"
    for size in 16 32 128 256 512; do
        double=$((size * 2))
        sips -z $size $size "$icon" --out "$set/icon_${size}x${size}.png" >/dev/null 2>&1
        sips -z $double $double "$icon" --out "$set/icon_${size}x${size}@2x.png" >/dev/null 2>&1
    done
    iconutil -c icns "$set" -o "$icns" || echo "-- editor app: the icon could not be made"
    rm -rf "$work"
fi

cat > "$contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Molten Metal Engine</string>
    <key>CFBundleDisplayName</key><string>Molten Metal Engine</string>
    <key>CFBundleIdentifier</key><string>com.gabrielevierti.molten-editor</string>
    <key>CFBundleExecutable</key><string>molten</string>
    <key>CFBundleIconFile</key><string>Molten</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$version</string>
    <key>CFBundleVersion</key><string>$version</string>
    <key>LSMinimumSystemVersion</key><string>14.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSPrincipalClass</key><string>NSApplication</string>
</dict>
</plist>
PLIST
# The Finder shows a new icon only when it notices the application changed.
touch "$app"
exit 0

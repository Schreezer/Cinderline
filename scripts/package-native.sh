#!/bin/bash
set -eu
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${CINDERLINE_BUILD_DIR:-$project_dir/build/native}"
cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DCINDERLINE_BUILD_NATIVE=ON
cmake --build "$build_dir" --target CinderlineNative --parallel 4
bundle="$project_dir/artifacts/Cinderline Playtest.app"
mkdir -p "$bundle/Contents/MacOS" "$bundle/Contents/Resources"
cp "$build_dir/CinderlineNative" "$bundle/Contents/MacOS/CinderlineNative"
cat > "$bundle/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>CinderlineNative</string>
<key>CFBundleIdentifier</key><string>dev.cinderline.playtest</string>
<key>CFBundleName</key><string>Cinderline Playtest</string>
<key>CFBundleDisplayName</key><string>Cinderline Playtest</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>0.1.0</string>
<key>CFBundleVersion</key><string>1</string>
<key>LSMinimumSystemVersion</key><string>13.0</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
codesign --force --sign - "$bundle"
printf '%s\n' "$bundle"

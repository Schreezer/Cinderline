#!/usr/bin/env bash
set -euo pipefail

storage_root="${CINDERLINE_ANDROID_STORAGE_ROOT:-/Volumes/Codex Storage/Cinderline/Android}"
sdk_root="$storage_root/sdk"
sdkmanager_command="$(command -v sdkmanager)"
sdkmanager_target="$(readlink "$sdkmanager_command" 2>/dev/null || true)"
if [[ -n "$sdkmanager_target" ]]; then
  [[ "$sdkmanager_target" == /* ]] || sdkmanager_target="$(dirname "$sdkmanager_command")/$sdkmanager_target"
else
  sdkmanager_target="$sdkmanager_command"
fi
source_sdk="$(cd "$(dirname "$sdkmanager_target")/../../.." && pwd -P)"

case "$storage_root" in
  /Volumes/*/Cinderline/Android) ;;
  *) printf 'Android storage must be a dedicated /Volumes/.../Cinderline/Android directory: %s\n' "$storage_root" >&2; exit 2 ;;
esac
mkdir -p "$sdk_root/licenses"
if [[ -d "$source_sdk/licenses" ]]; then
  rsync -a "$source_sdk/licenses/" "$sdk_root/licenses/"
fi

sdkmanager --sdk_root="$sdk_root" \
  'platform-tools' \
  'platforms;android-35' \
  'platforms;android-36' \
  'build-tools;35.0.1' \
  'ndk;27.2.12479018'

for required in \
  "$sdk_root/platform-tools/adb" \
  "$sdk_root/platforms/android-35/android.jar" \
  "$sdk_root/platforms/android-36/android.jar" \
  "$sdk_root/build-tools/35.0.1/aapt2" \
  "$sdk_root/ndk/27.2.12479018/ndk-build"; do
  [[ -e "$required" ]] || { printf 'Required Android tool is missing: %s\n' "$required" >&2; exit 2; }
done
printf 'Android SDK/NDK ready on SSD: %s\n' "$sdk_root"

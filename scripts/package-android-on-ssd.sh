#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd -P)"
storage_root="${CINDERLINE_ANDROID_STORAGE_ROOT:-/Volumes/Codex Storage/Cinderline/Android}"
workspace="$storage_root/workspace"
sdk_root="$storage_root/sdk"
cache_root="$storage_root/cache"
package_root="$storage_root/packages"
engine_root="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
jdk_root="${CINDERLINE_ANDROID_JAVA_HOME:-/opt/homebrew/opt/openjdk@21}"

case "$storage_root" in
  /Volumes/*/Cinderline/Android) ;;
  *) printf 'Android storage must be a dedicated /Volumes/.../Cinderline/Android directory: %s\n' "$storage_root" >&2; exit 2 ;;
esac
[[ -d "${storage_root%/Cinderline/Android}" ]] || {
  printf 'External storage volume is not mounted: %s\n' "${storage_root%/Cinderline/Android}" >&2
  exit 2
}
[[ ! -L "$storage_root" && ! -L "$workspace" ]] || {
  printf '%s\n' 'Android storage and workspace must not be symbolic links.' >&2
  exit 2
}
[[ -x "$jdk_root/bin/java" ]] || {
  printf 'JDK 21 is missing: %s\n' "$jdk_root" >&2
  exit 2
}
[[ -d "$sdk_root/platforms/android-35" && -d "$sdk_root/platforms/android-36" && -d "$sdk_root/build-tools/35.0.1" ]] || {
  printf 'Android SDK platforms 35/36 or build tools 35.0.1 are incomplete on the SSD: %s\n' "$sdk_root" >&2
  exit 2
}
[[ -d "$sdk_root/ndk/27.2.12479018" ]] || {
  printf 'Android NDK r27c is incomplete on the SSD: %s\n' "$sdk_root/ndk/27.2.12479018" >&2
  exit 2
}

mkdir -p "$workspace" "$cache_root/gradle" "$cache_root/unreal-ddc" "$cache_root/zen" "$package_root"
rsync -a --delete \
  --exclude '/.git/' \
  --exclude '/.DS_Store' \
  --exclude '/artifacts/' \
  --exclude '/Binaries/' \
  --exclude '/build/' \
  --exclude '/DerivedDataCache/' \
  --exclude '/Intermediate/' \
  --exclude '/Saved/' \
  "$project_root/" "$workspace/"

printf 'Android build workspace synced to: %s\n' "$workspace"
if [[ "${CINDERLINE_ANDROID_SYNC_ONLY:-0}" == 1 ]]; then
  exit 0
fi

env \
  UE_ROOT="$engine_root" \
  ANDROID_HOME="$sdk_root" \
  ANDROID_SDK_ROOT="$sdk_root" \
  NDKROOT="$sdk_root/ndk/27.2.12479018" \
  NDK_ROOT="$sdk_root/ndk/27.2.12479018" \
  JAVA_HOME="$jdk_root" \
  GRADLE_USER_HOME="$cache_root/gradle" \
  "UE-LocalDataCachePath=$cache_root/unreal-ddc" \
  "UE-ZenDataPath=$cache_root/zen" \
  CINDERLINE_ANDROID_ARTIFACT_DIR="$package_root" \
  "$workspace/scripts/unreal.sh" package-android "$@"

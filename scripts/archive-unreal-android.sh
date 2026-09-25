#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd -P)"
package_directory="${1:-$project_root/Saved/Packages/Android}"
artifact_directory="${2:-$project_root/artifacts}"
instructions="$project_root/docs/ANDROID_PLAYTEST.txt"
artifact="$artifact_directory/Cinderline-Android-arm64.apk"

[[ -d "$package_directory" ]] || {
  printf 'Unreal Android package directory is missing: %s\n' "$package_directory" >&2
  exit 2
}
[[ -f "$instructions" ]] || {
  printf 'Android package instructions are missing: %s\n' "$instructions" >&2
  exit 2
}

apk="$(find "$package_directory" -type f -name '*.apk' -print -quit)"
if [[ -z "$apk" ]]; then
  printf 'No APK was found under: %s\n' "$package_directory" >&2
  exit 2
fi

listing="$(unzip -Z1 "$apk")"
printf '%s\n' "$listing" | grep -q '^AndroidManifest.xml$' || {
  printf 'APK has no AndroidManifest.xml: %s\n' "$apk" >&2
  exit 2
}
printf '%s\n' "$listing" | grep -Eq '^lib/arm64-v8a/[^/]+\.so$' || {
  printf 'APK has no ARM64 native library: %s\n' "$apk" >&2
  exit 2
}
printf '%s\n' "$listing" | grep -Eq '^assets/.+\.(pak|utoc|ucas|obb\.png)$' || {
  printf 'APK has no packaged Unreal game data: %s\n' "$apk" >&2
  exit 2
}

mkdir -p "$artifact_directory"
temporary_apk="$(mktemp "$artifact_directory/.cinderline-android.XXXXXX.apk")"
cleanup() { rm -f "$temporary_apk"; }
trap cleanup EXIT
cp "$apk" "$temporary_apk"
mv -f "$temporary_apk" "$artifact"
trap - EXIT
install -m 0644 "$instructions" "$artifact_directory/README-ANDROID.txt"

if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "$artifact" > "$artifact.sha256"
else
  shasum -a 256 "$artifact" > "$artifact.sha256"
fi

printf 'Android playtest APK: %s\n' "$artifact"
printf 'Source package: %s\n' "$apk"
du -h "$artifact" "$artifact.sha256"

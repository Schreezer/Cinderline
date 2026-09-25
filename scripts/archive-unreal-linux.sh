#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd -P)"
package_directory="${1:-$project_root/Saved/Packages/Linux}"
artifact="${2:-$project_root/artifacts/Cinderline-Ubuntu-x86_64-Unreal.tar.gz}"
instructions="$project_root/docs/UBUNTU_UNREAL_PLAYTEST.txt"

if [[ ! -d "$package_directory" ]]; then
  printf 'Unreal Linux package directory is missing: %s\n' "$package_directory" >&2
  exit 2
fi
if [[ ! -f "$instructions" ]]; then
  printf 'Linux package instructions are missing: %s\n' "$instructions" >&2
  exit 2
fi

launcher="$(find "$package_directory" -type f \
  \( -name 'Cinderline.sh' -o -name 'Cinderline' -o -name 'Cinderline-Linux-Shipping' \) \
  -perm -111 -print -quit)"
if [[ -z "$launcher" ]]; then
  printf 'No executable Cinderline launcher was found under: %s\n' "$package_directory" >&2
  exit 2
fi

content_container="$(find "$package_directory" -type f \
  \( -name '*.pak' -o -name '*.utoc' -o -name '*.ucas' \) -print -quit)"
if [[ -z "$content_container" ]]; then
  printf 'No cooked Unreal content container was found under: %s\n' "$package_directory" >&2
  exit 2
fi

install -m 0644 "$instructions" "$package_directory/README-UBUNTU.txt"
mkdir -p "$(dirname "$artifact")"
temporary_archive="$(mktemp "$(dirname "$artifact")/.cinderline-linux-unreal.XXXXXX.tar.gz")"
cleanup() { rm -f "$temporary_archive"; }
trap cleanup EXIT
tar -czf "$temporary_archive" -C "$package_directory" .
mv -f "$temporary_archive" "$artifact"
trap - EXIT

if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "$artifact" > "$artifact.sha256"
else
  shasum -a 256 "$artifact" > "$artifact.sha256"
fi

printf 'Full Unreal Linux package: %s\n' "$artifact"
printf 'Launcher: %s\n' "${launcher#"$package_directory"/}"
printf 'Cooked content: %s\n' "${content_container#"$package_directory"/}"
du -h "$artifact" "$artifact.sha256"

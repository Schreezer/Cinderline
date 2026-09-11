#!/bin/bash
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
printf 'Cinderline development environment\n'
printf 'Project: %s\n' "$root"
xcodebuild -version
printf '\nC++ compiler\n'
xcrun clang++ --version | head -n 2
printf '\nUnreal installation\n'
engine="${UE_ROOT:-}"
if [ -z "$engine" ]; then
  for candidate in /Users/Shared/Epic\ Games/UE_* /Users/Shared/UnrealEngine/UE_*; do
    if [ -f "$candidate/Engine/Build/Build.version" ] && [ -f "$candidate/Engine/Build/BatchFiles/Mac/Build.sh" ]; then
      engine="$candidate"
    fi
  done
fi
if [ -n "$engine" ] && [ -f "$engine/Engine/Build/Build.version" ]; then
  printf 'Engine: %s\n' "$engine"
  cat "$engine/Engine/Build/Build.version"
  if [ -x "$engine/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" ]; then
    printf '\nEditor executable present. A successful project build and launch is still required.\n'
  else
    printf '\nEditor executable missing or installation incomplete.\n'
    exit 2
  fi
else
  printf 'No complete Unreal installation found. Set UE_ROOT to an installed engine root.\n'
  exit 2
fi

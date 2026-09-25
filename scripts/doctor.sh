#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
# Engine discovery is shared with every build, packaging and automation
# entrypoint. Doctor must report the engine those commands will actually use:
# a private /Users/Shared glob here reported a stock installed engine while the
# real builds ran against the prepared MetalFX clone beside the project.
# shellcheck source=lib/unreal-engine.sh
source "$project_root/scripts/lib/unreal-engine.sh"

printf 'Cinderline development environment\n'
printf 'Project: %s\n' "$project_root"
xcodebuild -version
printf '\nC++ compiler\n'
xcrun clang++ --version | head -n 2

printf '\nUnreal installation\n'
if [[ -n "${UE_ROOT:-}" ]]; then
  printf 'Selection: UE_ROOT override\n'
fi
# cinder_find_engine returns 2 with its own stderr message when no coherent
# engine exists, and also when a prepared clone fails its --check verification.
if ! cinder_find_engine "$project_root"; then
  printf 'No usable Unreal engine was selected. Set UE_ROOT to an installed engine root.\n' >&2
  exit 2
fi
printf 'Engine: %s\n' "$engine_root"
printf 'Platform: %s\n' "$platform"
# One prepared clone commonly carries both repairs, so report every manifest
# rather than the first match: a stock engine of the same version renders a
# different image and cannot build physical iOS, and both facts matter here.
engine_kinds=()
if [[ "$using_prepared_metalfx_engine" == true ]]; then
  engine_kinds+=("prepared MetalFX clone (verified by prepare-metalfx-engine.py --check)")
fi
if [[ -f "$engine_root/.cinderline-ios27-engine.json" ]]; then
  engine_kinds+=("prepared iOS 27 clone")
fi
if [[ ${#engine_kinds[@]} -eq 0 ]]; then
  if [[ -f "$engine_root/Engine/Build/InstalledBuild.txt" ]]; then
    engine_kinds+=("stock installed engine")
  else
    engine_kinds+=("source-built engine")
  fi
fi
for engine_kind in "${engine_kinds[@]}"; do
  printf 'Engine kind: %s\n' "$engine_kind"
done
for engine_manifest in .cinderline-metalfx-engine.json .cinderline-ios27-engine.json; do
  if [[ -f "$engine_root/$engine_manifest" ]]; then
    printf 'Prepared from (%s): %s\n' "$engine_manifest" \
      "$(cinder_prepared_engine_source "$engine_root/$engine_manifest")"
  fi
done
if [[ -f "$engine_root/Engine/Build/Build.version" ]]; then
  cat "$engine_root/Engine/Build/Build.version"
else
  printf 'Engine/Build/Build.version is missing; this engine tree is incomplete.\n' >&2
  exit 2
fi

printf '\nEditor: %s\n' "$editor"
printf 'Command editor: %s\n' "$command_editor"
if [[ -x "$editor" ]]; then
  printf 'Editor executable present. A successful project build and launch is still required.\n'
else
  printf 'Editor executable missing or installation incomplete.\n' >&2
  exit 2
fi
if [[ ! -x "$command_editor" ]]; then
  printf 'UnrealEditor-Cmd is missing; automation and visual capture cannot run.\n' >&2
  exit 2
fi

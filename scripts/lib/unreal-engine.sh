#!/usr/bin/env bash
# Shared, read-only engine discovery. Sourcing this file does not select an
# engine or launch a tool. cinder_find_engine PROJECT_ROOT populates the paths
# used by build, packaging and automation entrypoints.

cinder_prepared_engine_source() {
  "${CINDERLINE_TEST_PYTHON:-python3}" - "$1" <<'PY'
import json
from pathlib import Path
import sys

manifest = Path(sys.argv[1])
try:
    data = json.loads(manifest.read_text(encoding="utf-8"))
except (OSError, ValueError) as error:
    raise SystemExit(f"Cannot read engine preparation manifest {manifest}: {error}")
source = data.get("sourceEngine") if isinstance(data, dict) else None
if not isinstance(source, str) or not source:
    raise SystemExit(f"Invalid sourceEngine in preparation manifest: {manifest}")
print(source)
PY
}

cinder_find_engine() {
  local cinder_project_root="$1"
  local cinder_prepared_engine="$(dirname -- "$cinder_project_root")/CinderlineEngineIOS27"
  local candidate metalfx_source
  using_prepared_metalfx_engine=false
  if [[ -n "${UE_ROOT:-}" ]]; then
    engine_root="${UE_ROOT%/}"
    [[ "$(basename -- "$engine_root")" != Engine ]] || engine_root="$(dirname -- "$engine_root")"
  elif [[ -f "$cinder_prepared_engine/.cinderline-metalfx-engine.json" ]]; then
    engine_root="$cinder_prepared_engine"
  else
    engine_root=""
    for candidate in /Users/Shared/Epic\ Games/UE_5.* /Users/Shared/UnrealEngine/UE_5.* "$HOME"/UnrealEngine/UE_5.* /opt/unreal-engine /opt/UnrealEngine; do
      if [[ -d "$candidate/Engine/Build/BatchFiles" ]]; then engine_root="$candidate"; fi
    done
  fi
  if [[ -z "$engine_root" || ! -d "$engine_root/Engine/Build/BatchFiles" ]]; then
    printf '%s\n' 'Unreal Engine is not available. Set UE_ROOT to its installation directory.' >&2
    return 2
  fi
  engine_root="$(cd -- "$engine_root" && pwd -P)" || return 2
  if [[ -f "$engine_root/.cinderline-metalfx-engine.json" ]]; then
    metalfx_source="$(cinder_prepared_engine_source "$engine_root/.cinderline-metalfx-engine.json")" || return 2
    # --check verifies existing preparation; it must never prepare or rebuild.
    if ! "${CINDERLINE_TEST_PYTHON:-python3}" "$cinder_project_root/scripts/prepare-metalfx-engine.py" \
      --engine "$metalfx_source" --destination "$engine_root" --check; then
      printf '%s\n' 'The prepared MetalFX engine failed verification; repair it before using this runner.' >&2
      return 2
    fi
    using_prepared_metalfx_engine=true
  fi
  case "$(uname -s)" in
    Darwin)
      platform=Mac
      build_script="$engine_root/Engine/Build/BatchFiles/Mac/Build.sh"
      editor="$engine_root/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
      command_editor="$engine_root/Engine/Binaries/Mac/UnrealEditor-Cmd.app/Contents/MacOS/UnrealEditor-Cmd"
      [[ -x "$command_editor" ]] || command_editor="$engine_root/Engine/Binaries/Mac/UnrealEditor-Cmd"
      [[ -x "$command_editor" ]] || command_editor="$editor"
      ;;
    Linux)
      platform=Linux
      build_script="$engine_root/Engine/Build/BatchFiles/Linux/Build.sh"
      editor="$engine_root/Engine/Binaries/Linux/UnrealEditor"
      command_editor="$engine_root/Engine/Binaries/Linux/UnrealEditor-Cmd"
      [[ -x "$command_editor" ]] || command_editor="$editor"
      ;;
    *) printf '%s\n' 'These Unreal helpers support macOS and Linux; use UnrealBuildTool / UnrealEditor directly on Windows.' >&2; return 2 ;;
  esac
}

cinder_require_engine_tool() {
  local tool_path="$1"
  if [[ ! -x "$tool_path" ]]; then
    printf 'Required Unreal tool is missing or not executable: %s. Check UE_ROOT and build/install this engine first.\n' "$tool_path" >&2
    return 2
  fi
}

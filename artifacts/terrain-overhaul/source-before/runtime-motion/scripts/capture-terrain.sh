#!/usr/bin/env bash
set -euo pipefail

# One fresh process per canonical map; no Saved files are removed or reused as
# evidence. The editor build must include the canyon compilation-ready marker.
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
# shellcheck source=lib/unreal-engine.sh
source "$project_root/scripts/lib/unreal-engine.sh"
capture_python="${CINDERLINE_TEST_PYTHON:-python3}"
output_base="$project_root/artifacts/terrain-captures"
run_timeout=420
editor_pid=""
states=()

usage() {
  cat <<'USAGE'
Usage: ./scripts/capture-terrain.sh [sector map0 map1 map2 fog] [options]
Capture canonical terrain at 1440x900 (default: all five states).
sector frames a western expansion base, its mineral line and cliff entrance.

  --output DIR   Parent for a new, isolated run directory.
  --timeout N    Maximum capture wait per state in seconds (default: 420).
  -h, --help     Show this help.

Requires a current Development editor build and no running Unreal editor.
Uses scripts/lib/unreal-engine.sh discovery; UE_ROOT overrides the engine.
Each state gets its own log, process output, PNG and registry cache. Captures
require shader/mesh compilation proof and a fresh, complete 1440x900 PNG.
USAGE
}
fail() { printf '%s\n' "$@" >&2; exit 1; }
while (( $# )); do
  case "$1" in
    sector|map0|map1|map2|fog)
      case " ${states[*]:-} " in *" $1 "*) fail "Duplicate state: $1" ;; esac
      states+=("$1"); shift ;;
    --output) [[ $# -ge 2 && -n "$2" ]] || fail '--output requires a directory.'; output_base="$2"; shift 2 ;;
    --timeout) [[ $# -ge 2 ]] || fail '--timeout requires seconds.'; run_timeout="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) fail "Unknown argument: $1. Use --help." ;;
  esac
done
[[ "$run_timeout" =~ ^[1-9][0-9]*$ ]] || fail '--timeout must be a positive whole number.'
(( ${#states[@]} )) || states=(sector map0 map1 map2 fog)
command -v "$capture_python" >/dev/null || fail 'Python 3 is required; set CINDERLINE_TEST_PYTHON.'
[[ -f "$project_root/Cinderline.uproject" ]] || fail 'Cinderline.uproject is missing.'
cinder_find_engine "$project_root"
cinder_require_engine_tool "$command_editor"

require_no_editor() {
  if pgrep -f '[U]nrealEditor' >/dev/null; then
    fail 'Close the running Unreal editor/runtime before terrain capture.'
  fi
}
stop_editor() {
  [[ -n "$editor_pid" ]] || return 0
  if kill -0 "$editor_pid" 2>/dev/null; then
    kill -TERM "$editor_pid" 2>/dev/null || true
    local waited=0
    while kill -0 "$editor_pid" 2>/dev/null && (( waited < 30 )); do
      sleep 1
      waited=$((waited + 1))
    done
    kill -KILL "$editor_pid" 2>/dev/null || true
  fi
  wait "$editor_pid" 2>/dev/null || true
  editor_pid=""
}
trap stop_editor EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
require_no_editor
mkdir -p "$output_base"
output_base="$(cd -- "$output_base" && pwd -P)"
output_dir="$(mktemp -d "$output_base/run-$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")"
printf 'Engine: %s\nCaptures: %s\n' "$engine_root" "$output_dir"

# Read a complete PNG before copying. A previous Saved/CanyonPreview frame can
# never satisfy the per-process timestamp, even if the new request failed.
copy_ready_capture() {
  "$capture_python" - "$project_root" "$output_dir" "$state" <<'PY'
from pathlib import Path
import re, struct, sys
root, output = map(Path, sys.argv[1:3])
state = sys.argv[3]
try:
    log = (output / f"{state}.log").read_text(errors="replace")
    compiled = rf"CINDERLINE_CANYON_PREVIEW_COMPILED state={state} shader_jobs=\d+->0 static_meshes=\d+->0"
    ready = re.search(rf"CINDERLINE_CANYON_PREVIEW_READY state={state} [^\r\n]+", log)
    if not re.search(compiled, log) or not ready:
        raise ValueError("not ready")
    fields = dict(re.findall(r"(\w+)=([^\s]+)", ready[0]))
    expected_map = state[-1] if state.startswith("map") else "0"
    if fields.get("map") != expected_map or fields.get("canonical") != "1" or int(fields.get("landscape_visible", 0)) < 1:
        raise ValueError("canonical landscape missing")
    if state == "sector" and ("CINDERLINE_CANYON_SECTOR_READY buildings=4 mining_workers=3 army=5 ore_nodes=3" not in log or int(fields.get("fog_unknown", 0)) < 1):
        raise ValueError("sector fixture or ordinary fog missing")
    source = root / "Saved" / "CanyonPreview" / f"{state}.png"
    if source.stat().st_mtime_ns <= (output / f".{state}.started").stat().st_mtime_ns:
        raise ValueError("stale screenshot")
    data = source.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[-12:] != b"\x00\x00\x00\x00IEND\xaeB`\x82" or struct.unpack(">II", data[16:24]) != (1440, 900):
        raise ValueError("incomplete PNG or incorrect framebuffer")
    (output / f"{state}.png").write_bytes(data)
except (OSError, ValueError, struct.error):
    sys.exit(1)
PY
}

for state in "${states[@]}"; do
  require_no_editor
  log_file="$output_dir/$state.log"
  registry_dir="$output_dir/$state-registry"
  mkdir "$registry_dir"
  touch "$output_dir/.$state.started"
  printf 'Capturing %s at 1440x900; log: %s\n' "$state" "$log_file"
  "$command_editor" "$project_root/Cinderline.uproject" -game -unattended \
    -windowed -ForceRes -ResX=1440 -ResY=900 -nosound -nosplash -mobilehud \
    "-ExecCmds=t.MaxFPS 30,cinder.quality,cinder.canyonpreview $state" \
    "-AssetRegistryCacheRootFolder=$registry_dir" "-abslog=$log_file" \
    >"$output_dir/$state-process.log" 2>&1 &
  editor_pid=$!
  deadline=$((SECONDS + run_timeout))
  until copy_ready_capture; do
    kill -0 "$editor_pid" 2>/dev/null || fail "Editor exited before $state capture. Log: $log_file"
    (( SECONDS < deadline )) || fail "Timed out waiting for fresh $state capture and compilation proof. Log: $log_file"
    if [[ -f "$log_file" ]] && grep -qE 'CINDERLINE_CANYON_PREVIEW (refused=|failed=|.*capture=skipped)' "$log_file"; then
      fail "Canyon preview refused or failed for $state. Log: $log_file"
    fi
    sleep 1
  done
  stop_editor
  printf 'Captured %s: %s/%s.png\n' "$state" "$output_dir" "$state"
done
printf 'Terrain capture complete: %s\n' "$output_dir"

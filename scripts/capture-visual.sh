#!/usr/bin/env bash
set -euo pipefail

# Reproducible visual evidence for the art pass. Before this script the working
# capture invocation existed only as a command line recorded inside an old log,
# so no visual change could be proved: nothing pinned the framebuffer, nothing
# proved the frame was photographed after shader and mesh compilation finished,
# and nothing compared the result to what was accepted last time.

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
project_file="$project_root/Cinderline.uproject"
# shellcheck source=lib/unreal-engine.sh
source "$project_root/scripts/lib/unreal-engine.sh"

comparator="$project_root/scripts/lib/png_compare.py"
capture_python="${CINDERLINE_TEST_PYTHON:-python3}"
staging_dir="$project_root/Saved/VisualTarget"
registry_dir="$staging_dir/PreviewRegistry"
output_dir="$project_root/artifacts/visual-target/runtime"
baseline_dir="$project_root/artifacts/visual-baselines"
# Baselines are per-pixel comparisons, so the framebuffer is fixed rather than
# configurable. 1440x900 is the size every stored reference frame was taken at.
capture_res_x=1440
capture_res_y=900
# cinder.profile only accepts 120/180/600/1200/9000/18000 samples; 600 frames is
# 20 s at the 30 FPS cap, long enough for thermals to start moving.
profile_samples=600
# cinder.gpuprofile waits this long before instrumenting one frame, so the pass
# timings describe a warmed scene rather than the first frames after staging.
gpu_profile_delay=10
# Generous: the first rendered launch after an asset change compiles Metal
# shader maps before the fixture stages at all.
ready_timeout=180
run_timeout=420
run_compare=false
accept_baseline=false
compare_threshold=0.02
editor_pid=""

usage() {
  printf '%s\n' \
    'Capture reproducible Cinderline visual evidence from the rendered game.' \
    '' \
    '  ./scripts/capture-visual.sh shot [options]' \
    '      Stage the base and battle art fixtures, photograph both, and copy' \
    '      the frames beside the run log in artifacts/visual-target/runtime.' \
    '  ./scripts/capture-visual.sh profile [options]' \
    '      Stage the battle fixture live, sample frame pacing and one' \
    '      instrumented GPU frame, and reduce the markers to a one-line CSV.' \
    '' \
    'Options:' \
    '  --output DIR      Where captures, logs and the CSV are copied.' \
    '  --compare         shot only: measure each frame against its baseline.' \
    '  --accept          shot only: record the new frames as the baselines.' \
    '  --threshold N     Mean absolute difference gate for --compare (0.02).' \
    '  --baselines DIR   Accepted reference frames (artifacts/visual-baselines).' \
    '  --timeout N       Seconds allowed for the whole run (420).' \
    '' \
    'Both modes fail unless the log proves shader and static-mesh compilation' \
    'finished before the frame was taken; otherwise the capture is of gray' \
    'fallback material, and the profile is of the compiler, not the game.' \
    'The engine is the one scripts/lib/unreal-engine.sh selects; UE_ROOT wins.'
}

fail() {
  printf '%s\n' "$@" >&2
  exit 1
}

mode="${1:-}"
case "$mode" in
  shot|profile) shift ;;
  help|-h|--help) usage; exit 0 ;;
  '') usage >&2; exit 2 ;;
  *) printf 'Unknown mode: %s. Use shot, profile or --help.\n' "$mode" >&2; exit 2 ;;
esac

while (( $# )); do
  case "$1" in
    --output) [[ $# -ge 2 ]] || fail 'Option --output requires a directory.'; output_dir="$2"; shift 2 ;;
    --baselines) [[ $# -ge 2 ]] || fail 'Option --baselines requires a directory.'; baseline_dir="$2"; shift 2 ;;
    --threshold) [[ $# -ge 2 ]] || fail 'Option --threshold requires a number.'; compare_threshold="$2"; shift 2 ;;
    --timeout) [[ $# -ge 2 ]] || fail 'Option --timeout requires seconds.'; run_timeout="$2"; shift 2 ;;
    --compare) run_compare=true; shift ;;
    --accept) accept_baseline=true; run_compare=true; shift ;;
    --help|-h) usage; exit 0 ;;
    *) printf 'Unknown option: %s. Use --help.\n' "$1" >&2; exit 2 ;;
  esac
done

if [[ ! "$run_timeout" =~ ^[1-9][0-9]*$ ]]; then
  fail 'The --timeout value must be a positive whole number of seconds.'
fi
if [[ "$mode" == profile ]] && [[ "$run_compare" == true ]]; then
  fail 'Only shot mode produces frames; --compare and --accept do not apply to profile.'
fi

[[ -f "$project_file" ]] || fail "Cinderline.uproject is missing: $project_file"
[[ -f "$comparator" ]] || fail "The PNG comparator is missing: $comparator"
command -v "$capture_python" >/dev/null \
  || fail 'Python 3 is required. Set CINDERLINE_TEST_PYTHON if it is not on PATH.'

cinder_find_engine "$project_root"
cinder_require_engine_tool "$command_editor"
# Only the editor build carries the cinder.artpreview / cinder.profile commands;
# they are compiled out of Shipping, and the console would report them unknown.
if pgrep -f '[U]nrealEditor.*Cinderline\.uproject' >/dev/null; then
  fail 'Close the running Cinderline editor/runtime; two processes cannot share Saved/VisualTarget.'
fi

case "$platform" in
  Mac) capture_prefix=mac ;;
  Linux) capture_prefix=linux ;;
  *) fail "Visual capture supports macOS and Linux, not $platform." ;;
esac
# The recorded evidence in artifacts/visual-target/runtime is already named
# mac-capture.log / mac-profile.log / mac-base.png / mac-battle.png. Reuse those
# names so a new run replaces the file it supersedes instead of doubling it.
if [[ "$mode" == shot ]]; then log_basename=capture; else log_basename=profile; fi
log_file="$output_dir/$capture_prefix-$log_basename.log"
csv_file="$output_dir/$capture_prefix-profile.csv"

if [[ "$mode" == shot ]]; then
  exec_cmds='t.MaxFPS 30,cinder.quality,cinder.models,cinder.artpreview sweep'
  ready_wait_pattern='CINDERLINE_ART_PREVIEW_READY mode=battle shader_jobs='
  completion_pattern='CINDERLINE_ART_PREVIEW_CAPTURE mode=battle '
  ready_modes=(base battle)
else
  exec_cmds="cinder.artpreview battlelive,cinder.profile $profile_samples,cinder.gpuprofile $gpu_profile_delay"
  ready_wait_pattern='CINDERLINE_ART_PREVIEW_READY mode=battlelive shader_jobs='
  completion_pattern='CINDERLINE_FRAME_PROFILE_TIMING_SCOPE '
  ready_modes=(battlelive)
fi

stop_editor() {
  [[ -n "$editor_pid" ]] || return 0
  kill -0 "$editor_pid" 2>/dev/null || return 0
  # Unreal flushes the log and releases Saved/VisualTarget on a graceful
  # terminate; SIGKILL would leave a truncated log that proves nothing.
  kill -TERM "$editor_pid" 2>/dev/null || true
  local waited=0
  while (( waited < 30 )); do
    kill -0 "$editor_pid" 2>/dev/null || return 0
    sleep 1
    waited=$((waited + 1))
  done
  kill -KILL "$editor_pid" 2>/dev/null || true
}
trap stop_editor EXIT INT TERM

# 0: the pattern appeared. 1: the editor exited first. 2: the deadline passed.
wait_for_pattern() {
  local pattern="$1" limit="$2" waited=0
  while (( waited < limit )); do
    if [[ -f "$log_file" ]] && grep -qF "$pattern" "$log_file"; then return 0; fi
    if ! kill -0 "$editor_pid" 2>/dev/null; then
      # The final lines are flushed as the process exits, so look once more.
      if [[ -f "$log_file" ]] && grep -qF "$pattern" "$log_file"; then return 0; fi
      return 1
    fi
    sleep 1
    waited=$((waited + 1))
  done
  return 2
}

# The single guard against photographing gray fallback materials. Unreal renders
# whatever is resident, so an unfinished shader map produces a plausible-looking
# frame that is not the game's art; the same stall also dominates any timing
# taken beside it. CINDERLINE_ART_PREVIEW_READY is logged by
# FinishArtPreviewCompilation after FinishAllCompilation, and its
# shader_jobs=N->0 tail is the proof that nothing was still compiling.
require_art_preview_ready() {
  local preview_mode matched pending
  for preview_mode in "${ready_modes[@]}"; do
    matched="$(grep -cE \
      "CINDERLINE_ART_PREVIEW_READY mode=$preview_mode shader_jobs=[0-9]+->0 static_meshes=" \
      "$log_file" 2>/dev/null || true)"
    if [[ "$matched" != 0 ]]; then continue; fi
    pending="$(grep -E "CINDERLINE_ART_PREVIEW_READY mode=$preview_mode " "$log_file" 2>/dev/null \
      | tail -n 1 || true)"
    if [[ -n "$pending" ]]; then
      fail \
        "Refusing this capture: shader compilation had not finished for the $preview_mode frame." \
        "  $pending" \
        'The frame would show gray fallback materials, and any timing beside it would be' \
        'measuring the shader compiler. Re-run once the derived data cache is warm.' \
        "Log: $log_file"
    fi
    fail \
      "Refusing this capture: the log has no CINDERLINE_ART_PREVIEW_READY for mode=$preview_mode." \
      'That marker is the only proof the frame was taken after shader and static-mesh' \
      'compilation finished. Without it a capture may be of gray fallback materials and a' \
      'profile may be of the compiler, so neither can be evidence of an art change.' \
      'Confirm the editor build is current, that cinder.artpreview accepted the mode, and' \
      'that FinishArtPreviewCompilation runs on this path in CinderArtPreview.cpp.' \
      "Log: $log_file"
  done
}

mkdir -p "$output_dir" "$staging_dir" "$registry_dir"
# A stale frame from an earlier run must never be copied out as new evidence.
rm -f "$staging_dir/base.png" "$staging_dir/battle.png" "$log_file"

printf 'Engine: %s\nMode: %s\nFramebuffer: %sx%s\nLog: %s\n' \
  "$engine_root" "$mode" "$capture_res_x" "$capture_res_y" "$log_file"
printf 'ExecCmds: %s\n' "$exec_cmds"

"$command_editor" "$project_file" -game -windowed -ForceRes \
  "-ResX=$capture_res_x" "-ResY=$capture_res_y" -nosound -nosplash -mobilehud \
  "-ExecCmds=$exec_cmds" \
  "-AssetRegistryCacheRootFolder=$registry_dir" \
  "-abslog=$log_file" &
editor_pid=$!

wait_result=0
wait_for_pattern "$ready_wait_pattern" "$ready_timeout" || wait_result=$?
case "$wait_result" in
  1) stop_editor; fail \
       'The editor exited before the art preview reported that compilation finished.' \
       "Log: $log_file" ;;
  2) stop_editor; fail \
       "No art-preview readiness marker within ${ready_timeout}s; the fixture never staged." \
       "Log: $log_file" ;;
esac
wait_result=0
wait_for_pattern "$completion_pattern" "$run_timeout" || wait_result=$?
case "$wait_result" in
  1) stop_editor; fail "The editor exited before completing the $mode run. Log: $log_file" ;;
  2) stop_editor; fail "The $mode run did not finish within ${run_timeout}s. Log: $log_file" ;;
esac
stop_editor
wait "$editor_pid" 2>/dev/null || true
editor_pid=""

require_art_preview_ready

if [[ "$mode" == shot ]]; then
  # The screenshot request is serviced a frame after the capture marker, so the
  # file can trail the log line it belongs to by a fraction of a second.
  for frame in base battle; do
    waited=0
    while [[ ! -s "$staging_dir/$frame.png" ]] && (( waited < 30 )); do
      sleep 1
      waited=$((waited + 1))
    done
    [[ -s "$staging_dir/$frame.png" ]] \
      || fail "The $frame frame was never written: $staging_dir/$frame.png." "Log: $log_file"
    cp "$staging_dir/$frame.png" "$output_dir/$capture_prefix-$frame.png"
    printf 'Captured %s: %s\n' "$frame" "$output_dir/$capture_prefix-$frame.png"
  done
  if [[ "$run_compare" == true ]]; then
    mkdir -p "$baseline_dir"
    compare_status=0
    for frame in base battle; do
      compare_args=(
        "$comparator" --label "$frame" --threshold "$compare_threshold"
        --baseline "$baseline_dir/$frame.png"
        --candidate "$output_dir/$capture_prefix-$frame.png"
      )
      if [[ "$accept_baseline" == true ]]; then compare_args+=(--accept); fi
      # Both frames are always reported: knowing only that the first regressed
      # hides whether the change is global or local to one fixture.
      "$capture_python" "${compare_args[@]}" || compare_status=1
    done
    (( compare_status == 0 )) \
      || fail 'At least one frame differs from its accepted baseline beyond the threshold.'
  fi
else
  "$capture_python" - "$log_file" "$csv_file" <<'PY'
"""Reduce one profiling run's markers to a single comparable CSV row.

Only the last occurrence of each marker is used: cinder.artpreview restages the
fixture, so earlier lines describe the menu, not the profiled scene.
"""

from datetime import datetime, timezone
from pathlib import Path
import re
import sys

log_path, csv_path = Path(sys.argv[1]), Path(sys.argv[2])
text = log_path.read_text(encoding="utf-8", errors="replace")


def last(pattern):
    found = re.findall(pattern, text)
    return found[-1] if found else ""


def field(marker, name, extra=""):
    return last(rf"{marker}[^\r\n]*?{extra}\b{name}=([^\s,;]+)")


def timing(metric, name):
    return last(
        rf"CINDERLINE_FRAME_PROFILE_TIMING metric={metric} [^\r\n]*?\b{name}=([^\s,;]+)"
    )


row = {
    "captured_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "map": field(r"CINDERLINE_FRAME_PROFILE result", "map"),
    "viewport_px": field(r"CINDERLINE_FRAME_PROFILE result", "viewport_px"),
    "samples": field(r"CINDERLINE_FRAME_PROFILE result", "samples"),
    "frame_mean_ms": field(r"CINDERLINE_FRAME_PROFILE result", "mean_ms"),
    "frame_median_ms": field(r"CINDERLINE_FRAME_PROFILE result", "median_ms"),
    "frame_p95_ms": field(r"CINDERLINE_FRAME_PROFILE result", "p95_ms"),
    "fps": field(r"CINDERLINE_FRAME_PROFILE result", "fps"),
    "foreground": field(r"CINDERLINE_FRAME_PROFILE result", "foreground"),
    "game_thread_mean_ms": timing("game_thread", "mean_ms"),
    "render_thread_mean_ms": timing("render_thread", "mean_ms"),
    "rhi_thread_mean_ms": timing("rhi_thread", "mean_ms"),
    "gpu_mean_ms": timing("gpu", "mean_ms"),
    "gpu_p95_ms": timing("gpu", "p95_ms"),
    "models_loaded": field("CINDERLINE_RENDER_MODELS", "loaded"),
    "model_batches": field("CINDERLINE_RENDER_MODELS", "model_batches"),
    "rendered_model_entities": field("CINDERLINE_RENDER_MODELS", "rendered_model_entities"),
    "rendered_fallback_entities": field("CINDERLINE_RENDER_MODELS", "rendered_fallback_entities"),
    "instance_passes": field("CINDERLINE_INSTANCE_SUBMISSIONS", "passes"),
    "instance_transforms": field("CINDERLINE_INSTANCE_SUBMISSIONS", "transforms"),
    "instance_added": field("CINDERLINE_INSTANCE_SUBMISSIONS", "added"),
    "instance_removed": field("CINDERLINE_INSTANCE_SUBMISSIONS", "removed"),
    "instance_full_rebuilds": field("CINDERLINE_INSTANCE_SUBMISSIONS", "full_rebuilds"),
    "log": log_path.name,
}

missing = [name for name, value in row.items() if value == ""]
if missing:
    raise SystemExit(
        "Profiling markers are incomplete; refusing to write a partial row. "
        f"Missing: {', '.join(missing)}. Log: {log_path}"
    )
# No value can contain a comma or quote: every field above is a single log
# token, so the row needs no CSV quoting and stays greppable.
csv_path.write_text(
    ",".join(row) + "\n" + ",".join(row.values()) + "\n", encoding="utf-8"
)
print(",".join(row.values()))
PY
  printf 'Profile CSV: %s\n' "$csv_file"
fi

printf 'Visual capture complete: %s\n' "$output_dir"

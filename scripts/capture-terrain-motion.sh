#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
# shellcheck source=lib/unreal-engine.sh
source "$project_root/scripts/lib/unreal-engine.sh"
capture_python="${CINDERLINE_TEST_PYTHON:-python3}"
output_base="$project_root/artifacts/terrain-motion"
frame_count=90
capture_size=1440x900
run_timeout=420
editor_pid=""

usage() {
  cat <<'USAGE'
Usage: ./scripts/capture-terrain-motion.sh [options]
Capture one canonical sectorlive sequence and encode a verified 30 FPS MP4.

  --frames N     Number of consecutive frames, 60 through 90 (default: 90).
  --size WxH     Framebuffer dimensions, both positive and even (1440x900).
  --output DIR   Parent for a new, isolated run directory.
  --timeout N    Maximum capture wait in seconds (default: 420).
  -h, --help     Show this help without launching Unreal.

Requires a current Development editor build, Python 3, ffmpeg, ffprobe, and
no running Unreal editor. Uses scripts/lib/unreal-engine.sh; UE_ROOT overrides
engine discovery. Keeps source frames intact. Outputs fresh PNGs, motion.mp4,
frames.csv, manifest.json, logs, and an isolated asset registry cache.
USAGE
}
fail() { printf '%s\n' "$@" >&2; exit 1; }
while (( $# )); do
  case "$1" in
    --frames|--size|--output|--timeout)
      [[ $# -ge 2 && -n "$2" ]] || fail "$1 requires a value."
      case "$1" in
        --frames) frame_count="$2" ;; --size) capture_size="$2" ;;
        --output) output_base="$2" ;; --timeout) run_timeout="$2" ;;
      esac
      shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) fail "Unknown argument: $1. Use --help." ;;
  esac
done
[[ "$frame_count" =~ ^(6[0-9]|[78][0-9]|90)$ ]] || fail '--frames must be 60 through 90.'
[[ "$capture_size" =~ ^[1-9][0-9]*x[1-9][0-9]*$ ]] || fail '--size must be WIDTHxHEIGHT.'
[[ "$run_timeout" =~ ^[1-9][0-9]*$ ]] || fail '--timeout must be a positive whole number.'
for tool in "$capture_python" ffmpeg ffprobe pgrep; do
  command -v "$tool" >/dev/null || fail "Required tool is missing: $tool"
done
width="${capture_size%x*}"; height="${capture_size#*x}"
"$capture_python" - "$width" "$height" <<'PY'
import sys
if any(int(value) < 2 or int(value) % 2 for value in sys.argv[1:]):
    raise SystemExit('--size dimensions must be positive and even for MP4.')
PY
[[ -f "$project_root/Cinderline.uproject" ]] || fail 'Cinderline.uproject is missing.'
cinder_find_engine "$project_root"
cinder_require_engine_tool "$command_editor"
if pgrep -f '[U]nrealEditor' >/dev/null; then
  fail 'Close the running Unreal editor/runtime before terrain motion capture.'
fi
stop_editor() {
  [[ -n "$editor_pid" ]] || return 0
  if kill -0 "$editor_pid" 2>/dev/null; then
    kill -TERM "$editor_pid" 2>/dev/null || true
    local waited=0
    while kill -0 "$editor_pid" 2>/dev/null && (( waited < 30 )); do
      sleep 1; waited=$((waited + 1))
    done
    kill -KILL "$editor_pid" 2>/dev/null || true
  fi
  wait "$editor_pid" 2>/dev/null || true
  editor_pid=""
}
trap stop_editor EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p "$output_base"
output_base="$(cd -- "$output_base" && pwd -P)"
output_dir="$(mktemp -d "$output_base/run-$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")"
log_file="$output_dir/sectorlive.log"
mkdir "$output_dir/registry"
touch "$output_dir/.started"
printf 'Engine: %s\nCapture: %s (%s frames, %s)\n' "$engine_root" "$output_dir" "$frame_count" "$capture_size"
"$command_editor" "$project_root/Cinderline.uproject" -game -unattended \
  -windowed -ForceRes "-ResX=$width" "-ResY=$height" -nosound -nosplash -mobilehud \
  "-ExecCmds=t.MaxFPS 30,cinder.quality,cinder.canyonpreview sectorlive $frame_count" \
  "-AssetRegistryCacheRootFolder=$output_dir/registry" "-abslog=$log_file" \
  >"$output_dir/sectorlive-process.log" 2>&1 &
editor_pid=$!
deadline=$((SECONDS + run_timeout))
while :; do
  if [[ -f "$log_file" ]]; then
    if grep -qE 'CINDERLINE_CANYON_MOTION_ABORT|CINDERLINE_CANYON_PREVIEW (refused=|failed=|.*capture=skipped)' "$log_file"; then
      fail "Motion capture aborted or refused. Log: $log_file"
    fi
    grep -q 'CINDERLINE_CANYON_MOTION_DONE' "$log_file" && break
  fi
  kill -0 "$editor_pid" 2>/dev/null || fail "Editor exited before motion completion. Log: $log_file"
  (( SECONDS < deadline )) || fail "Timed out waiting for motion completion. Log: $log_file"
  sleep 1
done
stop_editor

"$capture_python" - "$project_root" "$output_dir" "$frame_count" "$width" "$height" <<'PY'
from pathlib import Path
import csv, hashlib, json, math, re, struct, sys, zlib
root, output = map(Path, sys.argv[1:3])
count, width, height = map(int, sys.argv[3:])
log = (output / 'sectorlive.log').read_text(errors='replace')
def require(condition, message):
    if not condition:
        raise SystemExit(f'Motion validation failed: {message}')
def one(pattern):
    matches = list(re.finditer(pattern, log))
    require(len(matches) == 1, f'expected exactly one {pattern}')
    return matches[0]
require('CINDERLINE_CANYON_MOTION_ABORT' not in log, 'abort marker present')
compiled = one(r'CINDERLINE_CANYON_PREVIEW_COMPILED state=sectorlive shader_jobs=\d+->0 static_meshes=\d+->0')
sector = one(r'CINDERLINE_CANYON_SECTOR_READY buildings=4 mining_workers=3 army=5 ore_nodes=3[^\r\n]*')
ready = one(r'CINDERLINE_CANYON_PREVIEW_READY state=sectorlive [^\r\n]+')
fields = dict(re.findall(r'(\w+)=([^\s]+)', ready[0]))
require(fields.get('map') == '0' and fields.get('canonical') == '1' and int(fields.get('landscape_visible', 0)) > 0 and int(fields.get('fog_unknown', 0)) > 0, 'canonical landscape or ordinary fog missing')
begin = one(r'CINDERLINE_CANYON_MOTION_BEGIN frames=(\d+) fps=30 fixed_delta=0\.033333333 directory=([^\r\n]+)')
done = one(r'CINDERLINE_CANYON_MOTION_DONE frames=(\d+) fps=30\b')
require(int(begin[1]) == int(done[1]) == count, 'frame count markers disagree')
require(compiled.start() < begin.start() < done.start() and sector.start() < begin.start(), 'readiness markers out of order')
motion_root = (root / 'Saved/CanyonPreview/Motion').resolve(strict=True)
directory = Path(begin[2])
require(directory.is_absolute(), 'source directory is not absolute')
require(re.fullmatch(r'[0-9a-fA-F]{32}|[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}', directory.name), 'source directory is not a GUID')
require(directory == directory.resolve(strict=True) and directory.parent == motion_root, 'source directory escapes canonical Motion root')
frames = list(re.finditer(r'CINDERLINE_CANYON_MOTION_FRAME index=(\d+) tick=(\d+) time=([^\s]+) file=([^\r\n]+)', log))
require(len(frames) == log.count('CINDERLINE_CANYON_MOTION_FRAME') == count, 'wrong number of frame records')
require(ready.end() < frames[0].start(), 'canonical terrain was not ready before capture')
stamp = (output / '.started').stat().st_mtime_ns
records, pending = [], []
for index, frame in enumerate(frames):
    require(int(frame[1]) == index and begin.end() < frame.start() < done.start(), 'unordered frame indices or markers')
    tick, seconds = int(frame[2]), float(frame[3])
    require(math.isfinite(seconds) and abs(seconds - tick * .05) <= .0001, 'simulation time does not match tick')
    require(not records or tick >= records[-1]['tick'], 'simulation ticks decreased')
    source = Path(frame[4])
    require(source == directory / f'frame-{index:04d}.png' and source.resolve(strict=True) == source, 'unexpected frame path')
    require(source.stat().st_mtime_ns > stamp, 'stale frame')
    data = source.read_bytes()
    require(data[:8] == b'\x89PNG\r\n\x1a\n' and data[8:16] == b'\x00\x00\x00\rIHDR', 'invalid PNG header')
    require(len(data) >= 45 and struct.unpack('>II', data[16:24]) == (width, height), 'incorrect PNG dimensions')
    offset, chunks = 8, []
    while offset + 12 <= len(data):
        length = struct.unpack('>I', data[offset:offset + 4])[0]
        end = offset + 12 + length
        require(end <= len(data), 'incomplete PNG chunk')
        chunk = data[offset + 4:end - 4]
        require(zlib.crc32(chunk) == struct.unpack('>I', data[end - 4:end])[0], 'PNG checksum mismatch')
        chunks.append(chunk[:4]); offset = end
    require(offset == len(data) and b'IDAT' in chunks and chunks[-1] == b'IEND' and data[-12:] == b'\x00\x00\x00\x00IEND\xaeB`\x82', 'missing or incomplete PNG IEND')
    records.append(dict(index=index, tick=tick, time=seconds, file=f'frames/{source.name}', width=width, height=height, sha256=hashlib.sha256(data).hexdigest()))
    pending.append((source.name, data))
require(records[-1]['tick'] > records[0]['tick'], 'simulation did not advance')
(output / 'frames').mkdir()
for name, data in pending:
    (output / 'frames' / name).write_bytes(data)
with (output / 'frames.csv').open('w', newline='') as handle:
    writer = csv.DictWriter(handle, fieldnames=['index', 'tick', 'time'], extrasaction='ignore')
    writer.writeheader(); writer.writerows(records)
manifest = dict(state='sectorlive', source_directory=str(directory), launch_timestamp_ns=stamp, frame_count=count, fps=30, width=width, height=height, duration=count / 30, tick_start=records[0]['tick'], tick_end=records[-1]['tick'], markers=dict(compiled=compiled[0], sector=sector[0], ready=ready[0], begin=begin[0], done=done[0]), frames=records)
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
PY
ffmpeg -nostdin -hide_banner -loglevel error -n -framerate 30 -start_number 0 \
  -i "$output_dir/frames/frame-%04d.png" -frames:v "$frame_count" \
  -c:v libx264 -pix_fmt yuv420p -r 30 -movflags +faststart "$output_dir/motion.mp4" \
  >"$output_dir/ffmpeg.log" 2>&1
ffprobe -v error -select_streams v:0 -count_frames \
  -show_entries stream=codec_name,pix_fmt,width,height,nb_read_frames,r_frame_rate,avg_frame_rate,duration:format=duration \
  -of json "$output_dir/motion.mp4" >"$output_dir/ffprobe.json"
"$capture_python" - "$output_dir" <<'PY'
from pathlib import Path
from fractions import Fraction
import hashlib, json, sys
output = Path(sys.argv[1])
manifest = json.loads((output / 'manifest.json').read_text())
probe = json.loads((output / 'ffprobe.json').read_text())
streams = probe.get('streams', [])
if len(streams) != 1:
    raise SystemExit('MP4 verification failed: expected one video stream.')
stream = streams[0]
valid = (stream.get('codec_name') == 'h264' and stream.get('pix_fmt') == 'yuv420p'
         and int(stream.get('nb_read_frames', -1)) == manifest['frame_count']
         and all(stream.get(key) == manifest[key] for key in ('width', 'height'))
         and all(Fraction(stream.get(key, '0')) == 30 for key in ('r_frame_rate', 'avg_frame_rate'))
         and all(abs(float(value) - manifest['duration']) <= .0015 for value in (stream.get('duration', -1), probe.get('format', {}).get('duration', -1))))
if not valid:
    raise SystemExit('MP4 verification failed: frame count, size, duration, format, or FPS mismatch.')
manifest['video'] = dict(file='motion.mp4', verified=True, sha256=hashlib.sha256((output / 'motion.mp4').read_bytes()).hexdigest(), probe=probe)
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
PY
printf 'Verified terrain motion capture: %s/motion.mp4\n' "$output_dir"

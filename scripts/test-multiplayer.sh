#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
project_file="$project_dir/Cinderline.uproject"
build_dir="${CINDERLINE_MULTIPLAYER_BUILD_DIR:-$project_dir/build/multiplayer}"
# shellcheck source=lib/unreal-engine.sh
source "$project_dir/scripts/lib/unreal-engine.sh"

build=true
allow_idevice_warning=false
for argument in "$@"; do
  case "$argument" in
    --no-build) build=false ;;
    --allow-idevice-id-warning) allow_idevice_warning=true ;;
    -h|--help)
      printf '%s\n' \
        'Usage: ./scripts/test-multiplayer.sh [--no-build] [--allow-idevice-id-warning]' \
        'Builds the native match worker and CinderlineEditor, starts an owned loopback server,' \
        'then runs exactly the two-player and four-player transport tests through UnrealEditor with NullRHI.' \
        'Expected tests and filter: scripts/lib/unreal_suites.json (multiplayer).' \
        '--no-build uses existing native and Unreal binaries.' \
        '--allow-idevice-id-warning permits only the documented idevice_id Bad CPU host-warning pair.' \
        'UE_ROOT overrides the shared verified prepared-engine default.' \
        'CINDERLINE_MULTIPLAYER_BUILD_DIR selects worker output; CINDERLINE_BUILD_JOBS is 1 or 2 (default 2).' \
        'CINDERLINE_TEST_PYTHON selects Python 3 for shared report validation.'
      exit 0
      ;;
    *) printf 'Unknown option: %s. Use --no-build, --allow-idevice-id-warning, or --help.\n' "$argument" >&2; exit 2 ;;
  esac
done
validation_python="${CINDERLINE_TEST_PYTHON:-python3}"
if ! command -v "$validation_python" >/dev/null; then
  printf '%s\n' 'Python 3 is required. Set CINDERLINE_TEST_PYTHON if needed.' >&2; exit 2
fi
validator="$project_dir/scripts/validate-unreal-report.py"
manifest="$project_dir/scripts/lib/unreal_suites.json"
test_filter="$("$validation_python" "$validator" --manifest "$manifest" --suite multiplayer --print-filter)"
cinder_find_engine "$project_dir"
cinder_require_engine_tool "$command_editor"
if [[ "$build" == true ]]; then cinder_require_engine_tool "$build_script"; fi
build_jobs="${CINDERLINE_BUILD_JOBS:-2}"
if [[ "$build_jobs" != 1 && "$build_jobs" != 2 ]]; then
  printf '%s\n' 'CINDERLINE_BUILD_JOBS must be 1 or 2 for bounded multiplayer builds.' >&2; exit 2
fi

if pgrep -f '[U]nrealEditor.*Cinderline\.uproject' >/dev/null; then
  printf '%s\n' 'Close the running Cinderline editor/runtime before launching multiplayer automation.' >&2
  exit 2
fi

if ! command -v node >/dev/null || ! command -v npm >/dev/null; then
  printf '%s\n' 'Node.js 22 or newer and npm are required for the local game server.' >&2
  exit 2
fi
node_major="$(node -p 'Number(process.versions.node.split(".")[0])')"
if [[ ! "$node_major" =~ ^[0-9]+$ ]] || (( node_major < 22 )); then
  printf 'Node.js 22 or newer is required; found %s.\n' "$(node --version)" >&2
  exit 2
fi

if [[ ! -d "$project_dir/Server/node_modules/ws" ]]; then
  npm --prefix "$project_dir/Server" install --ignore-scripts --no-audit --no-fund
fi

if [[ "$build" == true ]]; then
  cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DCINDERLINE_BUILD_NATIVE=OFF -DCINDERLINE_BUILD_SERVER=ON
  cmake --build "$build_dir" --target CinderlineMatchWorker --parallel "$build_jobs"
  UE_ROOT="$engine_root" "$project_dir/scripts/unreal.sh" build "-MaxParallelActions=$build_jobs"
fi

worker=""
for candidate in "$build_dir/CinderlineMatchWorker" "$build_dir/Release/CinderlineMatchWorker"; do
  if [[ -x "$candidate" ]]; then worker="$candidate"; break; fi
done
if [[ -z "$worker" ]]; then
  printf 'CinderlineMatchWorker was not found in %s. Run without --no-build first.\n' "$build_dir" >&2
  exit 2
fi

port="$(node -e 'const net=require("node:net");const s=net.createServer();s.listen(0,"127.0.0.1",()=>{console.log(s.address().port);s.close();});')"
if [[ ! "$port" =~ ^[0-9]+$ ]] || (( port < 1 || port > 65535 )); then
  printf 'Could not reserve a loopback test port: %s\n' "$port" >&2
  exit 1
fi

run_name="$(date -u '+%Y%m%dT%H%M%SZ')-$$"
report_dir="$project_dir/Saved/Automation/Multiplayer/$run_name"
mkdir -p "$report_dir"
server_pid=""
cleanup() {
  local status=$?
  trap - EXIT
  if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
    kill -TERM "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

HOST=127.0.0.1 PORT="$port" CINDERLINE_MATCH_WORKER="$worker" \
  node "$project_dir/Server/index.js" > "$report_dir/server.log" 2>&1 &
server_pid=$!

node --input-type=module - "http://127.0.0.1:$port/healthz" <<'NODE'
const url = process.argv[2];
for (let attempt = 0; attempt < 100; ++attempt) {
  try {
    const response = await fetch(url);
    const body = await response.json();
    if (response.status === 200 && body && body.status === "ok") process.exit(0);
  } catch {}
  await new Promise(resolve => setTimeout(resolve, 50));
}
console.error(`Local game server did not become healthy at ${url}`);
process.exit(1);
NODE
if ! kill -0 "$server_pid" 2>/dev/null; then
  printf 'Local game server exited before Unreal started. Inspect %s\n' "$report_dir/server.log" >&2
  exit 1
fi

endpoint="ws://127.0.0.1:$port/play"
printf 'Running Unreal multiplayer transport automation against %s\n' "$endpoint"
if CINDERLINE_TEST_PUBLIC=0 CINDERLINE_TEST_SERVER="$endpoint" \
  "$command_editor" "$project_file" /Engine/Maps/Entry -unattended -nop4 -nosplash -NullRHI -nosound \
    -stdout -FullStdOutLogOutput \
    "-ExecCmds=Automation RunTests $test_filter; Quit" \
    "-ReportExportPath=$report_dir" "-abslog=$report_dir/UnrealEditor.log" \
    > "$report_dir/stdout.log" 2>&1; then
  if [[ ! -s "$report_dir/index.json" ]]; then
    printf 'Editor exited without an automation report. Inspect %s\n' "$report_dir/stdout.log" >&2
    exit 1
  fi
else
  result=$?
  printf 'Unreal multiplayer automation failed with status %s. Inspect %s\n' "$result" "$report_dir/stdout.log" >&2
  exit "$result"
fi

validation_args=(--manifest "$manifest" --suite multiplayer)
if [[ "$allow_idevice_warning" == true ]]; then validation_args+=(--allow-idevice-id-warning); fi
"$validation_python" "$validator" "${validation_args[@]}" "$report_dir/index.json"

printf 'Unreal multiplayer automation passed. Report: %s/index.json\n' "$report_dir"

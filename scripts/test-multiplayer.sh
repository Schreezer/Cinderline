#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
project_file="$project_dir/Cinderline.uproject"
build_dir="${CINDERLINE_MULTIPLAYER_BUILD_DIR:-$project_dir/build/multiplayer}"
engine_root="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
engine_root="${engine_root%/}"
if [[ "$(basename -- "$engine_root")" == Engine ]]; then engine_root="$(dirname -- "$engine_root")"; fi

build=true
case "${1:-}" in
  '') ;;
  --no-build) build=false ;;
  -h|--help)
    printf '%s\n' \
      'Usage: ./scripts/test-multiplayer.sh [--no-build]' \
      'Builds the native match worker and CinderlineEditor, starts an owned loopback server,' \
      'then runs only Cinderline.Online.Transport through UnrealEditor-Cmd with NullRHI.' \
      '--no-build uses existing native and Unreal binaries.' \
      'UE_ROOT and CINDERLINE_MULTIPLAYER_BUILD_DIR may select compatible installations.'
    exit 0
    ;;
  *) printf '%s\n' 'Use --no-build, --help, or no argument.' >&2; exit 2 ;;
esac

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

case "$(uname -s)" in
  Darwin)
    platform=Mac
    editor="$engine_root/Engine/Binaries/Mac/UnrealEditor-Cmd"
    [[ -x "$editor" ]] || editor="$engine_root/Engine/Binaries/Mac/UnrealEditor-Cmd.app/Contents/MacOS/UnrealEditor-Cmd"
    build_script="$engine_root/Engine/Build/BatchFiles/Mac/Build.sh"
    ;;
  Linux)
    platform=Linux
    editor="$engine_root/Engine/Binaries/Linux/UnrealEditor-Cmd"
    [[ -x "$editor" ]] || editor="$engine_root/Engine/Binaries/Linux/UnrealEditor"
    build_script="$engine_root/Engine/Build/BatchFiles/Linux/Build.sh"
    ;;
  *) printf '%s\n' 'This runner supports macOS and Linux.' >&2; exit 2 ;;
esac
if [[ ! -x "$editor" || ! -x "$build_script" ]]; then
  printf 'A compatible Unreal installation was not found under %s. Set UE_ROOT.\n' "$engine_root" >&2
  exit 2
fi

if [[ ! -d "$project_dir/Server/node_modules/ws" ]]; then
  npm --prefix "$project_dir/Server" install --ignore-scripts --no-audit --no-fund
fi

if [[ "$build" == true ]]; then
  cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DCINDERLINE_BUILD_NATIVE=OFF
  cmake --build "$build_dir" --target CinderlineMatchWorker --parallel 2
  "$build_script" CinderlineEditor "$platform" Development "$project_file" -WaitMutex -MaxParallelActions=2
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
  "$editor" "$project_file" /Engine/Maps/Entry -unattended -nop4 -nosplash -NullRHI -nosound \
    -stdout -FullStdOutLogOutput \
    '-ExecCmds=Automation RunTests Cinderline.Online.Transport; Quit' \
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

node --input-type=module - "$report_dir/index.json" <<'NODE'
import { readFileSync } from "node:fs";

const reportPath = process.argv[2];
let report;
try {
  report = JSON.parse(readFileSync(reportPath, "utf8").replace(/^\uFEFF/, ""));
} catch (error) {
  throw new Error(`Cannot read Unreal automation report ${reportPath}: ${error.message}`);
}
const fail = message => {
  throw new Error(`Unreal multiplayer verification failed: ${message}. Report: ${reportPath}`);
};
if (!report || typeof report !== "object" || Array.isArray(report)) fail("report root is not an object");
const expectedPath = "Cinderline.Online.Transport";
const expectedCounts = {
  succeeded: 1,
  succeededWithWarnings: 0,
  failed: 0,
  notRun: 0,
  inProcess: 0,
};
for (const [field, expected] of Object.entries(expectedCounts)) {
  if (!Number.isInteger(report[field]) || report[field] !== expected)
    fail(`${field}=${JSON.stringify(report[field])}, expected ${expected}`);
}
if (!Array.isArray(report.tests) || report.tests.length !== 1)
  fail("report must contain exactly one test");
const test = report.tests[0];
if (!test || test.fullTestPath !== expectedPath) fail(`unexpected test path ${JSON.stringify(test?.fullTestPath)}`);
if (test.state !== "Success") fail(`${expectedPath} ended in state ${JSON.stringify(test.state)}`);
for (const field of ["errors", "warnings"]) {
  if (!Number.isInteger(test[field]) || test[field] !== 0)
    fail(`${expectedPath} has nonzero or missing ${field}`);
}
console.log(`Verified ${expectedPath}: success with no errors or warnings.`);
NODE

printf 'Unreal multiplayer automation passed. Report: %s/index.json\n' "$report_dir"

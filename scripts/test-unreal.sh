#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
project_file="$project_dir/Cinderline.uproject"
engine_root="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
engine_root="${engine_root%/}"
if [[ "$(basename -- "$engine_root")" == Engine ]]; then engine_root="$(dirname -- "$engine_root")"; fi

if [[ "${1:-}" == --help || "${1:-}" == -h ]]; then
  printf '%s\n' \
    'Build first with: ./scripts/unreal.sh build' \
    'Close the Cinderline editor/runtime, then run: ./scripts/test-unreal.sh' \
    'UE_ROOT may select another compatible UE 5.8 installation.' \
    'Runs transient-world integration tests through UnrealEditor-Cmd with NullRHI.' \
    'Reports go to a new Saved/Automation/Integration directory. Player saves are untouched.'
  exit 0
fi
if [[ $# -ne 0 ]]; then
  printf '%s\n' 'No positional arguments are supported. Use --help or set UE_ROOT.' >&2
  exit 2
fi

if pgrep -f '[U]nrealEditor.*Cinderline\.uproject' >/dev/null; then
  printf '%s\n' 'Close the running Cinderline editor/runtime before launching integration automation.' >&2
  exit 2
fi

case "$(uname -s)" in
  Darwin) editor="$engine_root/Engine/Binaries/Mac/UnrealEditor-Cmd" ;;
  Linux)
    editor="$engine_root/Engine/Binaries/Linux/UnrealEditor-Cmd"
    [[ -x "$editor" ]] || editor="$engine_root/Engine/Binaries/Linux/UnrealEditor"
    ;;
  *) printf '%s\n' 'This runner supports macOS and Linux. Use UnrealEditor-Cmd directly on Windows.' >&2; exit 2 ;;
esac
if [[ ! -x "$editor" ]]; then
  printf 'UnrealEditor-Cmd not found at %s. Set UE_ROOT to the engine installation.\n' "$editor" >&2
  exit 2
fi
validation_python="${CINDERLINE_TEST_PYTHON:-python3}"
if ! command -v "$validation_python" >/dev/null; then
  printf '%s\n' 'Python 3 is required to verify Unreal automation results. Set CINDERLINE_TEST_PYTHON if needed.' >&2
  exit 2
fi

run_name="$(date -u '+%Y%m%dT%H%M%SZ')-$$"
report_dir="$project_dir/Saved/Automation/Integration/$run_name"
mkdir -p "$report_dir"
printf 'Running Cinderline integration automation. Report directory: %s\n' "$report_dir"
# This is Automation's queued Quit. UE 5.8 waits for completion/report generation;
# it is not an immediate engine Quit command. Validate JSON even after exit zero.
if "$editor" "$project_file" /Engine/Maps/Entry -unattended -nop4 -nosplash -NullRHI -nosound \
  -stdout -FullStdOutLogOutput \
  '-ExecCmds=Automation RunTests Cinderline.Integration; Quit' \
  "-ReportExportPath=$report_dir" "-abslog=$report_dir/UnrealEditor.log" \
  > "$report_dir/stdout.log" 2>&1; then
  if [[ ! -s "$report_dir/index.json" ]]; then
    printf 'Editor exited without an automation report. Inspect %s\n' "$report_dir/stdout.log" >&2
    exit 1
  fi
else
  result=$?
  printf 'Unreal integration automation failed with status %s. Inspect %s\n' "$result" "$report_dir/stdout.log" >&2
  exit "$result"
fi
"$validation_python" - "$report_dir/index.json" <<'PY'
import json
import sys
from pathlib import Path

report_path = Path(sys.argv[1])
try:
    with report_path.open(encoding="utf-8-sig") as report_file:
        report = json.load(report_file)
except (OSError, ValueError) as error:
    raise SystemExit(f"Cannot read Unreal automation report {report_path}: {error}")

def fail(message):
    raise SystemExit(f"Unreal integration verification failed: {message}. Report: {report_path}")

# UE 5.8 FAutomatedTestPassResults / FAutomatedTestResult export these keys
# through FJsonObjectConverter using lower-initial case and enum name strings.
if not isinstance(report, dict):
    fail("report root is not an object")
required_counts = {
    "succeeded": 2,
    "succeededWithWarnings": 0,
    "failed": 0,
    "notRun": 0,
    "inProcess": 0,
}
for field, expected_count in required_counts.items():
    actual = report.get(field)
    if type(actual) is not int or actual != expected_count:
        fail(f"{field}={actual!r}, expected {expected_count}")

expected_paths = {
    "Cinderline.Integration.WorldLifecycle",
    "Cinderline.Integration.EconomyAndProduction",
}
tests = report.get("tests")
if not isinstance(tests, list) or len(tests) != len(expected_paths):
    fail("report must contain exactly the two expected tests")
seen = set()
for test in tests:
    if not isinstance(test, dict):
        fail("test entry is not an object")
    path = test.get("fullTestPath")
    if not isinstance(path, str) or path not in expected_paths or path in seen:
        fail(f"missing, unexpected, or duplicate test path {path!r}")
    seen.add(path)
    if test.get("state") != "Success":
        fail(f"{path} did not finish successfully")
    for field in ("errors", "warnings"):
        if type(test.get(field)) is not int or test[field] != 0:
            fail(f"{path} has nonzero or missing {field}")
if seen != expected_paths:
    fail("one or more expected test paths are missing")
print("Verified both expected Unreal integration tests: 2 succeeded, no errors, warnings, or unfinished tests.")
PY
printf 'Unreal integration automation passed. Report: %s/index.json\n' "$report_dir"

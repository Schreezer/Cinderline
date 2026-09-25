#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
project_file="$project_dir/Cinderline.uproject"
# shellcheck source=lib/unreal-engine.sh
source "$project_dir/scripts/lib/unreal-engine.sh"

suite_mode=integration
allow_idevice_warning=false
for argument in "$@"; do
  case "$argument" in
    --tutorials|--campaign|--all)
      if [[ "$suite_mode" != integration ]]; then
        printf '%s\n' 'Choose one of --tutorials, --campaign or --all.' >&2; exit 2
      fi
      suite_mode="$argument"
      ;;
    --allow-idevice-id-warning) allow_idevice_warning=true ;;
    --help|-h)
      printf '%s\n' \
        'Build first with: ./scripts/unreal.sh build' \
        'Close the Cinderline editor/runtime, then run: ./scripts/test-unreal.sh' \
        'Default: all Integration tests. --tutorials adds Tutorial tests.' \
        '--campaign runs campaign scenarios, guidance, persistence and lifecycle tests.' \
        '--all runs every local suite in scripts/lib/unreal_suites.json; transport and LAN discovery require separate setup.' \
        '--allow-idevice-id-warning permits only the documented idevice_id Bad CPU host-warning pair.' \
        'UE_ROOT overrides the shared verified prepared-engine default; it also accepts an Engine subdirectory.' \
        'CINDERLINE_TEST_PYTHON selects Python 3 for manifest/report validation.' \
        'Reports go to a fresh Saved/Automation/Integration directory. Player saves are untouched.'
      exit 0
      ;;
    *) printf 'Unknown option: %s. Use --tutorials, --campaign, --all, --allow-idevice-id-warning, or --help.\n' "$argument" >&2; exit 2 ;;
  esac
done

validation_python="${CINDERLINE_TEST_PYTHON:-python3}"
if ! command -v "$validation_python" >/dev/null; then
  printf '%s\n' 'Python 3 is required. Set CINDERLINE_TEST_PYTHON if needed.' >&2; exit 2
fi
validator="$project_dir/scripts/validate-unreal-report.py"
manifest="$project_dir/scripts/lib/unreal_suites.json"
suite_args=(--suite integration)
case "$suite_mode" in
  --tutorials) suite_args+=(--suite tutorials) ;;
  --campaign) suite_args=(--suite campaign) ;;
  --all) suite_args=(--suite local-all) ;;
esac
# Filters and exact expected paths live together in the shared manifest.
test_filter="$("$validation_python" "$validator" --manifest "$manifest" "${suite_args[@]}" --print-filter)"
cinder_find_engine "$project_dir"
cinder_require_engine_tool "$command_editor"

if pgrep -f '[U]nrealEditor.*Cinderline\.uproject' >/dev/null; then
  printf '%s\n' 'Close the running Cinderline editor/runtime before launching integration automation.' >&2; exit 2
fi

run_name="$(date -u '+%Y%m%dT%H%M%SZ')-$$"
report_dir="$project_dir/Saved/Automation/Integration/$run_name"
mkdir -p "$report_dir"
printf 'Running Unreal automation with %s. Report directory: %s\n' "$engine_root" "$report_dir"
# Automation's queued Quit waits for completion/report generation in UE 5.8.
if "$command_editor" "$project_file" /Engine/Maps/Entry -unattended -nop4 -nosplash -NullRHI -nosound \
  -stdout -FullStdOutLogOutput \
  "-ExecCmds=Automation RunTests $test_filter; Quit" \
  "-ReportExportPath=$report_dir" "-abslog=$report_dir/UnrealEditor.log" \
  > "$report_dir/stdout.log" 2>&1; then
  if [[ ! -s "$report_dir/index.json" ]]; then
    printf 'Editor exited without an automation report. Inspect %s\n' "$report_dir/stdout.log" >&2; exit 1
  fi
else
  result=$?
  printf 'Unreal automation failed with status %s. Inspect %s\n' "$result" "$report_dir/stdout.log" >&2
  exit "$result"
fi
validation_args=(--manifest "$manifest" "${suite_args[@]}")
if [[ "$allow_idevice_warning" == true ]]; then validation_args+=(--allow-idevice-id-warning); fi
"$validation_python" "$validator" "${validation_args[@]}" "$report_dir/index.json"
printf 'Unreal automation passed. Report: %s/index.json\n' "$report_dir"

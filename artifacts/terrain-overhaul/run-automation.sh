#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
cd "$project_root"
source scripts/lib/unreal-engine.sh
cinder_find_engine "$project_root"
report_dir="$project_root/artifacts/terrain-overhaul/final-automation"
test_filter="$(python3 scripts/validate-unreal-report.py --manifest artifacts/terrain-overhaul/test-manifest.json --suite terrain --print-filter)"
"$command_editor" "$project_root/Cinderline.uproject" /Engine/Maps/Entry \
  -unattended -nop4 -nosplash -NullRHI -nosound -stdout -FullStdOutLogOutput \
  -LoadOnlyHostAndTargetPlatformModules -TargetPlatform=MacEditor \
  "-ExecCmds=Automation RunTests $test_filter; Quit" \
  "-ReportExportPath=$report_dir" \
  "-abslog=$project_root/artifacts/terrain-overhaul/final-tests.log"
python3 scripts/validate-unreal-report.py --manifest artifacts/terrain-overhaul/test-manifest.json --suite terrain "$report_dir/index.json"

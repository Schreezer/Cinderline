#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${CINDERLINE_BUILD_DIR:-$project_dir/build/native}"
build_jobs="${CINDERLINE_BUILD_JOBS:-2}"
if [[ ! "$build_jobs" =~ ^[1-9][0-9]*$ ]]; then
  printf '%s\n' 'CINDERLINE_BUILD_JOBS must be a positive integer.' >&2
  exit 2
fi
cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$build_dir" --parallel "$build_jobs"
ctest --test-dir "$build_dir" --output-on-failure

#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${CINDERLINE_BUILD_DIR:-$project_dir/build/native}"
cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DCINDERLINE_BUILD_NATIVE=ON
cmake --build "$build_dir" --target CinderlineNative --parallel
cd "$project_dir"
exec "$build_dir/CinderlineNative" "$@"

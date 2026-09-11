#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export CINDERLINE_BUILD_DIR="$project_dir/build/sanitize"
exec "$project_dir/scripts/test.sh" -DCMAKE_BUILD_TYPE=Debug -DCINDERLINE_SANITIZE=ON -DCINDERLINE_BUILD_NATIVE=OFF "$@"

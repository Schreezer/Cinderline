#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
image="${CINDERLINE_LINUX_IMAGE:-cinderline-linux-builder:22.04}"

docker build --platform linux/amd64 -t "$image" -f "$project_dir/Tools/Linux/Dockerfile.build" "$project_dir"
docker run --platform linux/amd64 --rm \
  -v "$project_dir:/workspace" \
  -w /workspace \
  "$image" \
  bash -lc 'CINDERLINE_LINUX_BUILD_DIR=/tmp/cinderline-linux-build ./scripts/package-linux.sh'

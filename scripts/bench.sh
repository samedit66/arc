#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export ARC_CMAKE_PRESET=release
export ARC_SANITIZE=0
source scripts/build-env.sh
bash scripts/build.sh
exec python scripts/bench.py --arc "$build_dir/arc$exe" "$@"

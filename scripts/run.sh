#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "${ARC_BUILD_CONFIG:-}" != 1 ]]; then
    exec mise exec -- bash scripts/run.sh "$@"
fi
source scripts/build-env.sh
exec "$build_dir/arc$exe" "$@"

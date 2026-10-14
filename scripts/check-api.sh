#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/build-env.sh
ctest --test-dir "$build_dir" --output-on-failure --no-tests=error

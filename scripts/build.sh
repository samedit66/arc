#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/build-env.sh
bash scripts/configure.sh
cmake --build "$build_dir"

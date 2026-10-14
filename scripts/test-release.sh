#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
[[ $# -le 1 ]] || { echo 'Usage: bash scripts/test-release.sh [TARGET]' >&2; exit 1; }
source scripts/release-env.sh
export ARC_BINARY="$work/arc/arc$exe"
if command -v cygpath >/dev/null 2>&1; then export ARC_BINARY="$(cygpath -m "$ARC_BINARY")"; fi
[[ -f "$ARC_BINARY" ]] || { echo 'Build the release binary before testing it.' >&2; exit 1; }
ctest --preset release-vcpkg
export PYTHONDONTWRITEBYTECODE=1
uv run --python python --locked --project tests pytest -c tests/pyproject.toml
bash scripts/check-release-binary.sh "$target" "$ARC_BINARY"

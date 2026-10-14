#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
[[ $# == 1 ]] || { echo 'Usage: bash scripts/test-release.sh TARGET' >&2; exit 1; }
work=${ARC_RELEASE_BUILD_DIR:?Set ARC_RELEASE_BUILD_DIR}
suffix=
[[ "$1" != windows-* ]] || suffix=.exe
export ARC_BINARY="$work/arc/arc$suffix"
if command -v cygpath >/dev/null 2>&1; then export ARC_BINARY="$(cygpath -m "$ARC_BINARY")"; fi
[[ -f "$ARC_BINARY" ]] || { echo 'Build the release binary before testing it.' >&2; exit 1; }
cmake --build "$work/arc" --parallel 4
ctest --test-dir "$work/arc" --output-on-failure
export PYTHONDONTWRITEBYTECODE=1
uv run --python python --locked --project tests pytest -c tests/pyproject.toml
bash scripts/package-release.sh "$1"

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/build-env.sh
export ARC_BINARY="$build_dir/arc$exe"
if command -v cygpath >/dev/null 2>&1; then
    export ARC_BINARY="$(cygpath -m "$ARC_BINARY")"
fi
export PYTHONDONTWRITEBYTECODE=1
exec uv run --python python --locked --project tests pytest -c tests/pyproject.toml "$@"

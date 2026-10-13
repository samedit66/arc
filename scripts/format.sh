#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
flags=(--style=file)
if [[ "${1:-}" == --check ]]; then
    flags+=(--dry-run --Werror)
else
    flags+=(-i)
fi
exec clang-format "${flags[@]}" arc.h arc.c main.c internal/*.h internal/*.c filekit/*.h filekit/*.c

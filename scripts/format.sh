#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
flags=(--style=file)
if [[ "${1:-}" == --check ]]; then
    flags+=(--dry-run --Werror)
else
    flags+=(-i)
fi
exec clang-format "${flags[@]}" arc.h src/arc.c src/cli/*.h src/cli/*.c src/internal/*.h src/internal/*.c src/fs/*.h src/fs/*.c tests/consumer/main.c tests/fs.c tests/api*.c tests/cli_progress.c

#!/usr/bin/env bash
set -euo pipefail
repo=$(CDPATH= cd "$(dirname "$0")/.." && pwd -P)
case "${1:-}" in
    --help|-h)
        echo 'Usage: mise run clean'
        echo 'Remove project build/, test environments/caches and legacy root binaries.'
        echo 'Includes local release dependencies. External build directories and global caches are untouched.'
        exit 0 ;;
esac
[[ $# == 0 ]] || { echo 'Usage: mise run clean' >&2; exit 1; }
rm -rf -- "$repo/build" "$repo/tests/.venv" \
    "$repo/.pytest_cache" "$repo/tests/.pytest_cache" \
    "$repo/__pycache__" "$repo/tests/__pycache__" \
    "$repo/tests/consumer/__pycache__" "$repo/scripts/__pycache__" \
    "$repo/arc" "$repo/arc.exe" "$repo/arc.o" "$repo/libarc.a"
echo 'Removed project build products and test caches.'

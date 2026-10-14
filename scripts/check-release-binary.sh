#!/usr/bin/env bash
set -euo pipefail
[[ $# == 2 ]] || { echo 'Usage: bash scripts/check-release-binary.sh TARGET BINARY' >&2; exit 1; }
case "$1" in
    linux-x86_64|linux-arm64)
        machine=$(readelf -hW "$2" | awk -F: '/Machine:/ {gsub(/^[ \t]+/, "", $2); print $2}')
        case "$1:$machine" in
            'linux-x86_64:Advanced Micro Devices X86-64'|'linux-arm64:AArch64') ;;
            *) echo "Wrong binary architecture: $machine" >&2; exit 1 ;;
        esac
        # Fully static musl executables have neither an interpreter nor NEEDED libraries.
        headers=$(readelf -lW "$2")
        dynamic=$(readelf -dW "$2")
        if [[ "$headers" == *INTERP* || "$dynamic" == *NEEDED* ]]; then
            echo 'Linux release binary is dynamically linked.' >&2; exit 1
        fi ;;
    darwin-x86_64|darwin-arm64|macos-x86_64|macos-arm64)
        architecture=${1#*-}
        [[ "$(lipo -archs "$2")" == "$architecture" ]] || { echo 'Wrong macOS binary architecture' >&2; exit 1; }
        dependencies=$(otool -L "$2" | tail -n +2 | awk '{print $1}')
        while IFS= read -r library; do
            case "$library" in
                /usr/lib/libSystem.*|/usr/lib/libiconv.*|/usr/lib/libcharset.*|/System/Library/*) ;;
                *) echo "Unexpected macOS dependency: $library" >&2; exit 1 ;;
            esac
        done <<< "$dependencies" ;;
    windows-x86_64)
        format=$(objdump -f "$2")
        [[ "$format" == *'file format pei-x86-64'* ]] || { echo 'Wrong Windows binary architecture' >&2; exit 1; }
        dependencies=$(objdump -p "$2" | awk '/DLL Name:/ {print tolower($3)}')
        while IFS= read -r library; do
            case "$library" in
                kernel32.dll|advapi32.dll|bcrypt.dll|crypt32.dll|ws2_32.dll|user32.dll|shell32.dll|ole32.dll|ucrtbase.dll|msvcrt.dll|api-ms-win-*.dll) ;;
                *) echo "Unexpected Windows DLL dependency: $library" >&2; exit 1 ;;
            esac
        done <<< "$dependencies" ;;
    *) echo 'Unknown release platform' >&2; exit 1 ;;
esac

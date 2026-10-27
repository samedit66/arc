"""Check flat/tree listing, archive information and verification."""

from pathlib import Path
import sys

from check_cli import check_read_commands


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: check_read_cli.py EXECUTABLE [EXECUTABLE ...]")
    for argument in sys.argv[1:]:
        executable = Path(argument).resolve()
        check_read_commands(executable)
        print(f"PASS: {executable}: read CLI")

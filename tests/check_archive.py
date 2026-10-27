"""Run the assertion-enabled archive facade probe in an isolated filesystem."""

from pathlib import Path
import io
import tarfile
import subprocess
import sys
import tempfile
import zipfile


def main(arguments):
    """Check facade outcomes independently of the probe's Eiffel assertions."""
    if not arguments:
        raise SystemExit("usage: check_archive.py EXECUTABLE [EXECUTABLE ...]")
    for argument in arguments:
        executable = Path(argument).resolve()
        with tempfile.TemporaryDirectory(prefix="arc-archive-") as temporary:
            root = Path(temporary)
            corrupt = root / "corrupt.zip"
            with zipfile.ZipFile(corrupt, "w") as output:
                output.writestr("file", b"payload with deliberately invalid CRC")
            data = bytearray(corrupt.read_bytes())
            data[data.index(b"payload with deliberately invalid CRC")] ^= 1
            corrupt.write_bytes(data)
            bad_header = root / "bad-header.tar"
            with tarfile.open(bad_header, "w") as output:
                for name in ("first", "second"):
                    entry = tarfile.TarInfo(name)
                    entry.size = 3
                    output.addfile(entry, io.BytesIO(b"abc"))
            data = bytearray(bad_header.read_bytes())
            data[1024 + 148] = ord("?")
            bad_header.write_bytes(data)
            result = subprocess.run([str(executable)], cwd=root, capture_output=True,
                                    text=True, timeout=30)
            assert result.returncode == 0, (result.stdout, result.stderr)
            assert "PASS:" in result.stdout
            with zipfile.ZipFile(root / "registered.zip") as archive:
                assert archive.namelist() == ["first", "nested/later"]
                assert archive.read("first") == b"after registration"
                assert archive.read("nested/later") == b"x" * 150000
            assert (root / "selected.txt").read_bytes() == b"x" * 150000
            assert (root / "restored/first").read_bytes() == b"after registration"
            assert (root / "restored/nested/later").read_bytes() == b"x" * 150000
            for name in ("cancelled.tar", "aborted.zip", "failed.tar"):
                assert not (root / name).exists(), name
            print(result.stdout.strip())


if __name__ == "__main__":
    main(sys.argv[1:])

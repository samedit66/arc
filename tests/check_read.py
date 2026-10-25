"""Check streaming read contracts with explicitly rebuilt Eiffel probe executables."""

from pathlib import Path
import io
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile


def damaged_zip(path):
    """Damage stored contents while leaving all ZIP headers and declared CRC intact."""
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED) as archive:
        archive.writestr("a.txt", b"unique-payload-for-crc")
    data = path.read_bytes()
    assert data.count(b"unique-payload-for-crc") == 1
    path.write_bytes(data.replace(b"unique-payload-for-crc", b"Unique-payload-for-crc"))


def main():
    if len(sys.argv) < 2:
        raise SystemExit("Usage: python tests/check_read.py READ_PROBE [READ_PROBE...]")
    for name in sys.argv[1:]:
        executable = Path(name).resolve(strict=True)
        with tempfile.TemporaryDirectory(prefix="arc-read-") as directory:
            root = Path(directory)
            bad = root / "damaged.zip"
            damaged_zip(bad)
            bad_header = root / "bad-header.tar"
            with tarfile.open(bad_header, "w", format=tarfile.USTAR_FORMAT) as archive:
                archive.addfile(tarfile.TarInfo("first"))
                archive.addfile(tarfile.TarInfo("second"))
            contents = bytearray(bad_header.read_bytes())
            contents[512] ^= 1  # Damage only the second header's checksum.
            bad_header.write_bytes(contents)
            bad_body = root / "bad-body.tar"
            with tarfile.open(bad_body, "w", format=tarfile.USTAR_FORMAT) as archive:
                entry = tarfile.TarInfo("body")
                entry.size = 8192
                archive.addfile(entry, io.BytesIO(bytes(entry.size)))
            bad_body.write_bytes(bad_body.read_bytes()[:600])
            filtered = root / "filtered.tar.gz"
            with tarfile.open(filtered, "w:gz") as archive:
                entry = tarfile.TarInfo("payload")
                entry.size = 4
                archive.addfile(entry, io.BytesIO(b"data"))
            for password in (None, "пароль-test"):
                good = root / "данные.zip"
                if good.exists():
                    good.unlink()
                if password:
                    (root / "a.txt").write_bytes(b"first")
                    (root / "dir").mkdir()
                    (root / "dir/b.txt").write_bytes(b"second")
                    subprocess.run([shutil.which("zip") or "zip", "-q", "-P", password,
                                    str(good), "a.txt", "dir/b.txt"], cwd=root, check=True)
                    (root / "a.txt").unlink()
                    shutil.rmtree(root / "dir")
                else:
                    with zipfile.ZipFile(good, "w") as archive:
                        archive.writestr("a.txt", b"first")
                        archive.writestr("dir/b.txt", b"second")
                arguments = [str(executable), str(good), str(bad), str(filtered)]
                if password:
                    arguments.append(password)
                result = subprocess.run(arguments, cwd=root, capture_output=True, timeout=30)
                assert result.returncode == 0 and not result.stderr, result
                assert set(root.iterdir()) == {good, bad, filtered, bad_header, bad_body}
            print(f"{executable}: stream ownership, metadata snapshots, across, passwords and verification passed")


if __name__ == "__main__":
    main()

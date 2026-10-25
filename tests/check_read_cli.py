"""Check list/tree/info/verify behavior and absence of extraction."""

import bz2
import gzip
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import warnings
import zipfile

from check_read import damaged_zip


def run(executable, root, *arguments, status=0):
    result = subprocess.run([str(executable), *map(str, arguments)], cwd=root,
                            capture_output=True, timeout=30)
    assert result.returncode == status, (arguments, result)
    if status:
        assert result.stderr, (arguments, result)
    else:
        assert not result.stderr, (arguments, result)
    return result.stdout.decode("utf-8"), result.stderr


def info(executable, root, archive, **expected):
    before = set(root.iterdir())
    stdout, _ = run(executable, root, "info", archive)
    rows = [line.split(": ", 1) for line in stdout.splitlines()]
    assert all(len(row) == 2 for row in rows), stdout
    fields = dict(rows)
    assert len(rows) == len(fields) == 12, stdout
    for key, value in expected.items():
        assert fields[key.replace("_", " ")] == str(value), fields
    assert set(root.iterdir()) == before
    return fields


def check(executable):
    with tempfile.TemporaryDirectory(prefix="arc-read-cli-") as directory:
        root = Path(directory)
        names = ["dir/данные-😀.txt", "empty/", "line\n\x1b[31m.txt", "../outside", "/absolute", "same", "same", "same/child"]
        archive = root / "данные.zip"
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(archive, "w") as output:
                for name in names:
                    output.writestr(name, b"" if name.endswith("/") else b"payload")
        stdout, _ = run(executable, root, "list", archive)
        assert stdout.count("same\n") == 2 and "dir/данные-😀.txt\n" in stdout
        assert "\x1b" not in stdout and "line\n" not in stdout and "\\u{" in stdout
        assert "../outside\n" in stdout and "/absolute\n" in stdout
        tree, _ = run(executable, root, "list", "--tree", archive)
        assert tree.startswith(".\n") and tree.count("[file] same\n") == 2
        assert "[file] same/child\n" in tree and "[directory] empty/\n" in tree
        assert "[file] ../outside\n" in tree and "[file] /absolute\n" in tree
        assert not run(executable, root, "verify", archive)[0]
        fields = info(executable, root, archive, Entries=8, Files=7, Directories=1,
                      Symbolic_links=0, Hard_links=0, Other=0, Declared_file_bytes=49,
                      Files_with_unknown_size=0, Filters="none", Encryption="none detected")
        assert fields["Format"].startswith("ZIP") and "данные.zip" in fields["Archive"]

        for mode, suffix in (("w", "tar"), ("w:gz", "tar.gz"), ("w:xz", "tar.xz")):
            path = root / ("sample." + suffix)
            with tarfile.open(path, mode) as output:
                item = tarfile.TarInfo("implicit/file")
                item.size = 100_000
                output.addfile(item, io.BytesIO(b"x" * item.size))
                link = tarfile.TarInfo("link")
                link.type = tarfile.SYMTYPE
                link.linkname = "../external"
                output.addfile(link)
                hard = tarfile.TarInfo("hard")
                hard.type = tarfile.LNKTYPE
                hard.linkname = "implicit/file"
                output.addfile(hard)
                fifo = tarfile.TarInfo("fifo")
                fifo.type = tarfile.FIFOTYPE
                output.addfile(fifo)
            assert run(executable, root, "list", path)[0] == "implicit/file\nlink\nhard\nfifo\n"
            assert not run(executable, root, "verify", path)[0]
            listing = run(executable, root, "list", "--tree", path)[0]
            assert "[symlink] link" in listing and "[hardlink] hard" in listing and "[other] fifo" in listing
            fields = info(executable, root, path, Entries=4, Files=1, Directories=0,
                          Symbolic_links=1, Hard_links=1, Other=1, Declared_file_bytes=100_000,
                          Files_with_unknown_size=0, Filters={"w": "none", "w:gz": "gzip", "w:xz": "xz"}[mode],
                          Encryption="unsupported")
            assert "tar" in fields["Format"].lower() or "pax" in fields["Format"].lower()

        for format_name in ("zip", "tar"):
            empty = root / ("empty." + format_name)
            if format_name == "zip":
                with zipfile.ZipFile(empty, "w"):
                    pass
            else:
                with tarfile.open(empty, "w"):
                    pass
            assert run(executable, root, "list", empty)[0] == ""
            assert run(executable, root, "list", "--tree", empty)[0] == ".\n"
            assert not run(executable, root, "verify", empty)[0]
            info(executable, root, empty, Entries=0, Files=0, Directories=0, Symbolic_links=0,
                 Hard_links=0, Other=0, Declared_file_bytes=0, Files_with_unknown_size=0, Filters="none")

        bad = root / "damaged.zip"
        damaged_zip(bad)
        assert run(executable, root, "list", bad)[0] == "a.txt\n"
        info(executable, root, bad, Entries=1, Files=1)
        assert not run(executable, root, "verify", bad, status=1)[0]
        bad.write_bytes(b"not an archive")
        for command in (("list",), ("list", "--tree"), ("info",), ("verify",)):
            assert not run(executable, root, *command, bad, status=1)[0]
        # Valid first header, truncated later: flat output may be partial, tree is unpublished.
        truncated = root / "truncated.tar"
        with tarfile.open(truncated, "w", format=tarfile.USTAR_FORMAT) as output:
            item = tarfile.TarInfo("first")
            item.size = 8192
            output.addfile(item, io.BytesIO(b"x" * item.size))
        truncated.write_bytes(truncated.read_bytes()[:600])
        assert run(executable, root, "list", truncated, status=1)[0] == "first\n"
        assert not run(executable, root, "list", "--tree", truncated, status=1)[0]
        assert not run(executable, root, "verify", truncated, status=1)[0]
        assert not run(executable, root, "info", truncated, status=1)[0]

        secret = "пароль-secret"
        payload = root / "secret.txt"
        payload.write_bytes(b"private contents")
        encrypted = root / "encrypted.zip"
        subprocess.run([shutil.which("zip") or "zip", "-q", "-P", secret,
                        str(encrypted), payload.name], cwd=root, check=True)
        payload.unlink()
        assert run(executable, root, "list", encrypted)[0] == "secret.txt\n"
        info(executable, root, encrypted, Entries=1, Files=1, Encryption="yes")
        for options in ([], ["--password", "wrong"], ["--password", secret]):
            stdout, _ = run(executable, root, "info", *options, encrypted)
            assert "Encryption: yes\n" in stdout and secret not in stdout
        for options in ([], ["--password", "wrong"]):
            _, error = run(executable, root, "verify", *options, encrypted, status=1)
            assert secret.encode() not in error
        assert not run(executable, root, "verify", "--password", secret, encrypted)[0]
        # Detect format by content and preserve the complete outer filter chain.
        layered = root / "unexpected.extension"
        layered.write_bytes(bz2.compress(gzip.compress((root / "sample.tar").read_bytes())))
        info(executable, root, layered, Entries=4, Filters="gzip, bzip2")

        controlled = root / "line\n\x1b[31m\u202e.zip"
        with zipfile.ZipFile(controlled, "w"):
            pass
        fields = info(executable, root, controlled, Entries=0)
        assert "\x1b" not in fields["Archive"] and "\u202e" not in fields["Archive"]
        assert "\\u{" in fields["Archive"]

        # Every operation must escape external paths in diagnostics, including failures.
        controlled.unlink()
        for command in ("list", "info", "verify", "unpack"):
            _, error = run(executable, root, command, controlled, status=1)
            assert b"\x1b" not in error and "\u202e".encode() not in error and b"\\u{" in error
        _, error = run(executable, root, "pack", controlled, "missing\x1b[31m", status=1)
        assert b"\x1b" not in error and b"\\u{" in error
        controlled.write_bytes(b"not an archive")
        (root / "existing.txt").write_bytes(b"payload")
        _, error = run(executable, root, "pack", controlled, "existing.txt", status=1)
        assert b"\x1b" not in error and "\u202e".encode() not in error and b"\\u{" in error

        # The mtree header declares a body which is absent: reading must fail independently of native EOF.
        missing_body = root / "missing-body.mtree"
        missing_body.write_text("#mtree\nabsent type=file size=4\n")
        assert run(executable, root, "list", missing_body)[0] == "absent\n"
        _, error = run(executable, root, "verify", missing_body, status=1)
        assert b"Decoded file size" in error

        # Sparse payload size excludes leading, interior and trailing holes, including all-hole files.
        sparse = root / "sparse.tar"
        with tarfile.open(sparse, "w", format=tarfile.PAX_FORMAT) as output:
            for name, mapping, payload in (("leading", "1048576,4", b"tail"),
                                           ("trailing", "0,4,1048576,0", b"head"),
                                           ("all-hole", "1048576,0", b"")):
                entry = tarfile.TarInfo(name)
                entry.size = len(payload)
                entry.pax_headers = {"GNU.sparse.map": mapping,
                                     "GNU.sparse.size": "1048576" if name != "leading" else "1048580",
                                     "GNU.sparse.numblocks": str(len(mapping.split(",")) // 2)}
                output.addfile(entry, io.BytesIO(payload))
        assert not run(executable, root, "verify", sparse)[0]

        # Header-only mtree input can declare totals larger than NATURAL_64.
        huge = root / "huge.mtree"
        huge.write_text("#mtree\n" + "".join(f"{name} type=file size={2**63 - 1}\n" for name in ("a", "b", "c")))
        stdout, error = run(executable, root, "info", huge, status=1)
        assert not stdout and b"exceed" in error

        assert all(path.is_file() for path in root.iterdir()), list(root.iterdir())
        assert not (root.parent / "outside").exists()
    print(f"{executable}: list/tree/info/verify archive integration passed")


def main():
    if len(sys.argv) < 2:
        raise SystemExit("Usage: python tests/check_read_cli.py ARC [ARC...]")
    for name in sys.argv[1:]:
        check(Path(name).resolve(strict=True))


if __name__ == "__main__":
    main()

"""Run extraction scenarios against explicitly supplied Eiffel probe executables."""

import io
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import zipfile


def tar_entry(archive, name, data=b"", kind=tarfile.REGTYPE, target=""):
    entry = tarfile.TarInfo(name)
    entry.type = kind
    entry.linkname = target
    entry.size = len(data)
    archive.addfile(entry, io.BytesIO(data))


def check_binary(binary, unicode_only=False):
    count = 0
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary).resolve()

        def run(source, success=True, diagnostic=None, extra=()):
            nonlocal count
            result = subprocess.run([str(binary), str(source), *map(str, extra)], cwd=root, capture_output=True, timeout=15)
            output = result.stdout + result.stderr
            assert (result.returncode == 0) == success, output.decode(errors="replace")
            if diagnostic:
                assert diagnostic.encode() in output, output.decode(errors="replace")
            count += 1

        source = root / "проект📦.zip"
        payload = bytes(range(256)) * 10000
        with zipfile.ZipFile(source, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("nested/данные📦.bin", payload)
            archive.writestr("empty.txt", b"")
        run(source)
        destination = root / "проект📦"
        assert (destination / "nested/данные📦.bin").read_bytes() == payload
        assert (destination / "empty.txt").read_bytes() == b""
        run(source, False, "create destination")
        assert (destination / "nested/данные📦.bin").read_bytes() == payload

        if unicode_only:
            # This subset needs no POSIX links or inode semantics on Windows.
            run(root / "отсутствует📦.zip", False, "open archive")
            print(f"{binary}: {count} Unicode path cases passed")
            return

        source = root / "links.tar.gz"
        with tarfile.open(source, "w:gz") as archive:
            tar_entry(archive, "data/file.txt", b"content")
            tar_entry(archive, "hard.txt", kind=tarfile.LNKTYPE, target="data/file.txt")
            tar_entry(archive, "soft.txt", kind=tarfile.SYMTYPE, target="data/file.txt")
            tar_entry(archive, "dangling", kind=tarfile.SYMTYPE, target="missing")
            tar_entry(archive, "external", kind=tarfile.SYMTYPE, target="../outside")
        run(source)
        destination = root / "links"
        assert (destination / "hard.txt").stat().st_ino == (destination / "data/file.txt").stat().st_ino
        assert os.readlink(destination / "soft.txt") == "data/file.txt"
        assert os.readlink(destination / "dangling") == "missing"
        assert os.readlink(destination / "external") == "../outside"

        for suffix in ("zip", "tar"):
            source = root / f"empty.{suffix}"
            if suffix == "zip":
                with zipfile.ZipFile(source, "w"):
                    pass
            else:
                with tarfile.open(source, "w"):
                    pass
            run(source)
            assert (root / "empty").is_dir()
            (root / "empty").rmdir()

        source = root / "unknown.extension"
        with zipfile.ZipFile(source, "w") as archive:
            archive.writestr("file", b"detected by contents")
        run(source)
        assert (root / "unknown.extension.unpacked/file").read_bytes() == b"detected by contents"

        run(root / "missing.zip", False, "open archive")
        assert not (root / "missing").exists()

        source = root / "recovery.zip"
        with zipfile.ZipFile(source, "w") as archive:
            archive.writestr("после ошибки.txt", b"recovered")
        run(root / "missing.zip", extra=(source,))
        assert (root / "recovery/после ошибки.txt").read_bytes() == b"recovered"

        (root / "real/deep").mkdir(parents=True)
        (root / "link").symlink_to(root / "real/deep", target_is_directory=True)
        with zipfile.ZipFile(root / "real/selected.zip", "w") as archive:
            archive.writestr("marker", b"filesystem target")
        with zipfile.ZipFile(root / "selected.zip", "w") as archive:
            archive.writestr("marker", b"wrong lexical target")
        run(root / "link/../selected.zip")
        assert (root / "real/selected/marker").read_bytes() == b"filesystem target"
        assert not (root / "selected").exists()

        source = root / "unicode-error.tar"
        with tarfile.open(source, "w") as archive:
            tar_entry(archive, "alias", kind=tarfile.LNKTYPE, target="отсутствует")
        run(source, False, "отсутствует")

        source = root / "invalid.zip"
        source.write_bytes(b"not an archive")
        run(source, False, "open archive")
        assert not (root / "invalid").exists()

        for index, name in enumerate(("../escape", "nested/../../escape", str(root / "absolute-escape"))):
            source = root / f"unsafe{index}.zip"
            with zipfile.ZipFile(source, "w") as archive:
                archive.writestr(name, b"must not escape")
            run(source, False, "entry pathname")
        assert not (root / "escape").exists()
        assert not (root / "absolute-escape").exists()

        source = root / "through-link.tar"
        (root / "outside").mkdir()
        with tarfile.open(source, "w") as archive:
            tar_entry(archive, "link", kind=tarfile.SYMTYPE, target="../outside")
            tar_entry(archive, "link/escape", b"must not escape")
        run(source, False, "write entry header")
        assert not (root / "outside/escape").exists()

        source = root / "forward-hardlink.tar"
        with tarfile.open(source, "w") as archive:
            tar_entry(archive, "alias", kind=tarfile.LNKTYPE, target="later")
            tar_entry(archive, "later", b"later")
        run(source, False, "write entry header")

        source = root / "unsafe-hardlink.tar"
        with tarfile.open(source, "w") as archive:
            tar_entry(archive, "alias", kind=tarfile.LNKTYPE, target="../outside")
        run(source, False, "hardlink target")

        source = root / "device.tar"
        with tarfile.open(source, "w") as archive:
            tar_entry(archive, "device", kind=tarfile.FIFOTYPE)
        run(source, False, "entry type")

        source = root / "collision.tar"
        with tarfile.open(source, "w") as archive:
            tar_entry(archive, "file", b"first")
            tar_entry(archive, "file", b"second")
        run(source, False, "entry collision")
        assert (root / "collision/file").read_bytes() == b"first"

        source = root / "sparse.tar"
        with tarfile.open(source, "w", format=tarfile.PAX_FORMAT) as archive:
            entry = tarfile.TarInfo("sparse.bin")
            entry.size = 8
            entry.pax_headers = {
                "GNU.sparse.map": "0,4,1048576,4",
                "GNU.sparse.size": "1048580",
                "GNU.sparse.numblocks": "2",
            }
            archive.addfile(entry, io.BytesIO(b"headtail"))
        run(source)
        data = (root / "sparse/sparse.bin").read_bytes()
        assert data == b"head" + bytes(1048572) + b"tail"

        for label, mapping, payload_bytes, expected in (
            ("trailing", "0,4,1048576,0", b"head", b"head" + bytes(1048572)),
            ("all-hole", "1048576,0", b"", bytes(1048576)),
        ):
            source = root / ("sparse-" + label + ".tar")
            with tarfile.open(source, "w", format=tarfile.PAX_FORMAT) as archive:
                entry = tarfile.TarInfo("sparse.bin")
                entry.size = len(payload_bytes)
                entry.pax_headers = {"GNU.sparse.map": mapping, "GNU.sparse.size": "1048576",
                                     "GNU.sparse.numblocks": str(len(mapping.split(",")) // 2)}
                archive.addfile(entry, io.BytesIO(payload_bytes))
            run(source)
            assert (root / ("sparse-" + label) / "sparse.bin").read_bytes() == expected

        source = root / "missing-body.mtree"
        source.write_text("#mtree\nabsent type=file size=4\n")
        run(source, False, "validate entry size")

        source = root / "file-as-directory.tar"
        with tarfile.open(source, "w") as archive:
            tar_entry(archive, "directory", b"first")
            tar_entry(archive, "directory/", kind=tarfile.DIRTYPE)
        run(source, False)
        assert (root / "file-as-directory/directory").read_bytes() == b"first"

        source = root / "truncated.tar"
        with tarfile.open(source, "w") as archive:
            tar_entry(archive, "large", payload)
        source.write_bytes(source.read_bytes()[:4096])
        run(source, False, "read data block")

    print(f"{binary}: {count} extraction cases passed")


if __name__ == "__main__":
    arguments = sys.argv[1:]
    unicode_only = bool(arguments and arguments[0] == "--unicode-only")
    if unicode_only:
        arguments = arguments[1:]
    if not arguments:
        raise SystemExit("Usage: python tests/check_unpack.py [--unicode-only] UNPACK_PROBE [UNPACK_PROBE...]")
    for argument in arguments:
        executable = Path(argument).resolve(strict=True)
        check_binary(executable, unicode_only=unicode_only)

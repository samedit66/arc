"""Check archive creation through supplied Eiffel probes and independent readers."""

import os
from pathlib import Path
import subprocess
import shutil
import sys
import tarfile
import tempfile
import zipfile


def check_binary(binary, reader):
    count = 0
    with tempfile.TemporaryDirectory(prefix="arc-pack-") as temporary:
        root = Path(temporary)
        source = root / "исходники📦"
        source.mkdir()
        payload = bytes(range(256)) * 4097
        (source / "данные📦.bin").write_bytes(payload)
        (source / ".hidden").write_text("hidden")
        (source / "empty").mkdir()
        (source / "zero").touch()
        if os.name != "nt":
            (source / "dangling").symlink_to("missing")
            (source / "external").symlink_to("../outside")
            os.link(source / "данные📦.bin", source / "alias.bin")

        def run(output, mode="normal", pairs=None, success=True):
            nonlocal count
            if pairs is None:
                pairs = [(source, "tree")]
            args = [str(binary), str(output), mode]
            for path, name in pairs:
                args.extend((str(path), name))
            result = subprocess.run(args, cwd=root, capture_output=True, timeout=30)
            assert (result.returncode == 0) == success, (args, result.stdout, result.stderr)
            count += 1
            return result

        for suffix in ("zip", "tar", "tar.gz", "tar.bz2", "tar.xz", "tar.zst", "7z"):
            output = root / ("архив📦." + suffix)
            result = run(output)
            total, entries = map(int, result.stdout.split())
            expected = len(payload) * (2 if os.name != "nt" else 1) + len(b"hidden")
            assert total == expected, (suffix, total, expected)
            assert entries == (8 if os.name != "nt" else 5), (suffix, entries)
            # The system libarchive reader also covers 7z and outer zstd.
            read = subprocess.run([reader, "-xOf", str(output), "tree/данные📦.bin"], capture_output=True)
            assert read.returncode == 0 and read.stdout == payload, (suffix, read.stderr)
            if suffix == "zip":
                with zipfile.ZipFile(output) as archive:
                    assert archive.read("tree/.hidden") == b"hidden"
                    assert archive.read("tree/zero") == b""
                    assert "tree/empty/" in archive.namelist()
                    if os.name != "nt":
                        assert archive.read("tree/alias.bin") == payload
                        assert archive.read("tree/dangling") == b"missing"
            if suffix in ("tar", "tar.gz", "tar.bz2", "tar.xz"):
                with tarfile.open(output) as archive:
                    if os.name != "nt":
                        assert archive.getmember("tree/alias.bin").isreg()
                        assert archive.getmember("tree/данные📦.bin").isreg()
                        assert archive.getmember("tree/dangling").issym()
                        assert archive.getmember("tree/external").linkname == "../outside"
            previous = output.read_bytes()
            run(output, success=False)
            assert output.read_bytes() == previous

        for mode in ("basename", "basename_string", "path_as", "mutation", "repeat", "plain", "cwd", "explicit", "reset_encoding", "reenter", "raise", "final_raise"):
            output = root / (mode + ".zip")
            run(output, mode, [(Path(source.name), "tree")] if mode == "cwd" else None)
            if mode == "explicit":
                with tarfile.open(output) as archive:
                    assert archive.extractfile("tree/данные📦.bin").read() == payload
            elif mode in ("basename", "basename_string"):
                with zipfile.ZipFile(output) as archive:
                    assert archive.read(source.name + "/данные📦.bin") == payload

        for mode in ("contents_path", "contents_string"):
            output = root / (mode + ".zip")
            result = run(output, mode, [(source, "")])
            total, entries = map(int, result.stdout.split())
            assert total == expected
            assert entries == (7 if os.name != "nt" else 4)
            with zipfile.ZipFile(output) as archive:
                assert archive.read("данные📦.bin") == payload
                assert archive.read(".hidden") == b"hidden"
                assert "empty/" in archive.namelist()
                assert not any(name.startswith(source.name + "/") for name in archive.namelist())
                if os.name != "nt":
                    assert archive.read("dangling") == b"missing"
                    assert archive.read("external") == b"../outside"

            invalid_sources = [source / "zero"]
            if os.name != "nt":
                directory_link = root / (mode + "-directory-link")
                directory_link.symlink_to(source, target_is_directory=True)
                invalid_sources.extend((directory_link, source / "dangling"))
            for invalid_source in invalid_sources:
                output = root / (mode + "-invalid.zip")
                result = run(output, mode, [(invalid_source, "")], success=False)
                assert b"source_is_directory" in result.stderr
                assert not output.exists()

        for mode in ("normal", "path_as"):
            output = root / (mode + "-empty-name.zip")
            result = run(output, mode, [(source, "")], success=False)
            assert b"valid_name" in result.stderr
            assert not output.exists()

        late_source = root / "late-source"
        late_source.mkdir()
        output = root / "late.zip"
        run(output, "contents_late", [(late_source, "")])
        with zipfile.ZipFile(output) as archive:
            assert archive.namelist() == ["late.txt"]
            assert archive.read("late.txt") == b"created after registration"

        for mode, suffix in (("normal", "unknown"), ("unsupported", "7z"), ("password_tar", "tar")):
            output = root / (mode + "." + suffix)
            run(output, mode, success=False)
            assert not output.exists()

        for pairs in ([(source, "same"), (source, "same")],
                      [(source / "zero", "a"), (source / "zero", "a/b")],
                      [(source / "zero", "a/b"), (source / "zero", "a")]):
            output = root / "conflict.zip"
            run(output, pairs=pairs, success=False)
            assert not output.exists()

        vanished = root / "vanished"
        vanished.write_text("gone")
        output = root / "missing.zip"
        run(output, "missing", [(vanished, "gone")], success=False)
        assert not output.exists()

        # Creating output inside a selected tree must exclude the output object.
        output = source / "inside.zip"
        run(output)
        with zipfile.ZipFile(output) as archive:
            assert "tree/inside.zip" not in archive.namelist()
        output.unlink()

        if os.name != "nt":
            fifo = root / "fifo"
            os.mkfifo(fifo)
            output = root / "fifo.zip"
            run(output, pairs=[(fifo, "fifo")], success=False)
            assert not output.exists()

        if os.name != "nt":
            for relative in ("a\\b", "nested/a\\b", "C:drive"):
                discovered = root / "discovered"
                discovered.mkdir()
                bad_path = discovered / relative
                bad_path.parent.mkdir(parents=True, exist_ok=True)
                bad_path.write_bytes(b"payload")
                for suffix in ("zip", "tar"):
                    output = root / ("invalid-name." + suffix)
                    result = run(output, "invalid_name", [(discovered, "")], success=False)
                    assert b"entry name" in result.stderr
                    assert not output.exists()
                shutil.rmtree(discovered)

            # Colons and Windows device names remain valid below a portable root on POSIX.
            discovered = root / "valid-discovered"
            discovered.mkdir()
            for name in ("colon:name", "CON", "C:drive", "данные📦"):
                (discovered / name).write_bytes(b"payload")
            for suffix in ("zip", "tar"):
                output = root / ("valid-name." + suffix)
                run(output, pairs=[(discovered, "./tree//./")])
                if suffix == "zip":
                    with zipfile.ZipFile(output) as archive:
                        names = archive.namelist()
                else:
                    with tarfile.open(output) as archive:
                        names = archive.getnames()
                for name in ("colon:name", "CON", "C:drive", "данные📦"):
                    assert "tree/" + name in names

        empty = root / "empty-root"
        empty.mkdir()
        output = root / "empty.zip"
        for mode in ("contents_path", "contents_string"):
            output = root / (mode + "-empty.zip")
            run(output, mode, [(empty, "")])
            with zipfile.ZipFile(output) as archive:
                assert not archive.namelist()

        output = root / "no-parent" / "output.zip"
        run(output, success=False)
        assert not output.exists()

        if os.name != "nt":
            output = root / "link.zip"
            run(output, pairs=[(source / "dangling", "link")])
            with zipfile.ZipFile(output) as archive:
                assert archive.read("link") == b"missing"
            output = root / "existing-link.zip"
            output.symlink_to("nonexistent")
            run(output, success=False)
            assert output.is_symlink() and os.readlink(output) == "nonexistent"

        output = root / "encrypted.zip"
        run(output, "password")
        secret = "secret-Д📦"
        read = subprocess.run([reader, "--passphrase", secret, "-xOf", str(output), "tree/данные📦.bin"], capture_output=True)
        assert read.returncode == 0 and read.stdout == payload, read.stderr
        rejected = subprocess.run([reader, "--passphrase", "wrong", "-xOf", str(output), "tree/данные📦.bin"], capture_output=True)
        assert rejected.returncode != 0
    print(f"{binary}: {count} packing scenarios passed")


def main():
    if len(sys.argv) < 2:
        raise SystemExit("Supply at least one freshly built pack probe executable")
    reader = shutil.which("bsdtar") or shutil.which("tar")
    if not reader:
        raise SystemExit("Install libarchive bsdtar for independent archive checks")
    version = subprocess.run([reader, "--version"], capture_output=True, check=True)
    if b"bsdtar" not in version.stdout.lower() and b"libarchive" not in version.stdout.lower():
        raise SystemExit("These checks require libarchive bsdtar, not GNU tar")
    for argument in sys.argv[1:]:
        binary = Path(argument).resolve(strict=True)
        check_binary(binary, reader)


if __name__ == "__main__":
    main()

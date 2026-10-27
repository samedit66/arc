"""Exercise the current CLI against explicitly supplied rebuilt executables."""

from pathlib import Path
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import warnings
import zipfile

PROFILES = ("tar", "tar.gz", "tar.bz2", "tar.xz", "tar.zst", "tar.lz4", "zip", "7z")


def run(executable, root, *arguments, ok=True):
    """Run one CLI command and require the selected success/failure outcome."""
    result = subprocess.run([str(executable), *map(str, arguments)], cwd=root,
                            capture_output=True, text=True, encoding="utf-8", timeout=30,
                            env=dict(os.environ, LC_ALL="C", LANG="C"))
    assert (result.returncode == 0) == ok, (arguments, result)
    if not ok:
        assert result.stderr and not result.stdout, (arguments, result)
    return result


def check_read_commands(executable):
    """Keep listing lossless and terminal-safe and inspect/verify without extraction."""
    with tempfile.TemporaryDirectory(prefix="arc-read-cli-") as temporary:
        root = Path(temporary).resolve()
        archive = root / "данные.zip"
        names = ["dir/данные-😀.txt", "empty/", "line\n\x1b[31m.txt", "../outside",
                 "/absolute", "same", "same", "same/child"]
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(archive, "w") as output:
                for name in names:
                    output.writestr(name, b"" if name.endswith("/") else b"payload")
        before = set(root.iterdir())
        flat = run(executable, root, "list", archive).stdout
        assert flat.count("same\n") == 2 and "dir/данные-😀.txt\n" in flat
        assert "\x1b" not in flat and "line\n" not in flat and "\\u{" in flat
        assert "../outside\n" in flat and "/absolute\n" in flat
        tree = run(executable, root, "list", "--tree", archive).stdout
        assert tree.startswith(".\n") and tree.count("[file] same\n") == 2
        assert "[directory] empty/\n" in tree and "[file] same/child\n" in tree
        assert set(root.iterdir()) == before
        for command in ("info", "verify"):
            result = run(executable, root, command, archive)
            assert result.stdout
            assert set(root.iterdir()) == before
        broken = root / "broken.zip"
        broken.write_bytes(b"not an archive")
        run(executable, root, "list", broken, ok=False)
        run(executable, root, "unpack", broken, ok=False)
        assert not (root / "broken").exists()


def check_pack_and_unpack(executable):
    """Round-trip each profile, including physical symlinks, default mapping and progress."""
    with tempfile.TemporaryDirectory(prefix="arc-cli-") as temporary:
        root = Path(temporary).resolve()
        source = root / "проект📦"
        source.mkdir()
        (source / "nested").mkdir()
        (source / "empty").mkdir()
        (source / "data.txt").write_bytes(b"payload")
        (source / "nested/large.txt").write_bytes(b"x" * 200000)
        (source / "nested/empty.txt").write_bytes(b"")
        (source / "link").symlink_to("nested", target_is_directory=True)
        (source / "dangling").symlink_to("absent-🙂.txt")
        for profile in PROFILES:
            archive = root / ("roundtrip." + profile)
            created = run(executable, root, "pack", "--progress", archive, source)
            assert created.stdout == "Created " + str(archive).replace("\\", "\\\\") + "\n", created
            assert "Progress:" in created.stderr
            assert "100%" in created.stderr and "█" in created.stderr
            assert "\x1b[2K" in created.stderr and "\x1b" not in created.stdout
            listing = run(executable, root, "list", archive).stdout
            assert "nested/large.txt\n" in listing and source.name not in listing
            run(executable, root, "verify", archive)
            report = run(executable, root, "info", archive).stdout
            assert "Files: 3" in report and "Directories: 2" in report and "Symbolic links: 2" in report, report
            expected_filter = {"tar.gz": "gzip", "tar.bz2": "bzip2", "tar.xz": "xz",
                               "tar.zst": "zstd", "tar.lz4": "lz4"}.get(profile)
            if expected_filter:
                assert expected_filter in report, report
            original = archive.read_bytes()
            run(executable, root, "pack", archive, source, ok=False)
            assert archive.read_bytes() == original
            # Use the /var alias to verify that extraction resolves symlink parents.
            alias = str(archive).replace("/private/var/", "/var/", 1)
            extracted = run(executable, root, "unpack", "--progress", alias)
            destination = root / "roundtrip"
            assert extracted.stdout.startswith("Extracted to ") and extracted.stdout.count("\n") == 1, extracted
            reported = extracted.stdout.removeprefix("Extracted to ").removesuffix("\n").replace("\\\\", "\\")
            assert Path(reported).is_absolute() and Path(reported).samefile(destination), extracted.stdout
            assert "Progress:" in extracted.stderr
            assert "100%" in extracted.stderr and "█" in extracted.stderr
            assert "\x1b[2K" in extracted.stderr and "\x1b" not in extracted.stdout
            assert (destination / "data.txt").read_bytes() == b"payload"
            assert (destination / "nested/large.txt").read_bytes() == b"x" * 200000
            assert (destination / "nested/empty.txt").read_bytes() == b""
            assert (destination / "empty").is_dir()
            assert os.readlink(destination / "link") == "nested"
            assert os.readlink(destination / "dangling") == "absent-🙂.txt"
            run(executable, root, "unpack", archive, ok=False)
            shutil.rmtree(destination)
        kept = root / "kept.zip"
        run(executable, root, "pack", "--keep-root", kept, source)
        assert source.name + "/nested/large.txt\n" in run(executable, root, "list", kept).stdout
        dotted = root / "dotted.zip"
        run(executable, source, "pack", "--keep-root", dotted, ".")
        assert source.name + "/nested/large.txt\n" in run(executable, root, "list", dotted).stdout
        run(executable, root, "pack", "missing-source.zip", source, root / "absent", ok=False)
        assert not (root / "missing-source.zip").exists()
        implicit = root / "implicit"
        implicit.mkdir()
        (implicit / "file").write_bytes(b"implicit")
        run(executable, root, "pack", "implicit.zip")
        assert run(executable, root, "list", "implicit.zip").stdout == "file\n"
        run(executable, root, "pack", "--format", "tar.xz", "explicit.bin", source)
        assert "data.txt" in run(executable, root, "list", "explicit.bin").stdout
        run(executable, root, "pack", "--format", "zip", "conflict.tar", source, ok=False)
        assert not (root / "conflict.tar").exists()
        help_text = run(executable, root, "pack", "--help").stdout
        assert "choices:" in help_text
        for profile in PROFILES:
            assert f'"{profile}"' in help_text
        for invalid in ("rar", "ZIP", "формат"):
            result = run(executable, root, "pack", "--format", invalid,
                         "invalid.zip", source, ok=False)
            assert result.returncode == 2 and "expected one of:" in result.stderr
            assert not (root / "invalid.zip").exists()
        run(executable, root, "pack", "--format=", "automatic.zip", source)
        assert "data.txt" in run(executable, root, "list", "automatic.zip").stdout



def check_extraction_policy(executable):
    """Reject traversal and writes through restored links while preserving existing destinations."""
    with tempfile.TemporaryDirectory(prefix="arc-policy-") as temporary:
        root = Path(temporary).resolve()
        for index, name in enumerate(("../escape", "/absolute", "nested/../../escape")):
            archive = root / f"unsafe-{index}.zip"
            with zipfile.ZipFile(archive, "w") as output:
                output.writestr(name, b"bad")
            result = run(executable, root, "unpack", archive, ok=False)
            assert "Invalid relative" in result.stderr
            assert not (root / "escape").exists()
        outside = root / "outside"
        outside.mkdir()
        archive = root / "symlink.tar"
        with tarfile.open(archive, "w") as output:
            link = tarfile.TarInfo("link")
            link.type = tarfile.SYMTYPE
            link.linkname = "../outside"
            output.addfile(link)
            payload = tarfile.TarInfo("link/escape")
            payload.size = 3
            output.addfile(payload, io.BytesIO(b"bad"))
        run(executable, root, "unpack", archive, ok=False)
        assert not (outside / "escape").exists()
        (root / "occupied.zip").symlink_to("missing")
        source = root / "file"
        source.write_bytes(b"data")
        run(executable, root, "pack", "occupied.zip", source, ok=False)
        assert os.readlink(root / "occupied.zip") == "missing"
        assert not (root / "missing").exists()


def check_inspection_and_passwords(executable):
    """Check real payload failures, summaries and AES ZIP using an independent reader."""
    with tempfile.TemporaryDirectory(prefix="arc-inspection-") as temporary:
        root = Path(temporary)
        source = root / "payload.txt"
        source.write_bytes(b"independent encrypted payload" * 10000)
        password = "пароль-🙂"
        protected = root / "protected.zip"
        run(executable, root, "pack", "--password", password, protected, source)
        with zipfile.ZipFile(protected) as archive:
            entry = archive.infolist()[0]
            assert entry.flag_bits & 1 and entry.compress_type == 99
            assert b"independent encrypted payload" not in protected.read_bytes()
        for command in ("info", "list", "verify"):
            result = run(executable, root, command, "--password", password, protected)
            if command == "info":
                assert "Encryption: yes" in result.stdout
        before = set(root.iterdir())
        for arguments in ((), ("--password", "wrong")):
            result = run(executable, root, "verify", *arguments, protected, ok=False)
            assert password not in result.stderr
        assert set(root.iterdir()) == before
        run(executable, root, "unpack", "--password", password, protected)
        assert (root / "protected/payload.txt").read_bytes() == source.read_bytes()
        # bsdtar is an independent libarchive consumer and supports WinZip AES.
        bsdtar = shutil.which("bsdtar") or shutil.which("tar")
        assert bsdtar, "an independent libarchive tar reader is required"
        decoded = subprocess.run([bsdtar, "--passphrase", password, "-xOf", str(protected)],
                                 capture_output=True, timeout=30)
        assert decoded.returncode == 0 and decoded.stdout == source.read_bytes(), decoded.stderr
        for profile in PROFILES:
            if profile != "zip":
                output = root / ("rejected." + profile)
                result = run(executable, root, "pack", "--password", password, output, source, ok=False)
                assert "AES-256" in result.stderr and not output.exists()
        plain = root / "plain.zip"
        with zipfile.ZipFile(plain, "w") as archive:
            archive.writestr("file", b"unique payload for CRC validation")
        for command in ("list", "info", "verify"):
            run(executable, root, command, "--password", password, plain)
        # Headers still list, but fully consuming data must catch the CRC failure.
        damaged = bytearray(plain.read_bytes())
        offset = damaged.index(b"unique payload for CRC validation")
        damaged[offset] ^= 1
        broken = root / "damaged.zip"
        broken.write_bytes(damaged)
        run(executable, root, "list", broken)
        before = set(root.iterdir())
        run(executable, root, "verify", broken, ok=False)
        assert set(root.iterdir()) == before
        truncated = root / "truncated.tar.gz"
        with tarfile.open(truncated, "w:gz") as archive:
            archive.add(source, arcname="file")
        truncated.write_bytes(truncated.read_bytes()[:40])
        run(executable, root, "verify", truncated, ok=False)
        empty = root / "empty.zip"
        with zipfile.ZipFile(empty, "w"):
            pass
        assert "Entries: 0" in run(executable, root, "info", empty).stdout
        run(executable, root, "verify", empty)
        linked = root / "links.tar"
        with tarfile.open(linked, "w") as archive:
            entry = tarfile.TarInfo("file")
            entry.size = 3
            archive.addfile(entry, io.BytesIO(b"abc"))
            entry = tarfile.TarInfo("hard")
            entry.type = tarfile.LNKTYPE
            entry.linkname = "file"
            archive.addfile(entry)
            entry = tarfile.TarInfo("symbolic")
            entry.type = tarfile.SYMTYPE
            entry.linkname = "file"
            archive.addfile(entry)
        report = run(executable, root, "info", linked).stdout
        for text in ("Files: 1", "Hard links: 1", "Symbolic links: 1", "Declared file bytes: 3"):
            assert text in report, report
        run(executable, root, "verify", linked)
        run(executable, root, "unpack", linked, ok=False)


def check_windows_policy(executable):
    """Exercise Windows name rejection and junction non-traversal on a real Windows host."""
    if os.name != "nt":
        return
    with tempfile.TemporaryDirectory(prefix="arc-windows-") as temporary:
        root = Path(temporary).resolve()
        unsafe = (r"dir\..\escape", "C:escape", r"\escape", r"\\server\share\escape",
                  r"\\?\C:\escape", "nul.txt", "file:stream", "trailing.", "trailing ", "bad?.txt")
        for index, name in enumerate(unsafe):
            archive = root / f"invalid-{index}.zip"
            with zipfile.ZipFile(archive, "w") as output:
                output.writestr(name, b"bad")
            run(executable, root, "list", archive)
            run(executable, root, "unpack", archive, ok=False)
            assert not (root / "escape").exists()
        target = root / "target"
        target.mkdir()
        (target / "file").write_bytes(b"must not traverse")
        junction = root / "junction"
        result = subprocess.run(["cmd", "/c", "mklink", "/J", str(junction), str(target)],
                                capture_output=True, timeout=30)
        assert result.returncode == 0, result.stderr
        try:
            output = root / "junction.zip"
            run(executable, root, "pack", output, junction, ok=False)
            assert not output.exists() and (target / "file").read_bytes() == b"must not traverse"
        finally:
            junction.rmdir()


def main(arguments):
    """Check each supplied executable without using stale or implicitly discovered binaries."""
    if not arguments:
        raise SystemExit("usage: check_cli.py EXECUTABLE [EXECUTABLE ...]")
    for argument in arguments:
        executable = Path(argument).resolve()
        with tempfile.TemporaryDirectory(prefix="arc-help-") as temporary:
            root = Path(temporary)
            assert "Commands:" in run(executable, root, "--help").stdout
            assert "libarchive version number:" in run(executable, root, "--version").stdout
            for command in ("pack", "unpack", "list", "info", "verify"):
                run(executable, root, command, "--help")
            run(executable, root, "unknown", ok=False)
            run(executable, root, "unpack", "", ok=False)
        check_read_commands(executable)
        check_pack_and_unpack(executable)
        check_extraction_policy(executable)
        check_inspection_and_passwords(executable)
        check_windows_policy(executable)
        print(f"PASS: {executable}: CLI profiles, traversal, links, output ownership, listing, inspection, verification and passwords")


if __name__ == "__main__":
    main(sys.argv[1:])

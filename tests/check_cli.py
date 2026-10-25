"""Exercise explicitly supplied Arc executables in empty directories."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile
import tarfile
import os


COMMANDS = ("pack", "unpack", "list", "info", "verify")
SECRET = "private-password-value"


def cases():
    yield [], 0, "Usage:"
    yield ["help"], 2, ""
    yield ["help", "version"], 2, ""
    yield ["version"], 2, ""
    yield ["--version"], 0, "libarchive version number:"
    yield ["--help"], 0, "Commands:"
    yield ["-h"], 0, "Commands:"
    yield ["--version", "--help"], 0, "Commands:"
    yield ["--help", "--version"], 0, "Commands:"
    yield ["--help"], 0, "--version"
    for arguments in (["unknown"], ["--version=yes"], ["--version", "--version"],
                      ["--version", "pack", "a"], ["--help", "pack"],
                      ["version", "pack", "a"], ["version", "extra"],
                      ["help", "unknown"], ["help", "pack", "extra"]):
        yield arguments, 2, ""
    for command in COMMANDS:
        operation_status = 2 if command == "pack" else 1
        yield ["help", command], 2, ""
        yield [command, "--help"], 0, "--password"
        yield [command, "--help"], 0, f"arc {command}"
        yield [command, "-h"], 0, f"arc {command}"
        yield [command, "a", "--help"], 0, f"arc {command}"
        yield [command, "--help", "--unknown"], 0, f"arc {command}"
        yield [command, "--unknown", "--help"], 2, "Unknown option"
        yield [command, "--password", "--help"], 2, ""
        yield [command, "--password=--help", "a"], operation_status, ""
        for operands in (["a"], ["несуществующий архив.arc"], ["--", "-archive"]):
            yield [command, *operands], operation_status, ""
        for option in (["--password", SECRET], [f"--password={SECRET}"],
                       ["--password="], ["--password", ""], ["--password=a=b"]):
            yield [command, *option, "a"], operation_status, ""
        yield [command, "a", "--password", SECRET], operation_status, ""
        for arguments in ([], ["--password"], ["--unknown", SECRET],
                          ["--password", SECRET, "--password", SECRET, "a"],
                          ["--version", "a"], ["--help=yes"]):
            yield [command, *arguments], 2, ""
        if command != "pack":
            yield [command, "a", "a"], 2, "Unexpected argument"
    for command, flag in (("pack", "keep-root"), ("list", "tree")):
        yield [command, f"--{flag}", "a"], 1 if command == "list" else 2, ""
        yield [command, "a", f"--{flag}"], 1 if command == "list" else 2, ""
        yield [command, f"--{flag}", f"--{flag}", "a"], 2, "Repeated parameter"
        yield [command, f"--{flag}=yes", "a"], 2, "Flag does not accept a value"
    for arguments in (["--format", "zip"], ["--format=zip"], ["--format="]):
        yield ["pack", *arguments, "a", "source"], 2 if arguments == ["--format="] else 1, ""
    yield ["pack", "--format", "zip", "--format", "tar", "a"], 2, "Repeated parameter"
    yield ["unpack", "--entry", "one", "a"], 2, "Unknown option"
    yield ["unpack", "a", "--strip-root"], 2, "Unknown option"
    yield ["unpack", ""], 2, "Archive path must not be empty"
    yield ["info", ""], 2, "Archive path must not be empty"
    yield ["pack", "a", "--", "--help", "--keep-root"], 2, ""
    yield ["list", "--", "--password=secret"], 1, ""


def check_unpack(executable):
    count = 0
    for password in (None, "", "пароль-test"):
        with tempfile.TemporaryDirectory(prefix="arc-cli-unpack-") as directory:
            root = Path(directory)
            archive = root / "проект.zip"
            if password:
                (root / "data.txt").write_bytes(b"payload")
                subprocess.run([shutil.which("zip") or "zip", "-q", "-P", password,
                                str(archive), "data.txt"], cwd=root, check=True)
                (root / "data.txt").unlink()
            else:
                with zipfile.ZipFile(archive, "w") as output:
                    output.writestr("data.txt", b"payload")
            for unsupported in (["--strip-root"], ["--entry=data.txt"], ["destination"]):
                rejected = subprocess.run([str(executable), "unpack", archive.name, *unsupported],
                                          cwd=root, capture_output=True, timeout=10)
                assert rejected.returncode == 2 and rejected.stderr and not rejected.stdout, rejected
                assert set(root.iterdir()) == {archive}
                count += 1
            if password:
                for args in ([], ["--password", "wrong-password"]):
                    failed = subprocess.run([str(executable), "unpack", *args, archive.name],
                                            cwd=root, capture_output=True, timeout=10)
                    assert failed.returncode == 1 and failed.stderr and not failed.stdout, failed
                    assert password.encode() not in failed.stderr
                    shutil.rmtree(root / "проект")
                    count += 1
            args = [] if password is None else ["--password", password]
            result = subprocess.run([str(executable), "unpack", *args, archive.name],
                                    cwd=root, capture_output=True, timeout=10)
            assert result.returncode == 0 and not result.stdout and not result.stderr, result
            assert (root / "проект" / "data.txt").read_bytes() == b"payload"
            result = subprocess.run([str(executable), "unpack", *args, archive.name],
                                    cwd=root, capture_output=True, timeout=10)
            assert result.returncode == 1 and result.stderr and not result.stdout, result
            assert (root / "проект" / "data.txt").read_bytes() == b"payload"
            count += 2
    with tempfile.TemporaryDirectory(prefix="arc-cli-corrupt-") as directory:
        archive = Path(directory) / "broken.zip"
        archive.write_bytes(b"not an archive")
        failed = subprocess.run([str(executable), "unpack", str(archive)], cwd=directory,
                                capture_output=True, timeout=10)
        assert failed.returncode == 1 and failed.stderr and not failed.stdout, failed
        count += 1
    return count


def check_pack(executable):
    count = 0
    with tempfile.TemporaryDirectory(prefix="arc-cli-pack-") as directory:
        root = Path(directory)
        source = root / "проект📦"
        source.mkdir()
        (source / "data.txt").write_bytes(b"payload")
        (source / ".hidden").write_bytes(b"hidden")
        (source / "empty").mkdir()
        extra = root / "extra.txt"
        extra.write_bytes(b"extra")
        output_dir = root / "output"
        output_dir.mkdir()

        def run(arguments, status=0):
            nonlocal count
            result = subprocess.run([str(executable), "pack", *map(str, arguments)],
                                    cwd=root, capture_output=True, timeout=30)
            assert result.returncode == status, (arguments, status, result)
            if status:
                assert result.stderr and not result.stdout, result
            else:
                assert result.stdout.startswith(b"Created ") and not result.stderr, result
            count += 1
            return result

        for index, suffix in enumerate(("zip", "tar", "tar.gz", "tgz", "tar.bz2", "tbz2", "tar.xz", "txz", "tar.zst", "tzst", "7z", "ZIP")):
            output = output_dir / (f"archive-{index}." + suffix)
            run([output, source])
            reader = shutil.which("bsdtar")
            if reader is None:
                raise AssertionError("bsdtar is required to independently read packed archives")
            contents = subprocess.run([reader, "-xOf", str(output), "data.txt"],
                                      capture_output=True, timeout=30)
            assert contents.returncode == 0 and contents.stdout == b"payload", contents
            if suffix.lower() == "zip":
                with zipfile.ZipFile(output) as archive:
                    assert set(archive.namelist()) == {"data.txt", ".hidden", "empty/"}
            previous = output.read_bytes()
            run([output, source], 1)
            assert output.read_bytes() == previous

        for arguments, prefix in ((["--keep-root", source], source.name + "/"),
                                  ([source, extra], source.name + "/"),
                                  ([source / "data.txt"], "")):
            output = root / "mapped.zip"
            run([output, *arguments])
            with zipfile.ZipFile(output) as archive:
                assert archive.read(prefix + "data.txt") == b"payload"
                if extra in arguments:
                    assert archive.read("extra.txt") == b"extra"
            output.unlink()

        for argument in (".", "./", source.name + "/."):
            output = output_dir / "relative.zip"
            result = subprocess.run([str(executable), "pack", str(output), argument],
                                    cwd=source if argument in (".", "./") else root,
                                    capture_output=True, timeout=30)
            assert result.returncode == 0, result
            with zipfile.ZipFile(output) as archive:
                assert archive.read("data.txt") == b"payload"
            output.unlink()
            count += 1

        dashed = root / "--help"
        dashed.write_bytes(b"dash")
        run(["dashed.zip", "--", dashed.name])
        with zipfile.ZipFile(root / "dashed.zip") as archive:
            assert archive.read("--help") == b"dash"

        for keep_root in (False, True):
            output = output_dir / (source.name + ".tar.gz")
            run((["--keep-root"] if keep_root else []) + [output])
            with tarfile.open(output) as archive:
                assert archive.extractfile((source.name + "/" if keep_root else "") + "data.txt").read() == b"payload"
            output.unlink()

        for format_name in ("zip", "tar", "7z"):
            output = root / "explicit.bin"
            run(["--format", format_name, output, source])
            verified = subprocess.run([str(executable), "verify", str(output)],
                                      capture_output=True, timeout=30)
            assert verified.returncode == 0, verified
            output.unlink()
        run(["--format", "tar", root / "explicit.tar.gz", source])
        assert (root / "explicit.tar.gz").read_bytes().startswith(b"\x1f\x8b")

        result = run(["--format", "unsupported", "bad.zip", source], 1)
        assert b"Unsupported format" in result.stderr and b"conflicts" not in result.stderr

        for arguments, status in ((["--format", "zip", "bad.tar.gz", source], 2),
                                  (["--format", "unsupported", "bad.bin", source], 1),
                                  (["bad.bin", source], 2),
                                  (["--keep-root", "bad.zip", source, extra], 2),
                                  (["--keep-root", "bad.zip", extra], 2),
                                  (["bad.zip", ""], 2),
                                  (["bad.zip", "missing"], 1),
                                  (["missing.zip"], 1),
                                  (["extra.txt.zip"], 1),
                                  (["--format", "zip", ".zip"], 2),
                                  (["bad.zip", source / "data.txt", source / "data.txt"], 1),
                                  (["--password", SECRET, "bad.tar", source], 1)):
            result = run(arguments, status)
            assert SECRET.encode() not in result.stderr
            assert not (root / "bad.zip").exists()
            assert not (root / "bad.tar").exists()
            assert not (root / "bad.bin").exists()
            assert not (root / "bad.tar.gz").exists()

        if os.name != "nt":
            link = root / "directory-link"
            link.symlink_to(source.name, target_is_directory=True)
            dangling = root / "dangling"
            dangling.symlink_to("missing-target")
            output = root / "links.tar"
            run([output, link, dangling])
            with tarfile.open(output) as archive:
                assert archive.getmember("directory-link").issym()
                assert archive.getmember("dangling").linkname == "missing-target"
            run(["--keep-root", "bad.zip", link], 2)

        output = root / "encrypted.zip"
        run(["--password", SECRET, output, source])
        reader = shutil.which("bsdtar")
        contents = subprocess.run([reader, "--passphrase", SECRET, "-xOf", str(output), "data.txt"],
                                  capture_output=True, timeout=30)
        assert contents.returncode == 0 and contents.stdout == b"payload", contents
        wrong = subprocess.run([str(executable), "verify", "--password", "wrong", str(output)],
                               capture_output=True, timeout=30)
        assert wrong.returncode == 1, wrong
    return count


def main():
    if len(sys.argv) < 2:
        raise SystemExit("Usage: python tests/check_cli.py EXECUTABLE [EXECUTABLE...]")
    for name in sys.argv[1:]:
        executable = Path(name).resolve(strict=True)
        count = 0
        for arguments, status, expected in cases():
            with tempfile.TemporaryDirectory(prefix="arc-cli-") as directory:
                result = subprocess.run([str(executable), *arguments], cwd=directory,
                                        capture_output=True, text=True, encoding="utf-8", timeout=10)
                output = result.stderr if status else result.stdout
                if (result.returncode != status or (status != 0 and not result.stderr) or (expected not in output if expected else (not status and output != ""))
                        or (result.stdout if status else result.stderr)
                        or list(Path(directory).iterdir())):
                    raise AssertionError((executable, arguments, status, expected, result))
            count += 1
        count += check_unpack(executable)
        count += check_pack(executable)
        print(f"{executable}: {count} CLI cases passed")


if __name__ == "__main__":
    main()

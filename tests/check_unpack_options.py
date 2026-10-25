"""Check passwords, observer ordering and snapshots in compiled Eiffel probes."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile


def check_binary(binary):
    count = 0
    with tempfile.TemporaryDirectory(prefix="arc-options-") as temporary:
        root = Path(temporary).resolve()
        payload = bytes(range(256)) * 12000

        def run(source, mode="normal", password=None, success=True, totals=None):
            nonlocal count
            arguments = [str(binary), str(source), mode]
            if password is not None:
                arguments.append(password)
            result = subprocess.run(arguments, cwd=root, capture_output=True, timeout=20)
            assert (result.returncode == 0) == success, (source.name, mode, result.stderr.decode(errors="replace"))
            if password:
                assert password.encode() not in result.stdout + result.stderr
            if totals is not None:
                assert result.stdout.decode().strip() == f"{totals[0]} {totals[1]}", result.stdout
            count += 1

        for mode in ("normal", "raise", "final_raise", "reenter"):
            source = root / f"{mode}.zip"
            with zipfile.ZipFile(source, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("данные.bin", payload)
                archive.writestr("empty", b"")
            run(source, mode, totals=(len(payload), 2) if mode in ("normal", "reenter") else None)
            assert (root / mode / "данные.bin").read_bytes() == payload

        source = root / "empty.zip"
        with zipfile.ZipFile(source, "w"):
            pass
        run(source, totals=(0, 0))

        (root / "secret.txt").write_bytes(payload)
        fixture = root / "encrypted.zip"
        # Fixed public test data; this password is not user input or a stored credential.
        password = "пароль-test"
        subprocess.run(["zip", "-q", "-P", password, str(fixture), "secret.txt"], cwd=root, check=True)
        for label, value, success in (("correct", password, True), ("wrong", "wrong-password", False), ("absent", None, False)):
            source = root / f"encrypted-{label}.zip"
            shutil.copyfile(fixture, source)
            run(source, password=value, success=success, totals=(len(payload), 1) if success else None)
            if success:
                assert (root / f"encrypted-{label}/secret.txt").read_bytes() == payload

        source = root / "plain-with-password.zip"
        with zipfile.ZipFile(source, "w") as archive:
            archive.writestr("file", b"plain")
        run(source, password="unused-password", totals=(5, 1))

    print(f"{binary}: {count} options and observer scenarios passed")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("Usage: python tests/check_unpack_options.py OPTIONS_PROBE [OPTIONS_PROBE...]")
    for argument in sys.argv[1:]:
        check_binary(Path(argument).resolve(strict=True))

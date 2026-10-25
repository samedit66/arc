"""Prepare native dependencies on the current Windows, Linux, or macOS host."""

import argparse
from collections.abc import Sequence
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main(arguments: Sequence[str] | None = None) -> None:
    """Prepare and deploy native dependencies using explicit or detected settings.

    Parse arguments from the supplied sequence, or sys.argv when None. Run
    Conan with a project-local cache and replace the deployment after a
    successful install. Configuration errors exit; Conan failures propagate
    as CalledProcessError, preserving the previous deployment.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", help="Native Conan profile; otherwise detect arc-native")
    parser.add_argument("--no-remote", action="store_true", help="Use only cached recipes and packages")
    args = parser.parse_args(arguments)
    root = Path(__file__).resolve().parents[1]
    build = root / "build"
    build.mkdir(exist_ok=True)
    env = dict(os.environ, CONAN_HOME=str(build / "conan"))
    if sys.platform == "darwin":
        env.setdefault("CC", "/usr/bin/clang")
        env.setdefault("CXX", "/usr/bin/clang++")
    elif sys.platform == "win32" and shutil.which("cl") is None:
        raise SystemExit("Run from a Visual Studio Developer terminal with cl.exe on PATH.")
    elif not sys.platform.startswith("linux") and sys.platform != "win32":
        raise SystemExit(f"Unsupported host platform: {sys.platform}")
    conan = shutil.which("conan")
    if conan is None:
        raise SystemExit("Conan is not on PATH. Run this script through mise run prepare.")
    profile = args.profile or "arc-native"
    if args.profile is None:
        subprocess.run([conan, "profile", "detect", "--name", profile, "--force"],
                       cwd=root, env=env, check=True)
    # Keep the previous deployment usable if dependency installation fails.
    with tempfile.TemporaryDirectory(prefix=".native-", dir=build) as temporary:
        command = [conan, "install", ".", "-pr:h", profile, "-pr:b", profile,
                   "--build=missing", "--deployer=full_deploy", "-of", temporary]
        if args.no_remote:
            command.append("--no-remote")
        subprocess.run(command, cwd=root, env=env, check=True)
        native = build / "native"
        if native.exists():
            shutil.rmtree(native)
        Path(temporary).rename(native)
    print(build / "native/native.ecf")


if __name__ == "__main__":
    main()

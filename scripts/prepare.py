"""Prepare static dependencies, build Arc, or inspect an application executable."""

import argparse
from collections.abc import Mapping, Sequence
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile


def dependency_violations(system: str, dependencies: Sequence[str]) -> tuple[str, ...]:
    """Return dependencies forbidden in a distributable application."""
    windows = {"kernel32.dll", "kernelbase.dll", "ntdll.dll", "advapi32.dll", "bcrypt.dll",
               "crypt32.dll", "ws2_32.dll", "user32.dll", "shell32.dll", "ole32.dll",
               "oleaut32.dll", "gdi32.dll", "msimg32.dll", "comdlg32.dll", "comctl32.dll",
               "mpr.dll", "shlwapi.dll", "winspool.drv", "rpcrt4.dll", "secur32.dll"}
    if system == "Macos":
        return tuple(value for value in dependencies
                     if not value.startswith(("/usr/lib/", "/System/Library/")))
    if system == "Windows":
        return tuple(value for value in dependencies if value.lower() not in windows and
                     not (value.lower().startswith(("api-ms-win-", "ext-ms-win-")) and
                          not value.lower().startswith("api-ms-win-crt-")))
    if system == "Linux":
        return tuple(dependencies)
    raise ValueError(f"Unsupported system: {system}")


def check_binary(binary: Path, settings: Mapping[str, str]) -> None:
    """Inspect native architecture and imports; reject unexpected runtime dependencies."""
    if not binary.is_file():
        raise SystemExit(f"Executable not found: {binary}")
    system = settings["os"]
    architecture = settings["arch"]
    env = dict(os.environ, LC_ALL="C", LANG="C", VSLANG="1033")

    def output(*arguments: str) -> str:
        """Run an inspection tool, retaining its diagnostics on failure."""
        result = subprocess.run([*arguments, str(binary)], env=env, check=True,
                                capture_output=True, text=True, errors="replace")
        return result.stdout

    if system == "Macos":
        expected = {"armv8": "arm64", "x86_64": "x86_64"}[architecture]
        if output("lipo", "-archs").strip() != expected:
            raise SystemExit("Executable architecture disagrees with native manifest")
        dependencies = [line.strip().split(" (", 1)[0]
                        for line in output("otool", "-L").splitlines()[1:] if line.strip()]
    elif system == "Windows":
        if settings.get("compiler.runtime") != "static":
            raise SystemExit("Distributable Windows applications require static CRT (/MT)")
        report = output("dumpbin", "/HEADERS", "/DEPENDENTS")
        machine = {"armv8": "AA64", "x86_64": "8664"}[architecture]
        if not re.search(rf"\b{machine}\s+machine\b", report, re.IGNORECASE):
            raise SystemExit("Executable architecture disagrees with native manifest")
        dependencies = re.findall(r"^\s+([\w.-]+\.(?:dll|drv))\s*$", report,
                                  re.MULTILINE | re.IGNORECASE)
        if not dependencies:
            raise SystemExit("Could not read Windows executable dependencies")
    elif system == "Linux":
        if settings.get("libc") != "musl":
            raise SystemExit("Distributable Linux applications require the musl deployment")
        report = output("readelf", "-h", "-l", "-d")
        machine = {"armv8": "AArch64", "x86_64": "Advanced Micro Devices X86-64"}[architecture]
        if not re.search(rf"Machine:\s+{re.escape(machine)}\s*$", report, re.MULTILINE):
            raise SystemExit("Executable architecture disagrees with native manifest")
        if re.search(r"\bINTERP\b|\(NEEDED\)", report):
            raise SystemExit("Linux executable has an interpreter or shared dependencies")
        dependencies = []
    else:
        raise SystemExit(f"Unsupported system: {system}")
    forbidden = dependency_violations(system, dependencies)
    if forbidden:
        raise SystemExit("Unexpected executable dependencies: " + ", ".join(forbidden))


def native_libc() -> str:
    """Identify the native Linux libc; cross-libc builds require a separate toolchain workflow."""
    name = platform.libc_ver()[0]
    if name == "glibc":
        return name
    result = subprocess.run(["ldd", "--version"], capture_output=True, text=True)
    if "musl" in (result.stdout + result.stderr).lower():
        return "musl"
    raise SystemExit("Cannot identify the native Linux libc")


def build_application(root: Path) -> Path:
    """Build a finalized Gobo CLI with prepared dependencies and verify its imports."""
    manifest = json.loads((root / "build/native/manifest.json").read_text())
    settings = manifest["settings"]
    if settings["build_type"] != "Release":
        raise SystemExit("Application distribution requires Release native dependencies")
    if not all((root / path).is_file() for path in
               ("vendor/cli/cli.ecf", "vendor/progress_bar/progress_bar.ecf")):
        raise SystemExit("Run git submodule update --init --recursive before building the CLI")
    if settings["os"] == "Windows" and settings.get("compiler.runtime") != "static":
        raise SystemExit("Prepare Windows dependencies with compiler.runtime=static (/MT)")
    if settings["os"] == "Linux" and settings.get("libc") != "musl":
        raise SystemExit("Build distributable Linux applications in a native musl environment")
    env = dict(os.environ, GOBO_EIFFEL="ge")
    command = ["gec", "--gelint", "--finalize", "--gc=boehm",
               "--variable=ARC_NATIVE_MODE=conan",
               "--variable=ARC_LIBC=" + settings.get("libc", "glibc"), "--target=cli", "arc.ecf"]
    if os.name != "nt":
        command.insert(1, "--cc=no")
    subprocess.run(command, cwd=root, env=env, check=True)
    binary = root / ("arc.exe" if settings["os"] == "Windows" else "arc")
    if os.name != "nt":
        # Run the generated script explicitly to preserve Unicode paths and stop on C compile errors.
        subprocess.run(["sh", "-e", "arc.sh"], cwd=root / ".gobo", env=env, check=True)
        shutil.copy2(root / ".gobo/arc", binary)
    check_binary(binary, settings)
    return binary


def main(arguments: Sequence[str] | None = None) -> None:
    """Prepare a native deployment and optionally build, or inspect an existing executable."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", help="Conan host profile; otherwise detect arc-native")
    parser.add_argument("--build-profile", help="Conan build profile; defaults to the native detected profile")
    parser.add_argument("--no-remote", action="store_true", help="Use cached recipes and packages only")
    parser.add_argument("--libc", choices=("glibc", "musl"), help="Require this native Linux libc")
    parser.add_argument("--build", action="store_true", help="Build and inspect the release CLI")
    parser.add_argument("--skip-prepare", action="store_true", help="Use the existing native deployment")
    parser.add_argument("--check-binary", type=Path, help="Inspect an executable against the existing manifest")
    args = parser.parse_args(arguments)
    root = Path(__file__).resolve().parents[1]
    if args.check_binary:
        manifest = json.loads((root / "build/native/manifest.json").read_text())
        check_binary(args.check_binary.resolve(), manifest["settings"])
        print(f"Verified executable: {args.check_binary}")
        return
    if not args.skip_prepare:
        prepare_dependencies(root, args.profile, args.build_profile, args.no_remote, args.libc)
    if args.build:
        print(build_application(root))
    elif args.skip_prepare:
        parser.error("--skip-prepare requires --build or --check-binary")
    else:
        print(root / "build/native/native.ecf")


def prepare_dependencies(root: Path, profile: str | None = None, build_profile: str | None = None,
                         no_remote: bool = False, libc: str | None = None) -> None:
    """Install Conan dependencies and transactionally replace the prepared deployment."""
    build = root / "build"
    build.mkdir(exist_ok=True)
    detected_libc = native_libc() if sys.platform.startswith("linux") else None
    if libc and libc != detected_libc:
        raise SystemExit("--libc must match the native environment; use a native musl container for musl")
    cache = "conan-musl" if detected_libc == "musl" else "conan"
    env = dict(os.environ, CONAN_HOME=str(build / cache))
    if detected_libc:
        env["ARC_LIBC"] = detected_libc
    if sys.platform == "darwin":
        env.setdefault("CC", "/usr/bin/clang")
        env.setdefault("CXX", "/usr/bin/clang++")
    elif sys.platform == "win32" and shutil.which("cl") is None:
        raise SystemExit("Run from a Visual Studio Developer terminal with cl.exe on PATH")
    elif not sys.platform.startswith("linux") and sys.platform != "win32":
        raise SystemExit(f"Unsupported host platform: {sys.platform}")
    conan = shutil.which("conan")
    if conan is None:
        raise SystemExit("Conan is not on PATH. Run this script through mise run prepare")
    if profile is None or build_profile is None:
        subprocess.run([conan, "profile", "detect", "--name", "arc-native", "--force"],
                       cwd=root, env=env, check=True)
    host = profile or "arc-native"
    native_build = build_profile or "arc-native"
    with tempfile.TemporaryDirectory(prefix=".native-", dir=build) as temporary:
        command = [conan, "install", ".", "-pr:h", host, "-pr:b", native_build,
                   "--build=*" if detected_libc == "musl" else "--build=missing",
                   "--deployer=full_deploy", "-of", temporary]
        if sys.platform == "win32":
            # libarchive 3.8.7 does not search for Conan's Windows zstd_static.lib.
            hook = build / "libarchive-static-zstd.cmake"
            hook.write_text("find_library(ZSTD_LIBRARY NAMES zstd_static REQUIRED)\n", encoding="utf-8")
            variables = json.dumps({"CMAKE_PROJECT_libarchive_INCLUDE": hook.resolve().as_posix()})
            command += ["-c:h", "libarchive/*:tools.cmake.cmaketoolchain:extra_variables=" + variables,
                        "-c:h", 'libarchive/*:tools.info.package_id:confs=["tools.cmake.cmaketoolchain:extra_variables"]']
            if profile is None:
                command += ["-s:h", "compiler.runtime=static"]
        if no_remote:
            command.append("--no-remote")
        subprocess.run(command, cwd=root, env=env, check=True)
        native = build / "native"
        previous = build / ".native-previous"
        if previous.exists():
            raise SystemExit(f"Previous native deployment needs recovery or removal: {previous}")
        if native.exists():
            native.rename(previous)
        try:
            Path(temporary).rename(native)
        except OSError:
            if previous.exists():
                previous.rename(native)
            raise
        if previous.exists():
            shutil.rmtree(previous)


if __name__ == "__main__":
    main()

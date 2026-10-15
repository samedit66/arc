"""Build relocation and cleanup through the shell task entry points."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
BASH = os.environ.get("MISE_BASH_PATH", "bash")


def run_script(root, name, *arguments, env=None):
    return subprocess.run(
        [BASH, (root / "scripts" / name).as_posix(), *arguments],
        cwd=root, env={**os.environ, **(env or {})},
        capture_output=True, text=True, timeout=30,
    )


@pytest.mark.parametrize("move", ["source", "build", "alias"])
@pytest.mark.parametrize("sanitize", [False, True])
def test_configure_recovers_after_directory_move(tmp_path, move, sanitize):
    root = tmp_path / "original checkout"
    scripts = root / "scripts"
    scripts.mkdir(parents=True)
    for name in ("configure.sh", "build-env.sh", "cmake-cache.sh"):
        shutil.copyfile(ROOT / "scripts" / name, scripts / name)
    shutil.copyfile(ROOT / "CMakePresets.json", root / "CMakePresets.json")
    (root / "VERSION").write_text("0.1.0\n", newline="\n")
    (root / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.21)\nproject(relocation LANGUAGES C)\n'
        'option(ARC_SANITIZE "Enable sanitizers" OFF)\n'
        'file(WRITE "${CMAKE_BINARY_DIR}/configured-from" "${CMAKE_SOURCE_DIR}")\n',
        newline="\n",
    )
    environment = {"ARC_BUILD_DIR": "build/dev", "ARC_VERSION": "0.1.0",
                   "ARC_CMAKE_PRESET": "dev", "ARC_SANITIZE": "1" if sanitize else "0"}
    result = run_script(root, "configure.sh", env=environment)
    assert result.returncode == 0, result.stdout + result.stderr
    build = root / "build/dev"
    expected_mode = "ARC_SANITIZE:BOOL=" + ("ON" if sanitize else "OFF")
    assert expected_mode in (build / "CMakeCache.txt").read_text()
    if move == "source":
        relocated = tmp_path / "renamed checkout"
        root.rename(relocated)
        root = relocated
        build = root / "build/dev"
    elif move == "build":
        relocated = root / "build/moved"
        build.rename(relocated)
        build = relocated
    else:
        alias = tmp_path / "build alias"
        try:
            alias.symlink_to(build, target_is_directory=True)
        except OSError as error:
            if os.name != "nt":
                raise
            pytest.skip(f"Windows does not permit directory symlinks: {error}")
        build = alias
    environment["ARC_BUILD_DIR"] = build.as_posix()
    result = run_script(root, "configure.sh", env=environment)
    assert result.returncode == 0, result.stdout + result.stderr
    assert ("regenerating the CMake cache" in result.stdout) == (move != "alias")
    assert expected_mode in (build / "CMakeCache.txt").read_text()
    assert (build / "configured-from").read_text().casefold() == root.as_posix().casefold()
    result = subprocess.run(
        ["cmake", "--build", build.as_posix()], capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    result = run_script(root, "configure.sh", env=environment)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "regenerating" not in result.stdout


@pytest.mark.parametrize("argument", [None, "--help", "--unknown"])
def test_clean_only_removes_project_outputs(tmp_path, argument):
    root = tmp_path / "checkout with spaces"
    (root / "scripts").mkdir(parents=True)
    shutil.copyfile(ROOT / "scripts/clean.sh", root / "scripts/clean.sh")
    removable = [
        "build/release/target/vcpkg/.git/config", "build/dev/CMakeCache.txt",
        "tests/.venv/launcher", ".pytest_cache/cache", "tests/.pytest_cache/cache",
        "tests/__pycache__/test.pyc", "scripts/__pycache__/helper.pyc",
        "arc", "arc.exe", "arc.o", "libarc.a",
    ]
    preserved = [".git/config", "src/arc.c", "arc.h", "tests/fixtures/sample.rar", "docs/build.md"]
    for name in removable + preserved:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("sentinel")
    external = tmp_path / "external"
    external.mkdir()
    (external / "sentinel").write_text("external")
    arguments = [] if argument is None else [argument]
    result = run_script(root, "clean.sh", *arguments, env={
        "ARC_BUILD_DIR": str(external), "ARC_RELEASE_BUILD_DIR": str(external),
        "UV_PROJECT_ENVIRONMENT": str(external),
    })
    assert result.returncode == (1 if argument == "--unknown" else 0), result.stderr
    for name in preserved:
        assert (root / name).read_text() == "sentinel"
    assert (external / "sentinel").read_text() == "external"
    assert all((root / name).exists() == (argument is not None) for name in removable)
    if argument is None:
        assert run_script(root, "clean.sh").returncode == 0

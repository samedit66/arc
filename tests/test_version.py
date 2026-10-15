import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).parents[1]


@pytest.mark.parametrize("version", ["", "dev\n", "0.1.0\n0.2.0\n"])
def test_invalid_version_stops_cmake_build(tmp_path, version):
    (tmp_path / "scripts").mkdir()
    for name in ["build.sh", "build-env.sh", "configure.sh", "cmake-cache.sh"]:
        shutil.copyfile(ROOT / "scripts" / name, tmp_path / "scripts" / name)
    for name in ["CMakeLists.txt", "CMakePresets.json"]:
        shutil.copyfile(ROOT / name, tmp_path / name)
    (tmp_path / "VERSION").write_text(version)
    env = dict(os.environ, ARC_BUILD_DIR=str(tmp_path / "build"), ARC_CMAKE_PRESET="dev",
               ARC_SANITIZE="0", CC="cc")
    shell = os.environ.get("MISE_BASH_PATH") or shutil.which("bash")
    result = subprocess.run([shell, "scripts/build.sh"], cwd=tmp_path, env=env,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode != 0
    assert "VERSION must contain" in result.stderr
    assert not (tmp_path / "build/libarc.a").exists()

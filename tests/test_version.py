import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).parents[1]


@pytest.mark.parametrize("version", ["", "dev\n", "0.1.0\n0.2.0\n"])
def test_invalid_version_stops_shell_build(tmp_path, version):
    (tmp_path / "scripts").mkdir()
    (tmp_path / "filekit").mkdir()
    for name in ["build.sh", "build-env.sh"]:
        shutil.copyfile(ROOT / "scripts" / name, tmp_path / "scripts" / name)
    (tmp_path / "filekit/build.sh").write_text("exit 0\n", newline="\n")
    (tmp_path / "VERSION").write_text(version)
    env = dict(os.environ, ARC_BUILD_CONFIG="1", ARC_BUILD_DIR=str(tmp_path / "build"),
               ARC_PLATFORM="unix", ARC_EXE_SUFFIX="", ARC_CFLAGS="", ARC_SANITIZER_FLAGS="",
               ARC_LDFLAGS="", CC="cc")
    shell = os.environ.get("MISE_BASH_PATH") or shutil.which("bash")
    result = subprocess.run([shell, "scripts/build.sh"], cwd=tmp_path, env=env,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode != 0
    assert "VERSION must contain" in result.stderr
    assert not (tmp_path / "build/libarc.a").exists()

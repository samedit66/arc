import os
from pathlib import Path
import subprocess
import zipfile

import pytest

from support import file_entry, write_tar


def pytest_sessionstart(session):
    binary = os.environ.get("ARC_BINARY")
    if not binary or not Path(binary).is_absolute() or not Path(binary).is_file():
        raise pytest.UsageError("Run `mise run test`: it builds arc before starting pytest.")


@pytest.fixture(scope="session")
def arc_binary():
    return Path(os.environ["ARC_BINARY"])


@pytest.fixture
def arc(arc_binary, tmp_path):
    def invoke(*arguments, success=True, cwd=None, env=None):
        command = [str(arc_binary), *map(str, arguments)]
        environment = os.environ.copy()
        if env:
            environment.update(env)
        result = subprocess.run(
            command, cwd=cwd or tmp_path, env=environment,
            capture_output=True, text=True, encoding="utf-8", timeout=30,
        )
        diagnostic = (
            f"Command: {command!r}\nExit: {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
        if success is True:
            assert result.returncode == 0, diagnostic
        elif success is False:
            assert result.returncode > 0, diagnostic
        else:
            assert result.returncode >= 0, diagnostic
        return result
    return invoke


@pytest.fixture
def source_tree(tmp_path):
    source = tmp_path / "source"
    (source / "empty").mkdir(parents=True)
    (source / "sub").mkdir()
    (source / "plain.txt").write_bytes(b"content\n" * 20000)
    (source / ".hidden").write_bytes(b"hidden")
    (source / "sub/text-текст.txt").write_bytes("Hello мир\n".encode())
    (source / "zero").touch()
    if os.name != "nt":
        (source / "plain.txt").chmod(0o751)
    return source


@pytest.fixture
def make_tar(tmp_path):
    def create(entries, name="input.tar", *, mode="w"):
        return write_tar(tmp_path / name, entries, mode=mode)
    return create


@pytest.fixture
def simple_tar(make_tar):
    return make_tar([file_entry("value", b"payload")])


@pytest.fixture
def zip_archive(tmp_path):
    path = tmp_path / "input.zip"
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED) as archive:
        archive.writestr("value", b"payload")
        archive.writestr("safe", b"selected")
    return path


@pytest.fixture
def symlinks_available(tmp_path):
    target = tmp_path / "probe-target"
    target.write_bytes(b"probe")
    link = tmp_path / "probe-link"
    try:
        link.symlink_to(target.name)
    except OSError as error:
        if os.name != "nt":
            raise
        pytest.skip(f"Windows does not permit symlink creation: {error}")
    link.unlink()
    target.unlink()

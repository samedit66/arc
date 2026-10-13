import os
import zipfile

import pytest


def test_version(arc):
    result = arc("--version")
    assert result.stdout == f"arc {os.environ.get('ARC_VERSION') or 'dev'}\n"
    assert not result.stderr


@pytest.mark.parametrize("command", [None, "pack", "unpack", "list", "info", "verify"])
def test_help(arc, command):
    arguments = [command] if command else []
    result = arc(*arguments, "--help")
    assert "Usage:" in result.stdout
    assert not result.stderr


@pytest.mark.parametrize("arguments", [
    [], ["no-such-command"], ["--version", "extra"], ["pack"], ["unpack"], ["verify"],
    ["verify", "input.tar", "extra"], ["unpack", "--entry", "input.tar"],
    ["unpack", "--password-file", "secret", "input.tar"],
    ["verify", "--password-file", "secret", "input.tar"],
])
def test_invalid_arguments(arc, arguments):
    result = arc(*arguments, success=False)
    assert result.returncode == 2
    assert "Usage:" in result.stderr


def test_dash_prefixed_paths(arc, tmp_path):
    (tmp_path / "-input").write_bytes(b"dash")
    arc("pack", "--", "-output.zip", "-input")
    with zipfile.ZipFile(tmp_path / "-output.zip") as archive:
        assert archive.read("-input") == b"dash"
    assert "-input" in arc("list", "--", "-output.zip").stdout
    arc("verify", "--", "-output.zip")
    arc("unpack", "--", "-output.zip", "output")
    assert (tmp_path / "output/-input").read_bytes() == b"dash"


@pytest.mark.parametrize("command", ["list", "info", "verify", "unpack"])
def test_missing_archive(arc, command):
    result = arc(command, "missing.tar", success=False)
    assert result.stderr

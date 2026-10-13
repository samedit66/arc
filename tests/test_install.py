import os
from pathlib import Path
import shutil
import subprocess

import pytest


INSTALLER = Path(__file__).resolve().parents[1] / "scripts/install.sh"
COMMIT = "a" * 40


def write_script(path, source):
    # MSYS2 executes these files directly; CRLF would corrupt the shebang.
    path.write_text("#!/bin/sh\nset -eu\n" + source, encoding="utf-8", newline="\n")
    path.chmod(0o755)


@pytest.fixture
def installer(tmp_path):
    tools = tmp_path / "tools"
    tools.mkdir()
    source = tmp_path / "source fixture"
    (source / "scripts").mkdir(parents=True)
    temporary = tmp_path / "temporary"
    temporary.mkdir()
    install_dir = tmp_path / "installed with spaces"
    write_script(tools / "brew", 'printf "/unused/libarchive\\n"\n')
    host_os = subprocess.check_output(["uname", "-s"], text=True).strip()
    write_script(tools / "uname", 'printf "%s\\n" "$TEST_OS"\n')
    for command, stage in [("cp", "copy"), ("mv", "publish")]:
        original = Path(shutil.which(command)).as_posix()
        write_script(tools / command, f'''
if [ "${{FAIL_STAGE:-}}" = {stage} ] && [ "$1" != -R ]; then exit 9; fi
exec "{original}" "$@"
''')
    write_script(tools / "git", '''
if [ "$1" = clone ]; then
    [ "${FAIL_STAGE:-}" != clone ] || exit 7
    [ "$2" = --depth ] && [ "$3" = 1 ] && [ "$4" = --branch ] && [ "$5" = main ]
    [ "$6" = --single-branch ] && [ "$7" = https://github.com/samedit66/arc.git ]
    cp -R "$TEST_SOURCE" "$8"
else
    [ "$1" = -C ] && [ "$3" = rev-parse ] && [ "$4" = HEAD ]
    printf '%s\\n' "$TEST_COMMIT"
fi
''')
    write_script(tools / "mise", '''
if [ "$1" = trust ]; then exit 0; fi
[ "$1" = exec ] && [ "$2" = -- ]
[ "$MISE_DISABLE_TOOLS" = python,uv ]
[ "${ARC_BUILD_CONFIG:-}" = '' ]
export ARC_BUILD_CONFIG=1 CC=cc
[ "${FAIL_STAGE:-}" != compiler ] || CC=arc-no-such-compiler
shift 2
exec "$@"
''')
    write_script(tools / "pkg-config", '[ "${FAIL_STAGE:-}" != dependency ]\n')
    write_script(source / "scripts/build.sh", '''
[ "${FAIL_STAGE:-}" != build ] || exit 8
[ "$ARC_BUILD_CONFIG" = 1 ]
[ "$ARC_SANITIZE" = 0 ] && [ "$FILEKIT_SANITIZE" = 0 ]
mkdir -p "$ARC_BUILD_DIR"
[ "${FAIL_STAGE:-}" != missing ] || exit 0
version="$ARC_VERSION"
[ "${FAIL_STAGE:-}" != version ] || version=wrong
printf '#!/bin/sh\\nprintf "arc %s\\\\n"\\n' "$version" > "$ARC_BUILD_DIR/arc$TEST_EXE"
chmod 755 "$ARC_BUILD_DIR/arc$TEST_EXE"
''')
    environment = dict(os.environ)
    environment.update(
        PATH=str(tools) + os.pathsep + os.environ["PATH"],
        HOME=str(tmp_path / "home"), TMPDIR=str(temporary),
        TEST_SOURCE=source.as_posix(), TEST_COMMIT=COMMIT,
        TEST_EXE=".exe" if os.name == "nt" else "",
        TEST_OS=host_os,
        ARC_BUILD_CONFIG="1", ARC_SANITIZE="1", FILEKIT_SANITIZE="1",
    )
    shell = os.environ.get("MISE_BASH_PATH") if os.name == "nt" else shutil.which("sh")
    shell = shell or shutil.which("bash")
    assert shell, "Installer tests require the project's Bash/sh environment"

    def invoke(*arguments, stage=None, commit=COMMIT, piped=False, platform=None, script=None):
        env = dict(environment, FAIL_STAGE=stage or "", TEST_COMMIT=commit,
                   TEST_OS=platform or host_os)
        command = [shell, "-s", "--"] if piped else [shell, str(INSTALLER)]
        result = subprocess.run(
            [*command, *arguments], input=(script or INSTALLER.read_text()) if piped else None,
            env=env, cwd=tmp_path, capture_output=True, text=True, timeout=30,
        )
        assert not list(temporary.iterdir()), result.stderr
        if install_dir.exists():
            assert not list(install_dir.glob(".arc-install.*")), result.stderr
        return result

    return invoke, install_dir, tmp_path / "home/.local/bin", environment["TEST_EXE"]


def test_install_from_pipe_and_update(installer):
    invoke, directory, _, suffix = installer
    result = invoke("--install-dir", str(directory), piped=True)
    assert result.returncode == 0, result.stderr
    binary = directory / f"arc{suffix}"
    assert f"main.{COMMIT}" in binary.read_text()
    assert "PATH" in result.stdout
    result = invoke("--install-dir", str(directory), commit="b" * 40)
    assert result.returncode == 0, result.stderr
    assert "main." + "b" * 40 in binary.read_text()
    if os.name != "nt":
        assert binary.stat().st_mode & 0o777 == 0o755


def test_default_install_directory(installer):
    invoke, _, default, suffix = installer
    result = invoke()
    assert result.returncode == 0, result.stderr
    assert (default / f"arc{suffix}").is_file()


@pytest.mark.parametrize("stage", ["clone", "compiler", "dependency", "build", "missing", "version", "copy", "publish"])
def test_failed_install_preserves_previous_binary(installer, stage):
    invoke, directory, _, suffix = installer
    directory.mkdir()
    binary = directory / f"arc{suffix}"
    binary.write_bytes(b"previous binary")
    result = invoke("--install-dir", str(directory), stage=stage)
    assert result.returncode != 0
    assert binary.read_bytes() == b"previous binary"


@pytest.mark.parametrize("arguments", [["--unknown"], ["--install-dir"], ["--install-dir", "relative"]])
def test_invalid_install_arguments(installer, arguments):
    invoke, directory, default, _ = installer
    result = invoke(*arguments)
    assert result.returncode != 0
    assert "Error:" in result.stderr
    assert not directory.exists() and not default.exists()


def test_installer_help(installer):
    invoke, directory, default, _ = installer
    result = invoke("--help")
    assert result.returncode == 0
    assert "Usage:" in result.stdout
    assert not directory.exists() and not default.exists()


def test_refuses_directory_destination(installer):
    invoke, directory, _, suffix = installer
    (directory / f"arc{suffix}").mkdir(parents=True)
    result = invoke("--install-dir", str(directory))
    assert result.returncode != 0
    assert "non-regular file" in result.stderr


def test_refuses_symlink_destination(installer, symlinks_available):
    invoke, directory, _, suffix = installer
    directory.mkdir()
    target = directory / "keep"
    target.write_bytes(b"keep")
    (directory / f"arc{suffix}").symlink_to(target)
    result = invoke("--install-dir", str(directory))
    assert result.returncode != 0
    assert "symbolic link" in result.stderr
    assert target.read_bytes() == b"keep"


def test_invalid_commit_preserves_installation(installer):
    invoke, directory, default, _ = installer
    result = invoke(commit="invalid")
    assert result.returncode != 0
    assert "invalid commit ID" in result.stderr
    assert not directory.exists() and not default.exists()


def test_unsupported_platform(installer):
    invoke, directory, default, _ = installer
    result = invoke(platform="FreeBSD")
    assert result.returncode != 0
    assert "Supported systems" in result.stderr
    assert not directory.exists() and not default.exists()


def test_truncated_pipe_does_not_install(installer):
    invoke, directory, default, _ = installer
    script = INSTALLER.read_text().split('\nfail() {', 1)[0]
    result = invoke(piped=True, script=script)
    assert not directory.exists() and not default.exists()
    assert "Downloading" not in result.stdout


@pytest.mark.skipif(os.name == "nt", reason="Windows write access is controlled by ACLs")
def test_unwritable_directory_preserves_previous_binary(installer):
    if os.geteuid() == 0:
        pytest.skip("Root can write through POSIX mode restrictions")
    invoke, directory, _, suffix = installer
    directory.mkdir()
    binary = directory / f"arc{suffix}"
    binary.write_bytes(b"previous binary")
    directory.chmod(0o500)
    try:
        result = invoke("--install-dir", str(directory))
        assert result.returncode != 0
        assert binary.read_bytes() == b"previous binary"
    finally:
        directory.chmod(0o700)

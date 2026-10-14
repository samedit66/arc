import os
import hashlib
import io
from pathlib import Path
import shutil
import subprocess
import tarfile
import zipfile

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
    (source / "VERSION").write_text("0.1.0\n", newline="\n")
    (source / "LICENSE").write_text("Source license\n", newline="\n")
    temporary = tmp_path / "temporary"
    temporary.mkdir()
    install_dir = tmp_path / "installed with spaces and 'quotes'"
    write_script(tools / "brew", 'printf "/unused/libarchive\\n"\n')
    host_os = subprocess.check_output(["uname", "-s"], text=True).strip()
    host_arch = subprocess.check_output(["uname", "-m"], text=True).strip()
    write_script(tools / "uname", 'if [ "$1" = -m ]; then printf "%s\\n" "$TEST_ARCH"; else printf "%s\\n" "$TEST_OS"; fi\n')
    for command, stage in [("cp", "copy"), ("mv", "publish")]:
        original = Path(shutil.which(command)).as_posix()
        write_script(tools / command, f'''
if [ "${{FAIL_STAGE:-}}" = {stage} ] && [ "$1" != -R ]; then exit 9; fi
exec "{original}" "$@"
''')
    write_script(tools / "git", '''
if [ "$1" = clone ]; then
    [ "${FAIL_STAGE:-}" != clone ] || exit 7
    [ "$2" = --depth ] && [ "$3" = 1 ] && [ "$4" = --branch ] && [ "$5" = "$TEST_REF" ]
    [ "$6" = --single-branch ] && [ "$7" = https://github.com/samedit66/arc.git ]
    cp -R "$TEST_SOURCE" "$8"
else
    [ "$1" = -C ] && [ "$3" = rev-parse ] && [ "$4" = HEAD ]
    printf '%s\\n' "$TEST_COMMIT"
fi
''')
    write_script(tools / "mise", '''
if [ "$1" = trust ] || [ "$1" = install ]; then exit 0; fi
[ "$1" = exec ] && [ "$2" = -- ]
[ "$MISE_DISABLE_TOOLS" = python,uv,pipx:clang-format ]
[ "${ARC_BUILD_CONFIG:-}" = '' ]
export ARC_BUILD_CONFIG=1 CC=cc
[ "${FAIL_STAGE:-}" != compiler ] || CC=arc-no-such-compiler
shift 2
exec "$@"
''')
    write_script(tools / "curl", '''
case "$*" in
    *releases/latest*)
        [ "${FAIL_STAGE:-}" != release ] || exit 22
        printf "https://github.com/samedit66/arc/releases/tag/v0.1.0" ;;
    *)
        while [ "$#" -gt 0 ]; do
            case "$1" in
                https://*) url=$1; shift ;;
                -o) output=$2; shift 2 ;;
                --proto|--tlsv1.2) shift; [ "${1:-}" != '=https' ] || shift ;;
                *) shift ;;
            esac
        done
        case "$url" in
            */SHA256SUMS)
                [ "${FAIL_STAGE:-}" != checksums ] || exit 22
                cp "$TEST_MANIFEST" "$output" ;;
            */"$TEST_ASSET_NAME")
                [ "${FAIL_STAGE:-}" != download ] || exit 22
                cp "$TEST_ASSET" "$output" ;;
            *) exit 23 ;;
        esac ;;
esac
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
        TEST_ARCH=host_arch,
        ARC_BUILD_CONFIG="1", ARC_SANITIZE="1", FILEKIT_SANITIZE="1",
    )
    shell = os.environ.get("MISE_BASH_PATH") if os.name == "nt" else shutil.which("sh")
    shell = shell or shutil.which("bash")
    assert shell, "Installer tests require the project's Bash/sh environment"

    def invoke(*arguments, stage=None, commit=COMMIT, piped=False, platform=None, script=None, ref="main", asset_stage=None, architecture=None):
        env = dict(environment, FAIL_STAGE=stage or "", TEST_COMMIT=commit,
                   TEST_OS=platform or host_os, TEST_REF="v0.1.0" if ref in (None, "latest") else ref)
        if architecture:
            env["TEST_ARCH"] = architecture
        if asset_stage is not None:
            host = "windows" if os.name == "nt" else ("darwin" if host_os == "Darwin" else "linux")
            arch = "arm64" if environment["TEST_ARCH"] in ("arm64", "aarch64") else "x86_64"
            extension = "zip" if os.name == "nt" else "tar.gz"
            asset_name = f"arc-v0.1.0-{host}-{arch}.{extension}"
            asset = tmp_path / asset_name
            binary_version = "wrong" if asset_stage == "version" else "0.1.0"
            entries = {
                f'arc{environment["TEST_EXE"]}': f'#!/bin/sh\nprintf "arc {binary_version}\\n"\n'.encode(),
                "VERSION": b"9.0.0\n" if asset_stage == "metadata" else b"0.1.0\n",
                "THIRD_PARTY_NOTICES.txt": b"Bundled licenses\n",
            }
            if asset_stage == "missing":
                del entries[f'arc{environment["TEST_EXE"]}']
            if extension == "zip":
                with zipfile.ZipFile(asset, "w") as archive:
                    for name, data in entries.items():
                        archive.writestr(name, data)
            else:
                with tarfile.open(asset, "w:gz") as archive:
                    for name, data in entries.items():
                        entry = tarfile.TarInfo(name)
                        entry.size = len(data)
                        entry.mode = 0o755
                        archive.addfile(entry, io.BytesIO(data))
            digest = hashlib.sha256(asset.read_bytes()).hexdigest()
            if asset_stage == "hash":
                digest = "0" * 64
            manifest = tmp_path / "SHA256SUMS"
            line = f"{digest}  {asset_name}\n"
            if asset_stage == "duplicate":
                line += line
            if asset_stage == "absent":
                line = ""
            manifest.write_text(line, newline="\n")
            env.update(TEST_ASSET=asset.as_posix(), TEST_ASSET_NAME=asset_name,
                       TEST_MANIFEST=manifest.as_posix())
        command = [shell, "-s", "--"] if piped else [shell, str(INSTALLER)]
        result = subprocess.run(
            [*command, *(["--version", ref] if ref else []), *arguments], input=(script or INSTALLER.read_text()) if piped else None,
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
    assert "To uninstall, run: rm -- '" in result.stdout
    if os.name != "nt":
        quoted_path = str(binary.resolve()).replace("'", "'\\''")
        assert f"To uninstall, run: rm -- '{quoted_path}'" in result.stdout
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


@pytest.mark.parametrize("ref", [None, "latest", "v0.1.0"])
def test_stable_release_selection(installer, ref):
    invoke, directory, _, suffix = installer
    result = invoke("--install-dir", str(directory), "--from-source", ref=ref)
    assert result.returncode == 0, result.stderr
    assert "Installed arc 0.1.0 at" in result.stdout
    assert "main." not in (directory / f"arc{suffix}").read_text()


def test_no_stable_release_preserves_binary(installer):
    invoke, directory, _, suffix = installer
    directory.mkdir()
    binary = directory / f"arc{suffix}"
    binary.write_bytes(b"previous")
    result = invoke("--install-dir", str(directory), ref=None, stage="release")
    assert result.returncode != 0
    assert "No stable release" in result.stderr
    assert binary.read_bytes() == b"previous"


def test_release_tag_must_match_version(installer):
    invoke, directory, _, _ = installer
    result = invoke("--install-dir", str(directory), "--from-source", ref="v9.0.0")
    assert result.returncode != 0
    assert "disagrees with VERSION" in result.stderr
    assert not directory.exists()


@pytest.mark.parametrize("ref", [None, "latest", "v0.1.0"])
def test_install_verified_release_binary(installer, ref):
    invoke, directory, _, suffix = installer
    result = invoke("--install-dir", str(directory), ref=ref, asset_stage="ok", piped=True)
    assert result.returncode == 0, result.stderr
    assert "Building" not in result.stdout
    assert "Installed arc 0.1.0" in result.stdout
    assert (directory / f"arc{suffix}").is_file()
    assert (directory / "arc-THIRD_PARTY_NOTICES.txt").read_text() == "Bundled licenses\n"


@pytest.mark.parametrize("stage", ["hash", "metadata", "version", "missing", "duplicate", "absent", "checksums", "download", "copy", "publish"])
def test_failed_binary_install_preserves_previous_binary(installer, stage):
    invoke, directory, _, suffix = installer
    directory.mkdir()
    binary = directory / f"arc{suffix}"
    binary.write_bytes(b"previous")
    result = invoke("--install-dir", str(directory), ref="v0.1.0", asset_stage=stage, stage=stage)
    assert result.returncode != 0
    assert binary.read_bytes() == b"previous"


def test_binary_installer_rejects_unknown_architecture(installer):
    invoke, directory, _, _ = installer
    result = invoke("--install-dir", str(directory), ref="v0.1.0", asset_stage="ok", architecture="riscv64")
    assert result.returncode != 0
    assert "--from-source" in result.stderr
    assert not directory.exists()

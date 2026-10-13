import os
from pathlib import Path
import shutil
import subprocess

import pytest


pytestmark = pytest.mark.skipif(os.name != "nt", reason="PowerShell/MSYS2 Windows installer")
INSTALLER = Path(__file__).resolve().parents[1] / "scripts/install.ps1"


def run_installer(tmp_path, *, exit_code=0, invalid_root=False, version="main", release_failure=False):
    powershell = shutil.which("powershell.exe") or shutil.which("pwsh")
    assert powershell, "Windows installer tests require PowerShell"
    bash = Path(os.environ["MISE_BASH_PATH"])
    root = bash.parents[2]
    destination = tmp_path / "bin with spaces and 'quotes'"
    log = tmp_path / "arguments.txt"
    download = tmp_path / "download.txt"
    harness = tmp_path / "run.ps1"
    # Replace only the network boundary; exercise real PowerShell and MSYS2 Bash.
    harness.write_text('''
param([string] $Installer, [string] $Root, [string] $Destination)
$ErrorActionPreference = 'Stop'
function Invoke-RestMethod {
    param($Uri)
    if ($Uri -ne 'https://api.github.com/repos/samedit66/arc/releases/latest') { throw 'Unexpected API URL' }
    if ($env:ARC_TEST_RELEASE_FAILURE -eq '1') { throw 'No release' }
    return @{ tag_name = 'v0.1.0' }
}
function Invoke-WebRequest {
    param([switch] $UseBasicParsing, $Uri, $OutFile)
    if ($Uri -ne "https://raw.githubusercontent.com/samedit66/arc/$env:ARC_TEST_REF/scripts/install.sh") {
        throw 'Unexpected download URL'
    }
    $source = @'
#!/bin/sh
set -eu
printf '%s\\n' "$MSYSTEM" "$1" "$2" "$3" "$4" > "$(cygpath -u "$ARC_TEST_LOG")"
exit "$ARC_TEST_EXIT"
'@
    [IO.File]::WriteAllText($OutFile, $source + "`n", [Text.Encoding]::ASCII)
    [IO.File]::WriteAllText($env:ARC_TEST_DOWNLOAD, $OutFile)
}
$env:MSYSTEM = 'sentinel'
$env:ARC_WINDOWS_INSTALL_DIR = 'keep'
$env:ARC_WINDOWS_INSTALL_VERSION = 'keep-version'
try {
    & $Installer -Msys2Root $Root -InstallDir $Destination -Version $env:ARC_TEST_VERSION
} catch {
    Write-Output $_.Exception.Message
    $failed = $true
}
if ($env:MSYSTEM -ne 'sentinel' -or $env:ARC_WINDOWS_INSTALL_DIR -ne 'keep' -or $env:ARC_WINDOWS_INSTALL_VERSION -ne 'keep-version') {
    throw 'Installer leaked environment changes'
}
if ($failed) { exit 1 }
''', encoding="utf-8")
    env = dict(os.environ, ARC_TEST_LOG=str(log), ARC_TEST_DOWNLOAD=str(download),
               ARC_TEST_EXIT=str(exit_code), ARC_TEST_VERSION=version,
               ARC_TEST_REF="v0.1.0" if version == "latest" else version,
               ARC_TEST_RELEASE_FAILURE="1" if release_failure else "0")
    result = subprocess.run(
        [powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(harness),
         "-Installer", str(INSTALLER), "-Root", str(tmp_path / "missing" if invalid_root else root),
         "-Destination", str(destination)],
        env=env, capture_output=True, text=True, timeout=30,
    )
    if download.exists():
        script = Path(download.read_text())
        assert not script.exists() and not script.parent.exists(), result.stdout + result.stderr
    return result, log, destination


def test_windows_installer_passes_paths_and_selects_ucrt64(tmp_path):
    result, log, destination = run_installer(tmp_path)
    assert result.returncode == 0, result.stdout + result.stderr
    lines = log.read_text().splitlines()
    assert lines[:2] == ["UCRT64", "--install-dir"]
    assert lines[3:] == ["--version", "main"]
    native_path = subprocess.check_output(
        [str(Path(os.environ["MISE_BASH_PATH"]).parent / "cygpath.exe"), "-w", lines[2]], text=True,
    ).strip()
    assert Path(native_path) == destination
    assert "ucrt64" in result.stdout
    quoted_binary = str(destination / "arc.exe").replace("'", "''")
    assert f"To uninstall from PowerShell, run: Remove-Item -LiteralPath '{quoted_binary}'" in result.stdout


def test_windows_installer_propagates_shell_failure(tmp_path):
    result, log, _ = run_installer(tmp_path, exit_code=9)
    assert result.returncode != 0
    assert "exit 9" in result.stdout
    assert log.exists()


def test_windows_installer_reports_missing_msys2(tmp_path):
    result, log, _ = run_installer(tmp_path, invalid_root=True)
    assert result.returncode != 0
    assert "MSYS2 UCRT64 not found" in result.stdout
    assert not log.exists()


@pytest.mark.parametrize("version", ["latest", "v0.1.0"])
def test_windows_installer_selects_release(tmp_path, version):
    result, log, _ = run_installer(tmp_path, version=version)
    assert result.returncode == 0, result.stdout + result.stderr
    assert log.read_text().splitlines()[3:] == ["--version", "v0.1.0"]


def test_windows_installer_no_release(tmp_path):
    result, log, _ = run_installer(tmp_path, version="latest", release_failure=True)
    assert result.returncode != 0
    assert "Cannot resolve a stable release" in result.stdout
    assert not log.exists()

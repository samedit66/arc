# Requires Windows PowerShell 5.1+ and an existing MSYS2 UCRT64 toolchain.
[CmdletBinding()]
param(
    [string] $InstallDir = (Join-Path $env:USERPROFILE '.local\bin'),
    [string] $Msys2Root = 'C:\msys64'
)

$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This installer requires Windows and MSYS2 UCRT64.'
}
if (-not [IO.Path]::IsPathRooted($InstallDir) -or
    -not [IO.Path]::IsPathRooted($Msys2Root)) {
    throw 'InstallDir and Msys2Root must be absolute paths.'
}
$bash = Join-Path $Msys2Root 'usr\bin\bash.exe'
$runtime = Join-Path $Msys2Root 'ucrt64\bin'
if (-not (Test-Path -LiteralPath $bash -PathType Leaf) -or
    -not (Test-Path -LiteralPath $runtime -PathType Container)) {
    throw "MSYS2 UCRT64 not found at $Msys2Root. Install it first, or pass -Msys2Root."
}

$temporary = Join-Path ([IO.Path]::GetTempPath()) ('arc-install-' + [Guid]::NewGuid())
$variables = @('MSYSTEM', 'MSYS2_PATH_TYPE', 'CHERE_INVOKING',
    'ARC_WINDOWS_INSTALL_SCRIPT', 'ARC_WINDOWS_INSTALL_DIR')
$previous = @{}
foreach ($name in $variables) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    New-Item -ItemType Directory -Path $temporary | Out-Null
    $script = Join-Path $temporary 'install.sh'
    Invoke-WebRequest -UseBasicParsing -Uri 'https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.sh' -OutFile $script

    # The login shell selects UCRT64's compiler and DLLs while retaining mise
    # from the Windows PATH. Paths travel in environment variables, not shell code.
    $env:MSYSTEM = 'UCRT64'
    $env:MSYS2_PATH_TYPE = 'inherit'
    $env:CHERE_INVOKING = '1'
    $env:ARC_WINDOWS_INSTALL_SCRIPT = $script
    $env:ARC_WINDOWS_INSTALL_DIR = $InstallDir
    $command = @'
set -eu
script=$(cygpath -u "$ARC_WINDOWS_INSTALL_SCRIPT")
destination=$(cygpath -u "$ARC_WINDOWS_INSTALL_DIR")
exec sh "$script" --install-dir "$destination"
'@
    # stdin avoids Windows PowerShell's legacy native-argument quote handling.
    $command | & $bash --login -s
    if ($LASTEXITCODE -ne 0) {
        throw "arc installation failed (exit $LASTEXITCODE)."
    }
    # PowerShell escapes apostrophes in single-quoted strings by doubling them.
    $binary = (Join-Path $InstallDir 'arc.exe').Replace("'", "''")
    Write-Host "To uninstall from PowerShell, run: Remove-Item -LiteralPath '$binary'"
    # arc.exe is dynamically linked: its UCRT64 runtime DLLs remain in MSYS2.
    Write-Host "To run arc from PowerShell, add $InstallDir and $runtime to your PATH."
}
finally {
    foreach ($name in $variables) {
        [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
    }
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Recurse -Force
    }
}

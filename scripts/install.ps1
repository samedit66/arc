# Windows PowerShell 5.1+; MSYS2 is required only for source builds.
[CmdletBinding()]
param(
    [string] $InstallDir = (Join-Path $env:USERPROFILE '.local\bin'),
    [string] $Msys2Root = 'C:\msys64',
    [string] $Version = 'latest',
    [switch] $FromSource
)
$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'This installer requires Windows.' }
if ($InstallDir -notmatch '^(?:[a-zA-Z]:[\\/]|\\\\[^\\]+\\[^\\]+(?:\\|$))') { throw 'InstallDir must be an absolute path.' }
if ($Version -eq 'latest') {
    $lookupTls = [Net.ServicePointManager]::SecurityProtocol
    try {
        [Net.ServicePointManager]::SecurityProtocol = $lookupTls -bor [Net.SecurityProtocolType]::Tls12
        $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/samedit66/arc/releases/latest'
        $Version = $release.tag_name
    } catch { throw 'Cannot resolve a stable release; use -Version main explicitly.' }
    finally { [Net.ServicePointManager]::SecurityProtocol = $lookupTls }
}
if ($Version -ne 'main' -and $Version -cnotmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    throw 'Expected main or a stable release tag vMAJOR.MINOR.PATCH.'
}
function Assert-RegularDestination([string] $Path) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if ($null -ne $item -and ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint))) {
        throw "Refusing to replace a directory or reparse point: $Path"
    }
}
function Publish-File([string] $Source, [string] $Destination) {
    Assert-RegularDestination $Destination
    if ([IO.File]::Exists($Destination)) { [IO.File]::Replace($Source, $Destination, [NullString]::Value) }
    else { [IO.File]::Move($Source, $Destination) }
}
$destination = Join-Path $InstallDir 'arc.exe'
Assert-RegularDestination $destination
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('arc-install-' + [Guid]::NewGuid())
$variables = @('MSYSTEM', 'MSYS2_PATH_TYPE', 'CHERE_INVOKING', 'ARC_WINDOWS_INSTALL_SCRIPT', 'ARC_WINDOWS_INSTALL_DIR', 'ARC_WINDOWS_INSTALL_VERSION')
$previous = @{}
foreach ($name in $variables) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$staged = $null
$stagedNotices = $null
$previousTls = [Net.ServicePointManager]::SecurityProtocol
try {
    [Net.ServicePointManager]::SecurityProtocol = $previousTls -bor [Net.SecurityProtocolType]::Tls12
    New-Item -ItemType Directory -Path $temporary | Out-Null
    if ($FromSource -or $Version -eq 'main') {
        if ($Msys2Root -notmatch '^[a-zA-Z]:[\\/]') { throw 'Msys2Root must be an absolute path.' }
        $bash = Join-Path $Msys2Root 'usr\bin\bash.exe'
        $runtime = Join-Path $Msys2Root 'ucrt64\bin'
        if (-not (Test-Path -LiteralPath $bash -PathType Leaf) -or -not (Test-Path -LiteralPath $runtime -PathType Container)) {
            throw "MSYS2 UCRT64 not found at $Msys2Root. Install it first, or pass -Msys2Root."
        }
        $script = Join-Path $temporary 'install.sh'
        # The current installer can build older tags that have no binary assets.
        Invoke-WebRequest -UseBasicParsing -Uri 'https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.sh' -OutFile $script
        $env:MSYSTEM = 'UCRT64'
        $env:MSYS2_PATH_TYPE = 'inherit'
        $env:CHERE_INVOKING = '1'
        $env:ARC_WINDOWS_INSTALL_SCRIPT = $script
        $env:ARC_WINDOWS_INSTALL_DIR = $InstallDir
        $env:ARC_WINDOWS_INSTALL_VERSION = $Version
        $command = @'
set -eu
script=$(cygpath -u "$ARC_WINDOWS_INSTALL_SCRIPT")
destination=$(cygpath -u "$ARC_WINDOWS_INSTALL_DIR")
exec sh "$script" --install-dir "$destination" --version "$ARC_WINDOWS_INSTALL_VERSION" --from-source
'@
        $command | & $bash --login -s
        if ($LASTEXITCODE -ne 0) { throw "arc installation failed (exit $LASTEXITCODE)." }
        Write-Host "Source build: add $InstallDir and $runtime to your PATH."
    } else {
        $architecture = $env:PROCESSOR_ARCHITEW6432
        if (-not $architecture) { $architecture = $env:PROCESSOR_ARCHITECTURE }
        if ($architecture -ne 'AMD64') { throw 'Windows release binaries require x86_64; use -FromSource.' }
        $asset = "arc-$Version-windows-x86_64.zip"
        $base = "https://github.com/samedit66/arc/releases/download/$Version"
        $checksums = Join-Path $temporary 'SHA256SUMS'
        $archive = Join-Path $temporary $asset
        try { Invoke-WebRequest -UseBasicParsing -Uri "$base/SHA256SUMS" -OutFile $checksums }
        catch { throw 'Release checksums unavailable; older releases need -FromSource.' }
        $pattern = '^([0-9a-f]{64})  ' + [Regex]::Escape($asset) + '$'
        $matching = @([IO.File]::ReadAllLines($checksums) | Where-Object { $_ -cmatch $pattern })
        if ($matching.Count -ne 1) { throw 'Missing, invalid or duplicate asset checksum.' }
        $expected = [Regex]::Match($matching[0], $pattern).Groups[1].Value
        Invoke-WebRequest -UseBasicParsing -Uri "$base/$asset" -OutFile $archive
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'Release binary checksum mismatch.' }
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [IO.Compression.ZipFile]::OpenRead($archive)
        try {
            foreach ($name in @('arc.exe', 'VERSION', 'THIRD_PARTY_NOTICES.txt')) {
                $entries = @($zip.Entries | Where-Object { $_.FullName -ceq $name })
                if ($entries.Count -ne 1 -or (($entries[0].ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000) { throw "Invalid release package entry: $name" }
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0], (Join-Path $temporary $name))
            }
        } finally { $zip.Dispose() }
        $binary = Join-Path $temporary 'arc.exe'
        $releaseVersion = $Version.Substring(1)
        if ([IO.File]::ReadAllText((Join-Path $temporary 'VERSION')).TrimEnd([char[]]"`r`n") -cne $releaseVersion) { throw 'Release asset disagrees with its tag.' }
        $reported = & $binary --version
        if ($LASTEXITCODE -ne 0 -or $reported -cne "arc $releaseVersion") { throw 'arc failed its version check.' }
        $notices = Join-Path $temporary 'THIRD_PARTY_NOTICES.txt'
        if ((Get-Item -LiteralPath $notices).Length -eq 0) { throw 'Empty bundled license file.' }
        [IO.Directory]::CreateDirectory($InstallDir) | Out-Null
        $staged = Join-Path $InstallDir ('.arc-install-' + [Guid]::NewGuid())
        $stagedNotices = Join-Path $InstallDir ('.arc-install-' + [Guid]::NewGuid())
        [IO.File]::Copy($binary, $staged)
        [IO.File]::Copy($notices, $stagedNotices)
        Publish-File $stagedNotices (Join-Path $InstallDir 'arc-THIRD_PARTY_NOTICES.txt')
        $stagedNotices = $null
        Publish-File $staged $destination
        $staged = $null
        Write-Host "Installed arc $releaseVersion at $destination"
        Write-Host "Add $InstallDir to your PATH."
    }
    $quotedBinary = $destination.Replace("'", "''")
    Write-Host "To uninstall from PowerShell, run: Remove-Item -LiteralPath '$quotedBinary'"
} finally {
    [Net.ServicePointManager]::SecurityProtocol = $previousTls
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
    foreach ($path in @($staged, $stagedNotices)) { if ($path -and [IO.File]::Exists($path)) { [IO.File]::Delete($path) } }
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}

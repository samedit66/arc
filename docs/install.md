# Installing and removing arc

The installers select the latest stable GitHub release and build arc locally.
They fail if no stable release exists; there is no automatic fallback to `main`. They run `mise trust` on the downloaded
repository and replace an existing ordinary `arc` binary in the destination.
They refuse to replace a symbolic link or a non-regular file.

## Prerequisites

Install Git, curl, mise, Bash, a C11 compiler, `ar`, pkg-config and libarchive
development files before running the installer. The installer does not install
system dependencies. Python and uv are not needed for CLI installation.

### macOS

With Homebrew available, install Apple command-line tools if needed, then:

```sh
xcode-select --install
brew install git libarchive pkgconf mise
```

The build selects Homebrew's libarchive automatically.

### Linux: Debian / Ubuntu

```sh
sudo apt update
sudo apt install git curl bash build-essential pkg-config libarchive-dev
```

Install mise separately and make it available in `PATH`. Other distributions
need the equivalent compiler, pkg-config and libarchive development packages.

### Windows: MSYS2 UCRT64

Install MSYS2 and open its UCRT64 terminal. Update packages first; reopen the
terminal if requested, then repeat the update before installing dependencies:

```sh
pacman -Syu
pacman -S --needed git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libarchive mingw-w64-ucrt-x86_64-pkgconf
```

Install mise separately and make it available in `PATH`. Use the UCRT64
toolchain and libraries together. The PowerShell installer requires Windows
PowerShell 5.1 or newer and defaults to MSYS2 at `C:\msys64`.

## Installation

### macOS / Linux

```sh
curl -fsSL https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.sh | sh
```

The default destination is `~/.local/bin`. To choose another absolute directory:

```sh
curl -fsSL https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.sh | sh -s -- --install-dir /absolute/path
```

Add the destination directory to your shell's `PATH` if needed. For the default:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Add that line to your shell's startup file to retain it in future sessions.
The same shell installer works inside MSYS2 UCRT64.

### Windows PowerShell

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.ps1)))
```

The default destination is `%USERPROFILE%\.local\bin`. For custom locations:

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.ps1))) -InstallDir 'C:\Tools\arc' -Msys2Root 'D:\msys64'
```

Both paths must be absolute. Add the installation directory and MSYS2's
`ucrt64\bin` to your user `PATH`; the latter supplies libarchive's runtime DLLs.
For the default paths in the current PowerShell session:

```powershell
$env:PATH = "$env:USERPROFILE\.local\bin;C:\msys64\ucrt64\bin;$env:PATH"
```

Use Windows environment-variable settings to retain these entries.

## Select a version

To install a particular release or explicitly choose development builds:

```sh
curl -fsSL https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.sh | sh -s -- --version v0.1.0
curl -fsSL https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.sh | sh -s -- --version main
```

In PowerShell, append `-Version v0.1.0` or `-Version main` to the installation
command. The wrapper downloads the shell installer from that selected ref.
Release tags must be `vMAJOR.MINOR.PATCH` and agree with the source `VERSION`.

## Verify or update

```sh
arc --version
arc --help
```

Release installations report `arc <version>`; explicit `main` installations
report `arc <version>+main.<commit>`. Run the installer again with the same
destination to update to the latest stable release, or repeat the version
selection to update development builds. A failed build or staging step
preserves the existing binary; temporary download/build files are cleaned up.

## Removal

For the default macOS / Linux installation:

```sh
rm -- "$HOME/.local/bin/arc"
```

For the default Windows installation, in PowerShell:

```powershell
Remove-Item -LiteralPath "$env:USERPROFILE\.local\bin\arc.exe"
```

For a custom destination, remove its `arc` or `arc.exe` file. The installer
prints a removal command for the actual destination. Remove PATH entries only
if you added them exclusively for arc. Dependencies such as mise and libarchive
remain installed.

For source builds and library integration, see [Building arc](build.md).

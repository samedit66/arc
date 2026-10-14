# Installing and removing arc

The installers select the latest stable GitHub Release and download the binary
for your operating system and architecture. They verify its SHA256 against
`SHA256SUMS`, its package version and `arc --version` before replacing an
existing binary. There is no automatic fallback to `main` or source builds.
Symlinks, reparse points and non-regular destination files are rejected.

## Prerequisites

Binary installation requires no compiler, mise, Git, libarchive or MSYS2.

- macOS 13+: Intel x86_64 or Apple Silicon arm64; `sh`, curl, tar and shasum.
- Linux: x86_64 or arm64; `sh`, curl, tar and sha256sum (or shasum).
- Windows x86_64: Windows PowerShell 5.1+ or PowerShell 7. The binary uses
  Windows system DLLs; third-party library DLLs and MSYS2 are not needed.

Linux binaries use musl and are fully static. macOS binaries link third-party
libraries statically while using macOS system libraries. SHA256 detects a
corrupt download; the checksum manifest is obtained from the same GitHub
Release over HTTPS, rather than from an independent signing authority.

## Installation

### macOS / Linux

```sh
curl -fsSL https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.sh | sh
```

The default destination is `~/.local/bin`. To choose another absolute directory:

```sh
curl -fsSL https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.sh | sh -s -- --install-dir /absolute/path
```

Add the destination to your shell's `PATH`, for example:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Add that line to your shell startup file to retain it in future sessions.
The shell installer also works inside MSYS2 UCRT64; ZIP extraction there
requires unzip. Windows users can instead use PowerShell without MSYS2.

### Windows PowerShell

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.ps1)))
```

The default destination is `%USERPROFILE%\.local\bin`. For a custom location:

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.ps1))) -InstallDir 'C:\Tools\arc'
```

The installation directory must be absolute. Add only that directory to your
user `PATH`; no third-party runtime directory is required for release binaries.
For the default location in the current PowerShell session:

```powershell
$env:PATH = "$env:USERPROFILE\.local\bin;$env:PATH"
```

## Select a version or build from source

```sh
curl -fsSL https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.sh | sh -s -- --version v0.1.1
curl -fsSL https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.sh | sh -s -- --version v0.1.0 --from-source
curl -fsSL https://raw.githubusercontent.com/samedit66/arc/main/scripts/install.sh | sh -s -- --version main
```

In PowerShell, use `-Version v0.1.1`, `-Version v0.1.0 -FromSource` or
`-Version main`. `main` always builds from source. Version tags must be
`vMAJOR.MINOR.PATCH` without leading zeroes and match `VERSION` in the package
or downloaded source. The installer script is fetched from `main`, while the
selected tag controls the binary or source being installed.

Binary assets are available starting with v0.1.1. Older releases, including
v0.1.0, require the explicit source option. An unavailable binary or checksum
causes installation to fail, preserving the previous executable.

Source builds require Git, Bash, mise, a C11 compiler, ar, pkg-config and
libarchive development files. They run `mise trust` on the downloaded source.
CMake and Ninja are installed at their pinned versions through mise.
Python, uv and clang-format are not required by source installation. For macOS prerequisites, use Homebrew:

```sh
xcode-select --install
brew install git libarchive pkgconf mise
```

On Debian / Ubuntu:

```sh
sudo apt update
sudo apt install git curl bash build-essential pkg-config libarchive-dev
```

Install mise separately. On Windows, install MSYS2, update its packages, and
install the UCRT64 toolchain:

```sh
pacman -Syu
pacman -S --needed git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libarchive mingw-w64-ucrt-x86_64-pkgconf
```

Install mise separately. PowerShell source installation defaults to MSYS2
at `C:\msys64`; use `-Msys2Root 'D:\msys64'` to choose another root.
Source-built Windows executables need MSYS2's `ucrt64\bin` on `PATH`.

## Verify, update or remove

```sh
arc --version
arc --help
```

Release installations report `arc <version>`; `main` reports
`arc <version>+main.<commit>`. Run the installer again to update. Temporary
files are cleaned up and the executable is staged in the installation
filesystem before replacement. Bundled licenses are installed alongside it
as `arc-THIRD_PARTY_NOTICES.txt`.

To remove the default macOS / Linux installation:

```sh
rm -- "$HOME/.local/bin/arc" "$HOME/.local/bin/arc-THIRD_PARTY_NOTICES.txt"
```

In PowerShell:

```powershell
Remove-Item -LiteralPath "$env:USERPROFILE\.local\bin\arc.exe", "$env:USERPROFILE\.local\bin\arc-THIRD_PARTY_NOTICES.txt"
```

For a custom destination, remove those two files there. Remove PATH entries
only if you added them exclusively for arc. Source build dependencies remain
installed. See [Building arc](build.md) for library embedding and development.

# Building and packaging

Configure a native C toolchain and either EiffelStudio or Gobo. The dependencies
travel with the library package; consumers still need a compatible compiler.

## Platforms and verification

| Native target | C compiler | Crypto backend | Verification |
| --- | --- | --- | --- |
| macOS | Apple Clang | System CommonCrypto | Native ARM64 build, packaging, relocation, EiffelStudio and Gobo release links |
| Linux | GCC or Clang | Static OpenSSL | Conan graph resolution and build/packaging contract tests; no native Linux compilation yet |
| Windows | MSVC | System CNG / bcrypt | Conan graph resolution and build/packaging contract tests; no native Windows compilation yet |

The recipe accepts x86-64 and ARM64 (`x86_64` and `armv8` in Conan). MinGW and
cross-compilation are outside this workflow. A native package must match the
consumer's C ABI, architecture, runtime, and operating-system baseline. In
particular, a Linux package is not automatically portable across different libc
implementations or older distributions. Static third-party libraries do not make
an executable's system/Eiffel runtime fully static.

## Dependency features

All third-party libraries are requested as static libraries. `conan.lock` fixes
versions and recipe revisions across the three platform graphs:

| Dependency | Purpose |
| --- | --- |
| libarchive 3.8.7 | Archive formats and entry I/O |
| zlib 1.3.2 | Deflate / gzip |
| bzip2 1.0.8 | bzip2 compression |
| lz4 1.9.4 | LZ4 compression |
| xz_utils 5.8.4 | XZ / LZMA compression |
| zstd 1.5.7 | Zstandard compression |
| libiconv 1.17 | Character-set conversion |
| Expat 2.9.0 | XML metadata, including XAR support |
| OpenSSL 3.6.5, Linux only | Cryptographic operations where no selected native backend is available |

Windows uses CNG and macOS uses CommonCrypto instead of shipping redundant
OpenSSL libraries. ACL/xattr restoration is disabled for this portable baseline.
LZO and alternative crypto/XML backends are not enabled. Compression dependencies
extend libarchive's capabilities; they do not imply that every format supports
both reading/writing or encryption.

## Prepare

Prerequisites: mise and its pipx backend prerequisites, Python/tool installation
access, the native C toolchain, and a separately configured Eiffel toolchain.
On Windows use a Visual Studio Developer terminal with `cl.exe` on PATH. The
same developer environment and compatible CRT choice must be used for Eiffel.

```sh
mise trust
mise install
mise run prepare
```

`prepare` runs `scripts/prepare.py`, which:

1. Creates a project-local Conan cache in `build/conan`.
2. Detects the native toolchain into an automatically regenerated `arc-native`
   profile unless an explicit profile is selected. On macOS it defaults to
   `/usr/bin/clang` and `/usr/bin/clang++`; explicit CC/CXX values take precedence.
3. Installs/builds static dependencies into temporary staging using Conan's
   `full_deploy` mechanism.
4. Generates `native.ecf` and `manifest.json` from Conan metadata, then replaces
   `build/native` only after successful preparation. A failed install preserves
   the previous deployment and removes its temporary staging directory.

No Homebrew/system libarchive or `LIBARCHIVE_PREFIX` is required.

To use only already cached recipes and binaries/sources:

```sh
mise run prepare -- --no-remote
```

Toolchain detection is a convenience, not a release compatibility promise. For a
stable native configuration, select a profile matching the compiler actually used
by Eiffel:

```sh
mise run prepare -- --profile profiles/macos-arm64
```

Example profiles also exist as `profiles/linux-x86_64` (GCC 14) and
`profiles/windows-x86_64` (MSVC 194, dynamic Release CRT). Adjust these examples to
your installed toolchain. The same explicit argument is available for packaging:

```sh
mise run package -- --profile profiles/macos-arm64
```

```powershell
# Windows Developer PowerShell
mise run package -- --profile profiles/windows-x86_64
```

Both commands detect the native profile when `--profile` is omitted. No
project-specific environment variable selects a profile.

The normal Windows example uses `/MD` compatibility, not `/MT`. A static CRT
profile requires a matching Eiffel/C runtime configuration; changing Conan alone
does not switch the Eiffel runtime. The lockfile pins recipe revisions, not
byte-for-byte output of local builds or every binary-package revision.

## Compile and run the CLI

Configure the native Eiffel toolchain's normal variables and PATH first.
The same shared source works with EiffelStudio and Gobo; ECF selects their base
libraries. No compiler-specific Eiffel implementation classes are needed.

`arc.ecf` selects `arc` when consumed as a library. The `cli` target extends
that configuration and adds the vendored cli library and command definitions in
`src/cli`. The `core` cluster in `src/core` contains the libarchive wrapper;
the `cli` cluster contains `ARC_CLI` and its commands. The library target includes
only `core`, so importing Arc does not import CLI classes or the cli dependency. The ZIP
includes Eiffel sources and the native primitive header, keeping both targets in the packaged ECF usable.

```sh
# EiffelStudio: configure ISE_EIFFEL, ISE_LIBRARY, ISE_PLATFORM and PATH.
export GOBO_EIFFEL=ise
ec -batch -config arc.ecf -target cli -finalize -c_compile
./EIFGENs/cli/F_code/arc --help
```

```sh
# Gobo: configure GOBO and PATH.
export GOBO_EIFFEL=ge
gec --gelint --finalize --target=cli arc.ecf
./arc --help
```

On Windows use `$env:GOBO_EIFFEL = 'ise'` or `'ge'` in PowerShell and the `.exe`
executable names. Windows native compilation is not yet verified here. For the
verified macOS installation, EiffelStudio uses `ISE_PLATFORM=macosx-armv6`.

## Package and consume without Conan

```sh
mise run package
```

The packaging script first prepares dependencies with the supplied `--profile`
and `--no-remote` arguments, then creates the ZIP. For example, using only local
Conan resources:

```sh
mise run package -- --profile profiles/macos-arm64 --no-remote
```

The filename comes from the prepared target metadata, not the machine on which
packaging happens. For example:

```text
build/package/arc-macos-arm64-apple-clang21-release.zip
build/package/arc-linux-x86_64-gcc14-release.zip
build/package/arc-windows-x86_64-msvc194-md-release.zip
```

Only the macOS ZIP has been produced from a native build here. Windows/Linux
names above illustrate their profiles. A specified minimum OS version is included
in the name; MSVC runtime and debug-runtime variants receive different names.
Full settings and the build host are recorded in the manifest. Linux consumers
must additionally check libc/distribution compatibility.

The staging directory is `build/package/<package-name>/arc` and is replaced on
repeat packaging. The ZIP contains:

```text
arc/
    arc.ecf
    src/core/             # Eiffel API, sessions and native primitives
    src/cli/              # Optional CLI
    vendor/cli/           # Vendored parser and help library
    README.md
    docs/                 # API, CLI, implementation, build and audit notes
    conan.lock
    build/native/
        native.ecf
        manifest.json
        full_deploy/host/...
```

Native package folders include headers, static archives, and dependency licenses.
The manifest records settings, selected crypto backend, dependency options,
recipe/package revisions, and package IDs. Conan caches, build tools, environment
activation scripts, and generated executables are excluded. Missing deployed
files or inconsistent target metadata stop packaging.

Unpack under a consumer project's `vendor` directory and add to its ECF target:

```xml
<library name="arc" location="vendor/arc/arc.ecf"/>
```

The consumer uses its normal base library and compatible native Eiffel/C
compiler. No Conan, mise, native prefix variable, or regeneration of the ECF is
needed. Keep the package together when moving it. The generated ECF uses relative
`$ECF_CONFIG_PATH` references, explicit `.a`/`.lib` archives, compiler definitions
such as `LIBARCHIVE_STATIC`, and system libraries from Conan metadata. Shared
library overrides are rejected. Custom packaged framework directories are not
supported; the selected dependencies use static archives and system backends.

For source-checkout tests and release acceptance, see [Testing](testing.md).

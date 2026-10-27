# Development and packaging

## Supported code and retained metadata

The wrapper contains POSIX and Windows/MSVC implementations in the same header.
The CI matrix compiles and runs the same tests with Gobo and EiffelStudio on
Ubuntu x86_64/glibc, macOS ARM64 and Windows x86_64/MSVC. Check all six job results
before claiming a verified port. Gobo/macOS ARM64 is also verified locally;
Linux/musl release builds are outside this matrix.

The ECF retains EiffelStudio/EiffelBase and Gobo/FreeELKS selection. Library clients
need a compatible Eiffel compiler and C toolchain; the library target imports no CLI.

## Native dependencies

System mode uses libarchive development headers/libraries, with a supported
baseline of 3.8.7. Source ECF defaults to system linkage without scripts or a native
deployment. Prefix and exact library overrides are environment variables
`ARC_LIBARCHIVE_PREFIX` and `ARC_LIBARCHIVE_LIBRARY`. Windows system DLL clients
must explicitly select matching shared CRT (`ARC_SYSTEM_CRT=md`); static system
libarchive uses `ARC_SYSTEM_STATIC=true` and consumer-supplied transitive libraries.

The Conan recipe requests static dependencies; `conan.lock` pins recipe versions.
The existing dependency set remains libarchive, zlib, bzip2, LZ4, XZ/LZMA,
Zstandard, libiconv and Expat, with platform-specific crypto metadata. macOS uses
CommonCrypto, Linux OpenSSL, and the retained Windows graph uses CNG. ZIP AES-256
creation and passphrase-based reading are exposed by the Eiffel API.

```sh
git submodule update --init --recursive
mise trust
mise install
mise run prepare
# Or use cached recipes/sources/binaries only:
mise run prepare -- --profile profiles/macos-arm64 --no-remote
```

The preparation script uses the project-local `build/conan` cache (`conan-musl`
for native musl, with dependency rebuilds), detects a
native toolchain unless a profile is supplied, stages static dependencies and
replaces `build/native` only after successful staging, restoring the previous
deployment if the swap fails. Separate host/build profiles are accepted with
`--profile` and `--build-profile`. Windows host dependencies use static CRT.
Linux libc is detected natively; `--libc` asserts the expected environment rather
than changing a glibc compiler into a musl cross-toolchain.

A generated CMake hook fixes two libarchive 3.8.7 issues: Windows discovery
of Conan’s `zstd_static.lib`, and ZIP’s missing UTF-8 flag for ASCII-named links
with Unicode targets. The hook configuration contributes to the package ID, so
unpatched cached binaries are not reused. System libarchive is not modified;
Unicode ZIP symlink interoperability requires an equivalent libarchive patch there.
The result includes
`native.ecf`, `manifest.json`, licenses and `full_deploy/host`.

Do not hand-edit generated native ECF, build outputs, EIFGENs or `.gobo`.
The CLI uses the pinned `vendor/progress_bar` submodule for single-row Unicode bars
on stderr. Known file sizes show percentages; unknown sizes show a spinner and
byte counter. Rendering uses ANSI cursor controls and leaves stdout unchanged.
The progress library is Apache-2.0 licensed; application packages include its license.

The pinned CLI parser stays a Git submodule; do not edit its source or revision
for this migration.

## Build with Gobo

Configure GOBO and its normal compiler environment first:

```sh
export GOBO_EIFFEL=ge
gec --gelint --variable=ARC_NATIVE_MODE=conan --target=cli arc.ecf
gec --gelint --variable=ARC_NATIVE_MODE=conan tests/demo.ecf
gec --gelint --variable=ARC_NATIVE_MODE=conan tests/checks.ecf
gec --gelint --variable=ARC_NATIVE_MODE=conan tests/package_checks.ecf
gec --gelint --variable=ARC_NATIVE_MODE=conan tests/archive_checks.ecf
./arc --help
```

The executables are `arc`, `arc_demo`, `arc_checks`, `arc_package_checks` and `arc_archive_checks`.
Rebuild after source changes before integration tests. Run probes in fresh working
directories because output creation is exclusive. `arc_demo` creates three files
and archives for all eight profiles, then lists each archive.

`arc_checks ARCHIVE NEW_EXTRACTION_DIRECTORY` expects `second.txt` (200000 `x`
bytes) in its fresh working directory; use a demo archive. It checks selected
extraction, input ownership, error snapshots and failed-output cleanup.

`arc_package_checks SOURCE OUTPUT PROFILE NEW_EXTRACTION_DIRECTORY` expects:

- `first.txt`: `first file` plus newline;
- `nested/second.txt`: 200000 `x` bytes;
- `nested/empty.txt`: empty;
- `empty-dir/`: empty;
- `file-link -> first.txt`, `dir-link -> nested`, and a dangling link named
  `dangling-link` with a nonempty target.

An optional fifth argument `""` packs only directory contents. An optional sibling
`SOURCE/../denied` with mode 000 tests errno propagation under a non-root user.
The test checks output self-exclusion, links, byte progress, existing output,
failed/abandoned output and propagation of observer IO_FAILURE.

`arc_archive_checks` runs in a fresh working directory through `check_archive.py`.
It checks source registration without I/O, copied names, deferred contents,
commit/cancel/abort, selected/full extraction, progress and observer cleanup.
It also checks full/partial/empty remaining passes, stable inspection snapshots,
password-based verification and damaged payloads. `check_archive.py` creates the
corrupted ZIP and TAR fixtures required by the assertion-enabled probe.

For a future POSIX EiffelStudio verification, configure the normal ISE variables,
set GOBO_EIFFEL=ise and rebuild from source. Windows verification must use a real Windows host and matching MSVC toolchain. Do not use stale executables from the former implementation.

## Automated checks and distribution

```sh
mise run prepare
mise run test
# Metadata/import-policy tests only; no native toolchain needed:
mise run test-metadata
# Rebuild assertion-enabled probes and test the existing CLI:
python3 tests/check_archive.py --build
# System library consumer, without CLI or a Conan deployment:
ARC_LIBARCHIVE_PREFIX="$(brew --prefix libarchive)" mise run test-system
```

`mise run build` finalizes and checks the Conan-backed CLI. It enables Boehm GC
explicitly. Linux application builds require native musl; Windows requires static
CRT and Release dependencies. The finalized executable is inspected with
`otool`/`lipo`, `dumpbin`, or `readelf`. Third-party shared imports fail packaging;
Linux also rejects an ELF interpreter, and Windows rejects dynamic CRT imports.
The Windows native ECF propagates the profile's `/MT` (or `/MTd` for a debug library).

The native suite rebuilds all four probes, runs profile fixtures in fresh
directories, and runs `check_cli.py` with the explicit executable. Metadata tests
cover imports, ABI/libc names, library-only packaging and failed preparation.
Windows jobs also run the platform-specific native cases.

```sh
# Prepared library, no CLI submodule required:
mise run package
# Or prepare and package in one operation:
python3 scripts/package.py --profile profiles/macos-arm64 --no-remote
# Verified standalone application from the existing deployment and executable:
python3 scripts/package.py --kind application --binary ./arc
# Inspect an existing binary explicitly:
python3 scripts/prepare.py --check-binary ./arc
```

Artifacts are `build/package/arc-<ABI>-library.zip` and
`build/package/arc-<ABI>-application.zip`. The ABI includes MSVC runtime on Windows
and libc on Linux. The library ZIP contains:

```
arc/
    arc.ecf               # only the library target; bundled native linkage
    src/core/             # Eiffel and C wrapper sources
    README.md
    docs/
    conan.lock
    build/native/
        native.ecf
        manifest.json
        full_deploy/host/ # static dependencies, headers and licenses
```

Library packaging removes CLI targets, sources and submodule requirements. It
makes the native dependency unconditional, so the consumer root need not repeat
mode variables. Source ECF is not modified. Move the package together and connect
`vendor/arc/arc.ecf`. A compatible Eiffel/C toolchain is still required, but Conan,
mise and Python are not. System configuration is for source checkouts, not this
prepared ABI-specific package.

The application ZIP contains the inspected executable, native manifest, licenses
and `README.txt`. It contains no compiler, source tree or native deployment.
Application packaging requires Release dependencies/runtime and validates imports
before replacing its staging area. This check does not replace integration tests.

```sh
python3 tests/check_archive.py --relocate build/package/PACKAGE-library.zip
python3 tests/check_archive.py --relocate build/package/PACKAGE-application.zip
git diff --check
```

Relocation tests compile and execute a consumer, or run CLI integration from the
extracted application, in a path with spaces and Unicode. The current Gobo gecc
mangles Unicode include paths on macOS, and its script launcher fails with spaces;
the POSIX application builder and consumer test therefore use `gec --cc=no` and runs the generated script
directly with `sh -e`. No generated file is hand-edited. Windows uses the native compiler driver. Relocation checks remain separate from
the six-way compile/test matrix.

## Windows validation

Use Windows 10 1803 or later with a matching MSVC/UCRT environment and the existing
`profiles/windows-x86_64` profile.
Run prepare from a Visual Studio Developer terminal, configure Gobo for MSVC, and
set `GOBO_CC=msc`, and build the CLI and all four probe ECFs. Use the resulting `.exe` paths explicitly in
the Python checks above. Source tests use UTF-8 capture. Symlink tests require
Developer Mode or elevation; junction tests run only on Windows. AES ZIP checks use 7-Zip on Windows (already required by the Gobo installer)
and `bsdtar` on Unix, preserving Unicode passphrases at the command-line boundary. Tests do not silently skip links.

The header uses wide native paths, binary exclusive output, checked directory
iteration, physical reparse-point classification and scoped thread-local UTF-8
locale. Reparse points other than symlinks are rejected, not traversed. PAX can
retain Windows file/directory symlink metadata; other formats use libarchive's
available metadata and link restoration rules. Long paths and UNC destinations
must be tested separately before claiming those capabilities.

Standalone system diagnostics retain positive errno or negative original Win32
codes, with UTF-8 messages and separate cleanup errors. Libarchive diagnostics
retain libarchive's own status/code/message. Check Windows build, profile, AES, link/name-policy and cleanup results in CI.
Relocated consumer/CLI checks and long-path/UNC behavior require separate Windows
verification.

## Continuous integration

GitHub Actions builds the CLI and all four assertion-enabled probes on Ubuntu
24.04, macOS 15 ARM64 and Windows Server 2022, with Gobo 26.06 and EiffelStudio
25.12. Both compilers run the same eight-format, CLI, password and cleanup checks;
packaging contracts run in every job. Failures in one combination do not cancel
other combinations. Windows enables symlink creation and uses MSVC with static CRT.

Run the shared checks locally after preparing native dependencies:

```sh
python3 tests/check_archive.py --build --compiler ge
python3 tests/check_archive.py --build --compiler ise
```

macOS uses EiffelStudio’s headless `ecb` compiler, avoiding GTK/X11 dependencies.
EiffelStudio uses fresh projects and generated ECF copies in `build/ci`, leaving source configurations
unchanged, and finalizes with `-keep` to preserve probe assertions. CI uses Linux
glibc to test portability; it does not certify fully static musl releases.

ZIP creation explicitly selects UTF-8 header names on every platform, including
Windows where libarchive otherwise defaults to CP437. The profile suite checks
the Unicode entry name with Python’s independent ZIP reader.

# Development and packaging

## Supported code and retained metadata

The current wrapper is POSIX-only. Gobo/macOS ARM64 is the verified combination.
Linux native compilation and EiffelStudio are not verified by this migration.
Windows implementation is intentionally absent. Windows/Linux Conan profiles and
metadata tests remain infrastructure for future native work, not support claims.

The ECF retains EiffelStudio/EiffelBase and Gobo/FreeELKS selection. Library clients
need a compatible Eiffel compiler and C toolchain; the library target imports no CLI.

## Native dependencies

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

The preparation script uses the project-local `build/conan` cache, detects a
native toolchain unless a profile is supplied, stages static dependencies and
replaces `build/native` only after successful staging. The result includes
`native.ecf`, `manifest.json`, licenses and `full_deploy/host`.

Do not hand-edit generated native ECF, build outputs, EIFGENs or `.gobo`.
The pinned CLI parser stays a Git submodule; do not edit its source or revision
for this migration.

## Build with Gobo

Configure GOBO and its normal compiler environment first:

```sh
export GOBO_EIFFEL=ge
gec --gelint --target=cli arc.ecf
gec --gelint tests/demo.ecf
gec --gelint tests/checks.ecf
gec --gelint tests/package_checks.ecf
gec --gelint tests/archive_checks.ecf
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
set GOBO_EIFFEL=ise and rebuild from source. This migration does not add or test
Windows support. Do not use stale executables from the former implementation.

## Automated checks

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
python3 tests/check_cli.py ./arc
python3 tests/check_read_cli.py ./arc
python3 tests/check_archive.py ./arc_archive_checks
git diff --check
```

CLI tests use actual rebuilt executables and independent archive readers, checking
all profiles, source mapping, symlinks, progress, no-overwrite, pathname rejection,
terminal escaping, metadata summaries, corrupt payloads and AES-256 passwords. Packaging tests use synthetic native
metadata across retained profiles and validate licenses, static link paths and ZIP
relocation; these are not native Windows compilation tests.

## Package and consume without Conan

```sh
mise run package
# With a prepared/cached native profile:
mise run package -- --profile profiles/macos-arm64 --no-remote
```

`scripts/package.py` prepares dependencies with the supplied arguments, then calls
`create_package`. A failed prepare leaves the previous deployment intact. Package
names derive from target OS, architecture, compiler, build type and runtime metadata.

The target-specific ZIP under `build/package` contains:

```
arc/
    arc.ecf
    src/core/             # ARC_* API and c/libarchive_wrapper.h
    src/cli/              # archive commands and application entry point
    src/cli/presentation/     # CLI-only terminal rendering and pathname tree
    vendor/cli/           # pinned parser sources, no Git metadata
    README.md
    docs/
    conan.lock
    build/native/
        native.ecf
        manifest.json
        full_deploy/host/...
```

Unpack `arc` into a consumer's `vendor` directory and reference
`vendor/arc/arc.ecf`. Keep the package together when moving it. Native paths are
relative to `$ECF_CONFIG_PATH`, and consumers do not need Conan or mise. They still
need a compatible Eiffel/C ABI and system runtime. No generated executable,
Conan cache or compiler activation script is shipped. Native dependency licenses
are included. Packaging must fail for missing dependencies or inconsistent metadata.

Release validation includes copying the produced package to a different directory,
compiling a consumer and running its archive operations. Only native POSIX targets
are meaningful for the current wrapper. The [migration report](migration.md) lists
remaining migration limits.

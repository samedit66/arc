# Release binaries

`vcpkg.json` pins the dependency baseline and the vcpkg checkout to one commit.
At this baseline libarchive is 3.8.9, with zlib, bzip2, LZ4, liblzma, zstd,
XML and cryptographic support. vcpkg verifies downloaded source hashes.
Its installed `share/*/copyright` files accompany every release binary.

The workflow builds and tests these native targets:

| Target | Runtime |
| --- | --- |
| linux-x86_64 | Fully static musl |
| linux-arm64 | Fully static musl |
| macos-x86_64 | Static third-party libraries; macOS 13+ system libraries |
| macos-arm64 | Static third-party libraries; macOS 13+ system libraries |
| windows-x86_64 | Static third-party libraries; Windows system DLLs only |

`scripts/check-release-binary.sh` rejects unexpected dynamic dependencies.
`mise run test-release -- TARGET` runs the public API checks and complete CLI
suite against the release build, without rebuilding with system libarchive,
without packaging. `mise run dist` builds, tests and packages after success.
Binary jobs also run on branch pushes and pull requests so publication is not
the first time a release build is tested.

Each package contains `arc` (or `arc.exe`), `VERSION`, `LICENSE` and
`THIRD_PARTY_NOTICES.txt`, README and target/version metadata in `BUILD_INFO`. The publication job computes `SHA256SUMS`, uploads
all five packages into a draft, and publishes only after uploads succeed.
A rerun can resume a draft marked with the same checked commit; it does not
modify an already-published release or an unrelated draft.

To build a native distribution, install mise and platform tools:

- Linux: GCC/build-essential, musl-tools, linux-libc-dev, pkg-config, Git, curl,
  zip/unzip, autoconf, automake and libtool. Arc and C dependencies use musl-gcc;
  C++ helper tooling uses the platform g++. Kernel UAPI headers are isolated
  from glibc headers.
- macOS: Apple command-line tools, Git and Homebrew pkgconf, autoconf, automake,
  libtool. The deployment target is macOS 13. CI builds on macos-15-intel and
  macos-15 for the respective native architectures.
- Windows: MSYS2 UCRT64 GCC, pkgconf, Git, zip/unzip, autoconf, automake,
  libtool, make and Perl. Third-party libraries and GCC runtime link statically;
  Windows system DLLs remain external.

```sh
mise trust
mise install
mise run release
mise run dist
# CI and optional explicit native target/root:
RELEASE_TARGET=macos-arm64 ARC_RELEASE_BUILD_DIR=/tmp/arc-release mise run dist
```

Mise supplies CMake/Ninja, Python/uv for testing and the formatter. CI runners
and OS toolchains define the platform environment; compiler patch versions
are not frozen, and bit-for-bit reproducibility is not claimed. To select a
different compiler, use a new `ARC_RELEASE_BUILD_DIR`; dependencies and Arc
must use compatible toolchains. The existing vcpkg baseline and source hashes
control dependency versions without implicit updates. Binary caching is
available through `VCPKG_DEFAULT_BINARY_CACHE`.

Default output is `build/release/<target>`; archives appear in `assets/`.
The public target names are `linux-x86_64`, `linux-arm64`, `macos-x86_64`,
`macos-arm64` and `windows-x86_64`. Legacy `darwin-*` inputs are accepted and
published macOS archive names retain `darwin-*` for installer compatibility.
The `release-vcpkg` preset describes the CMake configuration; shell helpers
only prepare vcpkg/musl environment, invoke CMake and inspect/package outputs.
Only Release dependencies are built.

`licenses/` contains additional compiler/runtime notices: GCC GPLv3 and runtime
exception 3.1 (GCC 14.2.0 upstream), musl COPYRIGHT (upstream, retrieved
2026-10-14), LLVM compiler-rt license (19.1.7), and MinGW-w64 COPYING/AUTHORS
and runtime notices (upstream commit 63ebe6cdb3ec4d5c586b7f0f0c03dd502e44c296).
These accompany the dependency notices; Arc itself remains Apache-2.0.

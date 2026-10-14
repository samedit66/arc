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
| darwin-x86_64 | Static third-party libraries; macOS 13+ system libraries |
| darwin-arm64 | Static third-party libraries; macOS 13+ system libraries |
| windows-x86_64 | Static third-party libraries; Windows system DLLs only |

`scripts/check-release-binary.sh` rejects unexpected dynamic dependencies.
`mise run test-release -- TARGET` runs the public API checks and complete CLI
suite against the release build, without rebuilding with system libarchive,
and packages the tested executable after success.
Binary jobs also run on branch pushes and pull requests so publication is not
the first time a release build is tested.

Each package contains `arc` (or `arc.exe`), `VERSION`, `LICENSE` and
`THIRD_PARTY_NOTICES.txt`. The publication job computes `SHA256SUMS`, uploads
all five packages into a draft, and publishes only after uploads succeed.
A rerun can resume a draft marked with the same checked commit; it does not
modify an already-published release or an unrelated draft.

To reproduce a build, install CMake, Ninja, Git, curl and the target compiler;
Linux additionally needs musl-tools and kernel headers, Windows uses MSYS2
UCRT64, and macOS uses Apple command-line tools. Then:

```sh
ARC_RELEASE_BUILD_DIR=/tmp/arc-release bash scripts/build-release.sh darwin-arm64
ARC_RELEASE_BUILD_DIR=/tmp/arc-release mise run test-release -- darwin-arm64
```

Use Homebrew for CMake and build tools on macOS. Build products, downloaded
sources and installed dependencies remain under the temporary build directory.
The pinned vcpkg checkout can use a binary cache via `VCPKG_DEFAULT_BINARY_CACHE`.

`licenses/` contains additional compiler/runtime notices: GCC GPLv3 and runtime
exception 3.1 (GCC 14.2.0 upstream), musl COPYRIGHT (upstream, retrieved
2026-10-14), LLVM compiler-rt license (19.1.7), and MinGW-w64 COPYING/AUTHORS
and runtime notices (upstream commit 63ebe6cdb3ec4d5c586b7f0f0c03dd502e44c296).
These accompany the dependency notices; Arc itself remains Apache-2.0.

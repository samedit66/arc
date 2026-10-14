# Project

Arc is a human-oriented C11 archive library and CLI powered by libarchive.
It targets macOS, Linux, and Windows through MSYS2 UCRT64, with UTF-8 paths.

The API describes everyday user actions: create an archive, add files or
text, read entries, inspect contents, verify integrity, and unpack.
Callers should not need to understand backend handles, format codes,
or libarchive initialization sequences.

Usability comes before feature coverage. Arc intentionally supports a
focused subset of libarchive for common archive workflows. It does not
aim for feature parity or expose every backend option. Add functionality
when it serves a concrete everyday use case and fits the existing API.
Specialized archive features and fine-grained backend control belong
in direct libarchive usage.

Keep public contracts explicit, ownership predictable, and errors
represented by ArcResult. Preserve extraction safety: reject unsafe
paths and never overwrite existing files or links.

## Source Layout

- `arc.h`: public API declarations and authoritative operation contracts.
- `arc.c`: public wrappers, locale scopes, and cleanup sequencing.
- `cli/main.c`: CLI parsing and commands: pack, unpack, list, info, verify.
- `internal/`: archive writing, reading, extraction, path validation,
  and the libarchive adapter.
- `filekit/`: independently buildable C11 filesystem library with
  its own API and no libarchive dependency.
- `tests/`: CLI tests, public C API checks, and consumer integration checks.
- `scripts/`: build, run, test, formatting, and installation tooling.
- `CMakeLists.txt` and `cmake/`: library embedding and package integration.
- `VERSION`: project release version.

## Development

Run commands from the repository root.

Prerequisites: mise, Git, Bash, a C11 compiler, ar, pkg-config, and
libarchive development files. Formatting requires clang-format.
Consumer integration checks require CMake and C/C++ compilers.
CMake, Ninja, Python, uv and clang-format are pinned by mise for development tooling.

- Setup: `mise trust`, then `mise install`.
- Configure when needed: `mise run configure`.
- Build: `mise run build`.
- Native static Release: `mise run release`.
- Tested distribution: `mise run dist`.
- Run: `mise run run -- --help`.
- CLI and public C API tests: `mise run test`.
- Focused tests: `mise run test -- -k selection`.
- Tests and formatting validation: `mise run check`.
- Format C sources: `mise run format`.
- Independent filesystem checks: `mise run filekit:verify`.
- CMake/pkg-config consumer checks: `bash scripts/check-consumers.sh`.
- ASan/UBSan on macOS or Linux:
  `ARC_SANITIZE=1 CC=clang ARC_BUILD_DIR=/tmp/arc-sanitize mise run test`.

Development output defaults to build/dev (build/sanitize for sanitizers); use ARC_BUILD_DIR
to override it. Test tasks build first and use the resulting executable.
Do not run pytest directly or bypass the build dependency with --skip-deps.

After changes, run the relevant checks. Changes to filekit require its
independent checks; changes to embedding or packaging require consumer
integration checks. Preserve the existing C style and build conventions.

## Documentation

- `README.md`: overview and quick start.
- `docs/install.md`: prerequisites and installation.
- `docs/build.md`: builds, configuration, and library embedding.
- `docs/tutorial.md`: CLI and C API examples.
- `docs/design.md`: architecture, scope, and contracts.
- `arc.h`: authoritative public API contracts.
- `tests/README.md`: testing, fixtures, and sanitizers.
- `filekit/README.md`: filesystem library structure and verification.
- `filekit/filekit.h`: authoritative filesystem API contracts.

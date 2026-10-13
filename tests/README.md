# Arc CLI tests

Run from the repository root:

```sh
mise run test
mise run test -- -k selection
mise run test -- tests/test_pack.py::test_multiple_sources
mise run test -- --junitxml=/tmp/arc-results.xml
```

Mise runs `build` before `test`. A failing build stops the dependency chain
before pytest starts, regardless of any older executable. Do not bypass this
step with `--skip-deps`. Tests receive an absolute `ARC_BINARY`, not source
paths or build objects. Direct pytest startup without that input is rejected.

## Environment and checks

`test_install.py` runs the installer with local Git, mise and build substitutes.
It requires no network or system package installation and checks piped input,
updates, destination validation, temporary-file cleanup and preservation of an
existing binary after download, dependency, build, copy or publication failures.
`test_install_windows.py` uses real PowerShell and MSYS2 Bash on Windows,
replacing the download with a local shell script. It checks path quoting,
UCRT64 selection, failure propagation, environment restoration and cleanup.

Before pytest, `scripts/check-api.sh` compiles and runs `tests/api.c` against
the freshly built static library. It checks public writer/reader states, chunk
sizes, EOF, skip, handle consumption and capability output contracts, including
sanitizer instrumentation when enabled.

The Python suite treats arc as a black box: it invokes the executable and checks exit
status, output, archives and filesystem effects. The Python environment lives
in `tests/.venv`; dependencies are declared in `pyproject.toml` and pinned in
`uv.lock`. uv installs the locked dependencies on first use.

`mise run verify` is an alias for the build-and-test task. `mise run check` also
checks C formatting and requires clang-format. For an ASan/UBSan run on macOS
or Linux (mise preserves the explicit compiler and output directory):

```sh
ARC_SANITIZE=1 CC=clang ARC_BUILD_DIR=/tmp/arc-sanitize mise run test
```

Filekit's API checks run separately:

```sh
mise run filekit:verify
```

CI runs both suites on macOS, Linux and Windows, plus the arc sanitizer suite
on macOS and Linux. For Windows toolchain setup, see
[Building arc](../docs/build.md#windows).

## Fixtures and isolation

`conftest.py` provides a session-scoped binary path and function-scoped `arc`,
`source_tree`, `make_tar`, `simple_tar` and `zip_archive` fixtures. `tmp_path`
gives every parameterized case its own directory. Test methods request only
what they need; there is no shared mutable setup or prerequisite test.

`arc` runs subprocesses without a shell, with an explicit working directory,
UTF-8 output capture and a 30-second timeout. Failures include command, exit
status, stdout and stderr. A signal termination is never an expected rejection.
Tar and ZIP output is inspected with Python readers without extracting it;
malicious inputs are never extracted with Python. Path-escape tests use an
`outside` directory and sentinel inside the test's temporary root. This
contains the intended targets; the temporary directory is not an OS sandbox.

## Format coverage

Python reads TAR, gzip/bzip2/xz TAR and ZIP outputs independently.
The development-only pyzipper dependency checks AES ZIP independently; a
checked-in Info-ZIP fixture covers ZipCrypto without external executables. Zstandard TAR
and 7z retain round-trip checks because the pinned Python 3.13 standard library
cannot read those formats. RAR uses the checked-in fixture described in
`fixtures/README.md`; CPIO uses a small explicit ODC record generator. No
network is needed for fixtures. Installing the locked Python dependencies may
require network access on a new machine.

Windows-only tests skip on Unix with named reasons. Tests needing symlinks
probe host permission first; on an unprivileged Windows host they skip with
the actual OS error. An unexpected refusal on Unix remains a failure. The RAR
extraction test explicitly checks Windows's documented symlink limitation.

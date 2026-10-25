# Testing

Run these commands from the repository root, with the toolchains configured as
described in [Building and packaging](build.md). The source checkout includes the
tests and build scripts; the consumer ZIP includes the library, CLI and docs.

```sh
mise run test
```

`tests/test_packaging.py` contains focused standard-library tests for ECF
translation and ZIP packaging. They check preservation of supplied link order,
platform-specific library names and definitions, relative deployment paths,
package contents and relocation, distinct CRT package names, and rejection of
incomplete or inconsistent deployments.

The fixtures are text files with `.a`/`.lib` extensions. These tests do not run
Conan or a compiler, establish correct Conan graph ordering, or verify shell
quoting for paths containing spaces. They also do not cover preparation CLI
argument forwarding or prove native Windows/Linux compatibility. Release
acceptance requires preparing real dependencies, packaging, unpacking into a
separate consumer, compiling, and running it on each supported OS.

Local probe targets mark the Arc library writable so EiffelStudio tracks source
changes between builds. Packing probes use assertions and independent libarchive-based `bsdtar` reads
(`tar` on macOS). Install `bsdtar` for systems whose `tar` is GNU tar. Run:

```sh
GOBO_EIFFEL=ge gec --thread=1 --gelint --finalize tests/pack.ecf
GOBO_EIFFEL=ise ec -batch -config tests/pack.ecf -target pack -finalize -keep -c_compile
python3 tests/check_pack.py ./pack_probe ./EIFGENs/pack/F_code/pack_probe
python3 tests/check_pack_native.py
```

The integration checks cover all supported encodings, Unicode paths, root
mapping, explicit directory-contents registration, registration preconditions,
deferred traversal, links, conflicts, invalid discovered names, option capture, passwords,
callbacks, reuse and output cleanup. The POSIX native probe injects read/write,
finish, close, free and removal failures and verifies caller-locale restoration. Native Windows/Linux behavior
must still be verified on those platforms.

Extraction integration probes can be built and checked separately:

```sh
GOBO_EIFFEL=ge gec --gelint --finalize tests/unpack.ecf
GOBO_EIFFEL=ise ec -batch -config tests/unpack.ecf -target unpack -finalize -keep -c_compile
python tests/check_unpack.py ./unpack_probe ./EIFGENs/unpack/F_code/unpack_probe
python tests/check_unpack_native.py
```

The integration checker exercises content, Unicode paths, preserved links,
existing destinations, empty archives, format detection, unsafe paths, collisions,
unsupported entries, and corrupt input. The native POSIX probe injects read/write,
finish, close and free failures, checks UTF-8 selection under a caller's `C`
locale and restoration, and verifies that native allocation failures abort.
The Eiffel probe also checks status transitions, attached error access and its preconditions,
immutable error snapshots, malformed diagnostic decoding, Unicode paths including
characters outside the BMP, Unicode diagnostics and the Windows filename policy.
It does not establish native Windows filesystem behavior.

For a focused Windows path check after building with the native MSVC toolchain:

```powershell
python tests/check_unpack.py --unicode-only ./unpack_probe.exe ./EIFGENs/unpack/F_code/unpack_probe.exe
```

This subset checks Unicode source and entry names, including characters outside
the BMP, destination collisions and missing sources. It requires no POSIX links.
Run the password/observer checker below on Windows as well; the native fault
checker remains POSIX-only.

Password/observer probes use an additional test target:

```sh
GOBO_EIFFEL=ge gec --gelint --finalize tests/options.ecf
GOBO_EIFFEL=ise ec -batch -config tests/options.ecf -target options -finalize -keep -c_compile
python tests/check_unpack_options.py ./unpack_options_probe ./EIFGENs/options/F_code/unpack_options_probe
```

This checker requires the `zip` command to generate encrypted ZIP fixtures. It
checks captured options, password-string copying, missing/wrong/Unicode passwords,
progress accounting, empty archives, immutable snapshots, callback exceptions,
final-success ordering, reentry rejection, and reuse after interruption.
On POSIX it also selects a caller's `C` locale and checks its restoration after
extraction and inside observers, including callbacks that raise exceptions.

Conan resolved all three platform graphs. On macOS ARM64 the expanded dependency
set was built locally under Apple Clang 21, and offline preparation succeeded.
The library ZIP is consumed by a separate application whose CLI
calls `{ARCHIVE}.libarchive_version`. Both EiffelStudio and Gobo compile the CLI on macOS ARM64.
For the current audit run and its exact checks, see [the verification record](parity.md#verification-record).

The compiled CLI can be checked separately, without silently skipping missing
executables:

```sh
mise run test-cli -- ./arc ./EIFGENs/cli/F_code/arc
```

This checks each binary against the same scenarios: every command and option, help/version, invalid
syntax, repeated flags, option ordering, Unicode positional arguments, and `--`.
Parser and missing-input cases run in empty temporary directories and assert that
no files are created. Archive fixtures additionally check extraction and creation:
source inference in the current directory, directory-root mapping, multiple sources,
all creation suffixes including `.tbz2`, `.txz` and `.tzst`, explicit formats,
unknown explicit-format diagnostics independent of suffix, AES-256 passwords, links, no-overwrite and
failure cleanup. Packing checks require `bsdtar` as an independent archive reader.
These are executable CLI checks, separate from the packaging unit tests.

`tests/linkage.ecf` is a macOS-only linker probe. It forces reader, writer,
filter/format registration, and ZIP-writing symbols into the binary, without
adding Eiffel implementation code. Both executables run and contain those
symbols. Their only dynamic dependencies are macOS system libraries. This tests
static linkage, not archive round trips or password correctness.

EiffelStudio emits warnings from its generated C/runtime build and SDK linker;
the C compilation and executable run complete successfully. Native Windows and
Linux builds remain to be verified on those operating systems.

## Sequential reading

Sequential reading probes use assertions and require `zip` for encrypted fixtures:

```sh
GOBO_EIFFEL=ge gec --gelint --finalize tests/read.ecf
GOBO_EIFFEL=ise ec -batch -config tests/read.ecf -target read -finalize -keep -c_compile
python3 tests/check_read.py ./read_probe ./EIFGENs/read/F_code/read_probe
python3 tests/check_read_native.py
```

These checks cover manual advancement, direct-item `across`, independent streams,
retained entry and archive-metadata snapshots, captured passwords, early/repeated closing, client-exception
cleanup and verification failures. Regression scenarios additionally cover reader
stream factories, scoped `visit` success and visitor exceptions, and second-header
failures without the preceding entry's name, and unread-body skip failures
retaining the current entry name. Extraction and CLI checks cover
independent size validation for missing-body mtree input, and sparse payloads with
leading/trailing holes and all-hole entries. CLI checks additionally cover empty archives,
Unicode names, literal unsafe paths, duplicates, type conflicts, stored links,
special entries, control-character escaping in entry output and operation diagnostics, truncated streams and corrupted ZIP
contents whose headers still list successfully. Info checks additionally cover
entry/type/size totals, empty archives, outer filter chains, encryption detection,
content-based format detection, unpublished summaries on failure and total-size
overflow. The POSIX native probe injects
header/data WARN/RETRY/failures, partial reader opens, extraction writer
configuration failures and successive close/free failures. It checks locale
restoration and fatal allocation behavior. Native Windows/Linux execution remains
unverified.

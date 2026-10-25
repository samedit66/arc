# Parity with the original Arc

The five command names are present. That was the easy part.
This audit compares the library behind them, the observable CLI behavior, and the
build/test surface. It records missing work; it doesn't change archive behavior.

## Scope and evidence

The baseline is [samedit66/arc](https://github.com/samedit66/arc) at
[`260d95f47c0da94480cf9703f3c07a89c5350173`](https://github.com/samedit66/arc/tree/260d95f47c0da94480cf9703f3c07a89c5350173),
confirmed as upstream `main` on 2026-10-24. Public declarations and implementation
were compared, rather than treating the upstream README as a complete feature list.

Upstream evidence:

- [Public API and contracts](https://github.com/samedit66/arc/blob/260d95f47c0da94480cf9703f3c07a89c5350173/arc.h)
- [Formats, filters and capabilities](https://github.com/samedit66/arc/blob/260d95f47c0da94480cf9703f3c07a89c5350173/src/internal/libarchive_adapter.c)
- [Reader, statistics and verification](https://github.com/samedit66/arc/blob/260d95f47c0da94480cf9703f3c07a89c5350173/src/internal/reader.c)
- [Packing](https://github.com/samedit66/arc/blob/260d95f47c0da94480cf9703f3c07a89c5350173/src/internal/writer.c),
  [extraction](https://github.com/samedit66/arc/blob/260d95f47c0da94480cf9703f3c07a89c5350173/src/internal/unpack.c)
  and [CLI](https://github.com/samedit66/arc/blob/260d95f47c0da94480cf9703f3c07a89c5350173/src/cli/main.c)
- [CI matrix](https://github.com/samedit66/arc/blob/260d95f47c0da94480cf9703f3c07a89c5350173/.github/workflows/ci.yml)

Local evidence is linked beside each group. API absences and implementation
branches are source-confirmed. Observed behavior is distinguished from a contract
difference that still needs a dedicated regression fixture. Upstream CI configuration
is evidence of its configured checks, not a claim that this audit ran them.

## Missing library capabilities

| Capability | Original C Arc | Eiffel Arc | Practical consequence |
| --- | --- | --- | --- |
| Choose extraction destination | `arc_unpack(path, destination)` | Only derived sibling `unpack_path` | Clients cannot restore into their chosen directory |
| Merge into a directory | `ARC_DESTINATION_MERGE`; files/links still protected | Destination must be new | Cannot add an archive to an existing tree |
| Select paths/subtrees | `ArcUnpackOptions.paths`, unmatched-selector and link-chain checks | No selection input | Must extract everything or implement a separate extractor |
| Strip a common root | `strip_root`, preflight, transformed hardlink paths | No root-stripping policy | Archives with a wrapper directory retain that directory |
| Read entry contents | `arc_reader_read` fills caller buffers; explicit `arc_reader_skip` | Metadata, `forth`, and discard-only `consume` | Cannot process a file's bytes without extracting it |
| Write generated content | `arc_add_text`, `arc_begin_file`, `arc_write_chunk`, `arc_end_file` | Filesystem source registration only | Must create temporary source files for generated data |
| Incremental writer lifecycle | Owned writer with add/write/close/discard operations | One complete `pack` per captured source set | Cannot interleave generated data and filesystem additions in an open archive |
| Empty archive | Create a writer and close it without additions | `pack` requires `source_count > 0` | No source-free public creation path; an empty directory is a workaround |
| Runtime capabilities | `arc_get_capabilities`, including format/filter/entry-feature bits | No equivalent query | Support is discovered by trying an operation |
| Library statistics | `arc_get_size`, `arc_inspect[_ex]`, `ArcInfo` | Header metadata on stream; summary only in CLI | Applications must duplicate counting and filesystem-size logic |
| Read-operation observation | `ArcReadOptions.observer` for reader/list/inspect/verify | Read options contain only a password | No progress observer for long verification or metadata traversal |

See the local [reader](../src/core/unpack/archive_reader.e),
[read stream](../src/core/read/archive_read_stream.e),
[read options](../src/core/read/archive_read_options.e),
[unpack options](../src/core/unpack/unpack_options.e), and
[writer](../src/core/pack/archive_writer.e).
A metadata iteration API already exists; absence of a convenience `list` method
isn't itself a missing ability to enumerate entries.

The default directory mapping also differs at library level: C `arc_pack` adds
a single directory's contents, while Eiffel `add_path` retains its basename.
Use `add_directory_contents (path)` for the original mapping. The Eiffel CLI already
makes that choice for a single directory, so this difference is easy to miss
when testing only command-line examples.

## Formats and compression

| Area | Original | Eiffel | Assessment |
| --- | --- | --- | --- |
| Explicit writable containers | TAR, ZIP, 7z, CPIO ODC, BSD AR, ISO9660, XAR, WARC, subject to capability probes and backend limits | TAR, ZIP, 7z | Five container choices are missing |
| Inferred creation suffixes | `.tar`, `.tar.gz`, `.tgz`, `.tar.bz2`, `.tar.xz`, `.tar.zst`, `.zip`, `.7z` | Same in CLI and library, plus `.tbz2`, `.txz`, `.tzst` | Extra shared aliases, not a loss |
| Outer filters | none/gzip/bzip2/xz/zstd; valid combinations depend on format and runtime probes | Those five choices for TAR; ZIP and 7z require none | ZIP with an outer filter is a missing configuration relative to the C API |
| TAR encoding | Full pax writer | Restricted pax writer | Different metadata representation; no byte-for-byte output parity |
| Input formats | Libarchive all-format/all-filter registration, with Arc-specific compatibility checks | Libarchive all-format/all-filter registration | Broad reading is present; no verified format-by-format equivalence |
| Encrypted output | ZIP AES-256 | ZIP AES-256 | Present; content-only protection, visible names/metadata in both |

The additional C formats are explicit configurations: even upstream doesn't infer
all writable formats from their suffixes. Conversely, linking Expat or LZ4 doesn't
mean the Eiffel writer exposes XAR or LZ4 output.

Evidence: local [encoding options](../src/core/pack/pack_options.e),
[encoding selection](../src/core/pack/archive_pack_session.e),
[native writer configuration](../src/core/c/arc_pack.h), and
[native reader configuration](../src/core/c/arc_read.h).
There is no backend capability matrix for the Eiffel build yet.

## Integrity and link handling

| Area | Original | Eiffel | Status |
| --- | --- | --- | --- |
| Decoded entry-size validation | Reader/verify/extraction compare logical content length against known ordinary-file size, with overflow checks | Shared Eiffel reader validation compares decoded payload length with declared size or sparse-map data lengths, bounds offsets and checks overflow during verify/extraction | Missing-body mtree and sparse-hole regressions cover independent validation; hardlinks and unknown sizes are excluded |
| Forward hardlinks and chains during unpack | Defers link creation and validates target relationships | Writes in archive order; target must already exist | Valid archives with later targets cannot rely on equivalent extraction behavior |
| Hardlinks during packing | Preserves filesystem hardlink relationships where representable; unsupported preservation fails | Stores each ordinary-file path independently and clears native hardlink metadata | Deliberate local contract, with larger archives and different extracted topology |
| Ambiguous CPIO hardlink groups | Sequential reader and unpacker reject ambiguous ordinary-file CPIO link groups where normalization is unsupported | No corresponding format-specific guard or normalization | Semantic coverage gap; do not claim correct reconstruction of all CPIO link groups |
| Progress counters | Checked counters return overflow errors | Packing/unpacking totals use direct `NATURAL_64` additions | Missing explicit overflow-result behavior; no enormous-input reproduction claimed |

Evidence: [native reads](../src/core/c/arc_read.h),
[stream consumption](../src/core/read/archive_read_stream.e),
[extraction session](../src/core/unpack/archive_unpack_session.e),
[native extraction](../src/core/c/arc_unpack.h), and
[packing session/primitives](../src/core/pack/archive_pack_session.e).
The original's `reader.c`, `unpack.c` and `normalized_entry` provide the comparison.

Neither implementation can turn plain TAR into a content-checksummed format.
Neither verification operation establishes authenticity, safe extraction, or
inspection of every byte after logical archive EOF. Those are shared limits,
not missed features in this port.

## Diagnostics and public contracts

These differences need a decision, not a mechanical translation of C names.

| Area | Original | Eiffel | Assessment |
| --- | --- | --- | --- |
| Error taxonomy | Categories for invalid state/argument/path, existence, unsupported, invalid archive, overflow, selection and password outcomes | Seven broad operation-stage categories plus native status/domain/code | Native details are richer, but portable decisions such as missing vs rejected password lack dedicated Arc categories |
| Reason/path/output state | Structured `reason`, diagnostic `path`, truncation flag and `partial_output` | Stage, archive source path and optional entry name; no reason enum or partial-output flag | Cannot query the same high-level outcome fields without interpreting lower-level details |
| Diagnostic lifetime and size | By-value result with bounded buffers; one secondary cleanup-message field | Immutable snapshots with untruncated message bytes and a sequence of cleanup errors | Local strength, not missing compatibility |
| Invalid client calls | Returned argument/state errors in the C contract | Eiffel preconditions; unexpected exceptions propagate | Intentional Eiffel API design |
| Native allocation failure | Arc-owned allocation can return `ARC_OUT_OF_MEMORY`; backend fatal termination remains possible | Native allocation failures abort by policy | Intentional local boundary; changing it requires changing the accepted project contract |
| Metadata lifetime | Borrowed entry strings until next/close or visitor return | Immutable owned snapshots retainable after advance/close | Local strength with allocation/copy cost |
| Progress semantics | Preparing/processing/finalizing phases, entries seen and optional entry size | Successful byte/entry counts and explicit final-success snapshot | Local completed-entry semantics are useful; phases and size estimates are missing |
| Observer execution | Within temporary native UTF-8 locale; no unwind across C callback | In Eiffel, after locale restoration; documented exception cleanup and reentry rules | Deliberate integration improvement |

See [error snapshots](../src/core/archive_error.e),
[common operation results](../src/core/archive.e),
[pack progress](../src/core/pack/pack_progress.e), and
[unpack progress](../src/core/unpack/unpack_progress.e).
The local guarantee assumes clients honor contracts and keep sources unchanged;
there is no requirement here to defend against deliberate misuse or concurrent
filesystem replacement.

## CLI differences

All five commands execute real operations. Missing command names: none.
The remaining surface differences are still substantial:

| Area | Original CLI | Eiffel CLI |
| --- | --- | --- |
| `unpack` options | Optional destination, merge for explicit destination, repeated `--entry`, `--strip-root` | Password only |
| Default extraction location | Archive basename directory in the current directory | Sibling directory beside the archive |
| Destination suffix rules | Includes CAB, LHA/LZH, AR and WARC; unknown/invalid derived names receive `.extracted` | Those suffixes are not stripped; unknown names receive `.unpacked`; additional LZ4/shorthand suffixes are stripped |
| `info` | Archive bytes and declared content bytes plus type counts; rejects unknown sizes/unsupported types | No archive-byte count; additionally reports format, filters, all entry count, other types, unknown sizes and encryption state |
| Terminal progress | Synchronous progress rendering and terminal lifecycle | No progress rendering |
| Success feedback | Extraction destination and verification success messages | `unpack`/`verify` success is silent; `pack` prints `Created ...` |
| Version | Release version and runtime libarchive information | `arc dev` and integer libarchive version |
| Argument parser | Options before operands; separate option values | Vendored parser also supports `--name=value`, options after operands and `-h` |
| Empty option values | Explicit empty format/password follow upstream validation/error paths | Empty format selects suffix inference; empty password means no password |
| Source basename edge case | Directory `..` is resolved for stored basename | Implicit naming rejects a final `..`; the library's `_as` API can name it explicitly |

The changed default extraction location affects commands such as
`arc unpack /backups/docs.zip` even when run with no optional flags.
The richer local `info` output isn't a drop-in replacement for upstream output
parsers. `--format` also remains limited to the three local containers.

Color is not implemented in the local CLI. `NO_COLOR` therefore has no visible
work to do; this is not equivalent to having ported upstream's colored output.
Tree/list presentation and diagnostics are different implementations, not promised
byte-for-byte copies. The [CLI guide](cli.md) documents the actual local grammar.

Evidence: local [command declarations](../src/cli/arc_cli.e),
[pack](../src/cli/arc_pack_command.e),
[unpack](../src/cli/arc_unpack_command.e),
[info](../src/cli/arc_info_command.e),
[root/version](../src/cli/arc_root_command.e), and
[vendored parser documentation](../vendor/cli/README.md).

## Distribution and verification gaps

Upstream has native CI jobs for macOS, Linux and Windows, sanitizer jobs, release
packaging/installers and a benchmark suite. The local recipe has three platform
graphs, but native execution has been verified only on macOS ARM64. Windows/MSVC
and Linux profiles are preparation support, not equivalent proof of archive I/O
on those hosts. No local CI workflow, release installer or benchmark suite exists.
The original's speed claims must not be reused as Eiffel measurements.

The local ZIP is an ABI-specific Eiffel source/native-dependency package; upstream
also distributes standalone CLI releases and a C library with CMake/pkg-config
integration. Those C integration mechanisms aren't requirements for an Eiffel
port. A reproducible Eiffel consumer build is the relevant equivalent.

The checkout has no top-level project `LICENSE` file. Upstream's Apache-2.0 license
and shipped third-party notices do not by themselves supply that missing local
project document. Resolve project licensing before presenting a public release
as fully documented. This audit does not invent or assign license terms.

The documentation split also exposed a packaging consequence: a README linking to
`docs` is incomplete if the ZIP omits that directory. Packaging now includes it,
with a fixture assertion for retained content after relocation. This is the only
implementation change associated with this documentation audit.

## Suggested order of work

1. Add destination selection/new-or-merge mode, selectors and root stripping in the
   library, with the CLI as a thin client. Define hardlink selection and preflight
   behavior before implementing flags.
2. Add entry-content reading and generated/chunked writing with explicit ownership,
   size and close/discard contracts. These unlock actual application use cases.
3. Close the remaining link-order gaps with focused fixtures;
   decide whether preserving hardlinks should change the existing packing contract.
4. Add capability queries, a reusable inspection result, read observers and richer
   portable error categories. Then extend writable formats using capability tests.
5. Verify Linux and Windows natively, establish project licensing and automate the
   compiler/OS matrix. CLI progress and presentation parity can follow.

This is a proposed sequence, not approval to change the current public contracts.
Keeping the implementation small is useful. Forgetting which small pieces are
still missing is rather less useful.

## Verification record

The audit uses source inspection for API completeness and observed local checks
for existing behavior. It doesn't claim an exhaustive differential test of both
implementations or native execution on Linux/Windows.

The earlier documentation/package audit was checked on macOS ARM64:

| Check | Result |
| --- | --- |
| `python3 -m unittest discover -s tests -p 'test_*.py'` | 7 tests passed, including docs content in the ZIP and after relocation |
| `python3 tests/check_cli.py ./arc ./EIFGENs/cli/F_code/arc` | 225 cases passed per executable |
| `python3 tests/check_read_cli.py ./arc ./EIFGENs/cli/F_code/arc` | Both executables passed list/tree/info/verify integration |
| `python3 tests/check_pack.py ./pack_probe ./EIFGENs/pack/F_code/pack_probe` | 50 scenarios passed per executable |
| `python3 tests/check_unpack.py ./unpack_probe ./EIFGENs/unpack/F_code/unpack_probe` | 22 cases passed per executable |
| `python3 tests/check_read.py ./read_probe ./EIFGENs/read/F_code/read_probe` | Both passed stream, metadata, password and verification scenarios |
| `python3 tests/check_unpack_options.py ./unpack_options_probe ./EIFGENs/options/F_code/unpack_options_probe` | 9 scenarios passed per executable |

The library probes and checkout CLI checks use existing compiled executables;
archive implementation sources were not modified for this audit. A real package
was also created from the prepared static dependencies and extracted under a
separate temporary directory. Its CLI target was rebuilt with the local Gobo
patched build `00.00.00+dcb806f` and EiffelStudio 25.12.9.8922, then checked with
the same CLI suites. This checks actual native relocation as well as the
fixture-only packaging tests.

Local Markdown links/anchors and code fences, documented preparation/package help,
Python compilation and whitespace checks were validated. No native Linux/Windows
run, fresh dependency installation, upstream C test-suite run or benchmark was
performed. See [Testing](testing.md) for build commands and the wider matrix.

### Reader ownership refactor

The reader refactor was rebuilt from source with Gobo
`00.00.00+dcb806f` and EiffelStudio `25.02.9.8732`. Both produced executable
ARM64 programs on the local macOS host. The earlier audit record above is retained
as a separate checkpoint.

| Check | Result |
| --- | --- |
| Reading probe with both compilers | Passed metadata/password contracts, scoped visitors, and distinct header/skip failure attribution |
| Extraction probe with both compilers | 25 cases per executable, including sparse trailing/all-hole files and missing-body size rejection |
| Options probe with both compilers | 9 password/observer scenarios per executable |
| Packing probe with both compilers | 50 scenarios per executable |
| CLI with both compilers | 232 cases per executable, including shared suffix aliases and unknown-format diagnostics |
| Read CLI checks with both compilers | Passed list/tree/info/verify, escaped error paths, sparse payloads and independent size validation |
| Native reader, extraction and packing probes | Passed fault injection, diagnostic preservation, cleanup and locale checks |
| Packaging unit checks | 7 passed |
| Actual package relocation | Both compilers rebuilt the packaged CLI in a temporary path containing spaces and passed a pack/verify/unpack round trip |

Documentation links/anchors, changed Python syntax and whitespace checks passed.
Generated Eiffel C/runtime and SDK linker warnings remain; native Linux/Windows
execution and a fresh native dependency installation were not performed.

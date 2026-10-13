# Selective extraction and content verification

Date: 2026-10-13
Status: Implemented; local verification recorded below, cross-platform CI execution pending
Scope revision: password handling removed at the user's request
Targets: macOS, Linux, Windows MSYS2 UCRT64

## Scope and compatibility

Add selective extraction and a separate full-content verification operation. Passwords and encryption are not planned now. No password API, password CLI option, crypto probe, external interoperability tool or new dependency is introduced. Existing create/list/inspect/unpack structs, signatures and enum values remain intact; new result codes and capability flags are appended.

The filesystem/platform interfaces and writer implementation are unchanged. Existing unpack delegates to the extended operation with an empty selection. Existing listing/inspection and extraction retain their encryption and CPIO normalization restrictions.

## Public API

```c
typedef struct {
    ArcDestinationMode destination_mode;
    const char *const *paths;
    size_t path_count;
} ArcExtractOptions;

ArcResult arc_unpack_ex(const char *archive_path,
    const char *destination, const ArcExtractOptions *options);
ArcResult arc_verify(const char *archive_path);
```

Extended options are required. The paths/options are borrowed for the call; extraction copies and normalizes selectors before creating output. Zero path_count extracts all; positive count requires an array of valid safe relative UTF-8 paths. Empty/root selectors are rejected; empty selection is the way to select the entire archive.

`ARC_NOT_FOUND` identifies unmatched selectors. `ARC_SELECTION_ERROR` identifies a missing or excluded selected hard-link dependency; distinguishing a genuinely missing archive target from an excluded target does not require indexing the entire archive. Existing full extraction still uses INVALID_ARCHIVE for missing hard-link targets. `ARC_CAN_VERIFY` advertises the content-reading operation for a configured readable format/filter, not support for all encrypted variants or universal integrity detection.

## Selection semantics

- A normalized selector matches its own name and descendants at '/' component boundaries. `docs` matches `docs/readme`, but not `docs-old`. Implicit directory trees are selectable without explicit directory headers. Matching is byte-exact and case-sensitive on every host; filesystem collision rules remain platform-dependent.
- For candidate matching only, strip leading `./` and trailing `/`. Do not resolve `..`, collapse internal separators or apply host naming restrictions before selection. Selected output names still go through strict safety/representability validation.
- Repeated/overlapping selectors process each archive record once. Each selector is satisfied by a matching record or descendant. Archive duplicates retain existing selected-record conflict behavior.
- Filtering precedes parent creation, output creation, entry indexing and metadata cloning. Unselected unsafe paths/special entries/host-invalid names are not extracted or validated as outputs. Unreadable header/name text and skip errors can still fail the operation.
- Stored paths are preserved. Selecting `docs/readme` creates required `docs` parents. Unselected directory records do not apply metadata to implicit parents. Existing directories retain the existing metadata policy.
- Selected hard links require their target chains in the selected extraction index. Forward chains are supported; dependencies are never auto-extracted or copied, and unrelated existing destination files are never used as hard-link targets.
- Symlink targets are not auto-selected. Safe dangling links remain permitted under current host restrictions. Unknown Windows link target kind may return UNSUPPORTED. Absolute/external/cyclic targets and writes through links remain forbidden.
- Scan to archive EOF and then check unmatched selectors. Failures may leave partial output, including an empty root when nothing matched. No two-pass preflight or rollback is added.
- Selected encrypted entries are unsupported. Unselected encrypted headers do not trigger the wrapper's unconditional rejection, but the backend may need a password to parse/skip particular layouts and that failure is propagated.
- Skipping unselected payloads does not verify them. A successful selection is not whole-archive verification.

## Verification semantics

Verification opens input read-only, reads every entry body to entry EOF, reaches archive EOF and checks reader cleanup. It does not create an extraction root or modify input. Backend buffering/temp storage can still occur.

A fixed-size `archive_read_data` buffer preserves logical-byte counting including zero-filled sparse gaps. Byte counts are overflow-checked; ordinary data-bearing records are checked against declared non-negative sizes when available. Unknown sizes do not prevent verification. Other entry types are drained as well. `archive_read_data_skip` is not used as verification. Backend warnings/errors during headers/data/cleanup are failures. The first entry-body failure carries entry context where available.

Header decoding is separate from entry policy: verification rejects encryption but does not require listing's CPIO hard-link normalization, unique/safe names, permitted extraction object types or resolvable link targets.

Success means complete readable entry data and available backend/format integrity checks passed. ZIP CRC checking is delegated to libarchive. Plain TAR has no general per-file content checksum: arbitrary same-size payload changes can remain undetected. Verification is not origin authentication, extraction-safety validation, a detector of every trailing container byte, or a cryptographic guarantee. Unsafe paths, duplicate records, dangling links and special objects can verify while extraction remains forbidden.

## CLI

```text
arc unpack [--entry PATH]... ARCHIVE [DESTINATION]
arc verify ARCHIVE
```

Options precede operands; `--` handles names beginning with '-'. Unpack retains automatic NEW versus explicit MERGE destination policy. Verify prints success only after complete reading and cleanup. No password option exists.

```text
arc unpack --entry docs --entry images/logo.png project.zip out
arc verify project.zip
```

## Implementation sequence and ownership

1. **API and policies:** append the new types/codes/capability in arc.h; add locale-scoped public wrappers in arc.c; expose private operation declarations in arc_internal.h. Separate read_header from readable_entry/normalized_entry in libarchive_adapter. Existing clients must still compile and existing checks must pass.
2. **Verification:** implement the full-content loop in reader.c with size/overflow checks, backend error propagation and reader-release precedence. Test good archives, CRC damage, truncation, sparse entries, CPIO groups, special/unsafe/duplicate records and unsupported encrypted records.
3. **Selection:** keep normalized selector copies/match flags in unpack.c; put byte-exact candidate matching in archive_paths. Filter before extract_entry and preserve deferred-link/metadata sequencing. Test component boundaries, overlaps, implicit directories, excluded objects and link dependencies.
4. **CLI:** parse repeatable --entry and add verify in main.c using only the public API. Preserve argument conversion, -- behavior, help and exit-status conventions.
5. **Documentation and verification:** update the design, API examples and temporary fixture/API checks in justfile. Run formatting, clean build, regression checks, strict declaration checks, and ASan/UBSan. Existing CI invokes the same checks on all three targets.

## Verification coverage and environment

The temporary justfile suite includes existing format/filter round trips plus selection, CRC, truncation, sparse-map and CPIO fixtures generated with Python's standard library. It demonstrates that inspecting a damaged ZIP can succeed while verifying it fails, and that an unselected damaged ZIP body can be skipped without asserting whole-archive validity. A synthetic encrypted ZIP flag fixture tests continued refusal of selected encrypted content; it is not a claim of crypto interoperability.

API assertions cover exact result codes, invalid selector options before filesystem effects, deduplication, zero-count full extraction and capability advertisement. CLI assertions cover Unicode/case-exact matching, overlapping subtrees, partial/no matches, unsafe selected/excluded paths, preservation after collisions, selected forward hard-link chains and excluded targets, safe dangling symlinks on supported hosts, unselected parent metadata and option errors.

Local results: macOS with libarchive 3.8.9; clean `just check` passed 153 CLI/API checks, the ASan/UBSan run passed the same 153 checks, and strict prototype/declaration checks passed. Additional sparse TAR tail/all-hole probes succeeded. Run `just check`; run `CC=clang ARC_SANITIZE=1 just verify` with a separate build directory. Linux and Windows results require the existing CI matrix; they are not claimed from local macOS execution.

References:

- [Read data API and sparse semantics](https://github.com/libarchive/libarchive/blob/v3.8.9/libarchive/archive_read_data.3).
- [ZIP reader CRC behavior](https://github.com/libarchive/libarchive/blob/v3.8.9/libarchive/archive_read_support_format_zip.c).

## Not planned now

Password handling, encryption/decryption, password input/providers, hidden filenames, compression-level/method/thread tuning, ACL/xattr/owner restoration, generic memory/network/callback I/O, progress/cancellation, glob/exclusion selection, multipart archives, editing/appending archives, quotas, rollback and atomic publication. Existing partial-output and trusted-parent restrictions remain.

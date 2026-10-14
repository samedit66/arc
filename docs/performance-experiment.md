# Performance experiments

Measured on 15 October 2026, macOS 26.6.2 ARM64, Python 3.13.15.
The baseline is Arc 0.2.0 at `b6221fb`, native CMake Release, libarchive 3.8.9
with Apple's zlib 1.2.12. The updated build retains only compact extraction
metadata. Ouch is the official 0.8.3 ARM64 release. Compression prototypes use
an isolated copy of the same libarchive linked to zlib-ng 2.3.3, compiled in
zlib compatibility mode with Clang 22.1.8 and CMake Release. The loader was
checked to confirm the experimental binary actually used that copy.

## Outcomes

The implemented change replaces a retained `archive_entry` clone with the
existing `ArchiveMetadata` value (permission bits, timestamp presence,
seconds and nanoseconds). It preserves directory finalization and hard-link
metadata checks. No public API or extraction safety contract changes.

On a generated 20,000-file TAR, median peak RSS dropped from 41.17 MiB to
7.17 MiB, about 83%. The speed change on 2,000-file ZIP extraction was small:
300.04 ms before and 299.39 ms after. This is a memory improvement, not a
meaningful ZIP speed victory under the native timezone configuration.

ZIP speed also depends strongly on this machine's timezone conversion.
With `TZ=UTC0` applied to **every participant**, the updated Arc extracted
2,000 files in 131.92 ms versus ouch's 176.82 ms. With native settings,
Arc took 299.39 ms versus ouch's 176.73 ms. The exact same ouch-created ZIP
was used for both environments, preventing the input archive from changing
with the environment.

The zlib-ng prototype packed repeated text in 9.39 ms versus ordinary
Arc's 31.78 ms, approximately matching ouch's 9.63 ms. Its archive grew
from 57,895 to 107,103 bytes; ouch's was 123,319 bytes. The small time gap
between the two fast implementations is not a robust ranking. Pseudorandom
binary packing remained slower than ouch (203.14 versus 183.11 ms).

![ZIP and extraction-memory experiments](assets/benchmark/zip-experiment.svg)

The four panels measure different operations and have independent zero-based
linear scales. The memory panel uses a separate 20,000-file TAR corpus;
the other panels use 2,000 small files or one 16 MiB repeated-text file.

## Why timezone matters

A temporary instrumented build attributed roughly 170 ms of a 300 ms small
ZIP extraction to `archive_read_next_header`. Entry cloning took about 1 ms.
The backend-only probe reproduced the slowdown: 311.58 ms for extraction
versus 299.39 ms for Arc. Removing Arc's ownership/checking layer therefore
did not remove the main cost on these fixtures.

Libarchive converts DOS timestamps from both ZIP local headers and the
central directory through `dos_to_unix`, which calls `mktime`. Its writer
uses local-time conversion for ZIP timestamps. See the pinned sources:
[ZIP reader](https://github.com/libarchive/libarchive/blob/v3.8.9/libarchive/archive_read_support_format_zip.c),
[time conversion](https://github.com/libarchive/libarchive/blob/v3.8.9/libarchive/archive_time.c).
In a diagnostic extraction, changing only `TZ` to `UTC0` reduced header time
from about 173 ms to 5 ms. Compacting Arc's metadata did not account for that
difference.

The library does not change the caller's `TZ`. UTC is an explicit experimental
child-process setting, not an automatic performance switch. ZIP DOS timestamps
lack a timezone, so changing `TZ` can change restored times. The speed tests
validate names and file bytes, not equivalence of timestamps between timezone
settings. A fixed UTC environment is appropriate only when UTC is the intended
interpretation; these results do not justify rewriting global timezone state.

## Detailed ZIP measurements

Fixture version 1, seed 1729: the same 2,000 small files, 16 MiB repeated text
and 16 MiB seeded binary data as the [original benchmark](benchmark.md).
One warm-up, seven samples per case, rotating tool order. Medians below are
milliseconds. Every warm-up and measured output passed filename/SHA-256
content checks outside the timer. Each dataset uses one shared input ZIP,
created by ouch before the two environment comparisons.

[Raw ZIP samples](benchmark-zip-results.csv) contain all 420 measurements,
including the original Arc build and backend-only probe. The latter is a
fixture-only diagnostic program: it uses libarchive's disk writer and secure
extraction flags but does not implement Arc's complete ownership, conflict,
error or metadata contracts. It is not a replacement extraction path.

<details>
<summary>Native and UTC timings</summary>

| Environment | Dataset | Operation | Arc | Arc + zlib-ng | ouch | Backend-only |
|---|---|---|---:|---:|---:|---:|
| default | small | pack | 253.28 | 250.76 | 91.58 | 234.76 |
| default | small | unpack | 299.39 | 302.80 | 176.73 | 311.58 |
| default | text | pack | 31.78 | 9.39 | 9.63 | 31.78 |
| default | text | unpack | 10.03 | 9.51 | 13.80 | 10.37 |
| default | binary | pack | 232.46 | 203.14 | 183.11 | 231.99 |
| default | binary | unpack | 8.06 | 9.42 | 12.76 | 7.46 |
| utc | small | pack | 95.85 | 92.01 | 92.91 | 77.35 |
| utc | small | unpack | 131.92 | 132.01 | 176.82 | 147.50 |
| utc | text | pack | 31.73 | 9.25 | 9.67 | 31.60 |
| utc | text | unpack | 6.86 | 8.11 | 12.01 | 6.66 |
| utc | binary | pack | 233.11 | 202.70 | 181.39 | 232.54 |
| utc | binary | unpack | 7.77 | 8.96 | 13.06 | 7.18 |

</details>

## Compression ratio control

A separate seven-sample packing experiment on the same repeated-text fixture
raised only the experimental ZIP compression level from its default to 9.
This recovered the ordinary Arc archive size while retaining part of the
speed improvement. It still took longer than ouch, whose output was much
larger. Raw samples: [compression levels](benchmark-compression-levels.csv).

| Writer | Median packing ms | Archive bytes |
|---|---:|---:|
| Arc / system zlib | 31.71 | 57,895 |
| Arc / zlib-ng default | 9.47 | 107,103 |
| Arc / zlib-ng level 9 | 19.44 | 57,564 |
| ouch default | 10.01 | 123,319 |

The zlib-ng builds and level-9 adapter are isolated prototypes. Production
compression dependencies and defaults are unchanged. The candidate needs
broader corpus, malformed/encrypted-archive and platform validation before
adoption; this experiment validates only the generated plaintext payloads.

## Extraction memory

Hyperfine 2.0.0 measured peak RSS with `--metrics memory_peak_resident`, one
warm-up and three samples per command. All readers extracted one ouch-created
TAR containing 20,000 generated 2,048-byte files in 20 groups. The final output
was checked against the source manifest. Raw samples:
[memory results](benchmark-memory-results.csv).

| Reader | Median peak RSS MiB |
|---|---:|
| Arc before | 41.17 |
| Arc with compact metadata | 7.17 |
| ouch | 3.73 |

## Reproduce the ZIP comparison

Build the selected Arc binaries in Release and provide an ouch binary.
The comparison works without a probe or experimental backend:

```sh
mise exec -- python scripts/bench-zip.py --ouch /path/to/ouch --compare-utc
```

Optional `--arc-before /path/to/old/arc` and
`--arc-experimental /path/to/experimental/arc` include additional builds.
The script uses standard-library Python, generates disposable fixtures,
creates a shared ZIP per dataset and exports CSV plus JSON recording
parameters, versions, timezone, binary paths and input-archive hashes.
No payloads or third-party source trees are committed.

For the optional backend-only probe on macOS/Linux, compile
`scripts/bench-libarchive.c` with the **same libarchive** used by Arc:

```sh
# macOS Homebrew libarchive, if pkg-config does not find it automatically:
export PKG_CONFIG_PATH="$(brew --prefix libarchive)/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
mkdir -p build/bench
cc -std=c11 -O3 scripts/bench-libarchive.c -o build/bench/libarchive-probe $(pkg-config --cflags --libs libarchive)
mise exec -- python scripts/bench-zip.py --ouch /path/to/ouch --libarchive build/bench/libarchive-probe --compare-utc
```

The probe is a POSIX diagnostic helper. The experimental zlib-ng build uses
its `ZLIB_COMPAT=ON`, `BUILD_SHARED_LIBS=ON` and `CMAKE_BUILD_TYPE=Release`
settings and a separate libarchive linked to it. On this Mac, the existing
libarchive dylib was copied into the ignored experiment directory, its zlib
load path redirected to the isolated library and its signature renewed;
only the experimental executable links to that copy. The level-9 variant
sets libarchive's ZIP `compression-level` option to `9` in an isolated adapter.
These are not installation changes or a supported distribution configuration.

Regenerate the figure from committed samples with the plotting environment
from [the build guide](build.md):

```sh
python scripts/plot-bench.py --zip-experiment docs/benchmark-zip-results.csv
```

## Verification

The retained metadata change passed all four native C tests and the full
Python suite: 306 passed, 26 Windows-only skips. A separate ASan/UBSan build
passed all four C tests and 82 selected CLI tests, with 12 Windows-only skips.
Added cases cover nanosecond file/directory timestamps and conflicting
repeated-directory metadata; existing hard-link metadata, unsafe paths,
no-overwrite, selection and root-stripping tests passed. C formatting checks
passed. Windows behavior has not been executed on this host.

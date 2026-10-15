# Arc v0.2.1 — The Speedy Gonzales Release

Faster archives, lighter footprints. ¡Ándale!

This patch improves packing overhead and extraction memory use while keeping
the C API and ABI compatible with v0.2.0.

## Performance improvements

- Extraction retains compact permission/timestamp values instead of cloned
  backend entries. On a local generated 20,000-file TAR, median peak RSS fell
  from 41.17 to 7.17 MiB, about 83%.
- TAR output omits unsupported access, change and creation times, avoiding
  unnecessary PAX records for ordinary integer-mtime entries. The local
  2,000-file fixture became 29% smaller and packed about 9% faster.
- Packing reuses traversal metadata on POSIX and removes duplicate filesystem
  inspection. A later 10,000-file comparison improved TAR/TAR.GZ packing by
  about 5–7%.
- A 256 KiB extraction buffer improved large-file TAR extraction by about
  11–15% in the repeated local comparison.

These are separate measurements on macOS ARM64 with native libarchive;
they are not cumulative or guarantees for every workload or release target.
See the [benchmark report](https://github.com/samedit66/arc/blob/v0.2.1/docs/benchmark.md)
for graphs, raw data, versions and reproduction commands.

## Compatibility and tooling

- Modification times, including fractional TAR timestamps, ordinary
  permissions and hard-link relationships remain preserved. Unsafe extraction
  paths are rejected, and existing files or links are never overwritten.
- Reproducible benchmarks cover Arc, tar, zip/unzip and ouch, with disposable
  fixtures, shared-input reader comparisons, and compression time/size curves.
- zlib-ng, adaptive compression and parallel gzip remain experiments; the
  release does not enable them or change compression defaults.
- CI refreshes dependency-cache snapshots so subsequent binary builds can
  reuse newly cached variants.

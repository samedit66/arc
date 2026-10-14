# Arc v0.2.0

This release simplifies everyday archive operations and improves CLI output,
extraction behavior, and build integration.

## API migration

The C API and ABI are incompatible with v0.1.1. Recompile all consumers.

- `arc_unpack(archive, destination)` now creates a new destination using two
  arguments. For merge mode, selection, passwords, or root stripping, use
  `arc_unpack_ex` with `ArcExtractOptions`. `ArcUnpackOptions` was removed.
- `ArcResult` now includes structured reason, path, truncation, partial-output,
  and cleanup diagnostics. Zero-initialize option and result structures, or use
  designated initializers. Diagnostic messages are not a parsing interface.
- `arc_pack` and `arc_pack_ex` provide whole-operation packing helpers;
  `arc_ok` checks operation success.

## Changes

- Extraction supports removing a single common archive root with `strip_root`
  and CLI `unpack --strip-root`.
- Symbolic-link target text is preserved, including dangling and external
  targets on POSIX. Windows requires a known file or directory link kind.
  Extraction still rejects unsafe entry paths and never overwrites existing
  files or links.
- TAR output preserves fractional timestamps using full PAX headers.
- CLI diagnostics provide operation context and hints; terminal operations
  display an activity animation.
- Builds use CMake presets and mise, with improved Homebrew libarchive
  discovery for pkg-config consumers.
- Sources are organized under `src/`, including the internal UTF-8 filesystem
  module; documentation and integration checks were updated accordingly.

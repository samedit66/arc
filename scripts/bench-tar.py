#!/usr/bin/env python3
"""Compare TAR packing changes while checking contents and timestamp precision."""

import argparse
import csv
from decimal import Decimal
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import tarfile
import tempfile

from bench import file_manifest, generate_fixtures, positive_int, timed_run


def check_archive(archive: Path, source: Path, precise: bool) -> dict[str, int]:
    counts = {"entries": 0, "pax_entries": 0, "mtime_pax": 0, "atime_pax": 0, "ctime_pax": 0}
    with tarfile.open(archive) as reader:
        for entry in reader:
            counts["entries"] += 1
            counts["pax_entries"] += bool(entry.pax_headers)
            for field in ("mtime", "atime", "ctime"):
                counts[f"{field}_pax"] += field in entry.pax_headers
            if precise:
                relative = Path(entry.name).relative_to(source.name)
                original = source / relative
                seconds = Decimal(entry.pax_headers.get("mtime", str(entry.mtime)))
                if seconds * 1_000_000_000 != original.lstat().st_mtime_ns:
                    raise RuntimeError(f"Timestamp mismatch: {entry.name}")
    return counts


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arc", type=Path, default=Path("build/release/arc"))
    parser.add_argument("--arc-before", type=Path)
    parser.add_argument("--arc-metadata", type=Path, help="intermediate build with only timestamp cleanup")
    parser.add_argument("--ouch", type=Path, required=True)
    parser.add_argument("--source", type=Path, help="also measure a source directory; .git is omitted")
    parser.add_argument("--runs", type=positive_int, default=7)
    parser.add_argument("--small-files", type=positive_int, default=2000)
    parser.add_argument("--output", type=Path, default=Path("build/bench/tar-experiment.csv"))
    args = parser.parse_args()
    if args.output.suffix.lower() != ".csv":
        parser.error("--output must have a .csv suffix")
    tools = {"arc": args.arc.resolve(), "ouch": args.ouch.resolve()}
    for name, binary in (("before", args.arc_before), ("metadata", args.arc_metadata)):
        if binary:
            tools[name] = binary.resolve()
    if any(not binary.is_file() for binary in tools.values()):
        parser.error("all binaries must exist")
    if args.source and not args.source.is_dir():
        parser.error("--source must be a directory")
    metadata = {"seed": 1729, "fixture_version": 1, "runs": args.runs, "warmups": 1,
                "small_files": args.small_files, "platform": platform.platform(),
                "timezone": os.environ.get("TZ"), "source": str(args.source) if args.source else None,
                "versions": {name: subprocess.check_output([str(binary), "--version"], text=True)
                             for name, binary in tools.items()}, "archive_checks": {}}
    rows = []
    with tempfile.TemporaryDirectory(prefix="arc-tar-experiment-") as temporary:
        root = Path(temporary).resolve()
        integer = generate_fixtures(root / "fixtures", 1729, 1, args.small_files)[0]
        fractional = root / "fractional"
        shutil.copytree(integer, fractional)
        for path in (fractional, *fractional.rglob("*")):
            os.utime(path, ns=(1600000000123456700, 1700000000123456700))
        datasets = {"integer": integer, "fractional": fractional}
        if args.source:
            source = args.source.resolve()
            copied = root / source.name
            if copied.exists():
                parser.error("source basename conflicts with generated fixture directory")
            shutil.copytree(source, copied, symlinks=True, ignore=shutil.ignore_patterns(".git"))
            datasets["source"] = copied
        for dataset, source in datasets.items():
            expected = file_manifest(source)
            for fmt in ("tar", "tar.gz"):
                samples = {tool: [] for tool in tools}
                for iteration in range(args.runs + 1):
                    order = tuple(tools)
                    offset = iteration % len(order)
                    for tool in order[offset:] + order[:offset]:
                        archive = root / f"output.{fmt}"
                        command = ([str(tools[tool]), "--quiet", "compress", str(source), str(archive)]
                                   if tool == "ouch" else
                                   [str(tools[tool]), "pack", "--keep-root", str(archive), str(source)])
                        elapsed = timed_run(command, root)
                        if iteration == 0:
                            metadata["archive_checks"][f"{dataset}/{fmt}/{tool}"] = check_archive(
                                archive, source, tool != "ouch")
                            destination = root / "restored"
                            timed_run([str(tools["arc"]), "unpack", str(archive), str(destination)], root)
                            if file_manifest(destination / source.name) != expected:
                                raise RuntimeError(f"Content mismatch: {dataset}/{fmt}/{tool}")
                            shutil.rmtree(destination)
                        else:
                            rows.append({"dataset": dataset, "format": fmt, "tool": tool,
                                         "run": iteration, "seconds": elapsed,
                                         "archive_bytes": archive.stat().st_size})
                            samples[tool].append(elapsed)
                        archive.unlink()
                for tool, values in samples.items():
                    size = statistics.median(row["archive_bytes"] for row in rows
                                             if (row["dataset"], row["format"], row["tool"])
                                             == (dataset, fmt, tool))
                    print(f"{dataset:10} {fmt:7} {tool:9} "
                          f"{statistics.median(values) * 1000:10.2f} ms {size:12.0f} bytes", flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    args.output.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Saved {args.output}; temporary fixtures removed.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error

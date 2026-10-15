#!/usr/bin/env python3
"""Compare Arc builds on disposable fixtures using a shared extraction input."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import tempfile
import time

from bench import file_manifest, generate_fixtures, positive_int, timed_run


def binary_digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", action="append", required=True, metavar="NAME=PATH")
    parser.add_argument("--runs", type=positive_int, default=7)
    parser.add_argument("--size-mib", type=positive_int, default=128)
    parser.add_argument("--small-files", type=positive_int, default=2000)
    parser.add_argument("--datasets", nargs="+", choices=("small", "text", "binary"),
                        default=["small", "text", "binary"])
    parser.add_argument("--formats", nargs="+", choices=("tar", "tar.gz", "zip"), default=["tar"])
    parser.add_argument("--operations", nargs="+", choices=("pack", "unpack"), default=["pack", "unpack"])
    parser.add_argument("--pause-ms", type=positive_int, default=0,
                        help="optional pause after validation/cleanup, outside the timer")
    parser.add_argument("--output", type=Path, default=Path("build/bench/io-results.csv"))
    args = parser.parse_args()
    binaries = {}
    for specification in args.binary:
        name, separator, path = specification.partition("=")
        if not separator or not name or name in binaries or not Path(path).is_file():
            parser.error("--binary requires a unique NAME and an existing PATH")
        binaries[name] = Path(path).resolve()
    if args.output.suffix != ".csv":
        parser.error("--output must end in .csv")
    baseline = next(iter(binaries.values()))
    rows = []
    metadata = {"seed": 1729, "fixture_version": 1, "runs": args.runs, "warmups": 1,
                "size_mib": args.size_mib, "small_files": args.small_files,
                "formats": args.formats, "datasets": args.datasets,
                "operations": args.operations, "pause_ms": args.pause_ms,
                "platform": platform.platform(), "timezone": os.getenv("TZ"),
                "binaries": {name: str(binary) for name, binary in binaries.items()},
                "binary_sha256": {name: binary_digest(binary) for name, binary in binaries.items()},
                "versions": {name: subprocess.check_output([str(binary), "--version"], text=True)
                             for name, binary in binaries.items()}}
    with tempfile.TemporaryDirectory(prefix="arc-io-bench-") as temporary:
        root = Path(temporary).resolve()
        datasets = generate_fixtures(root / "fixtures", 1729, args.size_mib, args.small_files)
        for source in datasets:
            if source.name not in args.datasets:
                continue
            expected = file_manifest(source)
            for fmt in args.formats:
                shared = root / f"shared.{fmt}"
                timed_run([str(baseline), "pack", str(shared), str(source)], root)
                for operation in args.operations:
                    for iteration in range(args.runs + 1):
                        order = tuple(binaries)
                        offset = iteration % len(order)
                        for name in order[offset:] + order[:offset]:
                            archive = root / f"output.{fmt}"
                            destination = root / "restored"
                            command = ([str(binaries[name]), "pack", str(archive), str(source)]
                                       if operation == "pack" else
                                       [str(binaries[name]), "unpack", str(shared), str(destination)])
                            seconds = timed_run(command, root)
                            if operation == "pack":
                                timed_run([str(baseline), "unpack", str(archive), str(destination)], root)
                                size = archive.stat().st_size
                                archive.unlink()
                            else:
                                size = shared.stat().st_size
                            if file_manifest(destination) != expected:
                                raise RuntimeError(f"Content mismatch: {source.name}/{fmt}/{operation}/{name}")
                            shutil.rmtree(destination)
                            if args.pause_ms:
                                time.sleep(args.pause_ms / 1000)
                            if iteration:
                                rows.append({"dataset": source.name, "format": fmt, "operation": operation,
                                             "tool": name, "run": iteration, "seconds": seconds,
                                             "archive_bytes": size})
                    for name in binaries:
                        samples = [float(row["seconds"]) * 1000 for row in rows
                                   if (row["dataset"], row["format"], row["operation"], row["tool"])
                                   == (source.name, fmt, operation, name)]
                        print(f"{source.name:6} {fmt:6} {operation:6} {name:12} "
                              f"{statistics.median(samples):9.2f} ms", flush=True)
                shared.unlink()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    args.output.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Saved {args.output}; temporary payloads removed.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error

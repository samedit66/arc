#!/usr/bin/env python3
"""Compare ZIP backends and Arc builds on disposable, reproducible fixtures."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
import time
import shutil

from bench import file_manifest, generate_fixtures, positive_int


def run(command: list[str], cwd: Path, environment: dict[str, str] | None = None) -> float:
    started = time.perf_counter()
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, env=environment)
    elapsed = time.perf_counter() - started
    if result.returncode:
        raise RuntimeError(f"Command failed: {command}\n{result.stderr[-4000:]}")
    return elapsed


def commands(tool: str, binary: Path, source: Path, archive: Path,
             destination: Path) -> tuple[list[str], list[str]]:
    if tool == "libarchive":
        return ([str(binary), "pack", str(source), str(archive)],
                [str(binary), "unpack", str(archive), str(destination)])
    if tool == "ouch":
        children = [path.name for path in sorted(source.iterdir())]
        return ([str(binary), "--quiet", "compress", *children, str(archive)],
                [str(binary), "--quiet", "decompress", str(archive), "--dir", str(destination)])
    return ([str(binary), "pack", str(archive), str(source)],
            [str(binary), "unpack", str(archive), str(destination)])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arc", type=Path, default=Path("build/release/arc"))
    parser.add_argument("--arc-before", type=Path)
    parser.add_argument("--arc-experimental", type=Path)
    parser.add_argument("--ouch", type=Path, required=True)
    parser.add_argument("--libarchive", type=Path, help="binary built from scripts/bench-libarchive.c")
    parser.add_argument("--seed", type=int, default=1729)
    parser.add_argument("--size-mib", type=positive_int, default=16)
    parser.add_argument("--small-files", type=positive_int, default=2000)
    parser.add_argument("--runs", type=positive_int, default=7)
    parser.add_argument("--compare-utc", action="store_true",
                        help="also measure all tools with TZ=UTC0 using the same extraction input")
    parser.add_argument("--output", type=Path, default=Path("build/bench/zip-experiment.csv"))
    args = parser.parse_args()
    if args.output.suffix.lower() != ".csv":
        parser.error("--output must have a .csv suffix")
    tools = {"arc": args.arc, "ouch": args.ouch}
    for name, binary in (("before", args.arc_before), ("experimental", args.arc_experimental),
                         ("libarchive", args.libarchive)):
        if binary:
            tools[name] = binary
    if any(not binary.is_file() for binary in tools.values()):
        parser.error("all binaries must exist; build Arc and the probe separately")
    tools = {name: binary.resolve() for name, binary in tools.items()}
    environments = {"default": None}
    if args.compare_utc:
        environments["utc"] = dict(os.environ, TZ="UTC0")
    metadata = {"fixture_version": 1, "seed": args.seed, "size_mib": args.size_mib,
                "small_files": args.small_files, "runs": args.runs, "warmups": 1,
                "platform": platform.platform(), "timezone": os.environ.get("TZ"),
                "compare_utc": args.compare_utc,
                "executables": {name: str(binary) for name, binary in tools.items()},
                "versions": {name: subprocess.check_output([str(binary), "--version"], text=True)
                             for name, binary in tools.items()},
                "shared_input": "ouch-created ZIP", "archives": {}}
    rows = []
    with tempfile.TemporaryDirectory(prefix="arc-zip-experiment-") as temporary:
        root = Path(temporary).resolve()
        datasets = generate_fixtures(root / "fixtures", args.seed, args.size_mib, args.small_files)
        for source in datasets:
            expected = file_manifest(source)
            common = root / "input.zip"
            pack, _ = commands("ouch", tools["ouch"], source, common, root / "unused")
            run(pack, source)
            with common.open("rb") as stream:
                metadata["archives"][source.name] = hashlib.file_digest(stream, "sha256").hexdigest()
            for environment_name, environment in environments.items():
                for operation in ("pack", "unpack"):
                    samples = {tool: [] for tool in tools}
                    for iteration in range(args.runs + 1):
                        order = tuple(tools)
                        offset = iteration % len(order)
                        for tool in order[offset:] + order[:offset]:
                            archive = root / "output.zip" if operation == "pack" else common
                            destination = root / "restored"
                            pack, unpack = commands(tool, tools[tool], source, archive, destination)
                            elapsed = run(pack if operation == "pack" else unpack, source, environment)
                            if operation == "pack":
                                run([str(tools["arc"]), "unpack", str(archive), str(destination)],
                                    source, environment)
                            if file_manifest(destination) != expected:
                                raise RuntimeError(f"Content mismatch: {source.name}/{operation}/{tool}")
                            if iteration:
                                row = {"environment": environment_name, "dataset": source.name,
                                       "operation": operation, "tool": tool, "run": iteration,
                                       "seconds": elapsed, "archive_bytes": archive.stat().st_size}
                                rows.append(row)
                                samples[tool].append(elapsed)
                            shutil.rmtree(destination)
                            if operation == "pack":
                                archive.unlink()
                    for tool, values in samples.items():
                        print(f"{environment_name:7} {source.name:8} {operation:6} {tool:12} "
                              f"{statistics.median(values) * 1000:9.2f} ms", flush=True)
            common.unlink()
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

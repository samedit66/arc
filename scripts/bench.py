#!/usr/bin/env python3
"""Generate disposable fixtures and compare archive CLI wall-clock times."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import shutil
import statistics
import subprocess
import tempfile
import time
from datetime import datetime, timezone


def positive_int(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def generate_fixtures(root: Path, seed: int, size_mib: int, small_files: int) -> list[Path]:
    rng = random.Random(seed)
    datasets = [root / name for name in ("small", "text", "binary")]
    for directory in datasets:
        directory.mkdir(parents=True)
    for index in range(small_files):
        directory = datasets[0] / f"group-{index % 20:02d}"
        directory.mkdir(exist_ok=True)
        (directory / f"file-{index:06d}.txt").write_bytes(
            (f"Record {index}: reproducible archive benchmark.\n".encode() * 50)[:2048]
        )
    text_block = b"Archive benchmark: predictable text, repeated records and UTF-8: \xc3\xa9.\n"
    chunk_size = 65536
    text_chunk = (text_block * (chunk_size // len(text_block) + 1))[:chunk_size]
    for name, directory in (("records.txt", datasets[1]), ("random.bin", datasets[2])):
        with (directory / name).open("wb") as stream:
            for _ in range(size_mib * 16):
                stream.write(text_chunk if name == "records.txt" else rng.randbytes(chunk_size))
    # Normalize timestamps so generated source metadata is reproducible too.
    for path in sorted(root.rglob("*"), reverse=True):
        os.utime(path, (946684800, 946684800))
    return datasets


def file_manifest(root: Path) -> dict[str, str]:
    manifest = {}
    for path in sorted(root.rglob("*")):
        if path.is_file():
            with path.open("rb") as stream:
                manifest[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, "sha256").hexdigest()
    return manifest


def commands(tool: str, archive_format: str, source: Path, archive: Path,
             destination: Path, executables: dict[str, str]) -> tuple[list[str], list[str]]:
    if tool == "arc":
        return ([executables[tool], "pack", str(archive), str(source)],
                [executables[tool], "unpack", str(archive), str(destination)])
    if tool == "ouch":
        sources = [path.name for path in sorted(source.iterdir())]
        return ([executables[tool], "--quiet", "compress", *sources, str(archive)],
                [executables[tool], "--quiet", "decompress", str(archive),
                 "--dir", str(destination)])
    if archive_format == "zip":
        return ([executables["zip"], "-q", "-r", str(archive), "."],
                [executables["unzip"], "-q", str(archive), "-d", str(destination)])
    flag = "-czf" if archive_format == "tar.gz" else "-cf"
    return ([executables["tar"], flag, str(archive), "-C", str(source), "."],
            [executables["tar"], "-xf", str(archive), "-C", str(destination)])


def timed_run(command: list[str], cwd: Path, environment: dict[str, str] | None = None) -> float:
    started = time.perf_counter()
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, env=environment)
    elapsed = time.perf_counter() - started
    if result.returncode:
        raise RuntimeError(f"Command failed: {command}\n{result.stderr}")
    return elapsed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arc", type=Path, default=Path("build/release/arc"))
    parser.add_argument("--ouch", type=Path, help="include an existing ouch binary")
    parser.add_argument("--seed", type=int, default=1729)
    parser.add_argument("--size-mib", type=positive_int, default=16,
                        help="size of each large file (default: 16 MiB)")
    parser.add_argument("--small-files", type=positive_int, default=2000)
    parser.add_argument("--runs", type=positive_int, default=5)
    parser.add_argument("--output", type=Path, default=Path("build/bench/results.csv"))
    args = parser.parse_args()
    if args.output.suffix.lower() != ".csv":
        parser.error("--output must have a .csv suffix (metadata uses the adjacent .json file)")
    executables = {name: shutil.which(name) for name in ("tar", "zip", "unzip")}
    executables["arc"] = str(args.arc.resolve())
    if not args.arc.is_file() or any(value is None for value in executables.values()):
        parser.error("requires a built Arc binary and tar, zip, unzip on PATH")
    if args.ouch:
        if not args.ouch.is_file():
            parser.error("--ouch must point to an existing binary")
        executables["ouch"] = str(args.ouch.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    metadata = {
        "fixture_version": 1, "seed": args.seed, "size_mib": args.size_mib,
        "small_files": args.small_files, "runs": args.runs,
        "platform": platform.platform(), "python": platform.python_version(),
        "timezone": os.environ.get("TZ"),
        "recorded_at_utc": datetime.now(timezone.utc).isoformat(),
        "tool_order": "rotate starting tool each iteration",
        "executables": executables,
        "versions": {name: subprocess.run(
            [path, "-v" if name in ("zip", "unzip") else "--version"],
            capture_output=True, text=True).stdout for name, path in executables.items()},
    }
    rows = []
    print("One warm-up per tool; warm filesystem cache; times exclude verification.", flush=True)
    print(f"{'dataset':8} {'format':7} {'pack / unpack':12} {'pack ms':>10} {'unpack ms':>10} {'bytes':>12}")
    with tempfile.TemporaryDirectory(prefix="arc-bench-") as temporary:
        # Arc rejects symlink path components; macOS exposes its temp dir via /var.
        root = Path(temporary).resolve()
        datasets = generate_fixtures(root / "fixtures", args.seed, args.size_mib, args.small_files)
        metadata["manifests"] = {source.name: file_manifest(source) for source in datasets}
        for source in datasets:
            expected = metadata["manifests"][source.name]
            for archive_format in ("tar", "tar.gz", "zip"):
                tools = ("arc", "zip" if archive_format == "zip" else "tar")
                if args.ouch:
                    tools += ("ouch",)
                samples = {tool: [] for tool in tools}
                for iteration in range(args.runs + 1):
                    offset = iteration % len(tools)
                    for tool in tools[offset:] + tools[:offset]:
                        archive = root / f"archive.{archive_format}"
                        destination = root / "restored"
                        pack, unpack = commands(tool, archive_format, source, archive,
                                                destination, executables)
                        pack_seconds = timed_run(pack, source)
                        if tool in ("tar", "zip"):
                            destination.mkdir()
                        unpack_seconds = timed_run(unpack, source)
                        if file_manifest(destination) != expected:
                            raise RuntimeError(f"Content mismatch: {source.name}/{archive_format}/{tool}")
                        archive_bytes = archive.stat().st_size
                        if iteration:
                            row = dict(dataset=source.name, format=archive_format, tool=tool,
                                       run=iteration, pack_seconds=pack_seconds,
                                       unpack_seconds=unpack_seconds, archive_bytes=archive_bytes)
                            rows.append(row)
                            samples[tool].append(row)
                        archive.unlink()
                        shutil.rmtree(destination)
                for tool, measurements in samples.items():
                    pack_ms = statistics.median(row["pack_seconds"] for row in measurements) * 1000
                    unpack_ms = statistics.median(row["unpack_seconds"] for row in measurements) * 1000
                    size = statistics.median(row["archive_bytes"] for row in measurements)
                    label = "zip / unzip" if tool == "zip" else tool
                    print(f"{source.name:8} {archive_format:7} {label:12} {pack_ms:10.2f} "
                          f"{unpack_ms:10.2f} {size:12.0f}", flush=True)
    with args.output.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    args.output.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Saved {args.output} and {args.output.with_suffix('.json')}; fixtures removed.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as error:
        raise SystemExit(str(error)) from error

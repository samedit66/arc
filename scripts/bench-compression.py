#!/usr/bin/env python3
"""Measure compression time/size curves using isolated Arc adapter builds."""

import argparse
import csv
import gzip
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
import zipfile
import zlib

from bench import file_manifest, generate_fixtures, positive_int, timed_run


DEFAULT_KINDS = ("repeated", "structured", "mixed", "random", "compressed", "small")
KINDS = (*DEFAULT_KINDS, "front-random", "front-text", "buried-text")
TEXT = (b"Archive benchmark: repeated UTF-8 records \xc3\xa9 and predictable values.\n" * 1024)[:32768]


def payload(kind: str, size: int, seed: int) -> bytes:
    rng = random.Random(seed)
    if kind == "repeated":
        return (TEXT * (size // len(TEXT) + 1))[:size]
    if kind == "random":
        return rng.randbytes(size)
    if kind in ("front-random", "front-text", "buried-text"):
        repeated = (TEXT * (size // len(TEXT) + 1))[:size]
        if kind == "front-text":
            return repeated[:65536] + rng.randbytes(max(0, size - 65536))
        data = bytearray(repeated)
        if kind == "front-random":
            data[:min(size, 65536)] = rng.randbytes(min(size, 65536))
        else:
            # Deliberately challenge the three-position sampler, not a typical corpus.
            for offset in (0, max(0, size // 2 - 8192), max(0, size - 16384)):
                count = min(16384, size - offset)
                data[offset:offset + count] = rng.randbytes(count)
        return bytes(data)
    if kind == "compressed":
        return gzip.compress(rng.randbytes(size), compresslevel=6, mtime=0)
    if kind == "mixed":
        parts = []
        for _ in range((size + 65535) // 65536):
            parts.extend((TEXT, rng.randbytes(32768)))
        return b"".join(parts)[:size]
    if kind != "structured":
        raise ValueError(f"Unknown payload kind: {kind}")
    records = []
    length = 0
    while length < size:
        record = {"sequence": len(records), "user": rng.randrange(10000),
                  "route": rng.choice(("/api/files", "/api/archive", "/health", "/login")),
                  "status": rng.choice((200, 200, 200, 404, 500)),
                  "latency_us": rng.randrange(1000000), "request": f"{rng.getrandbits(64):016x}",
                  "message": "request completed successfully"}
        line = (json.dumps(record, separators=(",", ":")) + "\n").encode()
        records.append(line)
        length += len(line)
    return b"".join(records)[:size]


def sample_ratio(source: Path) -> tuple[float, int, float]:
    started = time.perf_counter()
    sample = bytearray()
    budget = 65536
    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        size = path.stat().st_size
        with path.open("rb") as stream:
            positions = (0,) if size <= 16384 else (0, max(0, size // 2 - 8192), max(0, size - 16384))
            for offset in positions:
                stream.seek(offset)
                sample.extend(stream.read(min(16384, budget - len(sample))))
                if len(sample) == budget:
                    break
        if len(sample) == budget:
            break
    ratio = len(zlib.compress(bytes(sample), 1)) / max(1, len(sample))
    return ratio, len(sample), time.perf_counter() - started


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def policy_choice(input_bytes: int, files: int, ratio: float) -> tuple[str, str]:
    """Illustrative sweep-fitted rule, deliberately confined to offline evaluation."""
    if files != 1 or input_bytes < 1048576:
        return "zlib", "default"
    if ratio >= 0.99:
        return "zlib", "0"
    return "ng", "9" if ratio < 0.05 else "6" if ratio < 0.35 else "3"


def evaluate_policy(rows: list[dict], fixtures: dict) -> list[dict]:
    evaluations = []
    for key, fixture in fixtures.items():
        kind, kib = key.split("/")
        for fmt in ("tar.gz", "zip"):
            group = [r for r in rows if (r["dataset"], str(r["kib"]), r["format"]) == (kind, kib, fmt)]
            if not group:
                continue
            backend, level = policy_choice(fixture["input_bytes"], fixture["files"], fixture["sample_ratio"])
            selected = [r for r in group if (r["backend"], r["level"]) == (backend, level)]
            baseline = [r for r in group if (r["backend"], r["level"]) == ("zlib", "default")]
            if not selected or not baseline:
                raise RuntimeError(f"Policy evaluation needs {backend}/{level} and zlib/default samples")
            cost = fixture["sample_seconds"] if (backend, level) != ("zlib", "default") else 0
            size = statistics.median(int(r["archive_bytes"]) for r in selected)
            baseline_size = statistics.median(int(r["archive_bytes"]) for r in baseline)
            baseline_seconds = statistics.median(float(r["seconds"]) for r in baseline)
            seconds = statistics.median(float(r["seconds"]) for r in selected) + cost
            evaluations.append({"dataset": kind, "kib": int(kib), "format": fmt,
                                "choice": f"{backend}/{level}", "sample_seconds": cost,
                                "size_change_percent": (size / baseline_size - 1) * 100,
                                "speedup_with_sample_cost": baseline_seconds / seconds,
                                "within_one_percent": size <= baseline_size * 1.01})
    return evaluations


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", action="append", required=True, metavar="NAME=PATH")
    parser.add_argument("--reader", type=Path, default=Path("build/release/arc"))
    parser.add_argument("--ouch", type=Path)
    parser.add_argument("--ouch-threads", type=positive_int)
    parser.add_argument("--compare-ouch-threads", action="store_true",
                        help="also run the same ouch binary with one thread, interleaved")
    parser.add_argument("--ouch-levels", nargs="*", type=int, choices=range(10), default=[])
    parser.add_argument("--seed", type=int, default=1729)
    parser.add_argument("--sizes-kib", nargs="+", type=positive_int, default=[64, 1024, 16384])
    parser.add_argument("--datasets", nargs="+", choices=KINDS, default=list(DEFAULT_KINDS))
    parser.add_argument("--levels", nargs="+", type=int, choices=range(10), default=[0, 1, 3, 6, 9])
    parser.add_argument("--formats", nargs="+", choices=("tar.gz", "zip"), default=["tar.gz", "zip"])
    parser.add_argument("--small-files", type=positive_int, default=2000)
    parser.add_argument("--runs", type=positive_int, default=3)
    parser.add_argument("--evaluate-policy", action="store_true",
                        help="offline sample-based rule; requires zlib/ng builds and levels 0/3/6/9")
    parser.add_argument("--output", type=Path, default=Path("build/bench/compression/results.csv"))
    args = parser.parse_args()
    if args.compare_ouch_threads and (not args.ouch or args.ouch_threads):
        parser.error("--compare-ouch-threads needs --ouch without --ouch-threads")
    if args.ouch_levels and not args.ouch:
        parser.error("--ouch-levels needs --ouch")
    binaries = {}
    for specification in args.backend:
        name, separator, path = specification.partition("=")
        if not separator or not name or name in ("ouch", "ouch-1") or name in binaries or not Path(path).is_file():
            parser.error("--backend requires a unique NAME (other than ouch/ouch-1) and an existing PATH")
        binaries[name] = Path(path).resolve()
    if args.ouch:
        binaries["ouch"] = args.ouch.resolve()
        if args.compare_ouch_threads:
            binaries["ouch-1"] = args.ouch.resolve()
    if not args.reader.is_file() or any(not binary.is_file() for binary in binaries.values()):
        parser.error("all binaries must exist")
    if args.output.suffix != ".csv":
        parser.error("--output must end in .csv")
    if args.evaluate_policy and (not {"zlib", "ng"} <= binaries.keys() or not {0, 3, 6, 9} <= set(args.levels)):
        parser.error("--evaluate-policy requires zlib/ng builds and levels 0/3/6/9")
    reader = args.reader.resolve()
    environment = os.environ.copy()
    environment.pop("ARC_BENCH_LEVEL", None)
    variants = [(name, level) for name in binaries for level in
                ["default", *map(str, args.ouch_levels if name in ("ouch", "ouch-1") else args.levels)]]
    metadata = {"fixture_version": 1, "seed": args.seed, "sizes_kib": args.sizes_kib,
                "datasets": args.datasets, "levels": args.levels, "formats": args.formats,
                "small_files": args.small_files, "runs": args.runs, "warmups": 1,
                "ouch_threads": args.ouch_threads, "ouch_levels": args.ouch_levels,
                "compare_ouch_threads": args.compare_ouch_threads,
                "platform": platform.platform(), "timezone": environment.get("TZ"),
                "python_zlib": zlib.ZLIB_RUNTIME_VERSION,
                "binaries": {name: str(path) for name, path in binaries.items()},
                "binary_sha256": {name: digest(path) for name, path in binaries.items()},
                "versions": {name: subprocess.check_output([str(path), "--version"], text=True)
                             for name, path in binaries.items()},
                "reader_sha256": digest(reader), "fixtures": {}}
    rows = []
    with tempfile.TemporaryDirectory(prefix="arc-compression-") as temporary:
        root = Path(temporary).resolve()
        sources = []
        for kind in args.datasets:
            if kind == "small":
                source = generate_fixtures(root / "small", args.seed, 1, args.small_files)[0]
                sources.append((kind, 0, source))
                continue
            for kib in args.sizes_kib:
                source = root / f"{kind}-{kib}"
                source.mkdir()
                (source / "payload.bin").write_bytes(payload(kind, kib * 1024, args.seed))
                for path in (source / "payload.bin", source):
                    os.utime(path, ns=(1700000000000000000, 1700000000000000000))
                sources.append((kind, kib, source))
        for kind, kib, source in sources:
            expected = file_manifest(source)
            input_bytes = sum(path.stat().st_size for path in source.rglob("*") if path.is_file())
            ratio, sample_bytes, sample_seconds = sample_ratio(source)
            metadata["fixtures"][f"{kind}/{kib}"] = {
                "input_bytes": input_bytes, "files": len(expected),
                "manifest_sha256": hashlib.sha256(json.dumps(expected, sort_keys=True).encode()).hexdigest(),
                "sample_ratio": ratio, "sample_bytes": sample_bytes, "sample_seconds": sample_seconds}
            for fmt in args.formats:
                # Ouch 0.8.3's ZIP writer does not receive its CLI compression level.
                format_variants = [item for item in variants
                                   if fmt != "zip" or item[0] not in ("ouch", "ouch-1") or item[1] == "default"]
                samples = {(name, level): [] for name, level in format_variants}
                for iteration in range(args.runs + 1):
                    order = format_variants.copy()
                    random.Random(args.seed + iteration).shuffle(order)
                    for name, level in order:
                        is_ouch = name in ("ouch", "ouch-1")
                        archive = root / f"output.{fmt}"
                        child_environment = environment.copy()
                        if level != "default" and not is_ouch:
                            child_environment["ARC_BENCH_LEVEL"] = level
                        command = ([str(binaries[name]), "--quiet", "compress",
                                    *[path.name for path in sorted(source.iterdir())], str(archive)]
                                   if is_ouch else
                                   [str(binaries[name]), "pack", str(archive), str(source)])
                        if is_ouch:
                            threads = 1 if name == "ouch-1" else args.ouch_threads
                            if threads:
                                command.extend(["--threads", str(threads)])
                            if level != "default":
                                command.extend(["--level", level])
                        seconds = timed_run(command, source, child_environment)
                        size = archive.stat().st_size
                        if iteration == 0 and fmt == "zip" and not is_ouch:
                            with zipfile.ZipFile(archive) as stream:
                                method = zipfile.ZIP_STORED if level == "0" else zipfile.ZIP_DEFLATED
                                if not all(entry.compress_type == method for entry in stream.infolist()
                                           if not entry.is_dir()):
                                    raise RuntimeError("Compression probe level was ignored")
                        destination = root / "restored"
                        timed_run([str(reader), "unpack", str(archive), str(destination)], root, environment)
                        if file_manifest(destination) != expected:
                            raise RuntimeError(f"Content mismatch: {kind}/{kib}/{fmt}/{name}/{level}")
                        shutil.rmtree(destination)
                        archive.unlink()
                        if iteration:
                            rows.append({"dataset": kind, "kib": kib, "format": fmt,
                                         "backend": name, "level": level, "run": iteration,
                                         "seconds": seconds, "archive_bytes": size, "input_bytes": input_bytes})
                            samples[name, level].append(seconds)
                baseline_name = next(iter(binaries))
                baseline_size = statistics.median(row["archive_bytes"] for row in rows
                    if (row["dataset"], row["kib"], row["format"], row["backend"], row["level"])
                    == (kind, kib, fmt, baseline_name, "default"))
                eligible = [(statistics.median(values), name, level) for (name, level), values in samples.items()
                            if statistics.median(row["archive_bytes"] for row in rows
                               if (row["dataset"], row["kib"], row["format"], row["backend"], row["level"])
                               == (kind, kib, fmt, name, level)) <= baseline_size * 1.01]
                fastest, name, level = min(eligible)
                print(f"{kind:10} {kib:6} KiB {fmt:6} best within +1% size: "
                      f"{name}/{level} {fastest * 1000:.2f} ms; sample ratio {ratio:.3f}", flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    if args.evaluate_policy:
        metadata["offline_policy"] = evaluate_policy(rows, metadata["fixtures"])
    args.output.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Saved {args.output}; temporary payloads removed.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error

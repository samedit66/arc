#!/usr/bin/env python3
"""Run ouch's Rust 1.65.0 workloads with Arc added; keep payloads temporary."""

import argparse
import csv
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import tempfile

from bench import file_manifest, positive_int


def run(command: list[str], cwd: Path) -> None:
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"Command failed: {command}\n{result.stderr[-4000:]}")


def wall_times(result: dict) -> list[float]:
    if "measurements" in result:
        return [sample["time_wall_clock"]["value"] for sample in result["measurements"]]
    return result["times"]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arc", type=Path, default=Path("build/release/arc"))
    parser.add_argument("--ouch", type=Path, required=True)
    parser.add_argument("--hyperfine", default="hyperfine")
    parser.add_argument("--source", type=Path, help="reuse a Rust 1.65.0 checkout instead of downloading")
    parser.add_argument("--runs", type=positive_int, default=10)
    parser.add_argument("--output", type=Path, default=Path("build/bench/ouch-workload"))
    args = parser.parse_args()
    executables = {name: shutil.which(name) for name in ("tar", "zip", "unzip", "git")}
    executables.update(arc=str(args.arc.resolve()), ouch=str(args.ouch.resolve()),
                       hyperfine=shutil.which(args.hyperfine))
    if any(path is None or not Path(path).is_file() for path in executables.values()):
        parser.error("requires Arc, ouch, Hyperfine, git, tar, zip and unzip")
    executables = {name: str(Path(path).resolve()) for name, path in executables.items()}
    if args.source and not (args.source / "compiler").is_dir():
        parser.error("--source must be a Rust checkout containing compiler/")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    measurements = []
    metadata = {"platform": platform.platform(), "timezone": os.environ.get("TZ"),
                "runs": args.runs, "warmups": 4,
                "source_tag": "1.65.0", "executables": executables,
                "versions": {name: subprocess.run([path, "--version"], capture_output=True,
                             text=True).stdout for name, path in executables.items()
                             if name not in ("zip", "unzip")}}
    with tempfile.TemporaryDirectory(prefix="arc-ouch-bench-") as temporary:
        root = Path(temporary).resolve()
        source = args.source.resolve() if args.source else root / "download"
        if not args.source:
            run([executables["git"], "clone", "--branch", "1.65.0", "--depth", "1",
                 "https://github.com/rust-lang/rust", str(source)], root)
        metadata["source_commit"] = subprocess.check_output(
            [executables["git"], "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        if metadata["source_commit"] != "897e37553bba8b42751c67658967889d11ecd120":
            parser.error("source revision differs from Rust 1.65.0")
        shutil.copytree(source, root / "rust", ignore=shutil.ignore_patterns(".git"), symlinks=True)
        shutil.copytree(root / "rust" / "compiler", root / "compiler", symlinks=True)
        metadata["datasets"] = {}
        for name in ("rust", "compiler"):
            files = [path for path in (root / name).rglob("*") if path.is_file()]
            metadata["datasets"][name] = {"files": len(files), "bytes": sum(p.stat().st_size for p in files)}
        for fmt, dataset in (("tar", "rust"), ("tar.gz", "compiler"), ("zip", "compiler")):
            expected = file_manifest(root / dataset)
            archive = f"output.{fmt}"
            common = f"input.{fmt}"
            tools = ("ouch", "arc", "zip" if fmt == "zip" else "tar")
            pack_commands = {
                "ouch": [executables["ouch"], "compress", dataset, archive],
                "arc": [executables["arc"], "pack", "--keep-root", archive, dataset],
                "zip": [executables["zip"], archive, "-r", dataset],
                "tar": [executables["tar"], "-cvzf" if fmt == "tar.gz" else "-cvf", archive, dataset],
            }
            unpack_commands = {
                "ouch": [executables["ouch"], "decompress", common, "--dir", "output"],
                "arc": [executables["arc"], "unpack", common, "output"],
                "zip": [executables["unzip"], common, "-d", "output"],
                "tar": [executables["tar"], "-xvz" if fmt == "tar.gz" else "-xv",
                        "-C", "output", "-f", common],
            }
            run([executables["ouch"], "compress", dataset, common, "--yes"], root)
            for operation, commands in (("pack", pack_commands), ("unpack", unpack_commands)):
                print(f"\n{fmt}: {operation} ({dataset})", flush=True)
                hyperfine = [executables["hyperfine"], "--warmup", "4", "--runs", str(args.runs),
                             "--shell", "default", "--export-json", str(output / f"{fmt}-{operation}.json"),
                             "--export-markdown", str(output / f"{fmt}-{operation}.md")]
                for tool in tools:
                    prepare = f"rm -f {shlex.quote(archive)}" if operation == "pack" else "rm -rf output"
                    if operation == "unpack" and tool == "tar":
                        prepare += "; mkdir output"
                    hyperfine += ["--prepare", prepare, "--command-name", tool, shlex.join(commands[tool])]
                subprocess.run(hyperfine, cwd=root, check=True)
                results = json.loads((output / f"{fmt}-{operation}.json").read_text())["results"]
                # Validate each participant separately, outside Hyperfine timings.
                for index, tool in enumerate(tools):
                    (root / archive).unlink(missing_ok=True)
                    shutil.rmtree(root / "output", ignore_errors=True)
                    size = (root / common).stat().st_size
                    if operation == "pack":
                        run(commands[tool], root)
                        size = (root / archive).stat().st_size
                        run([executables["arc"], "unpack", archive, "output"], root)
                    else:
                        if tool == "tar":
                            (root / "output").mkdir()
                        run(commands[tool], root)
                    if file_manifest(root / "output" / dataset) != expected:
                        raise RuntimeError(f"Content mismatch: {fmt}/{operation}/{tool}")
                    rows.append({"format": fmt, "operation": operation, "tool": tool,
                                 "archive_bytes": size})
                    for iteration, seconds in enumerate(wall_times(results[index]), start=1):
                        samples = results[index].get("measurements", [])
                        memory = (samples[iteration - 1].get("memory_peak_resident", {}).get("value")
                                  if samples else None)
                        measurements.append({"format": fmt, "operation": operation, "tool": tool,
                                             "run": iteration, "seconds": seconds, "archive_bytes": size,
                                             "memory_peak_resident_bytes": memory})
    with (output / "sizes.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    with (output / "results.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(measurements[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(measurements)
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Reports saved in {output}; temporary payloads removed.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error

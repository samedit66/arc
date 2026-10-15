#!/usr/bin/env python3
"""Render benchmark CSV reports as standalone SVG and PNG charts (matplotlib)."""

import argparse
import csv
from pathlib import Path
import statistics

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


COLORS = {"arc": "#2563eb", "ouch": "#f59e0b", "tar": "#64748b", "zip": "#64748b",
          "before": "#64748b", "after": "#2563eb", "experimental": "#0ea5e9"}


def draw_bars(axis, labels: list[str], values: list[float], tools: tuple[str, ...],
              unit: str, spread: list[float] | None = None) -> None:
    axis.barh(labels, values, color=[COLORS[tool] for tool in tools],
              xerr=spread, height=0.55, capsize=3)
    axis.invert_yaxis()
    maximum = max(value + (spread[index] if spread else 0) for index, value in enumerate(values))
    axis.set_xlim(0, maximum * 1.38)
    for index, value in enumerate(values):
        offset = spread[index] if spread else 0
        precision = 1 if unit == "milliseconds" else 2
        axis.text(value + offset + maximum * 0.025, index, f"{value:.{precision}f}", va="center", fontsize=9)
    axis.set_xlabel(unit, fontsize=9)
    axis.grid(axis="x", alpha=0.18)
    axis.set_axisbelow(True)
    axis.spines[["top", "right", "left"]].set_visible(False)
    axis.tick_params(axis="y", length=0)


def save_chart(figure, output: Path, name: str) -> None:
    # Freeze the Agg layout before exporting through SVG's different font metrics.
    figure.canvas.draw()
    figure.set_layout_engine("none")
    figure.savefig(output / f"{name}.svg", bbox_inches="tight")
    svg = output / f"{name}.svg"
    svg.write_text("\n".join(line.rstrip() for line in svg.read_text().splitlines()) + "\n")
    figure.savefig(output / f"{name}.png", dpi=150, bbox_inches="tight")
    plt.close(figure)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--synthetic", type=Path, default=Path("docs/benchmark-results.csv"))
    parser.add_argument("--rust", type=Path, default=Path("docs/benchmark-ouch-results.csv"))
    parser.add_argument("--output", type=Path, default=Path("docs/assets/benchmark"))
    parser.add_argument("--zip-experiment", type=Path, help="render only the ZIP experiment summary")
    parser.add_argument("--tar-experiment", type=Path, help="render only the TAR experiment summary")
    parser.add_argument("--io-experiment", type=Path, help="render only the I/O experiment summary")
    parser.add_argument("--memory-experiment", type=Path,
                        default=Path("docs/benchmark-memory-results.csv"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10,
                         "svg.fonttype": "none", "axes.titleweight": "bold"})
    if args.io_experiment:
        with args.io_experiment.open(newline="") as stream:
            samples = list(csv.DictReader(stream))
        figure, axes = plt.subplots(2, 2, figsize=(11, 7), layout="constrained")
        figure.suptitle("I/O experiments · macOS ARM64\nBefore vs retained changes · median time", fontsize=16)
        cases = (("small", "tar", "pack", "10,000 files / TAR packing"),
                 ("small", "tar.gz", "pack", "10,000 files / TAR.GZ packing"),
                 ("text", "tar", "unpack", "128 MiB text / TAR extraction"),
                 ("binary", "tar", "unpack", "128 MiB binary / TAR extraction"))
        for axis, (dataset, fmt, operation, title) in zip(axes.flat, cases):
            tools = ("before", "after")
            experiment = "many-files" if dataset == "small" else "large-repeat"
            values = [statistics.median(float(row["seconds"]) * 1000 for row in samples
                      if (row["dataset"], row["format"], row["operation"], row["tool"])
                      == (dataset, fmt, operation, tool) and row["experiment"] == experiment)
                      for tool in tools]
            draw_bars(axis, ["Before", "After"], values, tools, "milliseconds")
            axis.set_title(title)
        save_chart(figure, args.output, "io-experiment")
        print(f"I/O experiment summary saved in {args.output}.")
        return
    if args.tar_experiment:
        with args.tar_experiment.open(newline="") as stream:
            samples = list(csv.DictReader(stream))
        figure, axes = plt.subplots(2, 2, figsize=(11, 7), layout="constrained")
        figure.suptitle("TAR packing experiments · macOS ARM64\nMedian of 5 runs · lower is better", fontsize=16)
        tools = ("before", "arc", "ouch")
        for axis, dataset, metric in zip(axes.flat, ("integer", "integer", "source", "source"),
                                         ("seconds", "archive_bytes", "seconds", "archive_bytes")):
            scale = 1000 if metric == "seconds" else 1 / 1048576
            values = [statistics.median(float(row[metric]) * scale for row in samples
                      if (row["dataset"], row["format"], row["tool"]) == (dataset, "tar", tool))
                      for tool in tools]
            draw_bars(axis, ["Arc before", "Arc after", "ouch"], values, tools,
                      "milliseconds" if metric == "seconds" else "MiB")
            axis.set_title(("2,000 files / integer mtime" if dataset == "integer" else
                            "Rust source / fractional mtime") + (" / time" if metric == "seconds" else " / size"))
        save_chart(figure, args.output, "tar-experiment")
        print(f"TAR experiment summary saved in {args.output}.")
        return
    if args.zip_experiment:
        with args.zip_experiment.open(newline="") as stream:
            experiments = list(csv.DictReader(stream))
        with args.memory_experiment.open(newline="") as stream:
            memory = list(csv.DictReader(stream))
        figure, axes = plt.subplots(2, 2, figsize=(11, 7), layout="constrained")
        figure.suptitle("Performance experiments · macOS ARM64\nMedians · lower is better", fontsize=16)
        for axis, environment in zip(axes[0], ("default", "utc")):
            tools = ("arc", "ouch")
            values = [statistics.median(float(row["seconds"]) * 1000 for row in experiments
                      if (row["environment"], row["dataset"], row["operation"], row["tool"])
                      == (environment, "small", "unpack", tool)) for tool in tools]
            draw_bars(axis, ["Arc", "ouch"], values, tools, "milliseconds")
            axis.set_title("2,000-file ZIP extraction / " + ("native timezone" if environment == "default" else "TZ=UTC0"))
        tools = ("arc", "experimental", "ouch")
        values = [statistics.median(float(row["seconds"]) * 1000 for row in experiments
                  if (row["environment"], row["dataset"], row["operation"], row["tool"])
                  == ("default", "text", "pack", tool)) for tool in tools]
        draw_bars(axes[1, 0], ["Arc", "Arc + zlib-ng", "ouch"], values, tools, "milliseconds")
        axes[1, 0].set_title("16 MiB repeated text / ZIP packing")
        tools = ("before", "after", "ouch")
        values = [statistics.median(float(row["maximum_resident_bytes"]) / 1048576
                  for row in memory if row["tool"] == tool) for tool in tools]
        draw_bars(axes[1, 1], ["Arc before", "Arc after", "ouch"], values, tools, "MiB")
        axes[1, 1].set_title("20,000-file TAR extraction / peak RSS")
        save_chart(figure, args.output, "zip-experiment")
        print(f"Experiment summary saved in {args.output}.")
        return
    with args.synthetic.open(newline="") as stream:
        synthetic = list(csv.DictReader(stream))
    for operation in ("pack", "unpack"):
        figure, axes = plt.subplots(3, 3, figsize=(13, 8.5), layout="constrained")
        figure.suptitle(f"Synthetic fixtures · {operation} time\nMedian of 5 runs · lower is faster", fontsize=16)
        for row, dataset in enumerate(("small", "text", "binary")):
            for column, fmt in enumerate(("tar", "tar.gz", "zip")):
                tools = ("arc", "ouch", "zip" if fmt == "zip" else "tar")
                labels = ["unzip" if tool == "zip" and operation == "unpack" else tool for tool in tools]
                values = [statistics.median(float(sample[f"{operation}_seconds"]) * 1000
                          for sample in synthetic if (sample["dataset"], sample["format"], sample["tool"])
                          == (dataset, fmt, tool)) for tool in tools]
                draw_bars(axes[row, column], labels, values, tools, "milliseconds")
                axes[row, column].set_title(f"{dataset} / {fmt}")
        save_chart(figure, args.output, f"synthetic-{operation}")
    with args.rust.open(newline="") as stream:
        rust = list(csv.DictReader(stream))
    figure, axes = plt.subplots(2, 3, figsize=(13, 6), layout="constrained")
    figure.suptitle("Ouch workloads · Rust 1.65.0\nMean of 10 runs ± standard deviation · lower is faster", fontsize=16)
    for row, operation in enumerate(("pack", "unpack")):
        for column, fmt in enumerate(("tar", "tar.gz", "zip")):
            tools = ("arc", "ouch", "zip" if fmt == "zip" else "tar")
            labels = ["unzip" if tool == "zip" and operation == "unpack" else tool for tool in tools]
            samples = [[float(sample["seconds"]) for sample in rust
                       if (sample["operation"], sample["format"], sample["tool"]) == (operation, fmt, tool)]
                       for tool in tools]
            draw_bars(axes[row, column], labels, [statistics.mean(s) for s in samples], tools,
                      "seconds", [statistics.stdev(s) for s in samples])
            axes[row, column].set_title(f"{operation} / {fmt}")
    save_chart(figure, args.output, "rust-time")
    figure, axes = plt.subplots(1, 3, figsize=(13, 3.4), layout="constrained")
    figure.suptitle("Rust archive sizes · lower is smaller\nExtraction timings use the same ouch archive for all readers", fontsize=15)
    for axis, fmt in zip(axes, ("tar", "tar.gz", "zip")):
        tools = ("arc", "ouch", "zip" if fmt == "zip" else "tar")
        values = [next(int(sample["archive_bytes"]) / 1048576 for sample in rust
                  if (sample["operation"], sample["format"], sample["tool"]) == ("pack", fmt, tool))
                  for tool in tools]
        draw_bars(axis, list(tools), values, tools, "MiB")
        axis.set_title(fmt)
    save_chart(figure, args.output, "rust-size")
    print(f"Charts saved in {args.output}. Each panel uses its own zero-based linear scale.")


if __name__ == "__main__":
    main()

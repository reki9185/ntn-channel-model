#!/usr/bin/env python3
"""Plot one ``cho_comparison_all_strategies_*`` experiment directory.

The input directory is produced by ``compare_all_cho_strategies.sh``.  This
script deliberately reads only that one experiment; it never merges results
from other directories or uses hard-coded historic experiment names.

Examples:
    python3 tools/plot/plot_handover_comparison.py cho_comparison_all_strategies_20260805_120000
    python3 tools/plot/plot_handover_comparison.py cho_comparison_all_strategies_20260805_120000 \
        --output-dir result/figures/20260805
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


COLORS = {
    "SNR-CHO": "#c44e52",
    "TDCHO": "#4c72b0",
    "Distance-CHO": "#55a868",
    "Elevation-CHO": "#e7b800",
    "SD-TOPSIS-CHO": "#f59127",
    "Serving-Time-CHO": "#8172b3",
}
FALLBACK_COLORS = plt.get_cmap("tab10").colors


def as_float(value: str) -> float:
    """Convert a report cell to float, keeping unavailable values as NaN."""
    value = value.strip().replace("%", "")
    if value.lower() in {"", "n/a", "na", "nan"}:
        return float("nan")
    return float(value)


def resolve_result_file(path: Path) -> Path:
    """Accept either a comparison directory or its result.txt file."""
    if path.is_dir():
        path = path / "result.txt"
    if path.name != "result.txt":
        raise ValueError("input must be a cho_comparison_all_strategies_* directory or its result.txt")
    if not path.is_file():
        raise FileNotFoundError(f"result file not found: {path}")
    return path


def parse_result_file(result_file: Path) -> dict[str, dict[str, float]]:
    """Parse the single overall-metrics table from a comparison result file.

    Missing algorithms simply have no row and are not returned.  This makes a
    plot from a subset experiment behave exactly like a full comparison.
    """
    metrics: dict[str, dict[str, float]] = {}
    in_table = False

    for raw_line in result_file.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        if line.startswith("Algorithm") and "PDR" in line and "|" in line:
            in_table = True
            continue
        if not in_table:
            continue
        if not line or set(line) <= {"-", "=", "|"}:
            continue
        if "|" not in line:
            if line.startswith("PER-UE") or line.startswith("Raw simulation"):
                break
            continue

        cells = [cell.strip() for cell in line.split("|")]
        if len(cells) != 8 or cells[0] == "Algorithm":
            continue
        try:
            metrics[cells[0]] = {
                "pdr": as_float(cells[1]),
                "latency": as_float(cells[2]),
                "throughput": as_float(cells[3]),
                "ho_ok": as_float(cells[4]),
                "ho_fail": as_float(cells[5]),
                "mr_report": as_float(cells[6]),
                "success_rate": as_float(cells[7]),
            }
        except ValueError:
            # Ignore a malformed row instead of discarding valid algorithms.
            continue

    if not metrics:
        raise ValueError(f"no overall comparison rows found in {result_file}")
    return metrics


def label_bars(axis: plt.Axes, bars, values: list[float], suffix: str = "") -> None:
    for bar, value in zip(bars, values):
        if math.isnan(value):
            axis.text(
                bar.get_x() + bar.get_width() / 2,
                0,
                "N/A",
                ha="center",
                va="bottom",
                fontsize=9,
                color="dimgray",
            )
        else:
            axis.text(
                bar.get_x() + bar.get_width() / 2,
                bar.get_height(),
                f"{value:.2f}{suffix}",
                ha="center",
                va="bottom",
                fontsize=9,
            )


def plot_comparison(data: dict[str, dict[str, float]], output_file: Path) -> None:
    strategies = list(data)
    x = np.arange(len(strategies))
    colors = [COLORS.get(name, FALLBACK_COLORS[index % len(FALLBACK_COLORS)]) for index, name in enumerate(strategies)]
    plots = [
        ("pdr", "PDR (%)", "Packet Delivery Ratio", "%"),
        ("latency", "Latency (ms)", "Handover Interruption Latency", " ms"),
        ("throughput", "Throughput (Kbps)", "Throughput", " Kbps"),
        ("ho_ok", "Count", "Successful Handovers", ""),
        ("ho_fail", "Count", "Failed Handovers", ""),
        ("success_rate", "Success Rate (%)", "Handover Success Rate", "%"),
    ]

    figure, axes = plt.subplots(2, 3, figsize=(18, 10))
    figure.suptitle("CHO Handover Algorithm Comparison", fontsize=16, fontweight="bold")

    for axis, (key, ylabel, title, suffix) in zip(axes.flat, plots):
        values = [data[strategy][key] for strategy in strategies]
        plotted_values = [0.0 if math.isnan(value) else value for value in values]
        bars = axis.bar(x, plotted_values, color=colors, edgecolor="black", linewidth=1.0, alpha=0.9)
        label_bars(axis, bars, values, suffix)
        axis.set_xticks(x, strategies, rotation=0, ha="center")
        axis.set_ylabel(ylabel)
        axis.set_title(title, fontweight="bold")
        axis.grid(axis="y", alpha=0.3)
        axis.set_ylim(bottom=0)

    figure.tight_layout()
    output_file.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output_file, dpi=300, bbox_inches="tight")
    plt.close(figure)


def main() -> int:
    parser = argparse.ArgumentParser(description="Plot one CHO comparison experiment without merging other results.")
    parser.add_argument("input", type=Path, help="cho_comparison_all_strategies_* directory or its result.txt")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Directory for the PNG (default: tools/plot/figure)",
    )
    args = parser.parse_args()

    try:
        result_file = resolve_result_file(args.input)
        data = parse_result_file(result_file)
    except (FileNotFoundError, ValueError) as error:
        parser.error(str(error))

    output_dir = args.output_dir or Path(__file__).resolve().parent / "figure"
    output_file = output_dir / "handover_comparison.png"
    plot_comparison(data, output_file)

    print(f"Parsed {len(data)} algorithm(s) from {result_file}")
    print("Algorithms: " + ", ".join(data))
    print(f"Saved: {output_file}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

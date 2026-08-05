#!/usr/bin/env python3
"""Plot SNR against distance/elevation from explicitly supplied CSV files.

The old script referenced three deleted files beside the repository root.
Inputs are now command-line arguments, so it can plot any CSV exported by a
simulation without assuming where that experiment was stored.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


COLORS = {"Rural": "#c44e52", "Suburban": "#4c72b0", "Urban": "#55a868"}


def rolling_mean(x: np.ndarray, values: np.ndarray, window: float) -> np.ndarray:
    half_window = window / 2.0
    smoothed = np.empty_like(values, dtype=float)
    for index, point in enumerate(x):
        in_window = (x >= point - half_window) & (x <= point + half_window)
        smoothed[index] = values[in_window].mean()
    return smoothed


def load_csv(path: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    distances, elevations, snrs = [], [], []
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            distances.append(float(row["distance_km"]))
            elevations.append(float(row["elevation_deg"]))
            snrs.append(float(row["snr_dB"]))
    if not distances:
        raise ValueError(f"no records in {path}")
    return np.array(distances), np.array(elevations), np.array(snrs)


def save_figure(figure: plt.Figure, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    figure.tight_layout()
    figure.savefig(output, dpi=300, bbox_inches="tight")
    plt.close(figure)
    print(f"Saved: {output}")


def plot_distance_comparison(inputs: dict[str, Path], output: Path, window: float) -> None:
    figure, axis = plt.subplots(figsize=(12, 6))
    for label, path in inputs.items():
        distance, _, snr = load_csv(path)
        order = np.argsort(distance)
        axis.plot(distance[order], rolling_mean(distance[order], snr[order], window), label=label, color=COLORS[label], linewidth=2.2)
    axis.set(xlabel="Distance (km)", ylabel="SNR (dB)", title="SNR vs Distance")
    axis.grid(alpha=0.3)
    axis.legend()
    save_figure(figure, output)


def plot_single(path: Path, output_dir: Path, window: float) -> None:
    distance, elevation, snr = load_csv(path)

    distance_order = np.argsort(distance)
    figure, axis = plt.subplots(figsize=(12, 6))
    axis.plot(distance[distance_order], rolling_mean(distance[distance_order], snr[distance_order], window), color=COLORS["Rural"], linewidth=2.2)
    axis.set(xlabel="Distance (km)", ylabel="SNR (dB)", title="Rural SNR vs Distance")
    axis.grid(alpha=0.3)
    save_figure(figure, output_dir / "snr-vs-distance-rural.png")

    elevation_order = np.argsort(elevation)
    figure, axis = plt.subplots(figsize=(12, 6))
    axis.plot(elevation[elevation_order], rolling_mean(elevation[elevation_order], snr[elevation_order], 5.0), color=COLORS["Suburban"], linewidth=2.2)
    axis.set(xlabel="Elevation (degrees)", ylabel="SNR (dB)", title="Rural Elevation vs SNR")
    axis.grid(alpha=0.3)
    save_figure(figure, output_dir / "elevation-vs-snr-rural.png")


def main() -> int:
    parser = argparse.ArgumentParser(description="Plot SNR against distance/elevation from supplied CSV files.")
    parser.add_argument("--rural", type=Path, required=True, help="Rural SNR CSV")
    parser.add_argument("--suburban", type=Path, required=True, help="Suburban SNR CSV")
    parser.add_argument("--urban", type=Path, required=True, help="Urban SNR CSV")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path(__file__).resolve().parent / "figure",
    )
    parser.add_argument("--window-km", type=float, default=150.0)
    args = parser.parse_args()

    inputs = {"Rural": args.rural, "Suburban": args.suburban, "Urban": args.urban}
    missing = [str(path) for path in inputs.values() if not path.is_file()]
    if missing:
        parser.error("input CSV not found: " + ", ".join(missing))

    plot_distance_comparison(inputs, args.output_dir / "snr-vs-distance.png", args.window_km)
    plot_single(args.rural, args.output_dir, args.window_km)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

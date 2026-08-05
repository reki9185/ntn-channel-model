#!/usr/bin/env python3
"""Smooth a single-satellite full log into ntn-emulator/ntn_state.json.

The input is the ``*-full-log.csv`` emitted by ``sgp4-ntn-udp-example``.
It contains one record per second with PDR expressed as a percentage.  This
tool reproduces the state format used by the emulator: consecutive ten-second
windows are averaged, delays remain in milliseconds, and PDR is normalized to
the range 0.0--1.0.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
from collections import defaultdict
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = REPO_ROOT / "ntn-emulator" / "ntn_state.json"
REQUIRED_COLUMNS = {"time_s", "pdr", "ue_sat_delay_ms", "sat_cn_delay_ms"}
SATELLITE_FROM_NAME = re.compile(
    r"^(?P<satellite>.+)-(?:Rural|Suburban|Urban|DenseUrban)-full-log\.csv$"
)


def parse_float(row: dict[str, str], name: str, row_number: int) -> float:
    try:
        value = float(row[name])
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError(f"row {row_number}: invalid {name!r} value") from error
    if not math.isfinite(value):
        raise ValueError(f"row {row_number}: {name!r} must be finite")
    return value


def infer_satellite(input_path: Path) -> str:
    match = SATELLITE_FROM_NAME.match(input_path.name)
    if match:
        return match.group("satellite")
    raise ValueError(
        "cannot infer satellite from filename; pass --satellite explicitly "
        f"(input: {input_path.name})"
    )


def read_rows(input_path: Path) -> list[dict[str, float]]:
    with input_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        columns = set(reader.fieldnames or [])
        missing = REQUIRED_COLUMNS - columns
        if missing:
            raise ValueError(
                f"{input_path}: missing required column(s): {', '.join(sorted(missing))}"
            )

        rows: list[dict[str, float]] = []
        for row_number, row in enumerate(reader, start=2):
            rows.append(
                {
                    "time": parse_float(row, "time_s", row_number),
                    "pdr": parse_float(row, "pdr", row_number),
                    "delay_ue_ran": parse_float(row, "ue_sat_delay_ms", row_number),
                    "delay_ran_5g": parse_float(row, "sat_cn_delay_ms", row_number),
                }
            )

    if not rows:
        raise ValueError(f"{input_path}: no data rows")
    rows.sort(key=lambda row: row["time"])
    return rows


def mean(values: Iterable[float]) -> float:
    values = list(values)
    return sum(values) / len(values)


def smooth_rows(
    rows: list[dict[str, float]], satellite: str, window_seconds: float
) -> list[dict[str, float | str]]:
    if window_seconds <= 0:
        raise ValueError("window size must be greater than zero")

    start_time = rows[0]["time"]
    windows: dict[int, list[dict[str, float]]] = defaultdict(list)
    for row in rows:
        index = int(math.floor((row["time"] - start_time) / window_seconds))
        windows[index].append(row)

    # The emulator starts from a healthy zero-delay state.  Historical state
    # files then place the average of source [0, 10) at t=20, [10, 20) at
    # t=30, and so on, leaving one update period for delivery to the emulator.
    states: list[dict[str, float | str]] = [
        {
            "time": start_time,
            "satellite": satellite,
            "delay_ue_ran": 0.0,
            "delay_ran_5g": 0.0,
            "pdr": 1.0,
        }
    ]
    for index in sorted(windows):
        window = windows[index]
        states.append(
            {
                "time": start_time + (index + 2) * window_seconds,
                "satellite": satellite,
                # Keep the historical emulator precision: four decimal places
                # for one-way delays (ms) and six for normalized PDR.
                "delay_ue_ran": round(mean(row["delay_ue_ran"] for row in window), 4),
                "delay_ran_5g": round(mean(row["delay_ran_5g"] for row in window), 4),
                "pdr": round(
                    min(1.0, max(0.0, mean(row["pdr"] for row in window) / 100.0)),
                    6,
                ),
            }
        )
    return states


def write_json(output_path: Path, states: list[dict[str, float | str]]) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = output_path.with_suffix(output_path.suffix + ".tmp")
    with temporary_path.open("w", encoding="utf-8") as handle:
        json.dump(states, handle, indent=2)
        handle.write("\n")
    temporary_path.replace(output_path)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Average a single-satellite full CSV log into emulator state JSON."
    )
    parser.add_argument("input", type=Path, help="Path to *-full-log.csv")
    parser.add_argument(
        "--satellite",
        help="Satellite name for the JSON; inferred from the input filename when omitted",
    )
    parser.add_argument(
        "--window-seconds",
        type=float,
        default=10.0,
        help="Non-overlapping smoothing window in seconds (default: 10)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"Output JSON path (default: {DEFAULT_OUTPUT.relative_to(REPO_ROOT)})",
    )
    args = parser.parse_args()

    if not args.input.is_file():
        parser.error(f"input CSV not found: {args.input}")
    if args.window_seconds <= 0:
        parser.error("--window-seconds must be greater than zero")

    try:
        satellite = args.satellite or infer_satellite(args.input)
        rows = read_rows(args.input)
        states = smooth_rows(rows, satellite, args.window_seconds)
        write_json(args.output, states)
    except ValueError as error:
        parser.error(str(error))

    print(f"Read {len(rows)} input row(s) from {args.input}")
    print(f"Wrote {len(states)} state(s) to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

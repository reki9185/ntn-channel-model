#!/usr/bin/env python3
"""
Analyze whether serving SNR actually degrades after servingSnrTrend < -1.5 triggers.

Reads:
  result/tdcho/serving_snr_trigger_followup.csv

Outputs:
  - Summary to stdout
  - Optional summary text file
"""

import argparse
from pathlib import Path
from typing import Optional

import pandas as pd


DEFAULT_RESULT_DIR = Path(__file__).resolve().parents[2] / "result" / "tdcho"

REQUIRED_COLUMNS = [
    "time",
    "ueId",
    "servingCellId",
    "servingSnrTrend",
    "servingDistanceTrend",
    "currentSinr",
    "horizonSec",
    "verificationTime",
    "observed",
    "futureSinr",
    "sinrDelta",
    "becameWorse",
    "becameMuchWorse",
]


def pct(numerator: int, denominator: int) -> str:
    if denominator == 0:
        return f"{numerator}/{denominator} = n/a"
    return f"{numerator}/{denominator} = {numerator / denominator * 100.0:.2f}%"


def analyze(csv_path: Path, out_summary: Optional[Path] = None) -> int:
    if not csv_path.exists():
        print(f"Error: CSV not found: {csv_path}")
        return 2

    df = pd.read_csv(csv_path, na_values=["NA"])
    if df.empty:
        print("Error: CSV is empty")
        return 2

    missing = [col for col in REQUIRED_COLUMNS if col not in df.columns]
    if missing:
        print("Error: missing columns:", ", ".join(missing))
        return 2

    df["observed"] = df["observed"].astype(int)
    df["becameWorse"] = df["becameWorse"].astype(int)
    df["becameMuchWorse"] = df["becameMuchWorse"].astype(int)
    observed_df = df[df["observed"] == 1].copy()

    lines = []
    lines.append("Serving SNR Degradation Analysis")
    lines.append("================================")
    lines.append("")
    lines.append(f"Source: {csv_path}")
    lines.append("Condition analyzed: servingSnrTrend < -1.5")
    lines.append("")
    lines.append("[1] Overall coverage")
    lines.append("---")
    lines.append(f"Total follow-up rows: {len(df)}")
    lines.append(f"Observed future SNR rows: {pct(len(observed_df), len(df))}")
    lines.append("")

    lines.append("[2] By horizon")
    lines.append("---")
    for horizon in sorted(df["horizonSec"].unique()):
        h_df = df[df["horizonSec"] == horizon]
        h_obs = h_df[h_df["observed"] == 1]
        observed = len(h_obs)
        total = len(h_df)
        worse = int(h_obs["becameWorse"].sum())
        much_worse = int(h_obs["becameMuchWorse"].sum())
        mean_delta = h_obs["sinrDelta"].mean() if observed else float("nan")
        median_delta = h_obs["sinrDelta"].median() if observed else float("nan")

        lines.append(f"Horizon +{horizon:.1f}s")
        lines.append(f"  Observed: {pct(observed, total)}")
        lines.append(f"  P(SNR gets worse) = {pct(worse, observed)}")
        lines.append(f"  P(SNR drops by at least 1 dB) = {pct(much_worse, observed)}")
        if observed:
            lines.append(f"  Mean sinrDelta: {mean_delta:.3f} dB")
            lines.append(f"  Median sinrDelta: {median_delta:.3f} dB")

    lines.append("")
    lines.append("[3] Per-UE breakdown")
    lines.append("---")
    per_ue = (
        observed_df.groupby(["ueId", "horizonSec"])
        .agg(
            samples=("ueId", "size"),
            worse=("becameWorse", "sum"),
            muchWorse=("becameMuchWorse", "sum"),
            meanDelta=("sinrDelta", "mean"),
        )
        .reset_index()
        .sort_values(["ueId", "horizonSec"])
    )
    for _, row in per_ue.iterrows():
        lines.append(
            "UE {ue}, +{h:.1f}s: worse {worse}/{samples} ({worse_pct:.2f}%), "
            "drop>=1dB {mw}/{samples} ({mw_pct:.2f}%), meanDelta={delta:.3f} dB".format(
                ue=int(row["ueId"]),
                h=row["horizonSec"],
                worse=int(row["worse"]),
                mw=int(row["muchWorse"]),
                samples=int(row["samples"]),
                worse_pct=(row["worse"] / row["samples"] * 100.0),
                mw_pct=(row["muchWorse"] / row["samples"] * 100.0),
                delta=row["meanDelta"],
            )
        )

    report = "\n".join(lines)
    print(report)

    if out_summary is not None:
        out_summary.write_text(report)
        print(f"Wrote summary to: {out_summary}")

    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Analyze serving SNR degradation after servingSnrTrend trigger"
    )
    parser.add_argument(
        "--csv",
        "-c",
        default=str(DEFAULT_RESULT_DIR / "serving_snr_trigger_followup.csv"),
    )
    parser.add_argument(
        "--out",
        "-o",
        default=str(DEFAULT_RESULT_DIR / "serving_snr_degradation_summary.txt"),
    )
    args = parser.parse_args()

    raise SystemExit(analyze(Path(args.csv), Path(args.out)))

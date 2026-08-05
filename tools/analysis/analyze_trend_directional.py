#!/usr/bin/env python3

from __future__ import annotations

import argparse
import math
import re
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import pandas as pd

from analyze_prediction_validation import parse_log_line, records_to_dataframe


BIN_EDGES = [-float("inf"), -2.0, -1.0, 0.0, 1.0, float("inf")]
BIN_LABELS = ["(-inf,-2]", "(-2,-1]", "(-1,0]", "(0,1]", "(1,inf]"]
DEFAULT_THRESHOLDS = [-2.0, -1.5, -1.0, -0.5, 0.0]


def extract_seed(path: Path) -> Optional[int]:
    match = re.search(r"seed(\d+)", path.name)
    return int(match.group(1)) if match else None


def collect_strategy_logs(path_str: str, strategy: str) -> List[Path]:
    path = Path(path_str)
    if path.is_file():
        return [path] if path.name.startswith(f"{strategy}_seed") and path.suffix == ".txt" else []
    if not path.is_dir():
        return []
    return sorted(path.glob(f"{strategy}_seed*.txt"))


def parse_strategy_dataset(path_str: str, strategy: str) -> Tuple[pd.DataFrame, pd.DataFrame, List[Path]]:
    debug_records: List[Dict[str, object]] = []
    future_records: List[Dict[str, object]] = []
    files = collect_strategy_logs(path_str, strategy)

    for file_path in files:
        seed = extract_seed(file_path)
        with open(file_path, "r", encoding="utf-8", errors="replace") as handle:
            for raw_line in handle:
                kind, record = parse_log_line(raw_line)
                if kind is None or not record:
                    continue
                record["source_file"] = file_path.name
                if seed is not None:
                    record["seed"] = seed
                if kind == "debug":
                    debug_records.append(record)
                elif kind == "future":
                    future_records.append(record)

    return records_to_dataframe(debug_records), records_to_dataframe(future_records), files


def build_trend_future_dataset(path_str: str, strategy: str) -> Tuple[pd.DataFrame, List[Path]]:
    df_debug, df_future, files = parse_strategy_dataset(path_str, strategy)
    if df_debug.empty or df_future.empty:
        return pd.DataFrame(), files

    debug = df_debug.copy()
    future = df_future.copy()

    if "t" in debug.columns and "t_decision" not in debug.columns:
        debug = debug.rename(columns={"t": "t_decision"})

    needed_debug = [
        "ue",
        "t_decision",
        "serving_trend",
        "serving_sinr",
        "neighbor_sinr",
        "dynamic_margin",
        "source_file",
    ]
    if "seed" in debug.columns:
        needed_debug.insert(0, "seed")
    debug = debug[[c for c in needed_debug if c in debug.columns]].copy()

    needed_future = ["ue", "t_decision", "future_serving_sinr"]
    if "seed" in future.columns:
        needed_future.insert(0, "seed")
    future = future[[c for c in needed_future if c in future.columns]].copy()

    debug["t_decision_key"] = pd.to_numeric(debug["t_decision"], errors="coerce").round(6)
    future["t_decision_key"] = pd.to_numeric(future["t_decision"], errors="coerce").round(6)

    merge_keys = ["ue", "t_decision_key"]
    if "seed" in debug.columns and "seed" in future.columns:
        merge_keys.insert(0, "seed")

    merged = debug.merge(
        future[[c for c in [*merge_keys, "future_serving_sinr"] if c in future.columns]],
        on=merge_keys,
        how="inner",
    )

    merged["serving_trend"] = pd.to_numeric(merged["serving_trend"], errors="coerce")
    merged["current_serving_sinr"] = pd.to_numeric(merged["serving_sinr"], errors="coerce")
    merged["neighbor_sinr"] = pd.to_numeric(merged.get("neighbor_sinr"), errors="coerce")
    merged["dynamic_margin"] = pd.to_numeric(merged.get("dynamic_margin"), errors="coerce")
    merged["future_serving_sinr"] = pd.to_numeric(merged["future_serving_sinr"], errors="coerce")
    merged["future_change"] = merged["future_serving_sinr"] - merged["current_serving_sinr"]
    merged["current_gap"] = merged["neighbor_sinr"] - merged["current_serving_sinr"]

    # A3 boundary distance in dB: 0 means exactly on A3 threshold.
    merged["a3_distance"] = merged["current_gap"] - merged["dynamic_margin"]

    merged = merged.dropna(subset=["serving_trend", "current_serving_sinr", "future_serving_sinr", "future_change"]).copy()
    return merged, files


def safe_corr(df: pd.DataFrame, left: str, right: str) -> float:
    if left not in df.columns or right not in df.columns:
        return float("nan")
    subset = df[[left, right]].dropna()
    if len(subset) < 2:
        return float("nan")
    return float(subset.corr().iloc[0, 1])


def safe_rate(mask: pd.Series) -> float:
    values = pd.Series(mask).dropna()
    if values.empty:
        return float("nan")
    return float(values.mean())


def fmt_pct(value: float) -> str:
    return "nan" if math.isnan(value) else f"{value * 100:.2f}%"


def fmt_float(value: float, digits: int = 4) -> str:
    return "nan" if math.isnan(value) else f"{value:.{digits}f}"


def directional_accuracy(df: pd.DataFrame) -> Dict[str, float]:
    neg = df[df["serving_trend"] < 0]
    pos = df[df["serving_trend"] > 0]

    return {
        "n_neg": float(len(neg)),
        "n_pos": float(len(pos)),
        "p_worse_given_neg": safe_rate(neg["future_change"] < 0),
        "p_better_given_pos": safe_rate(pos["future_change"] > 0),
    }


def trend_bin_analysis(df: pd.DataFrame) -> pd.DataFrame:
    work = df.copy()
    work["trend_bin"] = pd.cut(work["serving_trend"], bins=BIN_EDGES, labels=BIN_LABELS, include_lowest=True)

    summary = (
        work.groupby("trend_bin", observed=False)
        .agg(
            count=("future_change", "size"),
            avg_future_change=("future_change", "mean"),
            p_future_worse=("future_change", lambda s: float((pd.to_numeric(s, errors="coerce") < 0).mean())),
        )
        .reset_index()
    )
    return summary


def split_correlations(df: pd.DataFrame) -> Dict[str, float]:
    neg = df[df["serving_trend"] < 0]
    pos = df[df["serving_trend"] > 0]
    all_df = df[df["serving_trend"].notna()]

    return {
        "corr_all": safe_corr(all_df, "serving_trend", "future_change"),
        "corr_neg": safe_corr(neg, "serving_trend", "future_change"),
        "corr_pos": safe_corr(pos, "serving_trend", "future_change"),
        "n_neg": float(len(neg)),
        "n_pos": float(len(pos)),
    }


def classification_metrics(df: pd.DataFrame, thresholds: List[float]) -> pd.DataFrame:
    rows: List[Dict[str, float]] = []
    label = df["future_change"] < 0
    label_count = int(label.sum())

    for threshold in thresholds:
        predict = df["serving_trend"] < threshold
        predict_count = int(predict.sum())

        if predict_count > 0:
            precision = float(label[predict].mean())
        else:
            precision = float("nan")

        if label_count > 0:
            recall = float(predict[label].mean())
        else:
            recall = float("nan")

        rows.append(
            {
                "threshold": threshold,
                "predicted_degradation": predict_count,
                "actual_degradation": label_count,
                "precision": precision,
                "recall": recall,
            }
        )

    return pd.DataFrame(rows)


def near_a3_boundary_metrics(df: pd.DataFrame, boundary_db: float) -> Dict[str, float]:
    trend_neg = df["serving_trend"] < 0

    if "a3_distance" in df.columns and df["a3_distance"].notna().any():
        near_boundary = df["a3_distance"].abs() <= boundary_db
        boundary_def = "abs((neighbor_sinr - serving_sinr) - dynamic_margin) <= boundary_db"
    else:
        near_boundary = df["current_gap"].abs() <= boundary_db
        boundary_def = "abs(neighbor_sinr - serving_sinr) <= boundary_db (fallback: dynamic_margin unavailable)"

    subset = df[trend_neg & near_boundary]

    return {
        "boundary_db": boundary_db,
        "count": float(len(subset)),
        "p_future_worse": safe_rate(subset["future_change"] < 0),
        "boundary_def": boundary_def,
    }


def print_report(
    df: pd.DataFrame,
    strategy: str,
    source_path: str,
    files: List[Path],
    thresholds: List[float],
    drop_zero_trend: bool,
    a3_boundary_db: float,
) -> None:
    print("=" * 88)
    print("Trend Directional Reliability Analysis")
    print("=" * 88)
    print(f"Source path: {source_path}")
    print(f"Strategy: {strategy}")
    print(f"Matched seed files: {len(files)}")

    if files:
        seeds = sorted(s for s in (extract_seed(p) for p in files) if s is not None)
        if seeds:
            print(f"Seed IDs: {', '.join(str(s) for s in seeds)}")

    print(f"Rows after merge (HO_DEBUG + FUTURE_DEBUG): {len(df)}")
    print(f"drop_zero_trend: {drop_zero_trend}")
    print()

    if df.empty:
        print("No valid merged rows found; cannot run analysis.")
        return

    # Step 1
    da = directional_accuracy(df)
    print("[Step 1] Direction Accuracy")
    print(f"P(future_change < 0 | trend < 0): {fmt_pct(da['p_worse_given_neg'])}  (n={int(da['n_neg'])})")
    print(f"P(future_change > 0 | trend > 0): {fmt_pct(da['p_better_given_pos'])}  (n={int(da['n_pos'])})")
    print()

    # Extra requested metric: near A3 boundary
    near_a3 = near_a3_boundary_metrics(df, a3_boundary_db)
    print("[Step 1.5] Near A3 Boundary (Requested)")
    print(f"Boundary definition: {near_a3['boundary_def']}")
    print(
        "P(future worse | trend < 0 AND near A3 boundary): "
        f"{fmt_pct(near_a3['p_future_worse'])}  "
        f"(n={int(near_a3['count'])}, boundary_db={near_a3['boundary_db']:.2f})"
    )
    print()

    # Step 2
    print("[Step 2] Strength Analysis by Trend Bin")
    bin_df = trend_bin_analysis(df)
    if bin_df.empty:
        print("No data for bin analysis")
    else:
        print(bin_df.to_string(index=False))
    print()

    # Step 3
    corr = split_correlations(df)
    print("[Step 3] Split Correlations")
    print(f"corr_all (trend vs future_change): {fmt_float(corr['corr_all'], 6)}")
    print(f"corr_neg (trend<0): {fmt_float(corr['corr_neg'], 6)}  (n={int(corr['n_neg'])})")
    print(f"corr_pos (trend>0): {fmt_float(corr['corr_pos'], 6)}  (n={int(corr['n_pos'])})")
    print()

    # Step 4
    print("[Step 4] Classification as Degradation Detector")
    cls_df = classification_metrics(df, thresholds)
    if cls_df.empty:
        print("No data for classification metrics")
    else:
        print(cls_df.to_string(index=False))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Analyze serving_trend reliability for future serving SINR change on a target strategy.",
    )
    parser.add_argument("path", help="Folder containing strategy seed logs, or a single seed file")
    parser.add_argument(
        "--strategy",
        default="SNR-CHO-Multi-Test3",
        help="Strategy prefix to analyze (default: SNR-CHO-Multi-Test3)",
    )
    parser.add_argument(
        "--thresholds",
        default=",".join(str(x) for x in DEFAULT_THRESHOLDS),
        help="Comma-separated trend thresholds for classification, e.g. -2,-1.5,-1,-0.5,0",
    )
    parser.add_argument(
        "--drop-zero-trend",
        action="store_true",
        help="Drop rows with serving_trend == 0 before analysis",
    )
    parser.add_argument(
        "--a3-boundary-db",
        type=float,
        default=0.5,
        help="Near-boundary window in dB for A3 boundary condition (default: 0.5)",
    )
    args = parser.parse_args()

    try:
        thresholds = [float(x.strip()) for x in args.thresholds.split(",") if x.strip()]
    except ValueError:
        raise SystemExit("Invalid --thresholds value; expected comma-separated floats")

    df, files = build_trend_future_dataset(args.path, args.strategy)
    if args.drop_zero_trend and not df.empty:
        df = df[pd.to_numeric(df["serving_trend"], errors="coerce") != 0].copy()

    print_report(
        df=df,
        strategy=args.strategy,
        source_path=args.path,
        files=files,
        thresholds=thresholds,
        drop_zero_trend=args.drop_zero_trend,
        a3_boundary_db=args.a3_boundary_db,
    )


if __name__ == "__main__":
    main()

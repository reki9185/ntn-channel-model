#!/usr/bin/env python3

from __future__ import annotations

import argparse
import math
import re
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import pandas as pd


KNOWN_PREFIXES = {
    "HO_DEBUG": "debug",
    "HO_RESULT": "result",
    "TREND_DEBUG": "trend",
    "HO_SKIPPED": "skipped",
}

LINE_RE = re.compile(r"^\[(?P<tag>[A-Z_]+)\]\s*,?\s*(?P<body>.*)$")
INT_FIELDS = {"ue", "a3", "prediction"}
FLOAT_FIELDS = {
    "t",
    "decision_delay",
    "dynamic_margin",
    "sinr_gain",
    "prev_snr",
    "curr_snr",
}
FLOAT_PATTERNS = (
    re.compile(r".*_sinr$"),
    re.compile(r".*_trend$"),
    re.compile(r".*_dist$"),
    re.compile(r"^pred_.*$"),
)
DEFAULT_TREND_BINS = [-float("inf"), -2.0, -1.0, 0.0, float("inf")]
DEFAULT_TREND_LABELS = ["<= -2", "(-2, -1]", "(-1, 0]", "> 0"]
TRIGGER_GROUPS = ["both_trigger", "suppressed", "prediction_only"]
TRIGGER_SOURCE_GROUPS = ["a3_only", "prediction_only_trigger", "a3_and_prediction"]


def _coerce_value(key: str, value: str):
    value = value.strip()
    if value == "":
        return None

    if key in INT_FIELDS:
        try:
            return int(float(value))
        except ValueError:
            return value

    if key in FLOAT_FIELDS or any(pattern.match(key) for pattern in FLOAT_PATTERNS):
        try:
            return float(value)
        except ValueError:
            return value

    lowered = value.lower()
    if lowered in {"true", "false"}:
        return lowered == "true"

    try:
        numeric = float(value)
    except ValueError:
        return value

    if math.isfinite(numeric) and numeric.is_integer() and re.fullmatch(r"[+-]?\d+", value):
        return int(numeric)
    return numeric


def parse_key_value_body(body: str) -> Dict[str, object]:
    record: Dict[str, object] = {}
    for part in body.split(","):
        part = part.strip()
        if not part or "=" not in part:
            continue
        key, raw_value = part.split("=", 1)
        key = key.strip()
        if not key:
            continue
        record[key] = _coerce_value(key, raw_value)
    return record


def parse_log_line(line: str) -> Tuple[Optional[str], Optional[Dict[str, object]]]:
    match = LINE_RE.match(line.strip())
    if not match:
        return None, None

    tag = match.group("tag")
    kind = KNOWN_PREFIXES.get(tag)
    if kind is None:
        return None, None

    try:
        record = parse_key_value_body(match.group("body"))
    except Exception:
        return None, None

    if not record:
        return kind, {}
    return kind, record


def records_to_dataframe(records: List[Dict[str, object]]) -> pd.DataFrame:
    if not records:
        return pd.DataFrame()

    df = pd.DataFrame.from_records(records)

    if "ue" in df.columns:
        df["ue"] = pd.to_numeric(df["ue"], errors="coerce").astype("Int64")
    if "a3" in df.columns:
        df["a3"] = pd.to_numeric(df["a3"], errors="coerce").astype("Int64")
    if "prediction" in df.columns:
        df["prediction"] = pd.to_numeric(df["prediction"], errors="coerce").astype("Int64")
    if "t" in df.columns:
        df["t"] = pd.to_numeric(df["t"], errors="coerce")

    for column in df.columns:
        if column in {"ue", "a3", "prediction", "t"}:
            continue
        if column in FLOAT_FIELDS or any(pattern.match(column) for pattern in FLOAT_PATTERNS):
            df[column] = pd.to_numeric(df[column], errors="coerce")

    return df


def _numeric_series(df: pd.DataFrame, column: str) -> pd.Series:
    if column not in df.columns:
        return pd.Series(dtype="float64")
    return pd.to_numeric(df[column], errors="coerce").dropna()


def _safe_corr(df: pd.DataFrame, x_col: str, y_col: str) -> float:
    if not {x_col, y_col}.issubset(df.columns):
        return float("nan")
    pair = df[[x_col, y_col]].apply(pd.to_numeric, errors="coerce").dropna()
    if len(pair) < 2:
        return float("nan")
    return float(pair[x_col].corr(pair[y_col]))


def _print_section(title: str) -> None:
    print(f"\n[{title}]")


def _extract_seed(path: Path) -> Optional[int]:
    match = re.search(r"seed(\d+)", path.name)
    return int(match.group(1)) if match else None


def _collect_log_files(path_str: str) -> Dict[int, Path]:
    path = Path(path_str)
    files: Dict[int, Path] = {}

    if path.is_file():
        seed = _extract_seed(path)
        if seed is not None:
            files[seed] = path
        return files

    if not path.is_dir():
        return files

    for file_path in sorted(path.glob("*.txt")):
        if file_path.name.startswith("_") or file_path.name == "result.txt":
            continue
        seed = _extract_seed(file_path)
        if seed is not None:
            files[seed] = file_path
    return files


def summarize_ho_quality(df_result: pd.DataFrame) -> Dict[str, float]:
    sinr_gain = _numeric_series(df_result, "sinr_gain")
    total = int(len(sinr_gain))
    good = int((sinr_gain > 0).sum()) if total else 0
    bad = int((sinr_gain <= 0).sum()) if total else 0
    avg_gain = float(sinr_gain.mean()) if total else float("nan")

    return {
        "total_ho_count": total,
        "average_sinr_gain": avg_gain,
        "good_ho_ratio": (good / total) if total else float("nan"),
        "bad_ho_ratio": (bad / total) if total else float("nan"),
    }


def analyze_trend_effectiveness(
    df_ho: pd.DataFrame,
    bins: Optional[List[float]] = None,
    labels: Optional[List[str]] = None,
) -> Tuple[pd.DataFrame, float]:
    if df_ho.empty or not {"serving_trend", "sinr_gain"}.issubset(df_ho.columns):
        return pd.DataFrame(), float("nan")

    bins = bins or DEFAULT_TREND_BINS
    labels = labels or DEFAULT_TREND_LABELS

    trend_df = df_ho[["serving_trend", "sinr_gain"]].copy()
    trend_df["serving_trend"] = pd.to_numeric(trend_df["serving_trend"], errors="coerce")
    trend_df["sinr_gain"] = pd.to_numeric(trend_df["sinr_gain"], errors="coerce")
    trend_df = trend_df.dropna()

    if trend_df.empty:
        return pd.DataFrame(), float("nan")

    trend_df["trend_bin"] = pd.cut(
        trend_df["serving_trend"],
        bins=bins,
        labels=labels,
        include_lowest=True,
    )

    stats = (
        trend_df.groupby("trend_bin", observed=False)
        .agg(
            count=("sinr_gain", "size"),
            avg_sinr_gain=("sinr_gain", "mean"),
            good_ho_probability=("sinr_gain", lambda s: (s > 0).mean()),
        )
        .reset_index()
    )

    correlation = float(trend_df["serving_trend"].corr(trend_df["sinr_gain"])) if len(trend_df) >= 2 else float("nan")
    return stats, correlation


def analyze_trigger_types(df_ho: pd.DataFrame) -> pd.DataFrame:
    if df_ho.empty or "trigger_type" not in df_ho.columns or "sinr_gain" not in df_ho.columns:
        return pd.DataFrame()

    trigger_df = df_ho[["trigger_type", "sinr_gain"]].copy()
    trigger_df["sinr_gain"] = pd.to_numeric(trigger_df["sinr_gain"], errors="coerce")
    trigger_df["trigger_type"] = trigger_df["trigger_type"].astype("string")
    trigger_df = trigger_df.dropna(subset=["trigger_type", "sinr_gain"])

    if trigger_df.empty:
        return pd.DataFrame()

    return (
        trigger_df.groupby("trigger_type", dropna=False)
        .agg(
            count=("sinr_gain", "size"),
            avg_sinr_gain=("sinr_gain", "mean"),
            good_ho_ratio=("sinr_gain", lambda s: (s > 0).mean()),
        )
        .reset_index()
    )


def analyze_decision_delay(df_result: pd.DataFrame) -> Dict[str, float]:
    return {
        "decision_delay_sinr_gain_corr": _safe_corr(df_result, "decision_delay", "sinr_gain"),
    }


def analyze_ho_suppression(df_skipped: pd.DataFrame, df_ho: pd.DataFrame) -> Dict[str, object]:
    empty_executed_dist = pd.DataFrame(columns=["trend_bin", "executed_ratio"])
    empty_skipped_dist = pd.DataFrame(columns=["trend_bin", "skipped_ratio"])

    executed = pd.DataFrame()
    if not df_ho.empty and {"serving_sinr", "serving_trend"}.issubset(df_ho.columns):
        executed = df_ho[["serving_sinr", "serving_trend"]].copy()

    skipped = pd.DataFrame()
    if not df_skipped.empty:
        available_cols = [col for col in ["serving_sinr", "serving_trend"] if col in df_skipped.columns]
        if available_cols:
            skipped = df_skipped[available_cols].copy()

    result: Dict[str, object] = {
        "executed_count": int(len(executed)) if not executed.empty else 0,
        "skipped_count": int(len(skipped)) if not skipped.empty else 0,
        "avg_serving_sinr_executed": float(_numeric_series(executed, "serving_sinr").mean()) if not executed.empty else float("nan"),
        "avg_serving_sinr_skipped": float(_numeric_series(skipped, "serving_sinr").mean()) if not skipped.empty else float("nan"),
    }

    if not executed.empty and "serving_trend" in executed.columns:
        executed_trend = pd.cut(
            pd.to_numeric(executed["serving_trend"], errors="coerce"),
            bins=DEFAULT_TREND_BINS,
            labels=DEFAULT_TREND_LABELS,
            include_lowest=True,
        )
        result["executed_trend_distribution"] = executed_trend.value_counts(normalize=True, sort=False).rename("executed_ratio").reset_index()
        result["executed_trend_distribution"].columns = ["trend_bin", "executed_ratio"]
    else:
        result["executed_trend_distribution"] = empty_executed_dist.copy()

    if not skipped.empty and "serving_trend" in skipped.columns:
        skipped_trend = pd.cut(
            pd.to_numeric(skipped["serving_trend"], errors="coerce"),
            bins=DEFAULT_TREND_BINS,
            labels=DEFAULT_TREND_LABELS,
            include_lowest=True,
        )
        result["skipped_trend_distribution"] = skipped_trend.value_counts(normalize=True, sort=False).rename("skipped_ratio").reset_index()
        result["skipped_trend_distribution"].columns = ["trend_bin", "skipped_ratio"]
    else:
        result["skipped_trend_distribution"] = empty_skipped_dist.copy()

    if not result["executed_trend_distribution"].empty or not result["skipped_trend_distribution"].empty:
        result["trend_distribution_comparison"] = pd.merge(
            result["executed_trend_distribution"],
            result["skipped_trend_distribution"],
            on="trend_bin",
            how="outer",
        )
        for col in ["executed_ratio", "skipped_ratio"]:
            if col in result["trend_distribution_comparison"].columns:
                result["trend_distribution_comparison"][col] = result["trend_distribution_comparison"][col].fillna(0.0)
    else:
        result["trend_distribution_comparison"] = pd.DataFrame()

    return result


def _merge_debug_with_ho(
    df_debug: pd.DataFrame,
    df_ho: pd.DataFrame,
    time_tolerance: Optional[float],
    prefix: str,
) -> pd.DataFrame:
    if df_debug.empty:
        return df_debug.copy()

    base = df_debug.copy()
    if "ue" not in base.columns or "t" not in base.columns or df_ho.empty or "ue" not in df_ho.columns or "t_debug" not in df_ho.columns:
        base[f"{prefix}_sinr_gain"] = pd.NA
        base[f"{prefix}_is_good_ho"] = pd.NA
        base[f"{prefix}_status"] = pd.NA
        return base

    left = base.copy()
    left["ue"] = pd.to_numeric(left["ue"], errors="coerce")
    left["t"] = pd.to_numeric(left["t"], errors="coerce")
    left = left.dropna(subset=["ue", "t"]).sort_values(["ue", "t"])

    right = df_ho.copy()
    keep_cols = [col for col in ["ue", "t_debug", "sinr_gain", "is_good_ho", "status"] if col in right.columns]
    right = right[keep_cols].copy()
    right["ue"] = pd.to_numeric(right["ue"], errors="coerce")
    right["t_debug"] = pd.to_numeric(right["t_debug"], errors="coerce")
    right = right.dropna(subset=["ue", "t_debug"]).sort_values(["ue", "t_debug"])

    if right.empty:
        base[f"{prefix}_sinr_gain"] = pd.NA
        base[f"{prefix}_is_good_ho"] = pd.NA
        base[f"{prefix}_status"] = pd.NA
        return base

    tolerance = None if time_tolerance is None else pd.Timedelta(seconds=time_tolerance)
    left["_t_left_td"] = pd.to_timedelta(left["t"], unit="s")
    right["_t_right_td"] = pd.to_timedelta(right["t_debug"], unit="s")

    merged_groups: List[pd.DataFrame] = []
    for ue_value, left_group in left.groupby("ue", sort=False):
        right_group = right[right["ue"] == ue_value]
        if right_group.empty:
            left_group[f"{prefix}_sinr_gain"] = pd.NA
            left_group[f"{prefix}_is_good_ho"] = pd.NA
            left_group[f"{prefix}_status"] = pd.NA
            merged_groups.append(left_group)
            continue

        left_group = left_group.sort_values("_t_left_td")
        right_group = right_group.sort_values("_t_right_td").drop(columns=["ue"])
        merged_groups.append(
            pd.merge_asof(
                left_group,
                right_group,
                left_on="_t_left_td",
                right_on="_t_right_td",
                direction="nearest",
                tolerance=tolerance,
            )
        )

    merged = pd.concat(merged_groups, ignore_index=True)
    rename_map = {
        "sinr_gain": f"{prefix}_sinr_gain",
        "is_good_ho": f"{prefix}_is_good_ho",
        "status": f"{prefix}_status",
    }
    merged = merged.rename(columns=rename_map)
    drop_cols = [col for col in ["_t_left_td", "_t_right_td", "t_debug"] if col in merged.columns]
    merged = merged.drop(columns=drop_cols)
    return merged


def build_trigger_comparison_frame(
    proposed_df_debug: pd.DataFrame,
    proposed_df_ho: pd.DataFrame,
    snr_df_ho: Optional[pd.DataFrame] = None,
    time_tolerance: Optional[float] = None,
) -> pd.DataFrame:
    if proposed_df_debug.empty:
        return pd.DataFrame()

    comparison_tolerance = 1.0 if time_tolerance is None else time_tolerance

    df = proposed_df_debug.copy()
    df["serving_sinr"] = pd.to_numeric(df.get("serving_sinr"), errors="coerce")
    df["neighbor_sinr"] = pd.to_numeric(df.get("neighbor_sinr"), errors="coerce")
    df["a3"] = pd.to_numeric(df.get("a3", 0), errors="coerce").fillna(0).astype("Int64")
    df["prediction"] = pd.to_numeric(df.get("prediction", 0), errors="coerce").fillna(0).astype("Int64")
    df["snr_gap"] = df["neighbor_sinr"] - df["serving_sinr"]
    df["snr_based_trigger"] = df["snr_gap"] > 0
    df["your_trigger"] = (df["a3"].fillna(0) == 1) | (df["prediction"].fillna(0) == 1)
    df["both_trigger"] = df["snr_based_trigger"] & df["your_trigger"]
    df["suppressed"] = df["snr_based_trigger"] & (~df["your_trigger"])
    df["prediction_only"] = (~df["snr_based_trigger"]) & df["your_trigger"]
    df["a3_only"] = (df["a3"].fillna(0) == 1) & (df["prediction"].fillna(0) == 0)
    df["prediction_only_trigger"] = (df["a3"].fillna(0) == 0) & (df["prediction"].fillna(0) == 1)
    df["a3_and_prediction"] = (df["a3"].fillna(0) == 1) & (df["prediction"].fillna(0) == 1)
    df["comparison_group"] = pd.NA
    df.loc[df["both_trigger"], "comparison_group"] = "both_trigger"
    df.loc[df["suppressed"], "comparison_group"] = "suppressed"
    df.loc[df["prediction_only"], "comparison_group"] = "prediction_only"
    df["trigger_source"] = pd.NA
    df.loc[df["a3_only"], "trigger_source"] = "a3_only"
    df.loc[df["prediction_only_trigger"], "trigger_source"] = "prediction_only_trigger"
    df.loc[df["a3_and_prediction"], "trigger_source"] = "a3_and_prediction"

    df = _merge_debug_with_ho(df, proposed_df_ho, time_tolerance=comparison_tolerance, prefix="proposed")
    if snr_df_ho is not None and not snr_df_ho.empty:
        df = _merge_debug_with_ho(df, snr_df_ho, time_tolerance=comparison_tolerance, prefix="snr")
    else:
        df["snr_sinr_gain"] = pd.NA
        df["snr_is_good_ho"] = pd.NA
        df["snr_status"] = pd.NA

    df["analysis_sinr_gain"] = pd.NA
    df["analysis_is_good_ho"] = pd.NA
    df["analysis_source"] = pd.NA

    use_proposed = df["comparison_group"].isin(["both_trigger", "prediction_only"])
    use_snr = df["comparison_group"].eq("suppressed")

    df.loc[use_proposed, "analysis_sinr_gain"] = df.loc[use_proposed, "proposed_sinr_gain"]
    df.loc[use_proposed, "analysis_is_good_ho"] = df.loc[use_proposed, "proposed_is_good_ho"]
    df.loc[use_proposed, "analysis_source"] = "proposed"

    df.loc[use_snr, "analysis_sinr_gain"] = df.loc[use_snr, "snr_sinr_gain"]
    df.loc[use_snr, "analysis_is_good_ho"] = df.loc[use_snr, "snr_is_good_ho"]
    df.loc[use_snr, "analysis_source"] = "snr_based"

    return df


def summarize_trigger_comparison(df_cmp: pd.DataFrame) -> pd.DataFrame:
    if df_cmp.empty or "comparison_group" not in df_cmp.columns:
        return pd.DataFrame(columns=["group", "count", "avg_snr_gap", "avg_sinr_gain", "good_ho_ratio"])

    rows = []
    for group in TRIGGER_GROUPS:
        group_df = df_cmp[df_cmp["comparison_group"] == group].copy()
        count = int(len(group_df))
        avg_snr_gap = float(pd.to_numeric(group_df.get("snr_gap"), errors="coerce").mean()) if count else float("nan")
        gain_series = pd.to_numeric(group_df.get("analysis_sinr_gain"), errors="coerce")
        gain_non_null = gain_series.dropna()
        avg_sinr_gain = float(gain_non_null.mean()) if not gain_non_null.empty else float("nan")
        good_series = pd.to_numeric(group_df.get("analysis_is_good_ho"), errors="coerce")
        good_non_null = good_series.dropna()
        good_ratio = float(good_non_null.mean()) if not good_non_null.empty else float("nan")
        rows.append(
            {
                "group": group,
                "count": count,
                "avg_snr_gap": avg_snr_gap,
                "avg_sinr_gain": avg_sinr_gain,
                "good_ho_ratio": good_ratio,
            }
        )

    return pd.DataFrame(rows)


def summarize_trigger_source(df_cmp: pd.DataFrame) -> pd.DataFrame:
    if df_cmp.empty or "trigger_source" not in df_cmp.columns:
        return pd.DataFrame(columns=["group", "count", "avg_snr_gap", "avg_sinr_gain", "good_ho_ratio"])

    rows = []
    for group in TRIGGER_SOURCE_GROUPS:
        group_df = df_cmp[df_cmp["trigger_source"] == group].copy()
        count = int(len(group_df))
        avg_snr_gap = float(pd.to_numeric(group_df.get("snr_gap"), errors="coerce").mean()) if count else float("nan")
        gain_series = pd.to_numeric(group_df.get("proposed_sinr_gain"), errors="coerce")
        gain_non_null = gain_series.dropna()
        avg_sinr_gain = float(gain_non_null.mean()) if not gain_non_null.empty else float("nan")
        good_series = pd.to_numeric(group_df.get("proposed_is_good_ho"), errors="coerce")
        good_non_null = good_series.dropna()
        good_ratio = float(good_non_null.mean()) if not good_non_null.empty else float("nan")
        rows.append(
            {
                "group": group,
                "count": count,
                "avg_snr_gap": avg_snr_gap,
                "avg_sinr_gain": avg_sinr_gain,
                "good_ho_ratio": good_ratio,
            }
        )
    return pd.DataFrame(rows)


def summarize_trigger_source_by_seed(df_cmp: pd.DataFrame) -> pd.DataFrame:
    if df_cmp.empty or "seed" not in df_cmp.columns:
        return pd.DataFrame()

    frames: List[pd.DataFrame] = []
    for seed, seed_df in df_cmp.groupby("seed", sort=True):
        summary = summarize_trigger_source(seed_df)
        if summary.empty:
            continue
        summary.insert(0, "seed", seed)
        frames.append(summary)

    if not frames:
        return pd.DataFrame()
    return pd.concat(frames, ignore_index=True)


def summarize_trigger_source_seed_average(df_cmp: pd.DataFrame) -> pd.DataFrame:
    by_seed = summarize_trigger_source_by_seed(df_cmp)
    if by_seed.empty:
        return pd.DataFrame(columns=["group", "count", "avg_snr_gap", "avg_sinr_gain", "good_ho_ratio"])

    rows = []
    for group in TRIGGER_SOURCE_GROUPS:
        group_df = by_seed[by_seed["group"] == group].copy()
        if group_df.empty:
            continue
        rows.append(
            {
                "group": group,
                "count": float(pd.to_numeric(group_df["count"], errors="coerce").mean()),
                "avg_snr_gap": float(pd.to_numeric(group_df["avg_snr_gap"], errors="coerce").mean()),
                "avg_sinr_gain": float(pd.to_numeric(group_df["avg_sinr_gain"], errors="coerce").mean()),
                "good_ho_ratio": float(pd.to_numeric(group_df["good_ho_ratio"], errors="coerce").mean()),
            }
        )

    return pd.DataFrame(rows)


def _format_trigger_summary_line(group: str, row: pd.Series) -> str:
    label_map = {
        "both_trigger": "Both trigger",
        "suppressed": "Suppressed",
        "prediction_only": "Prediction only (vs SNR-gap)",
        "a3_only": "A3 only",
        "prediction_only_trigger": "Prediction only (a3=0,prediction=1)",
        "a3_and_prediction": "A3+prediction",
    }
    label = label_map.get(group, str(group))
    avg_gap = "nan" if math.isnan(row["avg_snr_gap"]) else f"{row['avg_snr_gap']:.6f}"
    avg_gain = "nan" if math.isnan(row["avg_sinr_gain"]) else f"{row['avg_sinr_gain']:.6f}"
    good_ratio = "nan" if math.isnan(row["good_ho_ratio"]) else f"{row['good_ho_ratio']:.6f}"
    return (
        f"{label}: count={int(row['count'])}, "
        f"avg_snr_gap={avg_gap}, avg_sinr_gain={avg_gain}, good_ho_ratio={good_ratio}"
    )


def summarize_trigger_comparison_by_seed(df_cmp: pd.DataFrame) -> pd.DataFrame:
    if df_cmp.empty or "seed" not in df_cmp.columns:
        return pd.DataFrame()

    frames: List[pd.DataFrame] = []
    for seed, seed_df in df_cmp.groupby("seed", sort=True):
        summary = summarize_trigger_comparison(seed_df)
        if summary.empty:
            continue
        summary.insert(0, "seed", seed)
        frames.append(summary)

    if not frames:
        return pd.DataFrame()
    return pd.concat(frames, ignore_index=True)


def summarize_trigger_comparison_seed_average(df_cmp: pd.DataFrame) -> pd.DataFrame:
    by_seed = summarize_trigger_comparison_by_seed(df_cmp)
    if by_seed.empty:
        return pd.DataFrame(columns=["group", "count", "avg_snr_gap", "avg_sinr_gain", "good_ho_ratio"])

    rows = []
    for group in TRIGGER_GROUPS:
        group_df = by_seed[by_seed["group"] == group].copy()
        if group_df.empty:
            continue
        rows.append(
            {
                "group": group,
                "count": float(pd.to_numeric(group_df["count"], errors="coerce").mean()),
                "avg_snr_gap": float(pd.to_numeric(group_df["avg_snr_gap"], errors="coerce").mean()),
                "avg_sinr_gain": float(pd.to_numeric(group_df["avg_sinr_gain"], errors="coerce").mean()),
                "good_ho_ratio": float(pd.to_numeric(group_df["good_ho_ratio"], errors="coerce").mean()),
            }
        )

    return pd.DataFrame(rows)


def print_trigger_comparison_report(df_cmp: pd.DataFrame) -> None:
    _print_section("Comparison")
    if df_cmp.empty:
        print("No trigger-comparison data available.")
        return

    if "seed" not in df_cmp.columns:
        summary = summarize_trigger_comparison(df_cmp)
        if summary.empty:
            print("No trigger-comparison data available.")
            return
        for _, row in summary.iterrows():
            print(_format_trigger_summary_line(row["group"], row))
        return

    seeds = pd.to_numeric(df_cmp["seed"], errors="coerce").dropna().nunique()
    print(f"paired seeds: {int(seeds)}")

    by_seed = summarize_trigger_comparison_by_seed(df_cmp)
    for seed, seed_summary in by_seed.groupby("seed", sort=True):
        print(f"\nSeed {int(seed)}")
        for _, row in seed_summary.iterrows():
            print(_format_trigger_summary_line(row["group"], row))

    avg_summary = summarize_trigger_comparison_seed_average(df_cmp)
    if not avg_summary.empty:
        print("\nAverage Across Seeds")
        for _, row in avg_summary.iterrows():
            print(_format_trigger_summary_line(row["group"], row))

    source_summary = summarize_trigger_source(df_cmp)
    if not source_summary.empty:
        print("\nTrigger Source Breakdown")
        if "seed" in df_cmp.columns:
            source_by_seed = summarize_trigger_source_by_seed(df_cmp)
            for seed, seed_summary in source_by_seed.groupby("seed", sort=True):
                print(f"\nSeed {int(seed)}")
                for _, row in seed_summary.iterrows():
                    print(_format_trigger_summary_line(row["group"], row))

            source_avg = summarize_trigger_source_seed_average(df_cmp)
            if not source_avg.empty:
                print("\nAverage Across Seeds")
                for _, row in source_avg.iterrows():
                    print(_format_trigger_summary_line(row["group"], row))
        else:
            for _, row in source_summary.iterrows():
                print(_format_trigger_summary_line(row["group"], row))


def compare_logs(
    proposed_log_path: str,
    snr_log_path: str,
    use_nearest_time: bool = True,
    time_tolerance: Optional[float] = None,
) -> pd.DataFrame:
    proposed_df_debug, proposed_df_result, proposed_df_trend, proposed_df_skipped, proposed_df_ho = parse_log(
        proposed_log_path,
        use_nearest_time=use_nearest_time,
        time_tolerance=time_tolerance,
        verbose=False,
    )
    snr_df_debug, snr_df_result, snr_df_trend, snr_df_skipped, snr_df_ho = parse_log(
        snr_log_path,
        use_nearest_time=use_nearest_time,
        time_tolerance=time_tolerance,
        verbose=False,
    )
    _ = (proposed_df_result, proposed_df_trend, proposed_df_skipped, snr_df_debug, snr_df_result, snr_df_trend, snr_df_skipped)
    return build_trigger_comparison_frame(
        proposed_df_debug=proposed_df_debug,
        proposed_df_ho=proposed_df_ho,
        snr_df_ho=snr_df_ho,
        time_tolerance=time_tolerance,
    )


def compare_log_sets(
    proposed_path: str,
    snr_path: str,
    use_nearest_time: bool = True,
    time_tolerance: Optional[float] = None,
) -> pd.DataFrame:
    proposed_files = _collect_log_files(proposed_path)
    snr_files = _collect_log_files(snr_path)
    common_seeds = sorted(set(proposed_files) & set(snr_files))

    frames: List[pd.DataFrame] = []
    for seed in common_seeds:
        frame = compare_logs(
            str(proposed_files[seed]),
            str(snr_files[seed]),
            use_nearest_time=use_nearest_time,
            time_tolerance=time_tolerance,
        )
        if frame.empty:
            continue
        frame["seed"] = seed
        frame["proposed_file"] = proposed_files[seed].name
        frame["snr_file"] = snr_files[seed].name
        frames.append(frame)

    if not frames:
        return pd.DataFrame()
    return pd.concat(frames, ignore_index=True)


def print_analysis_report(
    df_result: pd.DataFrame,
    df_ho: pd.DataFrame,
    df_skipped: pd.DataFrame,
) -> None:
    ho_quality = summarize_ho_quality(df_result)
    trend_stats, trend_corr = analyze_trend_effectiveness(df_ho)
    trigger_stats = analyze_trigger_types(df_ho)
    delay_stats = analyze_decision_delay(df_result)
    suppression_stats = analyze_ho_suppression(df_skipped, df_ho)

    _print_section("HO Quality Analysis")
    print(f"total HO count: {ho_quality['total_ho_count']}")
    print(
        f"average sinr_gain: {ho_quality['average_sinr_gain']:.6f}"
        if not math.isnan(ho_quality["average_sinr_gain"])
        else "average sinr_gain: nan"
    )
    print(
        f"good HO ratio: {ho_quality['good_ho_ratio']:.6f}"
        if not math.isnan(ho_quality["good_ho_ratio"])
        else "good HO ratio: nan"
    )
    print(
        f"bad HO ratio: {ho_quality['bad_ho_ratio']:.6f}"
        if not math.isnan(ho_quality["bad_ho_ratio"])
        else "bad HO ratio: nan"
    )

    _print_section("Trend Effectiveness")
    print(
        f"corr(serving_trend, sinr_gain): {trend_corr:.6f}"
        if not math.isnan(trend_corr)
        else "corr(serving_trend, sinr_gain): nan"
    )
    if not trend_stats.empty:
        print(trend_stats.to_string(index=False))
    else:
        print("No serving_trend/sinr_gain pairs available.")

    _print_section("Trigger Type Comparison")
    if not trigger_stats.empty:
        print(trigger_stats.to_string(index=False))
    else:
        print("trigger_type not available.")

    _print_section("Decision Delay Analysis")
    delay_corr = delay_stats["decision_delay_sinr_gain_corr"]
    print(
        f"corr(decision_delay, sinr_gain): {delay_corr:.6f}"
        if not math.isnan(delay_corr)
        else "corr(decision_delay, sinr_gain): nan"
    )

    _print_section("HO Suppression Analysis")
    print(f"executed HO count: {suppression_stats['executed_count']}")
    print(f"skipped HO count: {suppression_stats['skipped_count']}")
    avg_exec = suppression_stats["avg_serving_sinr_executed"]
    avg_skip = suppression_stats["avg_serving_sinr_skipped"]
    print(
        f"average serving_sinr (executed): {avg_exec:.6f}"
        if not math.isnan(avg_exec)
        else "average serving_sinr (executed): nan"
    )
    print(
        f"average serving_sinr (skipped): {avg_skip:.6f}"
        if not math.isnan(avg_skip)
        else "average serving_sinr (skipped): nan"
    )
    trend_cmp = suppression_stats["trend_distribution_comparison"]
    if not trend_cmp.empty:
        print(trend_cmp.to_string(index=False))
    else:
        print("No skipped/executed trend distribution data available.")


def merge_handover_frames(
    df_debug: pd.DataFrame,
    df_result: pd.DataFrame,
    use_nearest_time: bool = True,
    time_tolerance: Optional[float] = None,
) -> pd.DataFrame:
    if df_debug.empty or df_result.empty:
        return pd.DataFrame()

    if not use_nearest_time or "t" not in df_debug.columns or "t" not in df_result.columns:
        df_ho = df_debug.merge(df_result, on=["ue"], how="inner", suffixes=("_debug", "_result"))
    else:
        debug = df_debug.copy()
        result = df_result.copy()

        debug["t"] = pd.to_numeric(debug["t"], errors="coerce")
        result["t"] = pd.to_numeric(result["t"], errors="coerce")
        debug = debug.dropna(subset=["ue", "t"])
        result = result.dropna(subset=["ue", "t"])

        debug = debug.rename(columns={"t": "t_debug"})
        result = result.rename(columns={"t": "t_result"})

        tolerance = None if time_tolerance is None else pd.Timedelta(seconds=time_tolerance)
        debug["_t_debug_td"] = pd.to_timedelta(debug["t_debug"], unit="s")
        result["_t_result_td"] = pd.to_timedelta(result["t_result"], unit="s")

        merged_groups: List[pd.DataFrame] = []
        for ue_value, result_group in result.groupby("ue", sort=False):
            debug_group = debug[debug["ue"] == ue_value]
            if debug_group.empty:
                continue

            result_group = result_group.sort_values("_t_result_td")
            debug_group = debug_group.sort_values("_t_debug_td").drop(columns=["ue"])

            merged_group = pd.merge_asof(
                result_group,
                debug_group,
                left_on="_t_result_td",
                right_on="_t_debug_td",
                direction="backward",
                tolerance=tolerance,
                suffixes=("_result", "_debug"),
            )
            merged_groups.append(merged_group)

        if not merged_groups:
            return pd.DataFrame()

        df_ho = pd.concat(merged_groups, ignore_index=True)
        df_ho = df_ho.drop(columns=["_t_result_td", "_t_debug_td"])
        if "t_debug" in df_ho.columns:
            df_ho = df_ho.dropna(subset=["t_debug"])

    if "sinr_gain" in df_ho.columns:
        df_ho["sinr_gain"] = pd.to_numeric(df_ho["sinr_gain"], errors="coerce")
        df_ho["is_good_ho"] = df_ho["sinr_gain"] > 0
    else:
        df_ho["is_good_ho"] = False

    return df_ho


def parse_log(
    log_path: str,
    use_nearest_time: bool = True,
    time_tolerance: Optional[float] = None,
    verbose: bool = True,
) -> Tuple[pd.DataFrame, pd.DataFrame, pd.DataFrame, pd.DataFrame, pd.DataFrame]:
    buckets = {
        "debug": [],
        "result": [],
        "trend": [],
        "skipped": [],
    }

    with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
        for raw_line in handle:
            kind, record = parse_log_line(raw_line)
            if kind is None:
                continue
            if not record:
                continue
            buckets[kind].append(record)

    df_debug = records_to_dataframe(buckets["debug"])
    df_result = records_to_dataframe(buckets["result"])
    df_trend = records_to_dataframe(buckets["trend"])
    df_skipped = records_to_dataframe(buckets["skipped"])
    df_ho = merge_handover_frames(
        df_debug,
        df_result,
        use_nearest_time=use_nearest_time,
        time_tolerance=time_tolerance,
    )

    if verbose:
        ho_quality = summarize_ho_quality(df_result)
        print(f"total HO count: {ho_quality['total_ho_count']}")
        print(
            f"average sinr_gain: {ho_quality['average_sinr_gain']:.6f}"
            if not math.isnan(ho_quality["average_sinr_gain"])
            else "average sinr_gain: nan"
        )

    return df_debug, df_result, df_trend, df_skipped, df_ho


def plot_handover_metrics(
    df_result: pd.DataFrame,
    df_debug: pd.DataFrame,
    df_skipped: pd.DataFrame,
    df_ho: pd.DataFrame,
    output_dir: Optional[str] = None,
    show: bool = True,
) -> None:
    import matplotlib.pyplot as plt

    if output_dir:
        Path(output_dir).mkdir(parents=True, exist_ok=True)

    def finalize_plot(filename: str) -> None:
        if output_dir:
            plt.savefig(Path(output_dir) / filename, dpi=150, bbox_inches="tight")
        if show:
            plt.show()
        else:
            plt.close()

    sinr_gain = _numeric_series(df_result, "sinr_gain")
    if not sinr_gain.empty:
        sorted_gain = sinr_gain.sort_values()
        y = pd.Series(range(1, len(sorted_gain) + 1), dtype="float64") / len(sorted_gain)
        plt.figure(figsize=(7, 5))
        plt.plot(sorted_gain.to_numpy(), y.to_numpy())
        plt.xlabel("sinr_gain (dB)")
        plt.ylabel("CDF")
        plt.title("CDF of sinr_gain")
        plt.grid(True, alpha=0.3)
        finalize_plot("sinr_gain_cdf.png")

        plt.figure(figsize=(7, 5))
        plt.hist(sinr_gain.to_numpy(), bins=30, edgecolor="black", alpha=0.8)
        plt.xlabel("sinr_gain (dB)")
        plt.ylabel("count")
        plt.title("Histogram of sinr_gain")
        plt.grid(True, alpha=0.3)
        finalize_plot("sinr_gain_hist.png")

    if not df_debug.empty and "serving_trend" in df_debug.columns:
        serving_trend = pd.to_numeric(df_debug["serving_trend"], errors="coerce").dropna()
        if not serving_trend.empty:
            plt.figure(figsize=(7, 5))
            plt.hist(serving_trend.to_numpy(), bins=30, edgecolor="black", alpha=0.8)
            plt.xlabel("serving_trend")
            plt.ylabel("count")
            plt.title("Histogram of serving_trend")
            plt.grid(True, alpha=0.3)
            finalize_plot("serving_trend_hist.png")

    if not df_ho.empty and {"serving_trend", "sinr_gain"}.issubset(df_ho.columns):
        scatter = df_ho[["serving_trend", "sinr_gain"]].apply(pd.to_numeric, errors="coerce").dropna()
        if not scatter.empty:
            plt.figure(figsize=(7, 5))
            plt.scatter(scatter["serving_trend"], scatter["sinr_gain"], alpha=0.7)
            plt.xlabel("serving_trend")
            plt.ylabel("sinr_gain")
            plt.title("serving_trend vs sinr_gain")
            plt.grid(True, alpha=0.3)
            finalize_plot("serving_trend_vs_sinr_gain.png")

    if not df_ho.empty and {"decision_delay", "sinr_gain"}.issubset(df_ho.columns):
        delay_scatter = df_ho[["decision_delay", "sinr_gain"]].apply(pd.to_numeric, errors="coerce").dropna()
        if not delay_scatter.empty:
            plt.figure(figsize=(7, 5))
            plt.scatter(delay_scatter["decision_delay"], delay_scatter["sinr_gain"], alpha=0.7)
            plt.xlabel("decision_delay (s)")
            plt.ylabel("sinr_gain")
            plt.title("decision_delay vs sinr_gain")
            plt.grid(True, alpha=0.3)
            finalize_plot("decision_delay_vs_sinr_gain.png")

    trigger_stats = analyze_trigger_types(df_ho)
    if not trigger_stats.empty:
        fig, axes = plt.subplots(1, 2, figsize=(11, 4.5))
        axes[0].bar(trigger_stats["trigger_type"], trigger_stats["avg_sinr_gain"], color="#4C72B0")
        axes[0].set_title("Average sinr_gain by trigger_type")
        axes[0].set_ylabel("avg sinr_gain (dB)")
        axes[0].tick_params(axis="x", rotation=20)
        axes[0].grid(True, axis="y", alpha=0.3)

        axes[1].bar(trigger_stats["trigger_type"], trigger_stats["good_ho_ratio"], color="#55A868")
        axes[1].set_title("Good HO ratio by trigger_type")
        axes[1].set_ylabel("good HO ratio")
        axes[1].tick_params(axis="x", rotation=20)
        axes[1].grid(True, axis="y", alpha=0.3)
        plt.tight_layout()
        finalize_plot("trigger_type_comparison.png")

    suppression_stats = analyze_ho_suppression(df_skipped, df_ho)
    trend_cmp = suppression_stats["trend_distribution_comparison"]
    if not trend_cmp.empty:
        plt.figure(figsize=(8, 5))
        plot_df = trend_cmp.set_index("trend_bin")
        plot_df.plot(kind="bar", ax=plt.gca())
        plt.xlabel("serving_trend bin")
        plt.ylabel("ratio")
        plt.title("Trend distribution: executed vs skipped")
        plt.grid(True, axis="y", alpha=0.3)
        plt.xticks(rotation=0)
        plt.tight_layout()
        finalize_plot("suppression_trend_distribution.png")

    avg_exec = suppression_stats["avg_serving_sinr_executed"]
    avg_skip = suppression_stats["avg_serving_sinr_skipped"]
    if not (math.isnan(avg_exec) and math.isnan(avg_skip)):
        plt.figure(figsize=(6, 4.5))
        plt.bar(["executed", "skipped"], [avg_exec, avg_skip], color=["#4C72B0", "#DD8452"])
        plt.ylabel("average serving_sinr (dB)")
        plt.title("Average serving_sinr: executed vs skipped")
        plt.grid(True, axis="y", alpha=0.3)
        finalize_plot("suppression_avg_serving_sinr.png")


def plot_trigger_comparison(
    df_cmp: pd.DataFrame,
    output_dir: Optional[str] = None,
    show: bool = True,
) -> None:
    import matplotlib.pyplot as plt

    if df_cmp.empty:
        return

    if output_dir:
        Path(output_dir).mkdir(parents=True, exist_ok=True)

    def finalize_plot(filename: str) -> None:
        if output_dir:
            plt.savefig(Path(output_dir) / filename, dpi=150, bbox_inches="tight")
        if show:
            plt.show()
        else:
            plt.close()

    plt.figure(figsize=(8, 5))
    plotted = False
    for group in TRIGGER_GROUPS:
        group_gap = pd.to_numeric(
            df_cmp.loc[df_cmp["comparison_group"] == group, "snr_gap"],
            errors="coerce",
        ).dropna()
        if group_gap.empty:
            continue
        plt.hist(group_gap.to_numpy(), bins=30, alpha=0.45, label=group, edgecolor="black")
        plotted = True
    if plotted:
        plt.xlabel("snr_gap (neighbor_sinr - serving_sinr)")
        plt.ylabel("count")
        plt.title("snr_gap by comparison group")
        plt.legend()
        plt.grid(True, alpha=0.3)
        finalize_plot("trigger_comparison_snr_gap_hist.png")
    else:
        plt.close()

    summary = summarize_trigger_comparison(df_cmp)
    if not summary.empty:
        plt.figure(figsize=(7, 4.5))
        plot_df = summary.copy()
        plot_df["good_ho_ratio"] = pd.to_numeric(plot_df["good_ho_ratio"], errors="coerce")
        plt.bar(plot_df["group"], plot_df["good_ho_ratio"], color=["#4C72B0", "#DD8452", "#55A868"])
        plt.xlabel("group")
        plt.ylabel("good HO ratio")
        plt.title("Good HO ratio by comparison group")
        plt.grid(True, axis="y", alpha=0.3)
        finalize_plot("trigger_comparison_good_ho_ratio.png")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("log_path")
    parser.add_argument("--compare-path", default=None)
    parser.add_argument("--exact-ue-merge", action="store_true")
    parser.add_argument("--time-tolerance", type=float, default=None)
    parser.add_argument("--plots", action="store_true")
    parser.add_argument("--plot-dir", default=None)
    parser.add_argument("--no-show", action="store_true")
    args = parser.parse_args()

    if args.compare_path:
        df_cmp = compare_log_sets(
            proposed_path=args.log_path,
            snr_path=args.compare_path,
            use_nearest_time=not args.exact_ue_merge,
            time_tolerance=args.time_tolerance,
        )
        print_trigger_comparison_report(df_cmp)
        if args.plots:
            plot_trigger_comparison(
                df_cmp=df_cmp,
                output_dir=args.plot_dir,
                show=not args.no_show,
            )
        return

    df_debug, df_result, df_trend, df_skipped, df_ho = parse_log(
        args.log_path,
        use_nearest_time=not args.exact_ue_merge,
        time_tolerance=args.time_tolerance,
    )

    print_analysis_report(
        df_result=df_result,
        df_ho=df_ho,
        df_skipped=df_skipped,
    )

    if args.plots:
        plot_handover_metrics(
            df_result=df_result,
            df_debug=df_debug,
            df_skipped=df_skipped,
            df_ho=df_ho,
            output_dir=args.plot_dir,
            show=not args.no_show,
        )

    _ = (df_debug, df_result, df_trend, df_skipped, df_ho)


if __name__ == "__main__":
    main()

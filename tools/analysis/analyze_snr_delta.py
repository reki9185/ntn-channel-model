#!/usr/bin/env python3
"""
SNR Delta Analysis Script
Analyzes ΔSNR (Target SINR - Source SINR) from CHO comparison log files
Supports batch analysis of multiple log files
"""

import re
import numpy as np
import matplotlib.pyplot as plt
import os
import sys
import glob
from pathlib import Path
from datetime import datetime

# === Configuration ===
REPO_ROOT = Path(__file__).resolve().parents[2]
OUTPUT_DIR = REPO_ROOT / "result" / "analysis" / "snr-delta"
HISTOGRAM_DPI = 300

# Historical experiment paths are intentionally not embedded here.  Pass a log
# path (or glob) on the command line so this tool works with any new run.
PRESET_TRIPLET = []

# === Regex Patterns ===
source_pattern = re.compile(r"Source: .*SINR=([\-0-9\.]+) dB")
target_pattern = re.compile(r"Target: .*SINR=([\-0-9\.]+) dB")
ho_result_pattern = re.compile(
    r"\[HO_RESULT\].*pre_serving_sinr=([\-0-9\.]+),pre_neighbor_sinr=([\-0-9\.]+)"
)
ho_debug_pattern = re.compile(r"serving_sinr=([\-0-9\.]+),\s*neighbor_sinr=([\-0-9\.]+)")


def extract_metadata(filepath):
    """
    Extract timestamp and scenario info from filepath.
    Example: cho_comparison_all_strategies_20260418_120338/SNR-CHO-Multi_seed1.txt
    Returns: (timestamp, scenario_name, seed)
    """
    filepath = str(filepath)
    
    # Extract timestamp from directory name (YYYYMMDD_HHMMSS)
    timestamp_match = re.search(r'(\d{8}_\d{6})', filepath)
    timestamp = timestamp_match.group(1) if timestamp_match else "unknown"
    
    # Extract filename (without extension)
    filename = Path(filepath).stem
    
    # Parse filename: SNR-CHO-Multi_seed1 or similar
    # Format: <scenario>_seed<N>
    seed_match = re.search(r'seed(\d+)$', filename)
    seed = seed_match.group(1) if seed_match else "unknown"
    
    # Get scenario name (everything before _seed)
    scenario = re.sub(r'_seed\d+$', '', filename)
    
    return timestamp, scenario, seed


def analyze_snr_file(filepath):
    """
    Analyze SNR delta from a log file.
    Returns: dict with statistics
    """
    delta_snr_list = []
    delta_result_list = []
    delta_debug_list = []
    
    try:
        with open(filepath, "r", encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
        
        i = 0
        while i < len(lines):
            line = lines[i]
            
            # HO_RESULT format (use pre_neighbor - pre_serving)
            result_match = ho_result_pattern.search(line)
            if result_match:
                pre_serving_sinr = float(result_match.group(1))
                pre_neighbor_sinr = float(result_match.group(2))
                delta_result_list.append(pre_neighbor_sinr - pre_serving_sinr)

            # Source/Target pair format
            source_match = source_pattern.search(line)
            if source_match:
                source_sinr = float(source_match.group(1))

                # Look for Target in next line
                if i + 1 < len(lines):
                    target_match = target_pattern.search(lines[i + 1])
                    if target_match:
                        target_sinr = float(target_match.group(1))

                        delta = target_sinr - source_sinr
                        delta_snr_list.append(delta)

            # HO_DEBUG format
            debug_match = ho_debug_pattern.search(line)
            if debug_match:
                serving_sinr = float(debug_match.group(1))
                neighbor_sinr = float(debug_match.group(2))

                delta_debug_list.append(neighbor_sinr - serving_sinr)
            
            i += 1
        
        if len(delta_snr_list) == 0 and len(delta_result_list) == 0 and len(delta_debug_list) == 0:
            return None

        if len(delta_result_list) > 0:
            delta_array = np.array(delta_result_list)
            data_source = "HO_RESULT (pre_neighbor - pre_serving)"
        elif len(delta_snr_list) > 0:
            delta_array = np.array(delta_snr_list)
            data_source = "Source/Target"
        else:
            delta_array = np.array(delta_debug_list)
            data_source = "HO_DEBUG (neighbor - serving)"
        
        stats = {
            'samples': len(delta_array),
            'mean': float(np.mean(delta_array)),
            'std': float(np.std(delta_array)),
            'min': float(np.min(delta_array)),
            'max': float(np.max(delta_array)),
            'median': float(np.median(delta_array)),
            'q25': float(np.percentile(delta_array, 25)),
            'q75': float(np.percentile(delta_array, 75)),
            'delta_array': delta_array,
            'data_source': data_source,
        }
        
        return stats
    
    except Exception as e:
        print(f"⚠️  Error analyzing {filepath}: {e}")
        return None


def generate_output_filename(timestamp, scenario, seed):
    """
    Generate output filename in format: YYYYMMDD_HHMMSS_<scenario>_seed<N>_snr_analye.txt
    """
    return f"{timestamp}_{scenario}_seed{seed}_snr_analye.txt"


def generate_histogram_filename(timestamp, scenario, seed):
    """
    Generate histogram filename: YYYYMMDD_HHMMSS_<scenario>_seed<N>_snr_histogram.png
    """
    return f"{timestamp}_{scenario}_seed{seed}_snr_histogram.png"


def save_results(stats, filepath, output_dir, timestamp, scenario, seed):
    """
    Save analysis results to text file and generate histogram.
    """
    os.makedirs(output_dir, exist_ok=True)
    
    # Save statistics to text file
    output_txt = os.path.join(output_dir, generate_output_filename(timestamp, scenario, seed))
    
    with open(output_txt, 'w') as f:
        f.write("=" * 80 + "\n")
        f.write("SNR DELTA ANALYSIS RESULTS\n")
        f.write("=" * 80 + "\n\n")
        
        f.write(f"Input File  : {filepath}\n")
        f.write(f"Timestamp   : {timestamp}\n")
        f.write(f"Scenario    : {scenario}\n")
        f.write(f"Seed        : {seed}\n")
        f.write(f"Data Source : {stats['data_source']}\n")
        f.write(f"Analysis Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")
        
        f.write("=== ΔSNR Statistics ===\n")
        f.write(f"Samples     : {stats['samples']}\n")
        f.write(f"Mean        : {stats['mean']:.4f} dB\n")
        f.write(f"Std Dev     : {stats['std']:.4f} dB\n")
        f.write(f"Median      : {stats['median']:.4f} dB\n")
        f.write(f"Min         : {stats['min']:.4f} dB\n")
        f.write(f"Max         : {stats['max']:.4f} dB\n")
        f.write(f"Q25 (25%)   : {stats['q25']:.4f} dB\n")
        f.write(f"Q75 (75%)   : {stats['q75']:.4f} dB\n")
        f.write("\n")
        f.write("=" * 80 + "\n")
    
    print(f"✓ Results saved: {output_txt}")
    
    # Generate histogram
    output_png = os.path.join(output_dir, generate_histogram_filename(timestamp, scenario, seed))
    
    plt.figure(figsize=(10, 6))
    plt.hist(stats['delta_array'], bins=30, color='#4c72b0', alpha=0.7, edgecolor='black')
    
    plt.axvline(stats['mean'], color='red', linestyle='--', linewidth=2, label=f"Mean: {stats['mean']:.2f} dB")
    plt.axvline(stats['median'], color='green', linestyle='--', linewidth=2, label=f"Median: {stats['median']:.2f} dB")
    
    plt.xlabel("ΔSNR (Target - Source) [dB]", fontsize=12, fontweight='bold')
    plt.ylabel("Count", fontsize=12, fontweight='bold')
    plt.title(f"ΔSNR Distribution - {scenario} seed{seed}\n({stats['samples']} samples)", 
              fontsize=13, fontweight='bold')
    plt.grid(True, alpha=0.3)
    plt.legend(fontsize=10)
    
    plt.tight_layout()
    plt.savefig(output_png, dpi=HISTOGRAM_DPI, bbox_inches='tight')
    plt.close()
    
    print(f"✓ Histogram saved: {output_png}")


def generate_triplet_histogram(results, output_dir):
    """
    Generate a single figure with multiple subplots for the triplet comparison.
    """
    os.makedirs(output_dir, exist_ok=True)

    all_values = np.concatenate([r['stats']['delta_array'] for r in results])
    min_val = float(np.min(all_values))
    max_val = float(np.max(all_values))

    if min_val == max_val:
        min_val -= 0.5
        max_val += 0.5

    bin_edges = np.linspace(min_val, max_val, 31)

    fig, axes = plt.subplots(1, len(results), figsize=(5 * len(results), 4), sharey=True)
    if len(results) == 1:
        axes = [axes]

    for ax, item in zip(axes, results):
        stats = item['stats']
        label = item['label']

        ax.hist(stats['delta_array'], bins=bin_edges, color='#4c72b0', alpha=0.7, edgecolor='black')
        ax.axvline(stats['mean'], color='red', linestyle='--', linewidth=2, label=f"Mean: {stats['mean']:.2f} dB")
        ax.axvline(stats['median'], color='green', linestyle='--', linewidth=2, label=f"Median: {stats['median']:.2f} dB")

        ax.set_title(label, fontsize=12, fontweight='bold')
        ax.set_xlabel("ΔSNR (Target - Source) [dB]", fontsize=10)
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=9)

    axes[0].set_ylabel("Count", fontsize=10, fontweight='bold')

    plt.tight_layout()

    output_png = os.path.join(output_dir, "snr_triplet_histogram.png")
    plt.savefig(output_png, dpi=HISTOGRAM_DPI, bbox_inches='tight')
    plt.close()

    print(f"✓ Triplet histogram saved: {output_png}")


def process_single_file(filepath):
    """Process a single log file."""
    filepath = str(filepath)
    
    if not os.path.exists(filepath):
        print(f"❌ File not found: {filepath}")
        return False
    
    print(f"\n📊 Analyzing: {filepath}")
    
    # Extract metadata
    timestamp, scenario, seed = extract_metadata(filepath)
    print(f"   Timestamp: {timestamp}, Scenario: {scenario}, Seed: {seed}")
    
    # Analyze SNR data
    stats = analyze_snr_file(filepath)
    
    if stats is None:
        print(f"⚠️  No SNR data found in file")
        return False
    
    print(f"   Samples: {stats['samples']}")
    print(f"   Data Source: {stats['data_source']}")
    print(f"   Mean ΔSNR: {stats['mean']:.4f} dB (±{stats['std']:.4f})")
    print(f"   Range: [{stats['min']:.4f}, {stats['max']:.4f}] dB")
    
    # Save results
    save_results(stats, filepath, OUTPUT_DIR, timestamp, scenario, seed)
    
    return True


def process_batch(pattern):
    """Process multiple files matching a glob pattern."""
    files = glob.glob(pattern)
    
    if not files:
        print(f"❌ No files matching pattern: {pattern}")
        return 0
    
    print(f"📁 Found {len(files)} matching files")
    
    success_count = 0
    for filepath in sorted(files):
        if process_single_file(filepath):
            success_count += 1
    
    return success_count


def process_preset_triplet():
    """Process the preset rural/urban/suburban triplet and plot as subplots."""
    results = []

    print("\n📊 Preset triplet mode")
    if not PRESET_TRIPLET:
        print("❌ No preset paths are configured. Pass a log file or glob pattern instead.")
        return 0

    for item in PRESET_TRIPLET:
        label = item['label']
        filepath = item['path']

        if not os.path.exists(filepath):
            print(f"❌ File not found for {label}: {filepath}")
            continue

        print(f"\n📊 Analyzing ({label}): {filepath}")
        timestamp, scenario, seed = extract_metadata(filepath)

        stats = analyze_snr_file(filepath)
        if stats is None:
            print("⚠️  No SNR data found in file")
            continue

        print(f"   Samples: {stats['samples']}")
        print(f"   Data Source: {stats['data_source']}")
        print(f"   Mean ΔSNR: {stats['mean']:.4f} dB (±{stats['std']:.4f})")
        print(f"   Range: [{stats['min']:.4f}, {stats['max']:.4f}] dB")

        save_results(stats, filepath, OUTPUT_DIR, timestamp, scenario, seed)
        results.append({
            'label': label,
            'filepath': filepath,
            'stats': stats,
        })

    if not results:
        return 0

    generate_triplet_histogram(results, OUTPUT_DIR)
    return len(results)


def main():
    print("\n" + "=" * 80)
    print("SNR DELTA ANALYSIS TOOL")
    print("=" * 80)
    
    if len(sys.argv) < 2:
        print("\n📖 Usage:")
        print(f"  Single file:   {sys.argv[0]} <log_file>")
        print(f"  Batch pattern: {sys.argv[0]} '<glob_pattern>'")
        print(f"  Preset triplet: {sys.argv[0]} --triplet")
        print(f"  Example:       {sys.argv[0]} 'cho_comparison_all_strategies_*/SNR-CHO_*.txt'")
        print(f"\n📂 Results will be saved to: {OUTPUT_DIR}/")
        sys.exit(1)
    
    input_path = sys.argv[1]

    if input_path in ("--triplet", "--preset"):
        success_count = process_preset_triplet()
        print("\n" + "=" * 80)
        print(f"✅ Analysis complete! ({success_count} file(s) processed)")
        print(f"📂 Results saved to: {OUTPUT_DIR}/")
        print("=" * 80 + "\n")
        return
    
    # Check if it's a pattern (contains wildcards)
    if '*' in input_path or '?' in input_path:
        print(f"\n🔍 Batch mode: {input_path}")
        success_count = process_batch(input_path)
    else:
        # Single file
        print(f"\n🔍 Single file mode")
        success_count = 1 if process_single_file(input_path) else 0
    
    print("\n" + "=" * 80)
    print(f"✅ Analysis complete! ({success_count} file(s) processed)")
    print(f"📂 Results saved to: {OUTPUT_DIR}/")
    print("=" * 80 + "\n")


if __name__ == '__main__':
    main()

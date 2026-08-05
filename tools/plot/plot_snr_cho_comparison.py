#!/usr/bin/env python3
"""
SNR-CHO Comparison Analysis & Visualization
Compares SNR-CHO-Multi vs SNR-CHO-Multi-Test2

Generates:
1. Overall metrics comparison (PDR, Latency, MR-Report, Success Rate)
2. Per-UE comparison across all metrics
3. Ping-Pong statistics comparison
"""

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np
import re
import os
import sys

# ── Configuration ──────────────────────────────────────────────────────────────

STRATEGY_COLORS = {
    'SNR-CHO-Multi':      '#c44e52',   # red
    'SNR-CHO-Multi-Test2': '#4c72b0',   # blue
}

COLORS_ORDERED = ['#c44e52', '#4c72b0']
UE_COLORS = ['#c44e52', '#55a868', '#4c72b0', '#e7b800', '#8c564b', '#e377c2']

# Global variable to hold ping-pong data loaded from file
PINGPONG_DATA = {}

# ── Parsing ───────────────────────────────────────────────────────────────────

def parse_result_file(filepath):
    """Parse result.txt and extract all metrics."""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    data = {}
    
    # Parse overall summary (first table)
    overall_pattern = r'SNR-CHO-Multi(?:-Test2)?\s+\|\s+([\d.]+)\s+\|\s+([\d.]+)\s+\|\s+([\d.]+)\s+\|\s+([\d.]+)\s+\|\s+([\d.]+)\s+\|\s+([\d.]+)\s+\|\s+([\d.]+)'
    for m in re.finditer(overall_pattern, content):
        # Just capture the lines and parse them properly
        pass
    
    # Better approach: parse line by line
    lines = content.split('\n')
    
    # Find overall summary
    for i, line in enumerate(lines):
        if 'SNR-CHO-Multi' in line and '|' in line and 'Multi-Test2' not in line:
            # This should be the original version
            parts = [p.strip() for p in line.split('|')]
            if len(parts) >= 8:
                data['SNR-CHO-Multi'] = {
                    'pdr': float(parts[1]),
                    'latency': float(parts[2]),
                    'tput': float(parts[3]),
                    'ho_ok': float(parts[4]),
                    'ho_fail': float(parts[5]),
                    'mr_report': float(parts[6]),
                    'success_rate': float(parts[7]),
                }
        elif 'SNR-CHO-Multi-Test2' in line and '|' in line:
            # This is the test2 version
            parts = [p.strip() for p in line.split('|')]
            if len(parts) >= 8:
                data['SNR-CHO-Multi-Test2'] = {
                    'pdr': float(parts[1]),
                    'latency': float(parts[2]),
                    'tput': float(parts[3]),
                    'ho_ok': float(parts[4]),
                    'ho_fail': float(parts[5]),
                    'mr_report': float(parts[6]),
                    'success_rate': float(parts[7]),
                }
    
    # Parse per-UE data
    data['per_ue'] = {}
    
    # Find SNR-CHO-Multi per-UE section
    multi_start = None
    for i, line in enumerate(lines):
        if 'PER-UE AVERAGE OVER 10 SEEDS: SNR-CHO-Multi' in line and 'Multi-Test2' not in line:
            multi_start = i
            break
    
    if multi_start:
        data['per_ue']['SNR-CHO-Multi'] = parse_ue_section(lines, multi_start)
    
    # Find SNR-CHO-Multi-Test2 per-UE section
    test2_start = None
    for i, line in enumerate(lines):
        if 'PER-UE AVERAGE OVER 10 SEEDS: SNR-CHO-Multi-Test2' in line:
            test2_start = i
            break
    
    if test2_start:
        data['per_ue']['SNR-CHO-Multi-Test2'] = parse_ue_section(lines, test2_start)
    
    return data


def parse_ue_section(lines, start_idx):
    """Parse a per-UE section starting from given index."""
    ues = {}
    reading_ue_data = False
    
    for i in range(start_idx + 1, len(lines)):
        line = lines[i]
        
        # Skip header separator lines
        if line.startswith('---'):
            continue
        
        # Stop if we hit a new section header or comparison section
        if 'PER-UE' in line or 'COMPARISON' in line:
            break
        
        # Skip empty lines
        if line.strip() == '':
            continue
            
        # Once we start reading UE data, stop when we see the summary line (starts with "SUMMARY")
        if line.startswith('SUMMARY'):
            break
            
        if line.startswith('UE-'):
            reading_ue_data = True
            # Parse UE line
            parts = [p.strip() for p in line.split('|')]
            if len(parts) >= 12:
                ue_name = parts[0]
                ues[ue_name] = {
                    'group': parts[1],
                    'users': int(float(parts[2])),
                    'lat': float(parts[3]),
                    'lon': float(parts[4]),
                    'ho_ok': float(parts[5]),
                    'ho_fail': float(parts[6]),
                    'mr_rep': float(parts[7]),
                    'success_rate': float(parts[8]),
                    'pdr': float(parts[9]),
                    'tput': float(parts[10]),
                    'latency': float(parts[11]) if len(parts) > 11 else 0,
                }
    
    return ues


def parse_pingpong_file(pingpong_file):
    """
    Parse ping-pong.txt file and extract data for all test versions.
    Returns dict: {test_name: {'mean': float, 'values': list, 'seeds': list}}
    """
    pingpong_data = {}
    
    if not os.path.exists(pingpong_file):
        print(f"⚠️  Warning: ping-pong file not found: {pingpong_file}")
        return pingpong_data
    
    try:
        with open(pingpong_file, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Split by test sections (marked by 📊)
        lines = content.split('\n')
        current_test = None
        
        for i, line in enumerate(lines):
            # Find test name line
            if '📊 ' in line:
                current_test = line.split('📊 ')[1].strip()
                pingpong_data[current_test] = {}
            
            # Find seed_ids line
            elif current_test and 'seed_ids:' in line:
                # Extract seed IDs: "seed_ids: 1, 2, 18, ..."
                seed_str = line.split('seed_ids:')[1].strip()
                # Remove parentheses if present
                seed_str = seed_str.rstrip(')')
                seeds = [int(x.strip()) for x in seed_str.split(',')]
                pingpong_data[current_test]['seeds'] = seeds
            
            # Find ping-pong counts line
            elif current_test and 'Ping-Pong counts per seed:' in line:
                # Extract counts: "Ping-Pong counts per seed: 19, 12, 15, ..."
                count_str = line.split('Ping-Pong counts per seed:')[1].strip()
                counts = [int(x.strip()) for x in count_str.split(',')]
                pingpong_data[current_test]['values'] = counts
            
            # Find mean line
            elif current_test and '- Mean:' in line:
                # Extract mean value
                mean_str = line.split('- Mean:')[1].strip()
                try:
                    mean_val = float(mean_str)
                    pingpong_data[current_test]['mean'] = mean_val
                except ValueError:
                    pass
        
        print(f"✓ Loaded ping-pong data from: {pingpong_file}")
        return pingpong_data
    
    except Exception as e:
        print(f"⚠️  Error parsing ping-pong file: {e}")
        return pingpong_data


def add_bar_labels(ax, bars, values, fmt='.1f'):
    """Add value labels on top of bars.
    fmt can be '.1f', '.2f' or include units like '.2f%', '.1fms', etc.
    """
    for bar, v in zip(bars, values):
        h = bar.get_height()
        if np.isnan(v):
            label_text = 'N/A'
            color = 'grey'
        else:
            # Parse format string to extract precision and units
            if fmt.startswith('{'):
                # Format like '{:.2f}%' - extract the numeric format and unit
                import re
                m = re.match(r'\{:([ \-+]?[0-9]*\.?[0-9]*[a-z])\}(.*)$', fmt)
                if m:
                    num_fmt, unit = m.groups()
                    label_text = f'{v:{num_fmt}}{unit}'
                else:
                    label_text = f'{v}'
            else:
                label_text = f'{v:{fmt}}'
            color = 'black'
        
        ax.text(bar.get_x() + bar.get_width() / 2., h,
                label_text,
                ha='center', va='bottom', fontsize=9, fontweight='bold', color=color)


# ── Plot Functions ─────────────────────────────────────────────────────────────

def plot_overall_metrics(data, output_dir):
    """
    Plot 1: Overall metrics comparison (2x2 subplots)
    PDR, MR-Report, Latency, Success Rate
    """
    strategies = ['SNR-CHO-Multi', 'SNR-CHO-Multi-Test2']
    x = np.arange(len(strategies))
    width = 0.6
    
    metrics = [
        ('pdr', 'PDR (%)', 'Packet Delivery Ratio', (60, 85), '{:.2f}%'),
        ('mr_report', 'Measurement Report', 'MR-Report Count', None, '{:.1f}'),
        ('latency', 'Latency (ms)', 'Handover Interruption Latency', (57, 60), '{:.2f}ms'),
        ('success_rate', 'Success Rate (%)', 'Handover Success Rate', (40, 80), '{:.2f}%'),
    ]
    
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle('SNR-CHO Comparison: Overall Metrics (10 Seeds Average)',
                 fontsize=15, fontweight='bold')
    
    for ax, (key, ylabel, title, ylim, fmt) in zip(axes.flat, metrics):
        values = [data[s][key] for s in strategies]
        bars = ax.bar(x, values, width, color=COLORS_ORDERED, alpha=0.85,
                     edgecolor='black', linewidth=1.2)
        add_bar_labels(ax, bars, values, fmt)
        
        ax.set_xticks(x)
        ax.set_xticklabels(strategies, fontsize=11, rotation=15, ha='right')
        ax.set_ylabel(ylabel, fontsize=11, fontweight='bold')
        ax.set_title(title, fontsize=12, fontweight='bold')
        ax.grid(True, axis='y', alpha=0.3)
        if ylim:
            ax.set_ylim(ylim)
    
    plt.tight_layout()
    out_path = os.path.join(output_dir, '01_overall_metrics_comparison.png')
    os.makedirs(output_dir, exist_ok=True)
    plt.savefig(out_path, dpi=300, bbox_inches='tight')
    print(f"✓ Saved: {out_path}")
    plt.close(fig)


def plot_per_ue_comparison(data, output_dir):
    """
    Plot 2: Per-UE comparison
    One subplot per metric (PDR, Success Rate, MR-Report, Latency)
    
    Handles cases where UE names differ between test versions.
    """
    strategies = ['SNR-CHO-Multi', 'SNR-CHO-Multi-Test2']
    
    # Check if per-UE data exists for both strategies
    if 'per_ue' not in data or not data['per_ue']:
        print("⚠️  No per-UE data available, skipping per-UE comparison plot")
        return
    
    ue_data_multi = data['per_ue'].get('SNR-CHO-Multi', {})
    ue_data_test2 = data['per_ue'].get('SNR-CHO-Multi-Test2', {})
    
    if not ue_data_multi or not ue_data_test2:
        print("⚠️  Missing per-UE data for one or both strategies, skipping per-UE comparison plot")
        return
    
    # Find common UE names
    ue_names_multi = set(ue_data_multi.keys())
    ue_names_test2 = set(ue_data_test2.keys())
    common_ue_names = sorted(ue_names_multi & ue_names_test2)
    
    if not common_ue_names:
        print("⚠️  No common UE names between test versions, cannot compare")
        print(f"   {strategies[0]} UEs: {sorted(ue_names_multi)}")
        print(f"   {strategies[1]} UEs: {sorted(ue_names_test2)}")
        
        # Plot separate per-UE data for each strategy if UEs don't match
        plot_per_ue_separate(ue_data_multi, 'SNR-CHO-Multi', output_dir, 'multi')
        plot_per_ue_separate(ue_data_test2, 'SNR-CHO-Multi-Test2', output_dir, 'test2')
        return
    
    x = np.arange(len(common_ue_names))
    width = 0.35
    
    metrics = [
        ('pdr', 'PDR (%)', 'PDR per UE', (60, 85), '{:.1f}%'),
        ('success_rate', 'Success Rate (%)', 'Handover Success Rate per UE', (40, 90), '{:.1f}%'),
        ('mr_rep', 'MR-Report Count', 'Measurement Reports per UE', None, '{:.1f}'),
        ('latency', 'Latency (ms)', 'Latency per UE', (57, 60), '{:.2f}ms'),
    ]
    
    fig, axes = plt.subplots(2, 2, figsize=(16, 11))
    fig.suptitle('SNR-CHO Comparison: Per-UE Metrics (Common UEs)',
                 fontsize=15, fontweight='bold')
    
    for ax, (key, ylabel, title, ylim, fmt) in zip(axes.flat, metrics):
        values_multi = [ue_data_multi[ue][key] for ue in common_ue_names]
        values_test2 = [ue_data_test2[ue][key] for ue in common_ue_names]
        
        bars1 = ax.bar(x - width/2, values_multi, width, label='SNR-CHO-Multi',
                      color=COLORS_ORDERED[0], alpha=0.85, edgecolor='black', linewidth=0.8)
        bars2 = ax.bar(x + width/2, values_test2, width, label='SNR-CHO-Multi-Test2',
                      color=COLORS_ORDERED[1], alpha=0.85, edgecolor='black', linewidth=0.8)
        
        add_bar_labels(ax, bars1, values_multi, fmt)
        add_bar_labels(ax, bars2, values_test2, fmt)
        
        ax.set_xticks(x)
        ax.set_xticklabels(common_ue_names, fontsize=10)
        ax.set_ylabel(ylabel, fontsize=11, fontweight='bold')
        ax.set_title(title, fontsize=12, fontweight='bold')
        ax.grid(True, axis='y', alpha=0.3)
        ax.legend(fontsize=10)
        if ylim:
            ax.set_ylim(ylim)
    
    plt.tight_layout()
    out_path = os.path.join(output_dir, '02_per_ue_comparison.png')
    plt.savefig(out_path, dpi=300, bbox_inches='tight')
    print(f"✓ Saved: {out_path}")
    plt.close(fig)


def plot_per_ue_separate(ue_data, strategy_name, output_dir, suffix):
    """
    Plot per-UE data for a single strategy (when UEs don't match between versions).
    """
    ue_names = sorted(ue_data.keys())
    
    if not ue_names:
        return
    
    x = np.arange(len(ue_names))
    width = 0.6
    
    metrics = [
        ('pdr', 'PDR (%)', 'PDR per UE', (60, 85), '{:.1f}%'),
        ('success_rate', 'Success Rate (%)', 'Success Rate per UE', (40, 90), '{:.1f}%'),
        ('mr_rep', 'MR-Report Count', 'Measurement Reports per UE', None, '{:.1f}'),
        ('latency', 'Latency (ms)', 'Latency per UE', (57, 61), '{:.2f}ms'),
    ]
    
    fig, axes = plt.subplots(2, 2, figsize=(16, 11))
    fig.suptitle(f'Per-UE Metrics: {strategy_name}',
                 fontsize=15, fontweight='bold')
    
    for ax, (key, ylabel, title, ylim, fmt) in zip(axes.flat, metrics):
        values = [ue_data[ue][key] for ue in ue_names]
        
        bars = ax.bar(x, values, width, color='#4c72b0', alpha=0.85,
                     edgecolor='black', linewidth=0.8)
        add_bar_labels(ax, bars, values, fmt)
        
        ax.set_xticks(x)
        ax.set_xticklabels(ue_names, fontsize=10)
        ax.set_ylabel(ylabel, fontsize=11, fontweight='bold')
        ax.set_title(title, fontsize=12, fontweight='bold')
        ax.grid(True, axis='y', alpha=0.3)
        if ylim:
            ax.set_ylim(ylim)
    
    plt.tight_layout()
    out_path = os.path.join(output_dir, f'02_per_ue_{suffix}.png')
    plt.savefig(out_path, dpi=300, bbox_inches='tight')
    print(f"✓ Saved: {out_path}")
    plt.close(fig)


def plot_pingpong_comparison(output_dir):
    """
    Plot 3: Ping-Pong statistics comparison
    """
    strategies = ['SNR-CHO-Multi', 'SNR-CHO-Multi-Test2']
    
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))
    fig.suptitle('Ping-Pong Handover Analysis Comparison',
                 fontsize=15, fontweight='bold')
    
    # Left: Bar chart of average ping-pong counts
    ax = axes[0]
    means = [PINGPONG_DATA[s]['mean'] for s in strategies]
    stds = [np.std(PINGPONG_DATA[s]['values']) for s in strategies]
    
    bars = ax.bar(strategies, means, color=COLORS_ORDERED, alpha=0.85,
                 edgecolor='black', linewidth=1.2, yerr=stds, capsize=8)
    add_bar_labels(ax, bars, means, '{:.2f}')
    
    ax.set_ylabel('Average Ping-Pong Count', fontsize=11, fontweight='bold')
    ax.set_title('Average Ping-Pong Occurrences (60s window)', fontsize=12, fontweight='bold')
    ax.set_xticklabels(strategies, rotation=15, ha='right', fontsize=10)
    ax.grid(True, axis='y', alpha=0.3)
    ax.set_ylim(0, 15)
    
    # Right: Distribution of ping-pong counts per seed
    ax = axes[1]
    for i, strategy in enumerate(strategies):
        seeds = PINGPONG_DATA[strategy]['seeds']
        values = PINGPONG_DATA[strategy]['values']
        x_pos = np.arange(len(seeds)) + i * 0.35
        bars = ax.bar(x_pos, values, width=0.35, label=strategy,
                     color=COLORS_ORDERED[i], alpha=0.85, edgecolor='black', linewidth=0.7)
    
    ax.set_xlabel('Seed', fontsize=11, fontweight='bold')
    ax.set_ylabel('Ping-Pong Count', fontsize=11, fontweight='bold')
    ax.set_title('Ping-Pong Distribution Across Seeds', fontsize=12, fontweight='bold')
    ax.set_xticks(np.arange(len(PINGPONG_DATA['SNR-CHO-Multi']['seeds'])) + 0.175)
    ax.set_xticklabels(PINGPONG_DATA['SNR-CHO-Multi']['seeds'], fontsize=9)
    ax.grid(True, axis='y', alpha=0.3)
    ax.legend(fontsize=10)
    
    plt.tight_layout()
    out_path = os.path.join(output_dir, '03_pingpong_comparison.png')
    plt.savefig(out_path, dpi=300, bbox_inches='tight')
    print(f"✓ Saved: {out_path}")
    plt.close(fig)


def plot_improvement_summary(data, output_dir):
    """
    Plot 4: Summary of improvements from Multi to Test2
    """
    strategies = ['SNR-CHO-Multi', 'SNR-CHO-Multi-Test2']
    
    # Calculate improvements
    pdr_improvement = data['SNR-CHO-Multi-Test2']['pdr'] - data['SNR-CHO-Multi']['pdr']
    mr_reduction = data['SNR-CHO-Multi']['mr_report'] - data['SNR-CHO-Multi-Test2']['mr_report']
    sr_improvement = data['SNR-CHO-Multi-Test2']['success_rate'] - data['SNR-CHO-Multi']['success_rate']
    latency_diff = data['SNR-CHO-Multi-Test2']['latency'] - data['SNR-CHO-Multi']['latency']
    pingpong_reduction = PINGPONG_DATA['SNR-CHO-Multi']['mean'] - PINGPONG_DATA['SNR-CHO-Multi-Test2']['mean']
    
    fig, ax = plt.subplots(figsize=(12, 7))
    
    metrics = [
        'PDR\nImprovement',
        'MR-Report\nReduction',
        'Success Rate\nImprovement',
        'Latency\nDifference',
        'Ping-Pong\nReduction',
    ]
    values = [pdr_improvement, mr_reduction, sr_improvement, latency_diff, pingpong_reduction]
    colors = ['#2ecc71' if v > 0 else '#e74c3c' for v in values]
    
    bars = ax.bar(metrics, values, color=colors, alpha=0.85, edgecolor='black', linewidth=1.2)
    
    # Add labels
    for bar, v in zip(bars, values):
        height = bar.get_height()
        label_y = height + (0.1 if height > 0 else -0.5)
        ax.text(bar.get_x() + bar.get_width()/2., label_y,
                f'{v:.2f}', ha='center', va='bottom' if height > 0 else 'top',
                fontsize=11, fontweight='bold')
    
    ax.axhline(y=0, color='black', linestyle='-', linewidth=0.8)
    ax.set_ylabel('Improvement/Change', fontsize=12, fontweight='bold')
    ax.set_title('SNR-CHO-Multi-Test2 vs SNR-CHO-Multi: Summary of Improvements',
                 fontsize=14, fontweight='bold')
    ax.grid(True, axis='y', alpha=0.3)
    
    # Add legend
    green_patch = mpatches.Patch(color='#2ecc71', label='Improvement')
    red_patch = mpatches.Patch(color='#e74c3c', label='Degradation')
    ax.legend(handles=[green_patch, red_patch], fontsize=11, loc='upper left')
    
    plt.tight_layout()
    out_path = os.path.join(output_dir, '04_improvement_summary.png')
    plt.savefig(out_path, dpi=300, bbox_inches='tight')
    print(f"✓ Saved: {out_path}")
    plt.close(fig)


def print_summary_table(data):
    """Print a nice summary table to console."""
    print("\n" + "="*80)
    print("OVERALL METRICS COMPARISON")
    print("="*80)
    print(f"{'Metric':<20} {'SNR-CHO-Multi':>18} {'SNR-CHO-Multi-Test2':>20} {'Change':>15}")
    print("-"*80)
    
    metrics = [
        ('PDR (%)', 'pdr'),
        ('Latency (ms)', 'latency'),
        ('Throughput (Kbps)', 'tput'),
        ('Handover Success (%)', 'success_rate'),
        ('MR-Report Count', 'mr_report'),
        ('Ping-Pong Count', None),
    ]
    
    for display_name, key in metrics:
        if key is None:
            v1 = PINGPONG_DATA['SNR-CHO-Multi']['mean']
            v2 = PINGPONG_DATA['SNR-CHO-Multi-Test2']['mean']
        else:
            v1 = data['SNR-CHO-Multi'][key]
            v2 = data['SNR-CHO-Multi-Test2'][key]
        
        change = v2 - v1
        pct_change = (change / v1 * 100) if v1 != 0 else 0
        
        change_str = f"{change:+.2f} ({pct_change:+.1f}%)"
        print(f"{display_name:<20} {v1:>18.2f} {v2:>20.2f} {change_str:>15}")
    
    print("="*80)


# ── Main ───────────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) not in (2, 3):
        print(f"Usage: {sys.argv[0]} <result.txt> [output_dir]")
        sys.exit(2)

    result_file = sys.argv[1]
    output_dir = sys.argv[2] if len(sys.argv) == 3 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "figure"
    )
    
    if not os.path.exists(result_file):
        print(f"Error: File not found: {result_file}")
        sys.exit(1)
    
    print(f"Parsing {result_file} ...")
    data = parse_result_file(result_file)
    
    if not data or 'SNR-CHO-Multi' not in data:
        print("Error: Failed to parse data from result file")
        sys.exit(1)
    
    # Try to load ping-pong data from folder
    result_folder = os.path.dirname(result_file)
    pingpong_file = os.path.join(result_folder, 'ping-pong.txt')
    
    global PINGPONG_DATA
    if os.path.exists(pingpong_file):
        PINGPONG_DATA = parse_pingpong_file(pingpong_file)
        if not PINGPONG_DATA:
            print("⚠️  Using fallback ping-pong data (file parsing failed)")
            # Could add fallback data here if needed
    else:
        print(f"⚠️  ping-pong.txt not found in {result_folder}")
        print("   Skipping ping-pong plots")
    
    os.makedirs(output_dir, exist_ok=True)
    
    print("\n📊 Generating plots...")
    plot_overall_metrics(data, output_dir)
    plot_per_ue_comparison(data, output_dir)
    
    if PINGPONG_DATA:
        plot_pingpong_comparison(output_dir)
        plot_improvement_summary(data, output_dir)
    
    print_summary_table(data)
    print("\n✅ All plots generated successfully!")


if __name__ == '__main__':
    main()

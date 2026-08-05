#!/usr/bin/env python3
"""
Universal Ping-Pong Analysis Script
自動分析任意資料夾中的 CHO handover log 檔案，統計 ping-pong 次數
支援不同數量的 seed、UE 和測試類型

✅ 支援兩種資料夾類型：
  1. snr_cho_comparison_YYYYMMDD_HHMMSS  （Single strategy: SNR-CHO-Multi + Test2）
  2. cho_comparison_all_strategies_YYYYMMDD_HHMMSS  （Multiple strategies: SNR, distance, elevation）

使用方法：
    python3 analyze_pingpong.py <資料夾路徑> [輸出檔案]
    
範例：
    python3 tools/analysis/analyze_pingpong.py <comparison_result_dir>
    python3 tools/analysis/analyze_pingpong.py <comparison_result_dir> <output_path>
    
說明：
  - 需要 log files 中包含 '_seed' 標記（例：SNR-CHO-Multi_seed45.txt）
  - 自動偵測每個 log 檔中的 handover events 並統計 ping-pong 次數
  - 輸出 txt 和 json 格式的結果分析
"""

import re
import os
import glob
import numpy as np
from pathlib import Path
from collections import defaultdict
import json

# ── Configuration ──────────────────────────────────────────────────────────────

PING_PONG_WINDOW = 60.0   # seconds

# ── Regex patterns ─────────────────────────────────────────────────────────────

RE_CHO_EXEC = re.compile(
    r'EXECUTION COMPLETE at t=([0-9.]+)s'
)
RE_CHO_EXEC_UE = re.compile(
    r'\[UE-(\d+)\].*EXECUTION COMPLETE at t=([0-9.]+)s'
)
RE_CHO_COMPLETE = re.compile(
    r'CHO HANDOVER COMPLETE:\s+(.+?)\s+→\s+(.+)'
)
RE_CHO_COMPLETE_UE = re.compile(
    r'\[UE-(\d+)\].*CHO HANDOVER COMPLETE:\s+(.+?)\s+→\s+(.+)'
)
RE_HO_RESULT = re.compile(
    r'^\s*\[HO_RESULT\],ue=(?P<ue>\d+),t=(?P<t>[0-9.]+)(?P<rest>.*)$'
)
RE_HO_DEBUG = re.compile(
    r'^\s*\[HO_DEBUG\],(?P<body>.*)$'
)


def parse_kv_pairs(text):
    data = {}
    for part in text.split(','):
        part = part.strip()
        if not part or '=' not in part:
            continue
        key, value = part.split('=', 1)
        data[key.strip()] = value.strip()
    return data


def normalize(sat_name):
    """Strip trailing tags like [DTC] for comparison."""
    return re.sub(r'\s*\[.*?\]', '', sat_name).strip()


def parse_handovers_cho(content):
    """Return list of dict(time, ue, src, tgt, trigger_type) for completed CHO handovers."""
    events = []
    lines = content.splitlines()
    pending_exec = {}
    pending_result = defaultdict(list)
    debug_by_ue = defaultdict(list)

    def _pick_debug_match(ue, target_time, window, prefer_a3=True):
        if ue not in debug_by_ue:
            return None
        best = None
        best_delta = None
        for t_val, serving, neighbor, a3 in debug_by_ue[ue]:
            delta = abs(t_val - target_time)
            if delta > window:
                continue
            if best is None:
                best = (serving, neighbor, a3)
                best_delta = delta
                continue
            if prefer_a3 and a3 == 1 and best[2] != 1:
                best = (serving, neighbor, a3)
                best_delta = delta
            elif delta < best_delta:
                best = (serving, neighbor, a3)
                best_delta = delta
        return best
    
    for line in lines:
        m_debug = RE_HO_DEBUG.search(line)
        if m_debug:
            extra = parse_kv_pairs(m_debug.group('body'))
            if 'ue' in extra and 't' in extra and 'serving' in extra and 'neighbor' in extra:
                try:
                    ue = int(extra['ue'])
                    t_val = float(extra['t'])
                except ValueError:
                    continue
                serving = normalize(extra['serving'])
                neighbor = normalize(extra['neighbor'])
                a3 = None
                if 'a3' in extra:
                    try:
                        a3 = int(float(extra['a3']))
                    except ValueError:
                        a3 = None
                debug_by_ue[ue].append((t_val, serving, neighbor, a3))
            continue

        m_result = RE_HO_RESULT.search(line)
        if m_result:
            ue = int(m_result.group('ue'))
            event = {
                'time': float(m_result.group('t')),
                'ue': ue,
                'src': None,
                'tgt': None,
                'trigger_type': None,
            }
            extra = parse_kv_pairs(m_result.group('rest').lstrip(','))
            if 'trigger_type' in extra:
                event['trigger_type'] = extra['trigger_type']
            decision_time = None
            if 'decision_delay' in extra:
                try:
                    decision_delay = float(extra['decision_delay'])
                    decision_time = event['time'] - decision_delay
                except ValueError:
                    decision_time = None

            best = None
            if decision_time is not None:
                best = _pick_debug_match(ue, decision_time, window=1.0, prefer_a3=True)
            if best is None:
                best = _pick_debug_match(ue, event['time'], window=2.0, prefer_a3=False)
            if best is not None:
                event['src'] = best[0]
                event['tgt'] = best[1]
            pending_result[ue].append(event)
            continue

        m_exec_ue = RE_CHO_EXEC_UE.search(line)
        if m_exec_ue:
            ue = int(m_exec_ue.group(1))
            pending_exec[ue] = float(m_exec_ue.group(2))
            continue

        m_exec = RE_CHO_EXEC.search(line)
        if m_exec:
            pending_exec[None] = float(m_exec.group(1))
            continue

        m_comp_ue = RE_CHO_COMPLETE_UE.search(line)
        if m_comp_ue:
            ue = int(m_comp_ue.group(1))
            src = normalize(m_comp_ue.group(2))
            tgt = normalize(m_comp_ue.group(3))

            if ue in pending_result and pending_result[ue]:
                event = pending_result[ue].pop(0)
                event['src'] = src
                event['tgt'] = tgt
                events.append(event)
            else:
                time_value = pending_exec.pop(ue, pending_exec.pop(None, None))
                if time_value is not None:
                    events.append({
                        'time': time_value,
                        'ue': ue,
                        'src': src,
                        'tgt': tgt,
                        'trigger_type': None,
                    })
            continue

        if pending_exec:
            m_comp = RE_CHO_COMPLETE.search(line)
            if m_comp:
                src = normalize(m_comp.group(1))
                tgt = normalize(m_comp.group(2))
                time_value = pending_exec.pop(None, None)
                if time_value is not None:
                    events.append({
                        'time': time_value,
                        'ue': None,
                        'src': src,
                        'tgt': tgt,
                        'trigger_type': None,
                    })
                continue
    
    for event_list in pending_result.values():
        events.extend(event_list)

    return events


def count_pingpong(events, window=PING_PONG_WINDOW):
    """
    Count ping-pong events per UE:
    consecutive pair (t1, A, B), (t2, B, A) for the same UE
    where t2 - t1 <= window.
    Returns: (count, list of ping-pong pairs)
    """
    count = 0
    pairs = []

    events_by_ue = defaultdict(list)
    for event in events:
        if event.get('src') is None or event.get('tgt') is None:
            continue
        events_by_ue[event.get('ue')].append(event)

    for ue, ue_events in events_by_ue.items():
        ue_events = sorted(ue_events, key=lambda event: event['time'])
        for i in range(len(ue_events) - 1):
            first = ue_events[i]
            second = ue_events[i + 1]

            if (
                second['src'] == first['tgt']
                and second['tgt'] == first['src']
                and (second['time'] - first['time']) <= window
            ):
                count += 1
                pairs.append((ue, first, second))
    
    return count, pairs


def extract_metadata(filename):
    """
    Extract test name and seed from log filename.
    
    Examples:
        SNR-CHO-Multi_seed45.txt -> ('SNR-CHO-Multi', '45')
        SNR-CHO-Multi-Test2_seed123.txt -> ('SNR-CHO-Multi-Test2', '123')
        distance-based-CHO_seed1.txt -> ('distance-based-CHO', '1')
        ping-pong.txt -> (None, None)  # Skip files without seed markers
    """
    basename = os.path.basename(filename)
    
    # Skip files that don't match the pattern (no seed marker)
    if '_seed' not in basename:
        return None, None
    
    # Try to extract seed number
    seed_match = re.search(r'seed(\d+)', basename)
    seed = seed_match.group(1) if seed_match else 'unknown'
    
    # Extract test name (everything before _seed)
    test_match = re.match(r'(.+?)_seed\d+\.txt$', basename)
    test_name = test_match.group(1) if test_match else basename.replace('.txt', '')
    
    return test_name, seed


def parse_log(filepath):
    """Parse a log file and return handover events."""
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
        return parse_handovers_cho(content)
    except Exception as e:
        print(f"  ⚠️  Error reading {filepath}: {e}")
        return []


def analyze_folder(folder_path):
    """
    Analyze all CHO log files in a folder.
    
    Returns:
        dict with structure:
        {
            'test_name': {
                'seeds': [1, 2, 3, ...],
                'counts': [11, 10, 13, ...],
                'handover_counts': [206, 209, ...],
                'stats': {...}
            },
            ...
        }
    """
    if not os.path.isdir(folder_path):
        raise ValueError(f"Folder not found: {folder_path}")
    
    # Find all log files (both CHO and others with handover data)
    log_files = sorted(glob.glob(os.path.join(folder_path, '*.txt')))
    log_files = [f for f in log_files 
                 if not f.endswith('_parsed_metrics.tsv') 
                 and os.path.basename(f) != 'result.txt']
    
    if not log_files:
        raise ValueError(f"No log files found in {folder_path}")
    
    # Group by test name
    results = defaultdict(lambda: {'seeds': [], 'counts': [], 'handover_counts': []})
    
    print(f"📁 Analyzing folder: {folder_path}")
    print(f"📄 Found {len(log_files)} log files\n")
    
    for log_file in log_files:
        filename = os.path.basename(log_file)
        test_name, seed = extract_metadata(filename)
        
        # Skip files that don't match the pattern (e.g., ping-pong.txt)
        if test_name is None or seed is None:
            continue
        
        try:
            events = parse_log(log_file)
            pp_count, _ = count_pingpong(events)
            
            results[test_name]['seeds'].append(int(seed))
            results[test_name]['counts'].append(pp_count)
            results[test_name]['handover_counts'].append(len(events))
            
            print(f"✓ {filename:<50} Handovers: {len(events):3d}  Ping-Pongs: {pp_count:2d}")
        
        except Exception as e:
            print(f"✗ {filename:<50} Error: {e}")
    
    # Calculate statistics for each test
    final_results = {}
    for test_name, data in results.items():
        seeds = data['seeds']
        counts = data['counts']
        ho_counts = data['handover_counts']
        
        # Skip if no data
        if not seeds:
            continue
        
        # Sort by seed
        sorted_indices = sorted(range(len(seeds)), key=lambda i: seeds[i])
        sorted_seeds = [seeds[i] for i in sorted_indices]
        sorted_counts = [counts[i] for i in sorted_indices]
        sorted_ho_counts = [ho_counts[i] for i in sorted_indices]
        
        final_results[test_name] = {
            'seeds': sorted_seeds,
            'counts': sorted_counts,
            'handover_counts': sorted_ho_counts,
            'stats': {
                'mean': np.mean(sorted_counts),
                'median': np.median(sorted_counts),
                'std': np.std(sorted_counts),
                'min': int(np.min(sorted_counts)),
                'max': int(np.max(sorted_counts)),
                'total': int(np.sum(sorted_counts)),
                'seed_count': len(sorted_seeds),
                'mean_handovers': np.mean(sorted_ho_counts),
            }
        }
    
    return final_results


def format_results(results):
    """Format results as a readable string."""
    output = []
    
    # Check if there are any results
    if not results:
        output.append("\n❌ No valid log files found (files must have '_seed' pattern)")
        return "\n".join(output)
    
    output.append("\n" + "="*90)
    output.append("PING-PONG ANALYSIS RESULTS")
    output.append("="*90)
    
    # Group by alphabetical order
    for test_name in sorted(results.keys()):
        data = results[test_name]
        stats = data['stats']
        seeds = data['seeds']
        counts = data['counts']
        
        output.append(f"\n📊 {test_name}")
        output.append("-" * 90)
        output.append(f"  Seeds tested: {len(seeds)} (seed_ids: {', '.join(map(str, seeds))})")
        output.append(f"  Ping-Pong counts per seed: {', '.join(map(str, counts))}")
        output.append(f"  ")
        output.append(f"  Statistics:")
        output.append(f"    - Mean:           {stats['mean']:>8.2f}")
        output.append(f"    - Median:         {stats['median']:>8.2f}")
        output.append(f"    - Std Dev:        {stats['std']:>8.2f}")
        output.append(f"    - Min / Max:      {stats['min']:>8} / {stats['max']:<8}")
        output.append(f"    - Total:          {stats['total']:>8}")
        output.append(f"    - Avg Handovers:  {stats['mean_handovers']:>8.2f}")
    
    # Summary comparison if multiple test versions
    if len(results) > 1:
        output.append("\n" + "="*90)
        output.append("COMPARISON SUMMARY")
        output.append("="*90)
        
        test_names = sorted(results.keys())
        
        # Create comparison table
        output.append(f"\n{'Test Name':<40} {'Mean PP':>12} {'Median':>12} {'Std Dev':>12} {'Range':>15}")
        output.append("-" * 90)
        
        for test_name in test_names:
            stats = results[test_name]['stats']
            output.append(
                f"{test_name:<40} "
                f"{stats['mean']:>12.2f} "
                f"{stats['median']:>12.2f} "
                f"{stats['std']:>12.2f} "
                f"{stats['min']:>6} ~ {stats['max']:<6}"
            )
        
        # Calculate improvements if there are exactly 2 test versions
        if len(results) == 2:
            sorted_tests = sorted(results.keys())
            test1, test2 = sorted_tests[0], sorted_tests[1]
            stats1, stats2 = results[test1]['stats'], results[test2]['stats']
            
            improvement = stats1['mean'] - stats2['mean']
            improvement_pct = (improvement / stats1['mean'] * 100) if stats1['mean'] != 0 else 0
            
            output.append("\n" + "-" * 90)
            output.append(f"Improvement from '{test1}' to '{test2}':")
            output.append(f"  Ping-Pong reduction: {improvement:>8.2f} ({improvement_pct:+.1f}%)")
            
            if improvement > 0:
                output.append(f"  ✅ Test2 shows {improvement_pct:.1f}% improvement (fewer ping-pongs is better)")
            elif improvement < 0:
                output.append(f"  ⚠️  Test2 shows {abs(improvement_pct):.1f}% degradation")
            else:
                output.append(f"  ℹ️  No significant difference")
    
    output.append("\n" + "="*90 + "\n")
    return "\n".join(output)


def save_results_json(results, output_file):
    """Save results as JSON for programmatic access."""
    # Convert numpy types to native Python types for JSON serialization
    json_results = {}
    for test_name, data in results.items():
        json_results[test_name] = {
            'seeds': data['seeds'],
            'counts': data['counts'],
            'handover_counts': data['handover_counts'],
            'stats': {
                k: float(v) if isinstance(v, np.number) else v
                for k, v in data['stats'].items()
            }
        }
    
    with open(output_file, 'w') as f:
        json.dump(json_results, f, indent=2)


def main():
    import sys
    
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    
    folder_path = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else None
    
    try:
        results = analyze_folder(folder_path)
        
        # Print formatted results
        formatted = format_results(results)
        print(formatted)
        
        # Optionally save to file
        if output_file:
            # Save text output
            txt_file = output_file.replace('.json', '').replace('.txt', '') + '.txt'
            with open(txt_file, 'w', encoding='utf-8') as f:
                f.write(formatted)
            print(f"📄 Text results saved to: {txt_file}")
            
            # Save JSON output
            json_file = output_file.replace('.txt', '').replace('.json', '') + '.json'
            save_results_json(results, json_file)
            print(f"📊 JSON results saved to: {json_file}")
        
    except Exception as e:
        print(f"❌ Error: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()

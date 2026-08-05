# NTN Channel Model feat. TD-CHO Algorithm

This repository contains six Multi-UE conditional-handover strategies for NTN experiments.
### Build & Run

```bash
# Configure once after cloning or changing the ns-3 module set.
# Tests are not included in this reduced repository.
./ns3 configure --disable-tests
./ns3 build

# Run all selected CHO strategies and create  cho_comparison_all_strategies_<timestamp> file.
./compare_all_cho_strategies.sh

# Run a short, reproducible smoke test without editing the script.
SEEDS_OVERRIDE="18" SIMULATION_TIME_OVERRIDE=10 \
SELECTED_ALGORITHMS_OVERRIDE="tdcho" \
./compare_all_cho_strategies.sh

# Compare the SNR-CHO variants configured at the top of this script.
./compare_snr_cho_seeds.sh
```

---

### Common Simulations

All simulations use `dataset/visible_satellites_hsinchu.csv` and the UE group CSVs in `dataset/ue/` by default. The common options are:

- `--simTime=<seconds>`: simulation duration.
- `--minElevation=<degrees>` and `--maxDistance=<km>`: candidate-satellite limits.
- `--timeToTrigger=<seconds>` and `--measurementPeriod=<seconds>`: handover timing.
- `--reportingOffset` and `--choExecutionOffset`: CHO preparation/execution margins. Their units depend on the strategy.

### 1. SNR-CHO

```bash
./ns3 run "scratch/MultiUE-CHO-SNR/SNR-CHO-Multi \
  --simTime=3600 --reportingOffset=1 --choExecutionOffset=3 \
  --minElevation=20 --maxDistance=1500 --timeToTrigger=1 --measurementPeriod=1"
```

SNR margins are expressed in dB. Logs are written to `result/snr-cho/`.

### 2. TD-CHO

```bash
./ns3 run "scratch/MultiUE-CHO-SNR-TDCHO/SNR-CHO-Multi-TDCHO \
  --simTime=3600 --reportingOffset=1 --choExecutionOffset=3 \
  --minElevation=20 --maxDistance=1500 --timeToTrigger=1 --measurementPeriod=1"
```

TDCHO adds serving-SNR trend analysis. Logs are written to `result/tdcho/`.

### 3. Distance-CHO

```bash
./ns3 run "scratch/MultiUE-CHO-distance/distance-CHO-Multi \
  --simTime=3600 --reportingOffset=1 --choExecutionOffset=3 \
  --minElevation=20 --maxDistance=1500 --timeToTrigger=1 --measurementPeriod=1"
```

The reporting and execution margins are expressed in km. Logs are written to `result/distance-cho/`.

### 4. Elevation-CHO

```bash
./ns3 run "scratch/MultiUE-CHO-elevation/elevation-CHO-Multi \
  --simTime=3600 --reportingOffset=1 --choExecutionOffset=3 \
  --minElevation=20 --maxDistance=1500 --timeToTrigger=1 --measurementPeriod=1"
```

The reporting and execution margins are expressed in km. Logs are written to `result/elevation-cho/`.

### 5. SD-TOPSIS-CHO

```bash
./ns3 run "scratch/MultiUE-CHO-TOPSIS/topsis-CHO-Multi \
  --simTime=3600 --reportingOffset=1 --choExecutionOffset=3 \
  --minElevation=20 --maxDistance=1500 --timeToTrigger=1 --measurementPeriod=1"
```

Logs are written to `result/sd-topsis-cho/`.

### 6. Serving-Time-CHO

```bash
./ns3 run "scratch/MultiUE-CHO-serving-time/serving-time-CHO-Multi \
  --simTime=3600 --reportingOffset=120 --choExecutionOffset=120 \
  --minElevation=20 --maxDistance=1800 --timeToTrigger=1 --measurementPeriod=1"
```

The reporting and execution margins are expressed in seconds. Logs are written to `result/serving-time-cho/`.

---

### Output Files

Each strategy directory in `result/` can contain the following CSV files:

- `cho-detailed-log.csv`: detailed CHO events.
- `sgp4-handover-log.csv`: handover events.
- `sgp4-multi-channel-log.csv`: per-satellite channel metrics.
- `sgp4-multi-snr-log.csv` and `sgp4-all-satellites-snr.csv`: SNR measurements.
- `sgp4-xn-signaling-log.csv`: Xn signalling phases.
- TDCHO additionally produces `serving_snr_trigger_analysis.csv`, `serving_snr_trigger_followup.csv`, `visualization_ue_positions.csv`, and `visualization_snapshot.csv`.

The comparison scripts keep their text logs, `_parsed_metrics.tsv`, and `result.txt` in their dated output directory.

---

### Emulator State Generation

`sgp4-ntn-udp-example` is the single-satellite UDP simulation used to produce the state consumed by `ntn-emulator/`. It reads `dataset/visible_satellites_hsinchu.csv` and writes its per-second full log to `result/single-satellite-udp/`.

```bash
# 1. Build once after cloning or after changing the C++ source.
./ns3 build sgp4-ntn-udp-example

# 2. Generate a single-satellite full log.
./ns3 run "sgp4-ntn-udp-example \
  --satellite=STARLINK-31077 --scenario=Rural --seed=45 --simTime=600"

# 3. Smooth the full log and replace ntn-emulator/ntn_state.json.
python3 tools/generate_ntn_state.py \
  result/single-satellite-udp/STARLINK-31077-Rural-full-log.csv
```

The Python tool uses non-overlapping 10-second windows by default. It averages `ue_sat_delay_ms`, `sat_cn_delay_ms`, and `pdr`, normalizes PDR from percent to 0–1, and writes `ntn-emulator/ntn_state.json` atomically. It also keeps the emulator's initial `time=0` state; the average of source `[0, 10)` is emitted at time 20. Use `--window-seconds` to change the smoothing interval, `--satellite` when the satellite cannot be inferred from the filename, or `--output` for a non-default JSON path.

---

### Tools and Plotting

Python tools require the packages used by their command, typically:

```bash
pip3 install matplotlib numpy pandas seaborn scipy
```

### Comparison plots

Create a figure from exactly one comparison result. The image is saved to `tools/plot/figure/handover_comparison.png` unless `--output-dir` is given.

```bash
python3 tools/plot/plot_handover_comparison.py \
  cho_comparison_all_strategies_<timestamp>
```

Plot SNR against distance/elevation for three explicitly supplied CSV files. Each CSV must provide `distance_km`, `elevation_deg`, and `snr_dB` columns.

```bash
python3 tools/plot/plot_snr-vs-distance.py \
  --rural <rural.csv> --suburban <suburban.csv> --urban <urban.csv> \
  --output-dir tools/plot/figure
```

### Log and handover analysis

| Tool | Function | Example |
| --- | --- | --- |
| `tools/parse_handover_log.py` | Parses `[HO_DEBUG]`, `[HO_RESULT]`, and trend records; can compare two logs and generate plots. | `python3 tools/parse_handover_log.py <log.txt> --plots --no-show --plot-dir tools/plot/figure` |
| `tools/analysis/analyze_pingpong.py` | Counts ping-pong handovers across all seed logs in one comparison directory. | `python3 tools/analysis/analyze_pingpong.py cho_comparison_all_strategies_<timestamp> cho_comparison_all_strategies_<timestamp>/ping-pong` |
| `tools/analysis/analyze_serving_snr_trigger.py` | Checks whether the serving satellite is receding when a serving-SNR trend trigger fires. Defaults to TDCHO CSV output; it can also read repeated `--log` arguments. | `python3 tools/analysis/analyze_serving_snr_trigger.py` |
| `tools/analysis/analyze_serving_snr_degradation.py` | Validates future SNR degradation after TDCHO trigger events. | `python3 tools/analysis/analyze_serving_snr_degradation.py` |
| `tools/analysis/analyze_snr_delta.py` | Produces SNR-delta analysis for one seed log or a glob of logs. | `python3 tools/analysis/analyze_snr_delta.py 'cho_comparison_all_strategies_<timestamp>/TDCHO_seed*.txt'` |
| `tools/analysis/analyze_trend_directional.py` | Evaluates how well `serving_trend` predicts future serving-SINR changes. | `python3 tools/analysis/analyze_trend_directional.py cho_comparison_all_strategies_<timestamp> --strategy TDCHO` |

Analysis output is written to the path supplied to each tool, or to its documented default under `result/`.

---

### Dashboards

Both dashboards use externally hosted map libraries, so open them while connected to the internet.

Start a local web server from the repository root:

```bash
python3 -m http.server 8000
```

Then open either dashboard in a browser:

- UE cluster map: <http://localhost:8000/dashboard/ue_cluster_map.html>. This is a standalone map of the configured UE groups.
- Handover globe player: <http://localhost:8000/dashboard/handover_globe_player.html>. It automatically loads `result/tdcho/visualization_ue_positions.csv` and `result/tdcho/visualization_snapshot.csv`; the two file inputs can be used to inspect another run.

---

### Build Errors

```bash
./ns3 configure --disable-tests
./ns3 build
```

This reduced repository intentionally contains only the modules required by the Multi-UE CHO simulations. Standard ns-3 examples and the ns-3 test module are not included.

### Missing Input or Output Files

- Verify trajectory and UE-group inputs under `dataset/`.
- Run the corresponding simulation before using tools that read `result/<strategy>/` CSV files.
- Pass explicit paths to plotting and analysis tools when working with archived comparison directories.

#!/usr/bin/env bash
# Run the selected multi-UE CHO algorithms and write one comparison report.
#
# Run a new experiment:
#   ./compare_all_cho_strategies.sh
# Rebuild the report from a previous parsed result:
#   ./compare_all_cho_strategies.sh cho_comparison_all_strategies_YYYYMMDD_HHMMSS

set -euo pipefail

###############################################################################
# Experiment configuration -- change these values for a new experiment.
###############################################################################

# 1. Random seeds. One run is made for every algorithm/seed pair.
SEEDS=(1 2 18 55 38 78 123)

# 2. Simulation duration, in seconds.
SIMULATION_TIME=3600

# 3. Algorithms to run. Remove keys here to skip them.
# Available keys: snr tdcho distance elevation topsis serving_time
SELECTED_ALGORITHMS=(snr tdcho distance elevation topsis serving_time)

# One-off overrides, useful for short validation runs without editing the file:
#   SEEDS_OVERRIDE="18" SIMULATION_TIME_OVERRIDE=10 \
#   SELECTED_ALGORITHMS_OVERRIDE="tdcho" ./compare_all_cho_strategies.sh
if [[ -n ${SEEDS_OVERRIDE:-} ]]; then
    read -r -a SEEDS <<< "$SEEDS_OVERRIDE"
fi
SIMULATION_TIME=${SIMULATION_TIME_OVERRIDE:-$SIMULATION_TIME}
if [[ -n ${SELECTED_ALGORITHMS_OVERRIDE:-} ]]; then
    read -r -a SELECTED_ALGORITHMS <<< "$SELECTED_ALGORITHMS_OVERRIDE"
fi

###############################################################################
# Per-algorithm parameters.
# Format: executable|scratch-directory|report-label|reporting-offset|
#         execution-offset|min-elevation|max-distance
# Offset units are dB for snr/tdcho/topsis, km for distance/elevation, and
# seconds for serving_time.
###############################################################################

declare -A ALGORITHM_CONFIG=(
    [snr]="SNR-CHO-Multi|MultiUE-CHO-SNR|SNR-CHO|1|3|20|1500"
    [tdcho]="SNR-CHO-Multi-TDCHO|MultiUE-CHO-SNR-TDCHO|TDCHO|1|3|20|1500"
    [distance]="distance-CHO-Multi|MultiUE-CHO-distance|Distance-CHO|1|3|20|1500"
    [elevation]="elevation-CHO-Multi|MultiUE-CHO-elevation|Elevation-CHO|1|3|20|1500"
    [topsis]="topsis-CHO-Multi|MultiUE-CHO-TOPSIS|SD-TOPSIS-CHO|1|3|20|1500"
    [serving_time]="serving-time-CHO-Multi|MultiUE-CHO-serving-time|Serving-Time-CHO|120|120|20|1800"
)

if [[ $# -gt 1 ]]; then
    echo "Usage: $0 [existing_output_dir]" >&2
    exit 2
fi

REPORT_ONLY=0
if [[ $# -eq 1 ]]; then
    OUTPUT_DIR=$1
    REPORT_ONLY=1
else
    OUTPUT_DIR="cho_comparison_all_strategies_$(date +"%Y%m%d_%H%M%S")"
fi
PARSED_TSV="$OUTPUT_DIR/_parsed_metrics.tsv"
RESULT_FILE="$OUTPUT_DIR/result.txt"

die() {
    echo "Error: $*" >&2
    exit 2
}

validate_configuration() {
    [[ ${#SEEDS[@]} -gt 0 ]] || die "SEEDS must not be empty."
    [[ ${#SELECTED_ALGORITHMS[@]} -gt 0 ]] || die "SELECTED_ALGORITHMS must not be empty."
    [[ $SIMULATION_TIME =~ ^[0-9]+([.][0-9]+)?$ ]] || die "SIMULATION_TIME must be numeric."

    local key config executable directory unused
    for key in "${SELECTED_ALGORITHMS[@]}"; do
        config=${ALGORITHM_CONFIG[$key]-}
        [[ -n $config ]] || die "unknown algorithm key '$key'. Available keys: ${!ALGORITHM_CONFIG[*]}"
        IFS='|' read -r executable directory unused <<< "$config"
        [[ -f "scratch/$directory/$executable.cc" ]] || die "missing source: scratch/$directory/$executable.cc"
    done
}

print_configuration() {
    local key config executable directory label reporting execution minimum maximum
    echo "Output directory: $OUTPUT_DIR"
    echo "Simulation time: ${SIMULATION_TIME}s"
    echo "Seeds: ${SEEDS[*]}"
    echo "Algorithms:"
    for key in "${SELECTED_ALGORITHMS[@]}"; do
        config=${ALGORITHM_CONFIG[$key]}
        IFS='|' read -r executable directory label reporting execution minimum maximum <<< "$config"
        printf '  - %-18s (%s; reporting=%s, execution=%s, minElevation=%s, maxDistance=%s)\n' \
            "$label" "$executable" "$reporting" "$execution" "$minimum" "$maximum"
    done
    echo
}

# Print TSV records: SUMMARY or UE. The parser follows the common per-UE table
# printed by every selected simulation program.
parse_log() {
    local label=$1 seed=$2 logfile=$3
    awk -v label="$label" -v seed="$seed" '
    function trim(value) { sub(/^[[:space:]]+/, "", value); sub(/[[:space:]]+$/, "", value); return value }
    function value(name, fallback, position) {
        position = header_index[name]
        return (position > 0 && position <= field_count) ? row[position] : fallback
    }
    BEGIN { in_table = 0; have_header = 0; overall_pdr = ""; ue_count = sum_latency = sum_ho_ok = sum_ho_fail = sum_mr = sum_pdr = sum_tput = 0; have_mr = 0 }
    {
        line = $0
        if (!in_table) {
            if (line ~ /Overall PDR:/) { overall_pdr = line; sub(/^.*Overall PDR:[[:space:]]*/, "", overall_pdr); sub(/%.*/, "", overall_pdr) }
            if (line ~ /PER-UE HANDOVER & PDR BREAKDOWN:/) { in_table = 1; have_header = 0 }
            next
        }
        line = trim(line)
        if (line == "" || line ~ /^-+$/) next
        if (!have_header && line ~ /^UE[[:space:]]+/) {
            header_count = split(line, header, /[[:space:]]+/)
            for (i = 1; i <= header_count; i++) header_index[header[i]] = i
            have_header = 1
            next
        }
        if (!have_header || line ~ /^TOTAL([[:space:]]|$)/ || line !~ /^UE-[0-9]+([[:space:]]|$)/) next
        field_count = split(line, row, /[[:space:]]+/)
        ue = row[1]; group = value("Group", ""); scenario = value("Scenario", ""); users = value("Users", "0")
        lat = value("Lat", "0"); lon = value("Lon", "0"); ho_ok = value("HO-OK", "0"); ho_fail = value("HO-Fail", "0")
        mr = value("MR-Report", "NA"); success = value("Successful-Rate%", "NA"); pdr = value("PDR%", "0")
        tput = value("Tput(Kbps)", "0"); latency = value("Latency(ms)", "0")
        ue_count++; sum_latency += latency + 0; sum_ho_ok += ho_ok + 0; sum_ho_fail += ho_fail + 0; sum_pdr += pdr + 0; sum_tput += tput + 0
        if (mr != "NA" && mr != "") { sum_mr += mr + 0; have_mr = 1 }
        printf "UE\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", label, seed, ue, group, scenario, users, lat, lon, ho_ok, ho_fail, mr, success, pdr, tput, latency
    }
    END {
        if (ue_count == 0) { printf "No per-UE result table found in %s\n", FILENAME > "/dev/stderr"; exit 1 }
        if (overall_pdr == "") overall_pdr = sum_pdr / ue_count
        mr_text = have_mr ? sprintf("%.3f", sum_mr) : "NA"
        success_text = (have_mr && sum_mr > 0) ? sprintf("%.3f", 100 * sum_ho_ok / sum_mr) : "NA"
        printf "SUMMARY\t%s\t%s\t%.3f\t%.3f\t%.3f\t%.3f\t%s\t%s\t%.3f\n", label, seed, overall_pdr + 0, sum_latency / ue_count, sum_ho_ok, sum_ho_fail, mr_text, success_text, sum_tput
    }' "$logfile"
}

run_algorithm() {
    local key=$1 config executable directory label reporting execution minimum maximum seed logfile parsed summary
    config=${ALGORITHM_CONFIG[$key]}
    IFS='|' read -r executable directory label reporting execution minimum maximum <<< "$config"
    echo "Running $label (${#SEEDS[@]} seed(s))..."
    for seed in "${SEEDS[@]}"; do
        logfile="$OUTPUT_DIR/${label}_seed${seed}.txt"
        printf '  seed=%-5s ' "$seed"
        if ! ./ns3 run "scratch/${directory}/${executable} --simTime=${SIMULATION_TIME} --reportingOffset=${reporting} --choExecutionOffset=${execution} --minElevation=${minimum} --maxDistance=${maximum} --timeToTrigger=1 --measurementPeriod=1 --seed=${seed}" >"$logfile" 2>&1; then
            die "simulation failed for $label (seed=$seed); see $logfile"
        fi
        parsed=$(parse_log "$label" "$seed" "$logfile")
        printf '%s\n' "$parsed" >> "$PARSED_TSV"
        summary=$(printf '%s\n' "$parsed" | awk -F '\t' '$1 == "SUMMARY" {printf "PDR=%s%%  Latency=%sms  HO-OK=%s  HO-Fail=%s  MR=%s  Success=%s%%  Tput=%sKbps", $4, $5, $6, $7, $8, $9, $10}')
        echo "$summary"
    done
    echo
}

print_summary() {
    echo "================================================================================================================================"
    echo "AVERAGED RESULTS OVER ${#SEEDS[@]} SEED(S)"
    echo "================================================================================================================================"
    printf '%-20s | %9s | %12s | %12s | %11s | %11s | %11s | %16s\n' \
        'Algorithm' 'PDR (%)' 'Latency(ms)' 'Tput(Kbps)' 'HO-OK' 'HO-Fail' 'MR-Report' 'Success Rate (%)'
    echo "--------------------------------------------------------------------------------------------------------------------------------"
    local key config label
    for key in "${SELECTED_ALGORITHMS[@]}"; do
        config=${ALGORITHM_CONFIG[$key]}
        IFS='|' read -r _ _ label _ <<< "$config"
        awk -F '\t' -v label="$label" '
            $1 == "SUMMARY" && $2 == label { pdr += $4; latency += $5; tput += $10; ho_ok += $6; ho_fail += $7; count++; if ($8 != "NA") { mr += $8; mr_count++ } }
            END {
                if (count == 0) exit 1
                mr_text = mr_count ? sprintf("%.3f", mr / mr_count) : "N/A"
                success = mr > 0 ? sprintf("%.3f", 100 * ho_ok / mr) : "N/A"
                printf "%-20s | %9.3f | %12.3f | %12.3f | %11.3f | %11.3f | %11s | %16s\n", label, pdr / count, latency / count, tput / count, ho_ok / count, ho_fail / count, mr_text, success
            }' "$PARSED_TSV"
    done
    echo "================================================================================================================================"
}

validate_configuration
if [[ $REPORT_ONLY -eq 1 ]]; then
    [[ -f $PARSED_TSV ]] || die "$PARSED_TSV not found."
else
    mkdir -p "$OUTPUT_DIR"
    : > "$PARSED_TSV"
fi

print_configuration
if [[ $REPORT_ONLY -eq 0 ]]; then
    for key in "${SELECTED_ALGORITHMS[@]}"; do
        run_algorithm "$key"
    done
fi

{
    print_summary
    echo
    echo "Raw simulation logs: $OUTPUT_DIR/"
    echo "Machine-readable metrics: $PARSED_TSV"
} | tee "$RESULT_FILE"

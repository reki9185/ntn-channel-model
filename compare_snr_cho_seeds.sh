#!/bin/bash
# Compare SNR-CHO variants across seeds and summarize both scenario-level
# and per-UE averages from the final per-UE breakdown table in each log.

# SEEDS=(1 2 18 55 38 45 78 99 123 150)
SEEDS=(18)
SCENARIO_NAMES=("SNR-CHO-Multi-TDCHO")
SCENARIO_DIRS=("MultiUE-CHO-SNR-TDCHO")
# SCENARIO_NAMES=("SNR-CHO-Multi" "SNR-CHO-Multi-TDCHO")
# SCENARIO_DIRS=("MultiUE-CHO-SNR" "MultiUE-CHO-SNR-TDCHO")

TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
OUTPUT_DIR="snr_cho_comparison_${TIMESTAMP}"
PARSED_TSV="$OUTPUT_DIR/_parsed_metrics.tsv"
RESULT_FILE="$OUTPUT_DIR/result.txt"

mkdir -p "$OUTPUT_DIR"
: > "$PARSED_TSV"

echo "Output directory: $OUTPUT_DIR"
echo ""

# Common simulation parameters
SIM_ARGS="--simTime=3600 --reportingOffset=1 --choExecutionOffset=3.0 --minElevation=20 --maxDistance=1500 --timeToTrigger=1 --measurementPeriod=1"

# Run one simulation and save log to $OUTPUT_DIR/<name>_seed<N>.txt
run_sim() {
    local name="$1"
    local seed="$2"
    local dir="$3"
    local logfile="$OUTPUT_DIR/${name}_seed${seed}.txt"
    ./ns3 run "scratch/${dir}/${name} ${SIM_ARGS} --seed=${seed}" > "$logfile" 2>&1
    echo "$logfile"
}

# Parse one log into tab-separated machine-readable records:
# SUMMARY <scenario> <seed> <pdr> <latency> <ho_ok> <ho_fail> <mr_report> <success_rate> <tput>
# UE      <scenario> <seed> <ue> <group> <scenario_col> <users> <lat> <lon> <ho_ok> <ho_fail> <mr_report> <success_rate> <pdr> <tput> <latency>
parse_log() {
    local scenario_name="$1"
    local seed="$2"
    local logfile="$3"

    awk -v scenario="$scenario_name" -v seed="$seed" '
    function trim(s) {
        sub(/^[[:space:]]+/, "", s)
        sub(/[[:space:]]+$/, "", s)
        return s
    }
    function field_value(name, default_value,    pos) {
        pos = headerIndex[name]
        if (pos > 0 && pos <= fieldCount) {
            return row[pos]
        }
        return default_value
    }
    BEGIN {
        inTable = 0
        haveHeader = 0
        overallPdr = ""
        ueCount = 0
        sumLatency = 0
        sumHoOk = 0
        sumHoFail = 0
        sumMr = 0
        haveMr = 0
        sumPdr = 0
        sumTput = 0
    }
    {
        line = $0

        if (!inTable) {
            if (line ~ /Overall PDR:/) {
                overallPdr = line
                sub(/^.*Overall PDR:[[:space:]]*/, "", overallPdr)
                sub(/%.*/, "", overallPdr)
            }
            if (line ~ /PER-UE HANDOVER & PDR BREAKDOWN:/) {
                inTable = 1
                haveHeader = 0
            }
            next
        }

        trimmed = trim(line)
        if (trimmed == "" || trimmed ~ /^-+$/) {
            next
        }

        if (!haveHeader && trimmed ~ /^UE[[:space:]]+/) {
            headerCount = split(trimmed, headerFields, /[[:space:]]+/)
            for (i = 1; i <= headerCount; i++) {
                headerIndex[headerFields[i]] = i
            }
            haveHeader = 1
            next
        }

        if (!haveHeader) {
            next
        }

        if (trimmed ~ /^TOTAL([[:space:]]|$)/) {
            next
        }

        if (trimmed ~ /^UE-[0-9]+([[:space:]]|$)/) {
            fieldCount = split(trimmed, row, /[[:space:]]+/)

            ue = row[1]
            group = field_value("Group", "")
            scenarioCol = field_value("Scenario", "")
            users = field_value("Users", "0")
            lat = field_value("Lat", "0")
            lon = field_value("Lon", "0")
            hoOk = field_value("HO-OK", "0")
            hoFail = field_value("HO-Fail", "0")
            mrReport = field_value("MR-Report", "NA")
            successRate = field_value("Successful-Rate%", "NA")
            pdr = field_value("PDR%", "0")
            tput = field_value("Tput(Kbps)", "0")
            latency = field_value("Latency(ms)", "0")

            ueCount++
            sumLatency += latency + 0
            sumHoOk += hoOk + 0
            sumHoFail += hoFail + 0
            sumPdr += pdr + 0
            sumTput += tput + 0
            if (mrReport != "NA" && mrReport != "") {
                sumMr += mrReport + 0
                haveMr = 1
            }

            printf "UE\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
                   scenario, seed, ue, group, scenarioCol, users, lat, lon,
                   hoOk, hoFail, mrReport, successRate, pdr, tput, latency
        }
    }
    END {
        avgLatency = ueCount > 0 ? sumLatency / ueCount : 0
        avgPdrFromUe = ueCount > 0 ? sumPdr / ueCount : 0
        if (overallPdr == "") {
            overallPdr = sprintf("%.3f", avgPdrFromUe)
        }

        if (haveMr && sumMr > 0) {
            computedSuccessRate = 100.0 * sumHoOk / sumMr
            mrValue = sprintf("%.3f", sumMr)
            srValue = sprintf("%.3f", computedSuccessRate)
        } else {
            mrValue = "NA"
            srValue = "NA"
        }

        printf "SUMMARY\t%s\t%s\t%.3f\t%.3f\t%.3f\t%.3f\t%s\t%s\t%.3f\n",
               scenario, seed, overallPdr + 0, avgLatency, sumHoOk, sumHoFail,
               mrValue, srValue, sumTput
    }' "$logfile"
}

run_scenario() {
    local name="$1"
    local dir="$2"

    echo "  Running $name (${#SEEDS[@]} seeds)..."
    for seed in "${SEEDS[@]}"; do
        local logfile
        local parsed
        local seed_summary

        printf "    seed=%-3s ... " "$seed"
        logfile=$(run_sim "$name" "$seed" "$dir")
        parsed=$(parse_log "$name" "$seed" "$logfile")
        printf "%s\n" "$parsed" >> "$PARSED_TSV"

        seed_summary=$(printf "%s\n" "$parsed" | awk -F'\t' '
            $1 == "SUMMARY" {
                printf "PDR=%s%%  Latency=%sms  HO-OK=%s  HO-Fail=%s  MR=%s  SR=%s%%  Tput=%sKbps",
                       $4, $5, $6, $7, $8, $9, $10
            }')
        echo "$seed_summary"
    done
}

print_scenario_summary_table() {
    echo "================================================================================================================================"
    echo "  AVERAGED RESULTS OVER ${#SEEDS[@]} SEEDS"
    echo "================================================================================================================================"
    printf "%-22s | %9s | %12s | %12s | %11s | %11s | %11s | %16s\n" \
        "Scenario" "PDR (%)" "Latency(ms)" "Tput(Kbps)" "HO-OK" "HO-Fail" "MR-Report" "Success Rate (%)"
    echo "--------------------------------------------------------------------------------------------------------------------------------"

    local i
    for i in "${!SCENARIO_NAMES[@]}"; do
        local scenario="${SCENARIO_NAMES[$i]}"
        awk -F'\t' -v scen="$scenario" '
            function format_or_na(value, count) {
                if (count > 0) {
                    return sprintf("%.3f", value / count)
                }
                return "N/A"
            }
            $1 == "SUMMARY" && $2 == scen {
                pdr += $4
                latency += $5
                tput += $10
                hoOk += $6
                hoFail += $7
                count++
                if ($8 != "NA") {
                    mr += $8
                    mrCount++
                }
            }
            END {
                avgPdr = format_or_na(pdr, count)
                avgLatency = format_or_na(latency, count)
                avgTput = format_or_na(tput, count)
                avgHoOk = format_or_na(hoOk, count)
                avgHoFail = format_or_na(hoFail, count)
                avgMr = format_or_na(mr, mrCount)
                if (mr > 0) {
                    avgSr = sprintf("%.3f", 100.0 * hoOk / mr)
                } else {
                    avgSr = "N/A"
                }

                printf "%-22s | %9s | %12s | %12s | %11s | %11s | %11s | %16s\n",
                       scen, avgPdr, avgLatency, avgTput, avgHoOk, avgHoFail, avgMr, avgSr
            }' "$PARSED_TSV"
    done
    echo "================================================================================================================================"
}

print_per_ue_table_for_scenario() {
    local scenario="$1"

    echo ""
    echo "--------------------------------------------------------------------------------------------------------------------------------"
    echo "  PER-UE AVERAGE OVER ${#SEEDS[@]} SEEDS: $scenario"
    echo "--------------------------------------------------------------------------------------------------------------------------------"
    printf "%-6s | %-13s | %5s | %7s | %7s | %8s | %8s | %9s | %16s | %8s | %12s | %12s\n" \
        "UE" "Group" "Users" "Lat" "Lon" "HO-OK" "HO-Fail" "MR-Rep" "Success Rate (%)" "PDR(%)" "Tput(Kbps)" "Latency(ms)"
    echo "--------------------------------------------------------------------------------------------------------------------------------"

    awk -F'\t' -v scen="$scenario" '
        $1 == "UE" && $2 == scen {
            ue = $4
            if (!(ue in seen)) {
                seen[ue] = 1
                group[ue] = $5
                users[ue] = $7
                lat[ue] = $8
                lon[ue] = $9
            }
            count[ue]++
            hoOk[ue] += $10
            hoFail[ue] += $11
            if ($12 != "NA") {
                mr[ue] += $12
                mrSeen[ue]++
            }
            pdr[ue] += $14
            tput[ue] += $15
            latency[ue] += $16
        }
        END {
            for (ue in count) {
                avgHoOk = hoOk[ue] / count[ue]
                avgHoFail = hoFail[ue] / count[ue]
                avgPdr = pdr[ue] / count[ue]
                avgTput = tput[ue] / count[ue]
                avgLatency = latency[ue] / count[ue]
                avgMr = (mrSeen[ue] > 0) ? mr[ue] / mrSeen[ue] : -1
                avgSr = (mr[ue] > 0) ? 100.0 * hoOk[ue] / mr[ue] : -1

                printf "%s\t%s\t%s\t%s\t%s\t%.3f\t%.3f\t%s\t%s\t%.3f\t%.3f\t%.3f\n",
                       ue, group[ue], users[ue], lat[ue], lon[ue],
                       avgHoOk, avgHoFail,
                       (avgMr >= 0 ? sprintf("%.3f", avgMr) : "N/A"),
                       (avgSr >= 0 ? sprintf("%.3f", avgSr) : "N/A"),
                       avgPdr, avgTput, avgLatency
            }
        }' "$PARSED_TSV" | sort -t$'\t' -k1,1V | awk -F'\t' '
            {
                printf "%-6s | %-13s | %5s | %7s | %7s | %8s | %8s | %9s | %16s | %8s | %12s | %12s\n",
                       $1, substr($2, 1, 13), $3, $4, $5, $6, $7, $8, $9, $10, $11, $12
            }'

    echo "--------------------------------------------------------------------------------------------------------------------------------"
}

print_per_ue_comparison_table() {
    if [ "${#SCENARIO_NAMES[@]}" -lt 2 ]; then
        return
    fi

    local base="${SCENARIO_NAMES[0]}"
    local test="${SCENARIO_NAMES[1]}"

    echo ""
    echo "---------------------------------------------------------------------------------------------------------------------"
    echo "  PER-UE COMPARISON (${test} - ${base})"
    echo "---------------------------------------------------------------------------------------------------------------------"
    printf "%-6s | %12s | %13s | %12s | %13s | %12s | %13s\n" \
        "UE" "Delta PDR" "Delta Latency" "Delta HO-OK" "Delta HO-Fail" "Delta MR-Rep" "Delta SR(%)"
    echo "---------------------------------------------------------------------------------------------------------------------"

    awk -F'\t' -v base="$base" -v test="$test" '
        $1 == "UE" {
            key = $2 SUBSEP $4
            scenario = $2
            ue = $4
            seen[ue] = 1
            count[key]++
            hoOk[key] += $10
            hoFail[key] += $11
            if ($12 != "NA") {
                mr[key] += $12
            }
            pdr[key] += $14
            latency[key] += $16
        }
        END {
            for (ue in seen) {
                baseKey = base SUBSEP ue
                testKey = test SUBSEP ue
                if (!(baseKey in count) || !(testKey in count)) {
                    continue
                }

                basePdr = pdr[baseKey] / count[baseKey]
                testPdr = pdr[testKey] / count[testKey]
                baseLatency = latency[baseKey] / count[baseKey]
                testLatency = latency[testKey] / count[testKey]
                baseHoOk = hoOk[baseKey] / count[baseKey]
                testHoOk = hoOk[testKey] / count[testKey]
                baseHoFail = hoFail[baseKey] / count[baseKey]
                testHoFail = hoFail[testKey] / count[testKey]
                baseMr = (mr[baseKey] > 0) ? mr[baseKey] / count[baseKey] : 0
                testMr = (mr[testKey] > 0) ? mr[testKey] / count[testKey] : 0
                baseSr = (mr[baseKey] > 0) ? 100.0 * hoOk[baseKey] / mr[baseKey] : 0
                testSr = (mr[testKey] > 0) ? 100.0 * hoOk[testKey] / mr[testKey] : 0

                printf "%s\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\n",
                       ue,
                       testPdr - basePdr,
                       testLatency - baseLatency,
                       testHoOk - baseHoOk,
                       testHoFail - baseHoFail,
                       testMr - baseMr,
                       testSr - baseSr
            }
        }' "$PARSED_TSV" | sort -t$'\t' -k1,1V | awk -F'\t' '
            {
                printf "%-6s | %12s | %13s | %12s | %13s | %12s | %13s\n",
                       $1, $2, $3, $4, $5, $6, $7
            }'

    echo "---------------------------------------------------------------------------------------------------------------------"
}

generate_report() {
    {
        print_scenario_summary_table
        local i
        for i in "${!SCENARIO_NAMES[@]}"; do
            print_per_ue_table_for_scenario "${SCENARIO_NAMES[$i]}"
        done
        print_per_ue_comparison_table
        echo ""
        echo "Individual logs saved in: $OUTPUT_DIR/"
        echo "Summary report saved in: $RESULT_FILE"
    } | tee "$RESULT_FILE"
}

# Run all configured scenarios
for i in "${!SCENARIO_NAMES[@]}"; do
    run_scenario "${SCENARIO_NAMES[$i]}" "${SCENARIO_DIRS[$i]}"
done

echo ""
generate_report

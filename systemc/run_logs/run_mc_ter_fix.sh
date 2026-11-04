#!/bin/bash
# Reruns Monte Carlo TER (1,000,000 samples) at every width, now using the
# corrected dropped-NAF/TER-BIN unsigned 0..2^N-1 value range (matching
# RCA/CLA/SD-TC's Monte Carlo methodology) instead of the old full-redundant
# signed-digit sampling. One width at a time so each gets all cores to itself.
set -e
cd "/mnt/storage/bachelor/adders/systemc/monte carlo"

run() {
    n="$1"
    bin="${n}bit/ter_${n}bit_pathEDP"
    log="/mnt/storage/bachelor/adders/systemc/run_logs/ter_mc_${n}_pathEDP.log"
    echo "=== starting ter_mc_${n}: $(date) ==="
    : > "$log"
    "$bin" 1000000 42 > "$log" 2>&1
    echo "=== finished ter_mc_${n}: $(date) ==="
    grep "pathEDP:" "$log" || echo "WARNING: no pathEDP summary line in $log"
}

for n in 2 4 8 12 16 32 64; do
    run "$n"
done

echo "ALL_MC_TER_FIX_DONE"

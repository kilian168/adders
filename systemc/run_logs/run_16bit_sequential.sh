#!/bin/bash
# Runs the 16-bit exhaustive path-aware EDP binaries ONE AT A TIME, so each
# gets all CPU cores to itself for its own fork-based multithreading, instead
# of 5 forked jobs (80 processes) fighting over 16 cores simultaneously.
set -e
cd /mnt/storage/bachelor/adders/systemc

run() {
    name="$1"
    bin="$2"
    log="run_logs/${name}_pathEDP.log"
    echo "=== starting $name: $(date) ==="
    : > "$log"
    "$bin" > "$log" 2>&1
    echo "=== finished $name: $(date) ==="
    grep "pathEDP:" "$log" || echo "WARNING: no pathEDP summary line in $log"
}

run rca_16   ./default/16bit/rca_16bit_pathEDP
run cla_16   ./default/16bit/cla_16bit_pathEDP
run ter_16   ./default/16bit/ter_2n_16bit_pathEDP
run sdtc_pm_16  ./default/16bit/sdtc_pm_16bit_pathEDP
run sdtc_ppm_16 ./default/16bit/sdtc_ppm_16bit_pathEDP

echo "ALL_16BIT_SEQUENTIAL_DONE"

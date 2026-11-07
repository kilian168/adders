#!/bin/bash
# Runs the full exhaustive_v2 suite (RCA, CLA, SD, SD/TC-PM x 2/4/8/12/16-bit),
# ONE COMBO AT A TIME (to avoid CPU oversubscription -- each combo's own
# warm-start fork already uses all cores). Logs each combo to its own file
# under run_logs/, and a running summary to run_logs/run_all_v2_summary.log.
#
# PAUSE / RESUME (freezes whatever is running right now, no progress lost):
#   kill -STOP -$(cat run_logs/run_all_v2.pid)   # pause
#   kill -CONT -$(cat run_logs/run_all_v2.pid)   # resume
# (the leading "-" sends the signal to the whole process group, so the
# currently-running adder binary -- and all its forked workers -- freeze
# together with the runner script itself).
set -e
cd "$(dirname "$0")"
mkdir -p run_logs
echo $$ > run_logs/run_all_v2.pid

SUMMARY=run_logs/run_all_v2_summary.log
: > "$SUMMARY"

run() {
    name="$1"
    bin="$2"
    log="run_logs/${name}.log"
    echo "=== starting $name: $(date) ===" | tee -a "$SUMMARY"
    : > "$log"
    "$bin" > "$log" 2>&1
    echo "=== finished $name: $(date) ===" | tee -a "$SUMMARY"
    grep "pathEDP:" "$log" | tee -a "$SUMMARY" || echo "WARNING: no pathEDP summary line in $log" | tee -a "$SUMMARY"
}

WIDTHS="${*:-2 4 8 12 16}"
for n in $WIDTHS; do
    run "rca_${n}"      "./${n}bit/rca_${n}bit_exhv2"
    run "cla_${n}"      "./${n}bit/cla_${n}bit_exhv2"
    run "sd_${n}"       "./${n}bit/sd_${n}bit_exhv2"
    run "sdtc_pm_${n}"  "./${n}bit/sdtc_pm_${n}bit_exhv2"
done

echo "ALL_EXHV2_DONE: $(date)" | tee -a "$SUMMARY"

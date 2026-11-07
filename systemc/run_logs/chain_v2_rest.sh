#!/bin/bash
until grep -q "CLA16 done" run_logs/v2_rca_cla_16.out 2>/dev/null; do
    sleep 10
done
echo "=== RCA/CLA-16 done, launching SD-16 and SDTC-PM-16: $(date) ==="
/tmp/v2_sd_16 > run_logs/v2_sd_16.log 2>&1
echo "=== SD-16 done: $(date) ==="
/tmp/v2_sdtc_pm_16 > run_logs/v2_sdtc_pm_16.log 2>&1
echo "=== SDTC-PM-16 done: $(date) ==="
echo "ALL_V2_DONE"

#!/bin/bash
cd "/mnt/storage/bachelor/adders/systemc/exhaustive v1 positive"
echo "=== starting exhaustive v1 positive (2 4 8 12 16): $(date) ==="
python3 run_exh.py 2 4 8 12 16 > run.log 2>&1
echo "=== finished exhaustive v1 positive: $(date) ==="
cd "/mnt/storage/bachelor/adders/systemc/exhaustive v2"
echo "=== starting exhaustive v2 (2 4 8 12 16): $(date) ==="
python3 run_exh.py 2 4 8 12 16 > run.log 2>&1
echo "=== finished exhaustive v2: $(date) ==="
echo "ALL_EXH_V1V2_DONE"

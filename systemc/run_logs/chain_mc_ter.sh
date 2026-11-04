#!/bin/bash
until grep -q "ALL_16BIT_SEQUENTIAL_DONE" run_logs/run_16bit_sequential.out 2>/dev/null; do
    sleep 10
done
echo "=== 16bit sequential done, launching MC TER fix: $(date) ==="
exec ./run_logs/run_mc_ter_fix.sh

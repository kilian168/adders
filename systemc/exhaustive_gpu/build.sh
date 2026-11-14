#!/bin/bash
# Builds sdtc_pm_exhaustive_gpu.cu. Requires nvcc (CUDA toolkit):
#   sudo pacman -S cuda
set -e
cd "$(dirname "$0")"
mkdir -p build
nvcc -O3 -arch=native -std=c++17 -o build/pm_exh_gpu sdtc_pm_exhaustive_gpu.cu
echo "built build/pm_exh_gpu"
echo
echo "Validate before trusting any wide-width output:"
echo "  ./build/pm_exh_gpu 8  delay_tables/pm_delay_8bit.txt"
echo "  ./build/pm_exh_gpu 12 delay_tables/pm_delay_12bit.txt"
echo "and diff AvgSwitches/AvgDelay_ns/AvgEDP_Correct against the matching rows in"
echo "  ../\"exhaustive v2\"/exhaustive_path_aware_edp_results_v2.csv"

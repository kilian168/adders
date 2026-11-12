#!/bin/bash
# Builds all pathEDP simulators of the v3 Monte Carlo test (SystemC 3.0.2 needs C++20).
# Same binaries serve all three representation modes (naf/binary/random) via argv[3].
cd "$(dirname "$0")"
for w in 2 4 8 12 16 32 64; do
  for a in sd rca cla sdtc_pm sdtc_ppm; do
    case $a in sdtc_*) f=${w}bit/sdtc_glitch_${a#sdtc_}_pathEDP.cpp;; *) f=${w}bit/${a}_glitch_pathEDP.cpp;; esac
    out=${w}bit/${a}_${w}bit_pathEDP
    [ -x "$out" ] && [ "$out" -nt "$f" ] && continue
    g++ -std=c++20 -O2 -I/usr/include -o "$out" "$f" -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc -pthread || echo "BUILD FAIL $out"
  done
done
echo BUILD DONE

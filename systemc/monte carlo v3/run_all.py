#!/usr/bin/env python3
"""Runs all v3 Monte Carlo simulators (1e6 samples, seed 42, 3 representation
modes for SD/PM/PPM) and writes path_aware_edp_results_v3.csv.
RCA/CLA are mode-independent (source identical to v2) -- run once per width,
same row repeated for all three modes."""
import subprocess, re, csv, os
here = os.path.dirname(os.path.abspath(__file__))
mode_archs = {'sd': 'SD', 'sdtc_pm': 'SD/TC-PM', 'sdtc_ppm': 'SD/TC-PPM'}
fixed_archs = {'rca': 'RCA', 'cla': 'CLA'}
MODES = ['naf', 'binary', 'random']

PAT = re.compile(
    r"pathEDP: MODE=(\S+) SAMPLES=(\d+) AVG_SWITCHES=(\S+) AVG_DELAY_ns=(\S+) "
    r"AVG_EDP_correct=(\S+) AVG_EDP_naive=(\S+) pct_diff=(\S+)% AVG_NONZERO_DIGITS=(\S+)")
PAT_FIXED = re.compile(
    r"pathEDP: SAMPLES=(\d+) AVG_SWITCHES=(\S+) AVG_DELAY_ns=(\S+) "
    r"AVG_EDP_correct=(\S+) AVG_EDP_naive=(\S+) pct_diff=(\S+)%")

rows = []
for w in [2, 4, 8, 12, 16, 32, 64]:
    lo, hi = -2**(w-1), 2**(w-1)-1

    for a, label in fixed_archs.items():
        exe = f"{here}/{w}bit/{a}_{w}bit_pathEDP"
        out = subprocess.run([exe], capture_output=True, text=True, cwd=f"{here}/{w}bit").stdout
        m = PAT_FIXED.search(out)
        assert m, (exe, out[-300:])
        n, s, d, e, nv, p = m.groups()
        for mode in MODES:
            rows.append([label, w, mode, n, s, d, e, nv, p, "", f"{lo}..{hi}"])
        print(label, w, "(mode-independent)", s, d, e, flush=True)

    for a, label in mode_archs.items():
        for mode in MODES:
            exe = f"{here}/{w}bit/{a}_{w}bit_pathEDP"
            out = subprocess.run([exe, "1000000", "42", mode], capture_output=True, text=True, cwd=f"{here}/{w}bit").stdout
            m = PAT.search(out)
            assert m, (exe, mode, out[-300:])
            mo, n, s, d, e, nv, p, nz = m.groups()
            assert mo == mode
            if label == "SD/TC-PPM":
                rng = f"SD operand {lo}..{hi} (rep={mode}); TC {lo}..{hi}"
            elif label.startswith("SD/TC"):
                rng = f"SD operand {lo}..{hi} (rep={mode}); TC {lo}..{hi}"
            else:
                rng = f"{lo}..{hi} (rep={mode})"
            rows.append([label, w, mode, n, s, d, e, nv, p, nz, rng])
            print(label, w, mode, s, d, e, "nonzero=" + nz, flush=True)

with open(f"{here}/path_aware_edp_results_v3.csv", "w", newline="") as f:
    f.write(
        "# Monte Carlo v3 (same value range as v2, -2^(N-1)..2^(N-1)-1; the REPRESENTATION\n"
        "# chosen for that value varies by mode, not the value itself -- RCA/CLA are\n"
        "# therefore completely unaffected and bit-identical to v2 in every mode).\n"
        "# RepMode=naf: v2's genuine Non-Adjacent Form (regression target, must match v2\n"
        "#   exactly -- verified separately, 0 mismatches across all widths/architectures).\n"
        "# RepMode=binary: the operand's own two's-complement bit pattern read as signed\n"
        "#   digits (bits 0..N-2 = 0/+1, top bit = 0/-1) -- same bits RCA/CLA already use.\n"
        "# RepMode=random: a uniformly random VALID N-digit representation of the same\n"
        "#   value, picked via the two-candidate recurrence (coin flip on a rng stream\n"
        "#   separate from the value stream, so naf/binary/random share the same values).\n"
        "# SD uses its own native dual-rail digit cell for both operands. PM/PPM: only the\n"
        "# SD-side operand's representation varies by mode; TC stays a plain random value.\n"
        "# AvgNonzeroDigits = mean number of nonzero digits per SD-side operand (explains\n"
        "# the AvgSwitches differences across modes directly).\n"
        "# Per-sample products switches_i*delay_i averaged (AvgEDP_Correct); AvgEDP_Naive = mean(switches)*mean(delay).\n"
    )
    w_ = csv.writer(f)
    w_.writerow(["Architecture", "Width", "RepMode", "Samples", "AvgSwitches", "AvgDelay_ns",
                 "AvgEDP_Correct", "AvgEDP_Naive", "PctDiff", "AvgNonzeroDigits", "ValueRange"])
    w_.writerows(rows)
print("FINISHED", f"{here}/path_aware_edp_results_v3.csv")

#!/usr/bin/env python3
"""Functional correctness check for every v3 adder architecture (RCA, CLA, SD,
SD/TC-PM, SD/TC-PPM) at every width, for every representation mode.

Each testbench already compares its gate-level result against the expected
arithmetic result for EVERY sample internally (number_of_errors / total_errors)
and would abort via assert(total_errors == 0) on any mismatch. This script
runs a dedicated sweep with a seed independent of the performance run
(path_aware_edp_results_v3.csv uses seed 42) and records the explicit sample
count / error count / pass-fail verdict for every (architecture, width, mode)
combination into functional_test_results_v3.csv.
"""
import subprocess, re, csv, os

here = os.path.dirname(os.path.abspath(__file__))
mode_archs = {'sd': 'SD', 'sdtc_pm': 'SD/TC-PM', 'sdtc_ppm': 'SD/TC-PPM'}
fixed_archs = {'rca': 'RCA', 'cla': 'CLA'}
MODES = ['naf', 'binary', 'random']
FUNCTIONAL_SEED = 777  # independent of the performance sweep's seed (42)
SAMPLES = 1000000

PAT = re.compile(r"pathEDP: MODE=(\S+) SAMPLES=(\d+) ERRORS=(\d+)")
PAT_FIXED = re.compile(r"pathEDP: SAMPLES=(\d+) ERRORS=(\d+)")

rows = []
for w in [2, 4, 8, 12, 16, 32, 64]:
    lo, hi = -2**(w-1), 2**(w-1)-1

    for a, label in fixed_archs.items():
        exe = f"{here}/{w}bit/{a}_{w}bit_pathEDP"
        out = subprocess.run([exe, str(SAMPLES), str(FUNCTIONAL_SEED)],
                              capture_output=True, text=True, cwd=f"{here}/{w}bit").stdout
        m = PAT_FIXED.search(out)
        assert m, (exe, out[-300:])
        n, errs = m.groups()
        verdict = "PASS" if errs == "0" else "FAIL"
        for mode in MODES:
            rows.append([label, w, mode, n, errs, verdict, f"{lo}..{hi}"])
        print(label, w, "(mode-independent)", "samples=" + n, "errors=" + errs, verdict, flush=True)

    for a, label in mode_archs.items():
        for mode in MODES:
            exe = f"{here}/{w}bit/{a}_{w}bit_pathEDP"
            out = subprocess.run([exe, str(SAMPLES), str(FUNCTIONAL_SEED), mode],
                                  capture_output=True, text=True, cwd=f"{here}/{w}bit").stdout
            m = PAT.search(out)
            assert m, (exe, mode, out[-300:])
            mo, n, errs = m.groups()
            assert mo == mode
            verdict = "PASS" if errs == "0" else "FAIL"
            if label == "SD/TC-PPM" or label.startswith("SD/TC"):
                rng = f"SD operand {lo}..{hi} (rep={mode}); TC {lo}..{hi}"
            else:
                rng = f"{lo}..{hi} (rep={mode})"
            rows.append([label, w, mode, n, errs, verdict, rng])
            print(label, w, mode, "samples=" + n, "errors=" + errs, verdict, flush=True)

n_fail = sum(1 for r in rows if r[5] == "FAIL")

with open(f"{here}/functional_test_results_v3.csv", "w", newline="") as f:
    f.write(
        "# Monte Carlo v3 functional correctness check: for every sample, the\n"
        "# testbench compares the gate-level pathEDP result against the expected\n"
        "# arithmetic sum and counts mismatches (Errors). Independent seed from\n"
        "# the performance sweep (path_aware_edp_results_v3.csv uses seed 42;\n"
        "# this run uses seed 777) so this is not just a replay of the same vectors.\n"
        "# RCA/CLA are mode-independent (representation only affects SD/PM/PPM's\n"
        "# own decoding); the same row is repeated for all three modes for easy\n"
        "# side-by-side comparison with the performance CSV.\n"
    )
    w_ = csv.writer(f)
    w_.writerow(["Architecture", "Width", "RepMode", "Samples", "Errors", "Result", "ValueRange"])
    w_.writerows(rows)

print(f"\nFINISHED {n_fail} FAILURES out of {len(rows)} rows ->",
      f"{here}/functional_test_results_v3.csv")

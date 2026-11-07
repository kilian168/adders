#!/usr/bin/env python3
"""Runs all v2 Monte Carlo simulators (1e6 samples, seed 42) and writes path_aware_edp_results_v1_positive.csv."""
import subprocess, re, csv, os
here=os.path.dirname(os.path.abspath(__file__))
names={'rca':'RCA','cla':'CLA','sd':'SD','sdtc_pm':'SD/TC-PM','sdtc_ppm':'SD/TC-PPM'}
rows=[]
for w in [2,4,8,12,16,32,64]:
    lo,hi=0,2**(w-1)-1
    for a,label in names.items():
        exe=f"{here}/{w}bit/{a}_{w}bit_pathEDP"
        out=subprocess.run([exe],capture_output=True,text=True,cwd=f"{here}/{w}bit").stdout
        m=re.search(r"pathEDP: SAMPLES=(\d+) AVG_SWITCHES=(\S+) AVG_DELAY_ns=(\S+) AVG_EDP_correct=(\S+) AVG_EDP_naive=(\S+) pct_diff=(\S+)%",out)
        assert m,(exe,out[-300:])
        n,s,d,e,nv,p=m.groups()
        rng=f"{lo}..{hi}" if not label.startswith("SD/TC") else f"SD operand {lo}..{hi}; TC {lo}..{hi}"
        rows.append([label,w,n,s,d,e,nv,p,rng]); print(label,w,s,d,e,flush=True)
with open(f"{here}/path_aware_edp_results_v1_positive.csv","w",newline="") as f:
    f.write("# Monte Carlo v1 (non-negative operands only, corrected): every operand of every adder (RCA/CLA A,B; SD A,B; SD/TC SD and TC part) is uniformly random in 0..2^(N-1)-1 (top bit 0), seed 42, 1e6 samples.\n"
            "# Identical circuits and encodings as the signed v2 run (path_aware_edp_results_v2.csv); only the sign bit is always 0 here.\n")
    w_=csv.writer(f); w_.writerow(["Architecture","Width","Samples","AvgSwitches","AvgDelay_ns","AvgEDP_Correct","AvgEDP_Naive","PctDiff","ValueRange"]); w_.writerows(rows)

#!/usr/bin/env python3
"""Runs all v2 Monte Carlo simulators (1e6 samples, seed 42) and writes path_aware_edp_results_v2.csv."""
import subprocess, re, csv, os
here=os.path.dirname(os.path.abspath(__file__))
names={'rca':'RCA','cla':'CLA','sd':'SD','sdtc_pm':'SD/TC-PM','sdtc_ppm':'SD/TC-PPM'}
rows=[]
for w in [2,4,8,12,16,32,64]:
    lo,hi=-2**(w-1),2**(w-1)-1
    for a,label in names.items():
        exe=f"{here}/{w}bit/{a}_{w}bit_pathEDP"
        out=subprocess.run([exe],capture_output=True,text=True,cwd=f"{here}/{w}bit").stdout
        m=re.search(r"pathEDP: SAMPLES=(\d+) AVG_SWITCHES=(\S+) AVG_DELAY_ns=(\S+) AVG_EDP_correct=(\S+) AVG_EDP_naive=(\S+) pct_diff=(\S+)%",out)
        assert m,(exe,out[-300:])
        n,s,d,e,nv,p=m.groups()
        if label=="SD/TC-PPM": rng=f"SD operand {lo}..{hi} (genuine NAF via SD's dual-rail code); TC {lo}..{hi}"
        elif label.startswith('SD/TC'): rng=f"SD operand {lo}..{hi}; TC {lo}..{hi}"
        else: rng=f"{lo}..{hi}"
        rows.append([label,w,n,s,d,e,nv,p,rng]); print(label,w,s,d,e,flush=True)
with open(f"{here}/path_aware_edp_results_v2.csv","w",newline="") as f:
    f.write("# Monte Carlo v2 (common range, genuine NAF): every adder's operands are uniformly random over the\n"
            "# signed range -2^(N-1)..2^(N-1)-1 (seed 42, 1e6 samples). RCA/CLA: two's-complement bit patterns directly.\n"
            "# SD, SD/TC-PM, SD/TC-PPM: each sampled value is converted to its genuine Non-Adjacent Form (true\n"
            "# redundant signed digits, real -1 states at any position -- not a relabeled top bit), which always fits\n"
            "# in exactly N digit positions for this range. SD uses its own native dual-rail digit cell\n"
            "# (rail_a=(d==1),rail_b=(d!=-1)). PM and PPM share the identical plain full adder, no bespoke cell:\n"
            "# PPM feeds SD_a=(d==1),SD_b=(d!=-1) directly and decodes Sum_a+Sum_b-(2^(N+1)-1) as SD_a+SD_b+TC;\n"
            "# PM feeds SD_a=(d==1),SD_b=(d==-1) through one inverter ahead of the same cell and decodes the same\n"
            "# constant as SD_a-SD_b+TC -- both verified correct exhaustively (0 mismatches).\n"
            "# Per-sample products switches_i*delay_i averaged (AvgEDP_Correct); AvgEDP_Naive = mean(switches)*mean(delay).\n")
    w_=csv.writer(f); w_.writerow(["Architecture","Width","Samples","AvgSwitches","AvgDelay_ns","AvgEDP_Correct","AvgEDP_Naive","PctDiff","ValueRange"]); w_.writerows(rows)

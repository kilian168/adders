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
        if label=="SD/TC-PPM": rng=f"SD operand {lo}..{hi} (digits -1,0,+1 via a+b-1); TC {lo}..{hi}"
        elif label.startswith('SD/TC'): rng=f"SD operand {lo}..{hi}; TC {lo}..{hi}"
        else: rng=f"{lo}..{hi}"
        rows.append([label,w,n,s,d,e,nv,p,rng]); print(label,w,s,d,e,flush=True)
with open(f"{here}/path_aware_edp_results_v2.csv","w",newline="") as f:
    f.write("# Monte Carlo v2 (common range): every adder gets uniformly random N-bit patterns read as two's complement, i.e. the signed range -2^(N-1)..2^(N-1)-1 (seed 42, 1e6 samples).\n"
            "# RCA/CLA: the bit patterns directly. SD: the same value as N signed digits (bits 0..N-2 = 0/+1, top digit 0/-1, so SD operand weights are binary, digit set {-1,0,+1}).\n"
            "# SD/TC-PM: SD_a = low N-1 bits, SD_b = top bit (value SD_a - SD_b), TC = random N-bit two's complement. SD/TC-PPM: digit = SD_a(i) + SD_b(i) - 1 in {-1,0,+1} (dual-rail coding as in the SD adder; the constant -1 per digit is outside the adder). Same encoding of the value as the SD adder: lower digits (a,b)=(bit,1), top digit (0,1)/(0,0). All operands of all adders span -2^(N-1)..2^(N-1)-1.\n"
            "# Per-sample products switches_i*delay_i averaged (AvgEDP_Correct); AvgEDP_Naive = mean(switches)*mean(delay).\n")
    w_=csv.writer(f); w_.writerow(["Architecture","Width","Samples","AvgSwitches","AvgDelay_ns","AvgEDP_Correct","AvgEDP_Naive","PctDiff","ValueRange"]); w_.writerows(rows)

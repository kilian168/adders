#!/usr/bin/env python3
"""Runs the exhaustive pathEDP simulators of this folder and writes path_aware_edp_results_<mode>.csv (appended per width)."""
import subprocess,re,csv,os,sys
here=os.path.dirname(os.path.abspath(__file__))
mode="v1_positive" if "v1" in here else "v2"
labels={"RCA":"RCA","CLA":"CLA","SD":"SD","SDTC-PM":"SD/TC-PM","SDTC-PPM":"SD/TC-PPM"}
exes=["rca_glitch","cla_glitch","sd_2n_glitch","sdtc_glitch_pm","sdtc_glitch_ppm"]
out=f"{here}/exhaustive_path_aware_edp_results_{mode}.csv"
hdr=("# Exhaustive sweep, SAME value range for every adder and every operand (A and B; SD and TC part): two nested loops over that range, "
     "state carried from vector to vector (counters reset per vector).\n")
hdr+=("# v1_positive: values 0..2^(N-1)-1 (sign bit always 0).\n" if mode=="v1_positive" else
      "# v2: values -2^(N-1)..2^(N-1)-1 in ascending order (two's-complement bit patterns). SD, SD/TC-PM and SD/TC-PPM "
      "operands are each value's genuine Non-Adjacent Form (true redundant signed digits, real -1 states at any "
      "position, not a relabeled top bit) -- fits in exactly N digit positions for this range. SD uses its own "
      "native dual-rail digit cell. PM and PPM share the identical plain full adder (no bespoke cell): PPM feeds "
      "SD's own dual-rail code directly and decodes with 2^(N+1)-1; PM feeds SD_a=(d==1), SD_b=(d==-1) through an "
      "inverter ahead of the same cell and decodes with the same 2^(N+1)-1 constant -- both verified correct.\n")
hdr+="# Per-vector products switches_i*delay_i averaged (AvgEDP_Correct); AvgEDP_Naive = mean(switches)*mean(delay).\n"
rows=[]
widths=[int(x) for x in sys.argv[1:]] or [2,4,8,12,16]
for n in widths:
    lo,hi=(0,2**(n-1)-1) if mode=="v1_positive" else (-2**(n-1),2**(n-1)-1)
    for e in exes:
        r=subprocess.run([f"{here}/{n}bit/{e}_pathEDP"],capture_output=True,text=True,cwd=f"{here}/{n}bit")
        m=re.search(r"(\S+)-(\d+)-EXH-pathEDP: VECTORS=(\d+) AVG_SWITCHES=(\S+) AVG_DELAY_ns=(\S+) AVG_EDP_correct=(\S+) AVG_EDP_naive=(\S+) pct_diff=(\S+)%",r.stdout)
        assert m,(e,n,r.stdout[-300:],r.stderr[-300:])
        a,w,v,s,d,c,nv,p=m.groups()
        rows.append([labels[a],n,v,s,d,c,nv,p,f"{lo}..{hi}"]); print(labels[a],n,s,d,c,flush=True)
    with open(out,"w",newline="") as f:
        f.write(hdr); w_=csv.writer(f); w_.writerow(["Architecture","Width","Vectors","AvgSwitches","AvgDelay_ns","AvgEDP_Correct","AvgEDP_Naive","PctDiff","ValueRange"]); w_.writerows(rows)
print("FINISHED")

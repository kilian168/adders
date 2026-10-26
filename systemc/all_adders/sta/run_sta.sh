#!/usr/bin/env bash
# Synthesizes each ICSC-generated .sv (behavioral gate netlist) onto the
# Nangate45 standard-cell library with Yosys, then runs OpenSTA on the
# resulting cell-level netlist to get a real critical-path delay number
# per architecture/bit-width.
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SV_OUT="$HERE/../build"   # where rca_8bit.sv etc. live (per width, in sv_out/)
LIB="$HERE/nangate45_slow.lib"
NETLIST_DIR="$HERE/netlist"
REPORT_DIR="$HERE/reports"
SUMMARY="$HERE/summary.csv"

PREPROC_DIR="$HERE/preproc"
mkdir -p "$NETLIST_DIR" "$REPORT_DIR" "$PREPROC_DIR"

# Yosys's SV frontend rejects unpacked single-bit-array *ports*
# (e.g. "input logic PG[2]", produced for ClaTree's sc_vector<sc_in<bool>>
# ports). Rewriting only the port declarations to packed-vector syntax
# isn't enough, though: any internal wire connected straight through to
# such a port (e.g. "logic blk_pg[2]; ... .PG(blk_pg)") must then also be
# packed, or Yosys rejects the now-mismatched connection. So convert
# every single-bit unpacked array declaration (ports AND internal
# wires -- but not multi-bit ones like "logic [3:0] a_slice[2]", which
# Yosys already handles fine) to packed-vector form consistently, in a
# throwaway copy fed to Yosys only. Bit-indexed usage elsewhere in the
# file (PG[0], blk_pg[1], ...) is valid for a packed vector too, so this
# is a syntax-only adaptation with no effect on the real ICSC-generated
# .sv deliverable in build/.
preprocess_for_yosys() {
  python3 -c "
import re, sys
src, dst = sys.argv[1], sys.argv[2]
with open(src) as f:
    text = f.read()
def fix(m):
    indent, dirn, name, n, punct = m.groups()
    n = int(n)
    dirn = dirn or ''
    return f'{indent}{dirn}logic [{n-1}:0] {name}{punct}'
text = re.sub(r'^(\s*)(input\s+|output\s+)?logic\s+(\w+)\[(\d+)\]([,;]?)\s*$', fix, text, flags=re.MULTILINE)
with open(dst, 'w') as f:
    f.write(text)
" "$1" "$2"
}

widths=(2 4 8 12 16 32 64)
# arch -> top module name template ('%d' replaced by width)
declare -A TOPFMT=(
  [rca]="RippleCarryAdder%d"
  [cla]="CarryLookaheadAdder%d"
  [ter]="BalancedTernaryAdder%d"
  [sdtc_pm]="SdTcAdderPM%d"
  [sdtc_ppm]="SdTcAdderPPM%d"
)
archs=(rca cla ter sdtc_pm sdtc_ppm)

echo "arch,width,top_module,synth_ok,sta_ok,critical_path_ns" > "$SUMMARY"

for w in "${widths[@]}"; do
  for arch in "${archs[@]}"; do
    top=$(printf "${TOPFMT[$arch]}" "$w")
    sv="$SV_OUT/${w}bit/sv_out/${arch}_${w}bit.sv"
    netlist="$NETLIST_DIR/${arch}_${w}bit_synth.v"
    ys_log="$REPORT_DIR/${arch}_${w}bit_synth.log"
    sta_log="$REPORT_DIR/${arch}_${w}bit_sta.log"

    if [ ! -f "$sv" ]; then
      echo "$arch,$w,$top,MISSING_SV,-,-" >> "$SUMMARY"
      continue
    fi

    # ---- 1. Synthesize onto Nangate45 with Yosys ----
    sv_pp="$PREPROC_DIR/${arch}_${w}bit.sv"
    preprocess_for_yosys "$sv" "$sv_pp"
    yosys -q -p "
      read_verilog -sv $sv_pp
      hierarchy -top $top
      synth -top $top
      abc -liberty $LIB
      clean -purge
      write_verilog -noattr $netlist
    " > "$ys_log" 2>&1
    synth_ok="FAIL"
    [ -f "$netlist" ] && synth_ok="OK"

    if [ "$synth_ok" != "OK" ]; then
      echo "$arch,$w,$top,FAIL,-,-" >> "$SUMMARY"
      echo "SYNTH FAIL: $arch $w (see $ys_log)"
      continue
    fi

    # ---- 2. Run OpenSTA on the synthesized netlist ----
    sta -no_init -exit > "$sta_log" 2>&1 <<EOF
read_liberty $LIB
read_verilog $netlist
link_design $top
create_clock -name vclk -period 100
set_input_delay -clock vclk 0 [all_inputs]
set_output_delay -clock vclk 0 [all_outputs]
report_checks -path_delay max -fields {slew cap} -digits 4
EOF
    sta_ok="FAIL"
    delay="-"
    if grep -q "data arrival time" "$sta_log"; then
      sta_ok="OK"
      # last "data arrival time" line's trailing number is the critical path delay
      delay=$(grep "data arrival time" "$sta_log" | tail -1 | grep -oE '[0-9]+\.[0-9]+' | tail -1)
    fi

    echo "$arch,$w,$top,$synth_ok,$sta_ok,$delay" >> "$SUMMARY"
    echo "$arch $w -> synth=$synth_ok sta=$sta_ok delay=${delay}ns"
  done
done

echo ""
echo "=== Summary written to $SUMMARY ==="
cat "$SUMMARY"

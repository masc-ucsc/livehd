#!/usr/bin/env bash
set -euo pipefail
# RTL differential for an ASYNCHRONOUS reset that is a combinational function
# of primary inputs (decision 2B; CORE-ET B05's DFT reset mux
# `rst_n = scanmode ? scan_rst_n : rst_ni`).
#
# `SourceDesc.flopQAsync` reads its reset from ONE input ordinal, so the
# exporter reads such a flop through an override node
# `MuxBool(rst, Q, resetValue)` and keeps `FlopDesc.resetPin`/`asyncReset` on
# the commit side. This compares the certificate's `interpretDesign`
# (Futamura_Projection_Scripts/cert_trace.lean) against iverilog on the ORIGINAL RTL, step by step,
# under two clock schedules:
#   all    every step clocks
#   third  every third step is QUIET -- where an asynchronous reset still acts
#          and a synchronous one does not
# and two negative controls that must DIFFER: the same RTL with a synchronous
# reset, and with the DFT mux's arms swapped.
#
# The Lean toolchain pin lives in formal/lean; lean runs from there.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/reset_expr_oracle}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v iverilog >/dev/null && command -v vvp >/dev/null || { echo "iverilog/vvp not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"

cat > "$OUT/arst.sv" <<'EOF'
module arst(input clk, input scanmode, input scan_rst_n, input rst_ni, input [7:0] d,
            output [7:0] q, output [7:0] r);
  wire rst_n = scanmode ? scan_rst_n : rst_ni;   // CORE-ET's DFT reset mux
  reg [7:0] a;
  always @(posedge clk or negedge rst_n) if (!rst_n) a <= 8'h5A; else a <= d;
  assign q = a;            // the async override is visible in the same step
  assign r = a + 8'd1;     // ...to a combinational reader too
endmodule
EOF
sed 's/always @(posedge clk or negedge rst_n)/always @(posedge clk)/' "$OUT/arst.sv" > "$OUT/arst_sync.sv"
sed 's/scanmode ? scan_rst_n : rst_ni/scanmode ? rst_ni : scan_rst_n/' "$OUT/arst.sv" > "$OUT/arst_swap.sv"
for v in sync swap; do
  cmp -s "$OUT/arst.sv" "$OUT/arst_$v.sv" && { echo "FAIL: the $v control did not apply"; exit 1; }
done

"$LHD" compile "$OUT/arst.sv" --reader yosys-slang --top arst --set yosys.setundef=zero --emit-dir "lg:$OUT/lg" \
  --workdir "$OUT/cw" > "$OUT/compile.log" 2>&1 || { tail -5 "$OUT/compile.log"; echo "FAIL: compile"; exit 1; }
"$LHD" compile "lg:$OUT/lg" --top arst --workdir "$OUT/lw" --emit-dir "lean:$OUT/lean" \
  --set formal.lean.mode=verified_compiler > "$OUT/lean.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$OUT/lean.log" | head -3; echo "FAIL: certificate emission refused the input-derived async reset"; exit 1; }
grep -q 'resetActiveLow := true, clock := 0, asyncReset := true' "$OUT/lean/arst_Lgraph.lean" \
  || { echo "FAIL: the flop is not an active-low ASYNC flop in the certificate"; exit 1; }
grep -q 'SourceDesc.flopQAsync' "$OUT/lean/arst_Lgraph.lean" \
  && { echo "FAIL: a flopQAsync source reads a raw input as the reset"; exit 1; }
echo "ok: emitted; the reset is a computed slot (no flopQAsync on a raw input)"

# 40 cycles: a reset in cycle 0, DFT mode on in cycles with k % 7 in {3,4}
# (scan_rst_n then decides), functional resets in cycles 9, 21 and 22.
python3 -I - "$OUT" <<'PY'
import random, sys
out = sys.argv[1]
random.seed(7)
rows = []
for c in range(40):
    sm = 1 if (c % 7) in (3, 4) else 0
    sr = random.choice([0, 1, 1]) if c else 1
    rn = 0 if c in (0, 9, 21, 22) else 1
    rows.append((c, sm, sr, rn, random.randrange(256)))
with open(f'{out}/stim.txt', 'w') as f:
    f.write('stim v1\n')
    for c, sm, sr, rn, d in rows:
        f.write(f'at {c} in 0 1 0\nat {c} in 1 8 {d}\nat {c} in 2 1 {rn}\nat {c} in 3 1 {sr}\nat {c} in 4 1 {sm}\n')
for mode in ('all', 'third'):
    with open(f'{out}/tb_{mode}.v', 'w') as f:
        f.write('`timescale 1ns/1ps\nmodule tb;\n reg clk=0, scanmode=0, scan_rst_n=1, rst_ni=1; reg [7:0] d=0;\n wire [7:0] q, r;\n')
        f.write(' arst u(.clk(clk), .scanmode(scanmode), .scan_rst_n(scan_rst_n), .rst_ni(rst_ni), .d(d), .q(q), .r(r));\n')
        f.write(' initial begin\n')
        for c, sm, sr, rn, d in rows:
            # inputs settle at 2, outputs sampled at 8, the edge (if any) at 10
            f.write(f'  #({20*c+2} - $time); d={d}; rst_ni={rn}; scan_rst_n={sr}; scanmode={sm};\n')
            f.write(f'  #({20*c+8} - $time); $display("STEP {c} %0d %0d", q, r);\n')
            if not (mode == 'third' and c % 3 == 2):
                f.write(f'  #({20*c+10} - $time); clk=1; #({20*c+15} - $time); clk=0;\n')
        f.write('  $finish;\n end\nendmodule\n')
PY

LP=$(cd "$ROOT/formal/lean" && lake env printenv LEAN_PATH)
rtl() { # <source.sv> <mode> <out>
  iverilog -g2012 -o "$OUT/$3.vvp" "$1" "$OUT/tb_$2.v" && vvp "$OUT/$3.vvp" | grep '^STEP' > "$OUT/$3.txt"
}
for mode in all third; do
  rtl "$OUT/arst.sv" "$mode" "rtl_$mode"
  (cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run Futamura_Projection_Scripts/cert_trace.lean \
     "$OUT/lean/arst.dcert" 40 "$OUT/stim.txt" "edges:$mode" > "$OUT/cert_$mode.txt" 2> "$OUT/cert_$mode.err") \
    || { cat "$OUT/cert_$mode.err"; echo "FAIL: cert_trace"; exit 1; }
  [[ $(wc -l < "$OUT/rtl_$mode.txt") -eq 40 ]] || { echo "FAIL: the RTL run did not produce 40 steps"; exit 1; }
  diff "$OUT/rtl_$mode.txt" "$OUT/cert_$mode.txt" > "$OUT/diff_$mode.txt" \
    || { head -5 "$OUT/diff_$mode.txt"; echo "FAIL: certificate and RTL disagree (edges:$mode)"; exit 1; }
  echo "ok: edges:$mode -- 40 steps, RTL and certificate identical ($(grep -c ' 90 91$' "$OUT/cert_$mode.txt") in reset)"
done
for v in sync swap; do
  rtl "$OUT/arst_$v.sv" third "ctl_$v"
  if diff -q "$OUT/ctl_$v.txt" "$OUT/cert_third.txt" > /dev/null; then
    echo "FAIL: the $v control AGREED with the certificate -- the comparison cannot see it"; exit 1
  fi
  echo "ok: control $v differs ($(diff "$OUT/ctl_$v.txt" "$OUT/cert_third.txt" | grep -c '^<') of 40 steps)"
done
echo "PASS: reset_expr_oracle"

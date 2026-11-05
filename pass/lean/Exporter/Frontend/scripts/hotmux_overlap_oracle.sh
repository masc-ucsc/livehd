#!/usr/bin/env bash
set -euo pipefail
# What each Lean simulator does when a Hotmux's controls overlap (G4).
#
# A Hotmux's controls are one-hot by contract. pass.formal refuses a top-level
# overlap it can show from free inputs, but one that needs a particular register
# state is only DEFERRED ("kept as a runtime check"), and the design is exported.
# This fixture is that case: `unique case (1'b1)` over two REGISTERED controls.
#
# The exporter lowers a Hotmux to a first-active-arm Mux chain
# (design_scan.cpp, lower_hotmux), so every Lean route takes the FIRST active
# arm; so does `lhd sim` (lhd/tests/lhd_sim_hotmux_overlap_test.sh) and Icarus
# on the source (a case statement takes its first matching item). The ABC
# blaster is the one place that ORs the active arms instead
# (pass/synth/blast_mux_smoke.cpp, HotmuxWithTwoActiveArmsOrsTheirValues).
#
# Compared step by step against iverilog on the ORIGINAL RTL:
#   IR      cert_trace (interpretDesign), every clock firing
#   VC      cert_trace route:vc (compileDesign)
#   legacy  the legacy fast model (make_legacy_sim.py --stim), compiled with
#           `lean -R` into this run's directory
#   FP      dcert2_memroute `outputs`; FP's `edges:all` leaves every third step
#           quiet, so it is compared with cert_trace `edges:third`
# Two controls must DIFFER from the IR trace, which shows that the stimulus
# really overlaps the controls and that the comparison tells the answers apart:
#   rtl_or      the RTL with the arms ORed together (the blaster's answer)
#   rtl_second  the RTL with the two arms swapped (the second arm wins)
#
# The Lean toolchain pin lives in formal/lean; lean runs from there.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/hotmux_overlap_oracle}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v iverilog >/dev/null && command -v vvp >/dev/null || { echo "iverilog/vvp not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"
CYCLES=32

rtl() { # <file> <case body>
  cat > "$1" <<EOF
module hot_overlap(input logic clk, input logic p, input logic q,
                   input logic [7:0] a, input logic [7:0] b, input logic [7:0] c,
                   output logic [7:0] res);
  logic pr, qr;
  always_ff @(posedge clk) begin
    pr <= p;
    qr <= q;
  end
  always_comb begin
$2
  end
endmodule
EOF
}
rtl "$OUT/hot.sv" '    unique case (1'"'"'b1)
      pr: res = a;
      qr: res = b;
      default: res = c;
    endcase'
rtl "$OUT/hot_or.sv" '    res = (pr ? a : 8'"'"'d0) | (qr ? b : 8'"'"'d0) | ((pr || qr) ? 8'"'"'d0 : c);'
rtl "$OUT/hot_second.sv" '    unique case (1'"'"'b1)
      qr: res = b;
      pr: res = a;
      default: res = c;
    endcase'

"$LHD" compile "$OUT/hot.sv" --reader slang --top hot_overlap --emit-dir "lg:$OUT/lg" --workdir "$OUT/cw" > "$OUT/compile.log" 2>&1 \
  || { tail -5 "$OUT/compile.log"; echo "FAIL: compile"; exit 1; }
grep -q '"code":"onehot-deferred"' "$OUT/compile.log" || { echo "FAIL: expected the one-hot check to be DEFERRED"; exit 1; }
NH=$("$LHD" tool grep "lg:$OUT/lg" kind=hotmux 2>/dev/null | grep -c '"t":"node"' || true)
[[ $NH -eq 1 ]] || { echo "FAIL: expected one Hotmux in the graph (got $NH)"; exit 1; }
"$LHD" compile "lg:$OUT/lg" --top hot_overlap --workdir "$OUT/vw" --emit-dir "lean:$OUT/vc" \
  --set formal.lean.mode=verified_compiler > "$OUT/vc.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$OUT/vc.log" | head -3; echo "FAIL: certificate emission"; exit 1; }
"$LHD" compile "lg:$OUT/lg" --top hot_overlap --workdir "$OUT/lw" --emit-dir "lean:$OUT/legacy" \
  --set formal.lean.emit_cert=true --set formal.lean.emit_fast_bridge=true > "$OUT/legacy.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$OUT/legacy.log" | head -3; echo "FAIL: legacy emission"; exit 1; }
echo "ok: compiled (one Hotmux, one-hot check deferred), certificate and legacy model emitted"

python3 -I - "$OUT" "$CYCLES" <<'PY'
import json, random, sys
out, cycles = sys.argv[1], int(sys.argv[2])
io = json.load(open(f'{out}/vc/hot_overlap_io.json'))
ins = {i['name']: (i['ordinal'], i['width']) for i in io['inputs']}
outs = sorted(io['outputs'], key=lambda o: o['ordinal'])
random.seed(31)
# The controls are registered: step k shows the (p, q) of step k-1. The first
# steps walk every pair, both-active twice; the rest are random.
pq = [(0, 0), (1, 1), (1, 0), (0, 1), (1, 1), (0, 0)]
rows = [dict(p=pq[k][0] if k < len(pq) else random.randrange(2),
             q=pq[k][1] if k < len(pq) else random.randrange(2),
             a=random.randrange(256), b=random.randrange(256), c=random.randrange(256)) for k in range(cycles)]
with open(f'{out}/stim.txt', 'w') as f:
    f.write('stim v1\n')
    for k, r in enumerate(rows):
        for name, v in r.items():
            o, w = ins[name]
            f.write(f'at {k} in {o} {w} {v}\n')
with open(f'{out}/tb.v', 'w') as f:
    f.write('`timescale 1ns/1ps\nmodule tb;\n reg clk=0; reg p=0; reg q=0; reg [7:0] a=0; reg [7:0] b=0; reg [7:0] c=0;\n')
    for o in outs:
        f.write(f' wire [{o["width"]-1}:0] {o["name"]};\n')
    f.write(' hot_overlap u(.clk(clk), .p(p), .q(q), .a(a), .b(b), .c(c)'
            + ''.join(f', .{o["name"]}({o["name"]})' for o in outs) + ');\n')
    # The registers start at 0, as the Lean start state (no reset: reset value 0).
    f.write(' initial begin\n  u.pr = 0; u.qr = 0;\n')
    fmt = ' '.join('%0d' for _ in outs)
    for k, r in enumerate(rows):
        f.write(f'  #({10*k+1} - $time); p={r["p"]}; q={r["q"]}; a={r["a"]}; b={r["b"]}; c={r["c"]};\n')
        f.write(f'  #({10*k+4} - $time); $display("STEP {k} {fmt}", {", ".join(o["name"] for o in outs)});\n')
        f.write(f'  #({10*k+5} - $time); clk=1;\n  #({10*k+8} - $time); clk=0;\n')
    f.write('  $finish;\n end\nendmodule\n')
overlap = sum(1 for k in range(1, cycles) if rows[k-1]['p'] and rows[k-1]['q'])
print(f'ok: stimulus written; {overlap} of {cycles} steps have both registered controls active')
PY

sim() { # <rtl> <out>
  iverilog -g2012 -o "$OUT/$2.vvp" "$1" "$OUT/tb.v" 2> "$OUT/$2.iverilog.err" && vvp "$OUT/$2.vvp" | grep '^STEP' > "$OUT/$2.txt"
}
sim "$OUT/hot.sv" rtl
grep -q 'x' "$OUT/rtl.txt" && { grep -m3 'x' "$OUT/rtl.txt"; echo "FAIL: the RTL produced X"; exit 1; }
[[ $(wc -l < "$OUT/rtl.txt") -eq $CYCLES ]] || { echo "FAIL: the RTL run did not produce $CYCLES steps"; exit 1; }

cd "$ROOT/formal/lean"
LP=$(lake env printenv LEAN_PATH)
ct() { # <out> <options...>
  local o=$1; shift
  (ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run Futamura_Projection_Scripts/cert_trace.lean \
     "$OUT/vc/hot_overlap.dcert" "$CYCLES" "$OUT/stim.txt" "$@" > "$OUT/$o.txt" 2> "$OUT/$o.err") \
    || { cat "$OUT/$o.err"; echo "FAIL: cert_trace $*"; exit 1; }
}
same() { # <a> <b> <label>
  diff "$OUT/$1.txt" "$OUT/$2.txt" > "$OUT/diff_$1_$2.txt" || { head -5 "$OUT/diff_$1_$2.txt"; echo "FAIL: $3"; exit 1; }
  echo "ok: $3"
}
ct ir edges:all
same rtl ir "IR == RTL, $CYCLES steps (the first active arm)"
ct vc edges:all route:vc
same rtl vc "VC == RTL"

# legacy, compiled outside the shared lake workspace
LR="$OUT/lroot"; OL="$OUT/olean"
mkdir -p "$LR/LegacyHot" "$OL/LegacyHot"
cp "$OUT/legacy/hot_overlap_Lgraph.lean" "$LR/LegacyHot/"
python3 -I "$ROOT/pass/lean/Legacy_Fast_Model_Simulator/scripts/make_legacy_sim.py" "$LR/LegacyHot/hot_overlap_Lgraph.lean" \
  --cycles "$CYCLES" --stim --out "$LR/LegacyHot" --module-prefix LegacyHot > "$OUT/legacy_gen.log" 2>&1 \
  || { tail -3 "$OUT/legacy_gen.log"; echo "FAIL: make_legacy_sim"; exit 1; }
(ulimit -s unlimited && LEAN_PATH="$LP:$OL" lean -R "$LR" -o "$OL/LegacyHot/hot_overlap_Lgraph.olean" \
   "$LR/LegacyHot/hot_overlap_Lgraph.lean" > "$OUT/legacy_build.log" 2>&1) \
  || { grep -m5 error "$OUT/legacy_build.log"; echo "FAIL: legacy elaboration (bridge proofs)"; exit 1; }
grep -q sorry "$OUT/legacy_build.log" && { echo "FAIL: sorry in the legacy proofs"; exit 1; }
(ulimit -s unlimited && LEAN_PATH="$LP:$OL" lean --run "$LR/LegacyHot/hot_overlap_LegacySim.lean" "$OUT/stim.txt" \
   > "$OUT/legacy.txt" 2> "$OUT/legacy.err") || { cat "$OUT/legacy.err"; echo "FAIL: legacy run"; exit 1; }
same rtl legacy "legacy == RTL"

# FP: its `edges:all` is cert_trace's `edges:third`
ct ir_third edges:third
(ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run Futamura_Projection_Scripts/dcert2_memroute.lean \
   "$OUT/vc/hot_overlap.dcert" 20000 200 "$CYCLES" zero edges:all "stim:$OUT/stim.txt" outputs nocompare \
   > "$OUT/fp.log" 2> "$OUT/fp.err") || { tail -3 "$OUT/fp.log"; cat "$OUT/fp.err"; echo "FAIL: FP runner"; exit 1; }
grep '^STEP' "$OUT/fp.log" > "$OUT/fp.txt"
same ir_third fp "FP == IR with every third step quiet"

cd "$OUT"
for ctl in rtl_or:hot_or.sv rtl_second:hot_second.sv; do
  name=${ctl%%:*}; src=${ctl##*:}
  sim "$OUT/$src" "ctl_$name"
  if diff -q "$OUT/ctl_$name.txt" "$OUT/ir.txt" > /dev/null; then
    echo "FAIL: the $name control AGREED -- the stimulus does not tell the answers apart"; exit 1
  fi
  echo "ok: control $name differs ($(diff "$OUT/ctl_$name.txt" "$OUT/ir.txt" | grep -c '^<') of $CYCLES steps)"
done
echo "PASS: hotmux_overlap_oracle"

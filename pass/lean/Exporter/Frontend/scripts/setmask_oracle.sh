#!/usr/bin/env bash
set -euo pipefail
# Differential for Op_SetMask (D6 Tier 1: the projection simulator executes it).
#
# The Verilog readers never build a Set_mask -- yosys lowers a partial
# assignment to shifts and masks -- so the fixture is Pyrope, whose bit-range
# writes (`r#[2..=5] = v`) lower to Set_mask chains. Two independent lowerings
# of the SAME LGraph are compared step by step:
#   the certificate's `interpretDesign` (Futamura_Projection_Scripts/cert_trace.lean), and
#   LiveHD's own Verilog for the graph (--emit verilog), under iverilog,
# on a contiguous window, two single-bit writes, a window inside a SIGNED
# carrier, and a window filled from a signed source.
# Control (must DIFFER): the Verilog of the same fixture with one window moved
# by a bit, against the real certificate.
# With FPNAT=<dir of native FP drivers> the projection is run too: generation
# and readiness on the certificate, whose reference comparison is against
# `interpretDesign` on every cycle.
#
# The Lean toolchain pin lives in formal/lean; lean runs from there.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/setmask_oracle}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
FPNAT="${FPNAT:-}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v iverilog >/dev/null && command -v vvp >/dev/null || { echo "iverilog/vvp not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"
CYCLES=48

cat > "$OUT/smk.prp" <<'EOF'
pub comb smk(base:U8, v:U4, s:S6, w2:U3) -> (y:U8, z:U8, q:S6, n:U8) {
  mut y1:U8 = base
  y1#[2..=5] = v
  y = y1
  mut z1:U8 = base
  z1#[0] = w2#[0]
  z1#[7] = w2#[2]
  z = z1
  mut q1:S6 = s
  q1#[1..=3] = w2
  q = q1
  mut n1:U8 = base
  n1#[4..=7] = s#[0..=3]
  n = n1
}
EOF
sed 's/y1#\[2..=5\] = v/y1#[1..=4] = v/' "$OUT/smk.prp" > "$OUT/smk_moved.prp"
cmp -s "$OUT/smk.prp" "$OUT/smk_moved.prp" && { echo "FAIL: the moved-window control did not apply"; exit 1; }

lower() { # <prp> <tag>: LGraph, then the certificate (real fixture only) and LiveHD's Verilog
  "$LHD" compile "$1" --emit-dir "lg:$OUT/$2_lg" --workdir "$OUT/$2_cw" > "$OUT/$2_compile.log" 2>&1 \
    || { grep -o '"message":"[^"]*' "$OUT/$2_compile.log" | head -3; echo "FAIL: compile $2"; exit 1; }
  "$LHD" compile "lg:$OUT/$2_lg" --top smk --workdir "$OUT/$2_vw" --emit "verilog:$OUT/$2.gen.v" > "$OUT/$2_v.log" 2>&1 \
    || { echo "FAIL: Verilog emission $2"; exit 1; }
}
lower "$OUT/smk.prp" smk
lower "$OUT/smk_moved.prp" moved
"$LHD" compile "lg:$OUT/smk_lg" --top smk --workdir "$OUT/smk_lw" --emit-dir "lean:$OUT/lean" \
  --set formal.lean.mode=verified_compiler > "$OUT/lean.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$OUT/lean.log" | head -3; echo "FAIL: certificate emission"; exit 1; }
NS=$(grep -c 'LGraphOp.Op_SetMask' "$OUT/lean/smk_Lgraph.lean" || true)
[[ $NS -ge 4 ]] || { echo "FAIL: expected at least 4 Op_SetMask nodes, got $NS"; exit 1; }
echo "ok: emitted; $NS Op_SetMask nodes"

python3 -I - "$OUT" "$CYCLES" <<'PY'
import json, random, sys
out, cycles = sys.argv[1], int(sys.argv[2])
io = json.load(open(f'{out}/lean/smk_io.json'))
ins = {i['name']: (i['ordinal'], i['width']) for i in io['inputs']}
outs = sorted(io['outputs'], key=lambda o: o['ordinal'])
random.seed(5)
rows = [dict(base=0, v=15, s=-32, w2=7), dict(base=255, v=0, s=31, w2=0), dict(base=0x5a, v=9, s=-1, w2=5)]
while len(rows) < cycles:
    rows.append(dict(base=random.randrange(256), v=random.randrange(16), s=random.randrange(-32, 32), w2=random.randrange(8)))
with open(f'{out}/stim.txt', 'w') as f:
    f.write('stim v1\n')
    for c, r in enumerate(rows):
        for name, v in r.items():
            o, w = ins[name]
            f.write(f'at {c} in {o} {w} {v}\n')
with open(f'{out}/tb.v', 'w') as f:
    f.write('`timescale 1ns/1ps\nmodule tb;\n reg [7:0] base=0; reg [3:0] v=0; reg [5:0] s=0; reg [2:0] w2=0;\n')
    for o in outs:
        f.write(f' wire [{o["width"]-1}:0] {o["name"]};\n')
    f.write(' smk u(.base(base), .v(v), .s(s), .w2(w2)')
    f.write(''.join(f', .{o["name"]}({o["name"]})' for o in outs) + ');\n initial begin\n')
    fmt = ' '.join('%0d' for _ in outs)
    for c, r in enumerate(rows):
        f.write(f'  #({10*c+2} - $time); base={r["base"]}; v={r["v"]}; s={r["s"] & 63}; w2={r["w2"]};\n')
        f.write(f'  #({10*c+8} - $time); $display("STEP {c} {fmt}", {", ".join(o["name"] for o in outs)});\n')
    f.write('  $finish;\n end\nendmodule\n')
PY

rtl() { # <verilog> <out>
  iverilog -g2012 -o "$OUT/$2.vvp" "$1" "$OUT/tb.v" && vvp "$OUT/$2.vvp" | grep '^STEP' > "$OUT/$2.txt"
}
LP=$(cd "$ROOT/formal/lean" && lake env printenv LEAN_PATH)
(cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run Futamura_Projection_Scripts/cert_trace.lean \
   "$OUT/lean/smk.dcert" "$CYCLES" "$OUT/stim.txt" edges:all > "$OUT/cert.txt" 2> "$OUT/cert.err") \
  || { cat "$OUT/cert.err"; echo "FAIL: cert_trace"; exit 1; }
rtl "$OUT/smk.gen.v" lhdv
[[ $(wc -l < "$OUT/lhdv.txt") -eq $CYCLES ]] || { echo "FAIL: the Verilog run did not produce $CYCLES steps"; exit 1; }
diff "$OUT/lhdv.txt" "$OUT/cert.txt" > "$OUT/diff.txt" \
  || { head -5 "$OUT/diff.txt"; echo "FAIL: certificate and LiveHD's Verilog disagree"; exit 1; }
echo "ok: $CYCLES steps, certificate and LiveHD's Verilog identical on every output"
rtl "$OUT/moved.gen.v" ctl_moved
if diff -q "$OUT/ctl_moved.txt" "$OUT/cert.txt" > /dev/null; then
  echo "FAIL: the moved-window control AGREED -- the comparison cannot see the window"; exit 1
fi
echo "ok: control moved differs ($(diff "$OUT/ctl_moved.txt" "$OUT/cert.txt" | grep -c '^<') of $CYCLES steps)"

if [[ -n "$FPNAT" ]]; then
  "$FPNAT/gen_probe_v2" "$OUT/lean/smk.dcert" 20000 200 "save:$OUT/smk.fpres" > "$OUT/fp_gen.txt" 2>&1 \
    || { tail -5 "$OUT/fp_gen.txt"; echo "FAIL: FP generation"; exit 1; }
  "$FPNAT/gen_readiness_v2" "$OUT/lean/smk.dcert" 20000 200 "$CYCLES" "load:$OUT/smk.fpres" > "$OUT/fp_ready.txt" 2>&1 \
    || { tail -5 "$OUT/fp_ready.txt"; echo "FAIL: FP readiness"; exit 1; }
  grep -q 'RESULT: READINESS OK' "$OUT/fp_ready.txt" && grep -q 'reference comparison: COMPLETE' "$OUT/fp_ready.txt" \
    || { tail -5 "$OUT/fp_ready.txt"; echo "FAIL: FP readiness is not OK/COMPLETE"; exit 1; }
  echo "ok: projection $(grep -o 'residual [0-9]* terms' "$OUT/fp_ready.txt"), $CYCLES cycles, reference comparison COMPLETE"
fi
echo "PASS: setmask_oracle"

#!/usr/bin/env bash
set -euo pipefail
# RTL differential for REDUCE-XOR (8a2f78ee8: Rxor is expanded before the scan).
#
# Only the native slang reader and Pyrope make Rxor cells; the Yosys readers
# lower `$reduce_xor` themselves, so no CVA6/CORE-ET/DINO run reaches the
# expansion. This compiles reduce-XOR and reduce-XNOR at widths 1, 4, 8, 17
# and 33 with `--reader slang`, checks that the graph really holds Rxor cells,
# and compares the certificate's `interpretDesign`
# (Futamura_Projection_Scripts/cert_trace.lean) against iverilog on the
# ORIGINAL RTL, step by step. One control must DIFFER from the RTL:
#   rtl_or  the RTL with every reduce-XOR replaced by reduce-OR, against the
#           real certificate
# Popcount comes only from Pyrope; the equiv corpus covers it.
#
# The Lean toolchain pin lives in formal/lean; lean runs from there.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/rxor_oracle}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v iverilog >/dev/null && command -v vvp >/dev/null || { echo "iverilog/vvp not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"
CYCLES=32

cat > "$OUT/rx.v" <<'EOF'
module rx(input [7:0] a, input [32:0] b, input c, output p8, output p33, output n8, output p1, output [1:0] two);
  assign p8  = ^a;
  assign p33 = ^b;
  assign n8  = ~^a;
  assign p1  = ^c;
  assign two = {^a[3:0], ^b[32:16]};
endmodule
EOF
sed 's/\^/|/g' "$OUT/rx.v" > "$OUT/rx_or.v"
cmp -s "$OUT/rx.v" "$OUT/rx_or.v" && { echo "FAIL: the reduce-OR control did not apply"; exit 1; }

"$LHD" compile "$OUT/rx.v" --reader slang --top rx --emit-dir "lg:$OUT/lg" --workdir "$OUT/cw" > "$OUT/compile.log" 2>&1 \
  || { tail -5 "$OUT/compile.log"; echo "FAIL: compile"; exit 1; }
NR=$("$LHD" tool grep "lg:$OUT/lg" kind=rxor 2>/dev/null | grep -c '"kind":"rxor"' || true)
[[ $NR -ge 4 ]] || { echo "FAIL: expected the slang reader to make Rxor cells (got $NR)"; exit 1; }
"$LHD" compile "lg:$OUT/lg" --top rx --workdir "$OUT/lw" --emit-dir "lean:$OUT/lean" \
  --set formal.lean.mode=verified_compiler > "$OUT/lean.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$OUT/lean.log" | head -3; echo "FAIL: certificate emission"; exit 1; }
echo "ok: emitted; the graph held $NR Rxor cells"

python3 -I - "$OUT" "$CYCLES" <<'PY'
import json, random, sys
out, cycles = sys.argv[1], int(sys.argv[2])
io = json.load(open(f'{out}/lean/rx_io.json'))
ins = {i['name']: (i['ordinal'], i['width']) for i in io['inputs']}
outs = sorted(io['outputs'], key=lambda o: o['ordinal'])
random.seed(19)
corner = [(0, 0, 0), (255, (1 << 33) - 1, 1), (1, 1, 1), (128, 1 << 32, 0)]
rows = [dict(zip("abc", corner[c])) if c < len(corner) else
        dict(a=random.randrange(256), b=random.randrange(1 << 33), c=random.randrange(2)) for c in range(cycles)]
with open(f'{out}/stim.txt', 'w') as f:
    f.write('stim v1\n')
    for c, r in enumerate(rows):
        for name, v in r.items():
            o, w = ins[name]
            f.write(f'at {c} in {o} {w} {v}\n')
with open(f'{out}/tb.v', 'w') as f:
    f.write('`timescale 1ns/1ps\nmodule tb;\n reg [7:0] a=0; reg [32:0] b=0; reg c=0;\n')
    for o in outs:
        f.write(f' wire [{o["width"]-1}:0] {o["name"]};\n')
    f.write(' rx u(.a(a), .b(b), .c(c)' + ''.join(f', .{o["name"]}({o["name"]})' for o in outs) + ');\n initial begin\n')
    fmt = ' '.join('%0d' for _ in outs)
    for k, r in enumerate(rows):
        f.write(f'  #({10*k+2} - $time); a={r["a"]}; b={r["b"]}; c={r["c"]};\n')
        f.write(f'  #({10*k+8} - $time); $display("STEP {k} {fmt}", {", ".join(o["name"] for o in outs)});\n')
    f.write('  $finish;\n end\nendmodule\n')
PY

LP=$(cd "$ROOT/formal/lean" && lake env printenv LEAN_PATH)
rtl() { # <source.v> <out>
  iverilog -g2012 -o "$OUT/$2.vvp" "$1" "$OUT/tb.v" && vvp "$OUT/$2.vvp" | grep '^STEP' > "$OUT/$2.txt"
}
rtl "$OUT/rx.v" rtl
grep -q 'x' "$OUT/rtl.txt" && { grep -m3 'x' "$OUT/rtl.txt"; echo "FAIL: the RTL produced X"; exit 1; }
[[ $(wc -l < "$OUT/rtl.txt") -eq $CYCLES ]] || { echo "FAIL: the RTL run did not produce $CYCLES steps"; exit 1; }
(cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run Futamura_Projection_Scripts/cert_trace.lean \
   "$OUT/lean/rx.dcert" "$CYCLES" "$OUT/stim.txt" edges:all > "$OUT/cert.txt" 2> "$OUT/cert.err") \
  || { cat "$OUT/cert.err"; echo "FAIL: cert_trace"; exit 1; }
diff "$OUT/rtl.txt" "$OUT/cert.txt" > "$OUT/diff.txt" \
  || { head -5 "$OUT/diff.txt"; echo "FAIL: certificate and RTL disagree"; exit 1; }
echo "ok: $CYCLES steps, RTL and certificate identical on every output"
rtl "$OUT/rx_or.v" ctl_rtl_or
if diff -q "$OUT/ctl_rtl_or.txt" "$OUT/cert.txt" > /dev/null; then
  echo "FAIL: the rtl_or control AGREED -- the comparison cannot see the reduction"; exit 1
fi
echo "ok: control rtl_or differs ($(diff "$OUT/ctl_rtl_or.txt" "$OUT/cert.txt" | grep -c '^<') of $CYCLES steps)"
echo "PASS: rxor_oracle"

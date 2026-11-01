#!/usr/bin/env bash
set -euo pipefail
# RTL differential for a Get_mask WINDOW ABOVE A SIGNED SOURCE (897049f16).
#
# A Get_mask that reads a signed, non-constant source narrower than the
# window's top bit must see the source's sign in every position above it: the
# native lowering reads it at a carrier wide enough to hold the window
# (inou/cgen/cgen_sim.cpp). The certificate's Get_mask reads zero-extended, so
# the exporter inserts a Sext to the window's top bit first. The Yosys readers
# produce exactly this shape from a 0 / all-ones mask: a signed 1-bit Mux read
# by Get_mask windows at [17,18), [28,32) and [13,21).
#
# This compares the certificate's `interpretDesign`
# (Futamura_Projection_Scripts/cert_trace.lean) against iverilog on the
# ORIGINAL RTL, step by step. One control must DIFFER from the RTL:
#   cert_zext  the same certificate with every Sext rewritten to a
#              zero-extending Or of its source -- the read before 897049f16.
#              Each window carries TWO: the scan's adapter (897049f16) and the
#              CertificateBuilder's own widening of the adapter's signed 1-bit
#              operand. Either one alone restores the sign, so rewriting only
#              one of them leaves every value unchanged; this fixture has no
#              other Sext, so all of them go.
#
# The Lean toolchain pin lives in formal/lean; lean runs from there.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/getmask_sign_oracle}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v iverilog >/dev/null && command -v vvp >/dev/null || { echo "iverilog/vvp not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"
CYCLES=32

cat > "$OUT/gm1.v" <<'EOF'
module gm1(input sel, input [7:0] a, output b17, output [3:0] hi, output [7:0] mid);
  wire [31:0] mask = sel ? 32'hFFFF_FFFF : 32'h0000_0000;
  assign b17 = mask[17];
  assign hi  = mask[31:28];
  assign mid = mask[20:13] & a;
endmodule
EOF

"$LHD" compile "$OUT/gm1.v" --reader yosys-slang --top gm1 --set yosys.setundef=zero --emit-dir "lg:$OUT/lg" \
  --workdir "$OUT/cw" > "$OUT/compile.log" 2>&1 || { tail -5 "$OUT/compile.log"; echo "FAIL: compile"; exit 1; }
"$LHD" compile "lg:$OUT/lg" --top gm1 --workdir "$OUT/lw" --emit-dir "lean:$OUT/lean" \
  --set formal.lean.mode=verified_compiler > "$OUT/lean.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$OUT/lean.log" | head -3; echo "FAIL: certificate emission"; exit 1; }

# The cert_zext control. DCERT2 node lines are
# `<opcode> <arg> <width> <ndeps> <deps...> <origin>`; Sext is opcode 23, Or 8
# (CertIOCodec.opCode).
python3 -I - "$OUT/lean/gm1.dcert" "$OUT/gm1_zext.dcert" <<'PY'
import sys
lines, n = open(sys.argv[1]).read().split("\n"), 0
for i, l in enumerate(lines):
    f = l.split()
    if len(f) == 7 and f[0] == "23" and f[3] == "2":
        lines[i] = " ".join(["8", "0", f[2], "1", f[4], f[6]])
        n += 1
if n != 6:
    sys.exit(f"FAIL: expected two Sext per window over three windows (6), found {n}")
open(sys.argv[2], "w").write("\n".join(lines))
print(f"ok: emitted; {n} Sext widenings over the three windows (the shape under test)")
PY

python3 -I - "$OUT" "$CYCLES" <<'PY'
import json, random, sys
out, cycles = sys.argv[1], int(sys.argv[2])
io = json.load(open(f'{out}/lean/gm1_io.json'))
ins = {i['name']: (i['ordinal'], i['width']) for i in io['inputs']}
outs = sorted(io['outputs'], key=lambda o: o['ordinal'])
random.seed(17)
rows = [dict(sel=(c % 3 != 0) if c < 6 else random.randrange(2), a=random.randrange(256)) for c in range(cycles)]
with open(f'{out}/stim.txt', 'w') as f:
    f.write('stim v1\n')
    for c, r in enumerate(rows):
        for name, v in r.items():
            o, w = ins[name]
            f.write(f'at {c} in {o} {w} {int(v)}\n')
with open(f'{out}/tb.v', 'w') as f:
    f.write('`timescale 1ns/1ps\nmodule tb;\n reg sel=0; reg [7:0] a=0;\n')
    for o in outs:
        f.write(f' wire [{o["width"]-1}:0] {o["name"]};\n')
    f.write(' gm1 u(.sel(sel), .a(a)' + ''.join(f', .{o["name"]}({o["name"]})' for o in outs) + ');\n initial begin\n')
    fmt = ' '.join('%0d' for _ in outs)
    for c, r in enumerate(rows):
        f.write(f'  #({10*c+2} - $time); sel={int(r["sel"])}; a={r["a"]};\n')
        f.write(f'  #({10*c+8} - $time); $display("STEP {c} {fmt}", {", ".join(o["name"] for o in outs)});\n')
    f.write('  $finish;\n end\nendmodule\n')
PY

LP=$(cd "$ROOT/formal/lean" && lake env printenv LEAN_PATH)
iverilog -g2012 -o "$OUT/rtl.vvp" "$OUT/gm1.v" "$OUT/tb.v" && vvp "$OUT/rtl.vvp" | grep '^STEP' > "$OUT/rtl.txt"
grep -q 'x' "$OUT/rtl.txt" && { grep -m3 'x' "$OUT/rtl.txt"; echo "FAIL: the RTL produced X"; exit 1; }
[[ $(wc -l < "$OUT/rtl.txt") -eq $CYCLES ]] || { echo "FAIL: the RTL run did not produce $CYCLES steps"; exit 1; }
cert() { # <cert> <out>
  (cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run Futamura_Projection_Scripts/cert_trace.lean \
     "$1" "$CYCLES" "$OUT/stim.txt" edges:all > "$OUT/$2.txt" 2> "$OUT/$2.err") \
    || { cat "$OUT/$2.err"; echo "FAIL: cert_trace on $1"; exit 1; }
}
cert "$OUT/lean/gm1.dcert" cert
diff "$OUT/rtl.txt" "$OUT/cert.txt" > "$OUT/diff.txt" \
  || { head -5 "$OUT/diff.txt"; echo "FAIL: certificate and RTL disagree"; exit 1; }
echo "ok: $CYCLES steps, RTL and certificate identical on every output"
cert "$OUT/gm1_zext.dcert" ctl_cert_zext
if diff -q "$OUT/rtl.txt" "$OUT/ctl_cert_zext.txt" > /dev/null; then
  echo "FAIL: the cert_zext control AGREED -- the comparison cannot see the widening"; exit 1
fi
echo "ok: control cert_zext differs ($(diff "$OUT/rtl.txt" "$OUT/ctl_cert_zext.txt" | grep -c '^<') of $CYCLES steps)"
echo "PASS: getmask_sign_oracle"

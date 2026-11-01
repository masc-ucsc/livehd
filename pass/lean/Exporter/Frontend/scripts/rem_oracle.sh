#!/usr/bin/env bash
set -euo pipefail
# RTL differential for REMAINDER by a nonzero constant.
#
# LGraph's Rem is the truncated remainder (the sign follows the dividend, like
# Verilog `%`). pass.lean lowers `a % c` (c a nonzero constant) to
# `a - (a / c) * c` over the existing Div/Mult/Sum encodings; a variable or zero
# divisor is refused by name, because `x % 0` is X in Verilog. This compares
# the certificate's `interpretDesign` (Futamura_Projection_Scripts/cert_trace.lean)
# against iverilog on the ORIGINAL RTL, step by step:
#   r_uk   unsigned % 10           r_sk   signed % 3 (negative dividends)
#   r_skn  signed % -3             r_min  signed % -1 (-128 % -1 = 0)
#   r_nar  a 4-bit result          r_mix  signed % unsigned (Verilog: unsigned)
#   r_big  unsigned % 200          r_reg  a registered signed remainder
# Two controls must DIFFER from the RTL:
#   cert_udiv  the same certificate with every Op_SDiv rewritten to Op_UDiv
#   rtl_uns    the RTL with every `signed` removed, against the real certificate
# And a variable divisor (`a % b`) must be refused with the Rem hint.
#
# The Lean toolchain pin lives in formal/lean; lean runs from there.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/rem_oracle}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v iverilog >/dev/null && command -v vvp >/dev/null || { echo "iverilog/vvp not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"
CYCLES=64

cat > "$OUT/rmd.sv" <<'EOF'
module rmd(input clk, input rst, input signed [7:0] a, input [7:0] ua,
           output [7:0] r_uk, output signed [7:0] r_sk, output signed [7:0] r_skn, output signed [7:0] r_min,
           output [3:0] r_nar, output [7:0] r_mix, output [7:0] r_big, output signed [7:0] r_reg);
  assign r_uk  = ua % 8'd10;
  assign r_sk  = a % 8'sd3;
  assign r_skn = a % -8'sd3;
  assign r_min = a % -8'sd1;
  assign r_nar = ua % 8'd7;
  assign r_mix = a % 8'd3;
  assign r_big = ua % 8'd200;
  reg signed [7:0] r;
  always @(posedge clk or posedge rst) if (rst) r <= 8'sd0; else r <= a % 8'sd5;
  assign r_reg = r;
endmodule
EOF
sed "s/\bsigned\b//g; s/8'sd/8'd/g" "$OUT/rmd.sv" > "$OUT/rmd_uns.sv"
cmp -s "$OUT/rmd.sv" "$OUT/rmd_uns.sv" && { echo "FAIL: the unsigned control did not apply"; exit 1; }

"$LHD" compile "$OUT/rmd.sv" --reader yosys-slang --top rmd --set yosys.setundef=zero --emit-dir "lg:$OUT/lg" \
  --workdir "$OUT/cw" > "$OUT/compile.log" 2>&1 || { tail -5 "$OUT/compile.log"; echo "FAIL: compile"; exit 1; }
"$LHD" compile "lg:$OUT/lg" --top rmd --workdir "$OUT/lw" --emit-dir "lean:$OUT/lean" \
  --set formal.lean.mode=verified_compiler > "$OUT/lean.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$OUT/lean.log" | head -3; echo "FAIL: certificate emission"; exit 1; }
NS=$(grep -c 'LGraphOp.Op_SDiv' "$OUT/lean/rmd_Lgraph.lean" || true)
NU=$(grep -c 'LGraphOp.Op_UDiv' "$OUT/lean/rmd_Lgraph.lean" || true)
[[ $NS -ge 1 && $NU -ge 1 ]] || { echo "FAIL: expected both Op_SDiv and Op_UDiv nodes (got $NS SDiv, $NU UDiv)"; exit 1; }
echo "ok: emitted; $NS Op_SDiv and $NU Op_UDiv quotient nodes"

# The cert_udiv control: DCERT2 node lines are `<opcode> <arg> <width> ...`;
# SDiv is opcode 6, UDiv 5 (CertIOCodec.opCode).
sed -E 's/^6 0 /5 0 /' "$OUT/lean/rmd.dcert" > "$OUT/rmd_udiv.dcert"
NM=$(diff "$OUT/lean/rmd.dcert" "$OUT/rmd_udiv.dcert" | grep -c '^>' || true)
[[ $NM -eq $NS ]] || { echo "FAIL: the cert_udiv control rewrote $NM lines, expected $NS"; exit 1; }

python3 -I - "$OUT" "$CYCLES" <<'PY'
import json, random, sys
out, cycles = sys.argv[1], int(sys.argv[2])
io = json.load(open(f'{out}/lean/rmd_io.json'))
ins = {i['name']: (i['ordinal'], i['width']) for i in io['inputs']}
outs = sorted(io['outputs'], key=lambda o: o['ordinal'])
random.seed(13)
corner = [(-128, 255), (-127, 0), (-1, 199), (0, 200), (1, 9), (127, 10), (-7, 7), (7, 6), (-3, 3), (3, 201)]
rows = []
for c in range(cycles):
    a, ua = corner[c - 1] if 1 <= c <= len(corner) else (random.randrange(-128, 128), random.randrange(256))
    rows.append(dict(clk=0, rst=1 if c == 0 else 0, a=a, ua=ua))
with open(f'{out}/stim.txt', 'w') as f:
    f.write('stim v1\n')
    for c, r in enumerate(rows):
        for name, v in r.items():
            o, w = ins[name]
            f.write(f'at {c} in {o} {w} {v}\n')
with open(f'{out}/tb.v', 'w') as f:
    f.write('`timescale 1ns/1ps\nmodule tb;\n reg clk=0, rst=0; reg [7:0] a=0, ua=0;\n')
    for o in outs:
        f.write(f' wire [{o["width"]-1}:0] {o["name"]};\n')
    f.write(' rmd u(.clk(clk), .rst(rst), .a(a), .ua(ua)')
    f.write(''.join(f', .{o["name"]}({o["name"]})' for o in outs) + ');\n initial begin\n')
    fmt = ' '.join('%0d' for _ in outs)
    for c, r in enumerate(rows):
        f.write(f'  #({20*c+2} - $time); a={r["a"] & 255}; ua={r["ua"]}; rst={r["rst"]};\n')
        f.write(f'  #({20*c+8} - $time); $display("STEP {c} {fmt}", {", ".join(o["name"] for o in outs)});\n')
        f.write(f'  #({20*c+10} - $time); clk=1; #({20*c+15} - $time); clk=0;\n')
    f.write('  $finish;\n end\nendmodule\n')
PY

LP=$(cd "$ROOT/formal/lean" && lake env printenv LEAN_PATH)
rtl() { # <source.sv> <out>
  iverilog -g2012 -o "$OUT/$2.vvp" "$1" "$OUT/tb.v" && vvp "$OUT/$2.vvp" | grep '^STEP' > "$OUT/$2.txt"
}
cert() { # <cert> <out>
  (cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run Futamura_Projection_Scripts/cert_trace.lean \
     "$1" "$CYCLES" "$OUT/stim.txt" edges:all > "$OUT/$2.txt" 2> "$OUT/$2.err") \
    || { cat "$OUT/$2.err"; echo "FAIL: cert_trace on $1"; exit 1; }
}
rtl "$OUT/rmd.sv" rtl
grep -q 'x' "$OUT/rtl.txt" && { grep -m3 'x' "$OUT/rtl.txt"; echo "FAIL: the RTL produced X"; exit 1; }
cert "$OUT/lean/rmd.dcert" cert
[[ $(wc -l < "$OUT/rtl.txt") -eq $CYCLES ]] || { echo "FAIL: the RTL run did not produce $CYCLES steps"; exit 1; }
diff "$OUT/rtl.txt" "$OUT/cert.txt" > "$OUT/diff.txt" \
  || { head -5 "$OUT/diff.txt"; echo "FAIL: certificate and RTL disagree"; exit 1; }
echo "ok: $CYCLES steps, RTL and certificate identical on every output"
cert "$OUT/rmd_udiv.dcert" ctl_cert_udiv
rtl "$OUT/rmd_uns.sv" ctl_rtl_uns
for v in cert_udiv rtl_uns; do
  if [[ $v == cert_udiv ]]; then a="$OUT/rtl.txt"; b="$OUT/ctl_$v.txt"; else a="$OUT/ctl_$v.txt"; b="$OUT/cert.txt"; fi
  if diff -q "$a" "$b" > /dev/null; then
    echo "FAIL: the $v control AGREED -- the comparison cannot see signedness"; exit 1
  fi
  echo "ok: control $v differs ($(diff "$a" "$b" | grep -c '^<') of $CYCLES steps)"
done

# A variable divisor stays refused, by name.
cat > "$OUT/rmv.sv" <<'EOF'
module rmv(input [7:0] a, input [3:0] b, output [7:0] r);
  assign r = a % b;
endmodule
EOF
"$LHD" compile "$OUT/rmv.sv" --reader yosys-slang --top rmv --set yosys.setundef=zero --emit-dir "lg:$OUT/lgv" \
  --workdir "$OUT/cwv" > "$OUT/compile_v.log" 2>&1 || { tail -5 "$OUT/compile_v.log"; echo "FAIL: compile rmv"; exit 1; }
if "$LHD" compile "lg:$OUT/lgv" --top rmv --workdir "$OUT/lwv" --emit-dir "lean:$OUT/leanv" \
     --set formal.lean.mode=verified_compiler > "$OUT/lean_v.log" 2>&1; then
  echo "FAIL: a variable-divisor remainder was exported"; exit 1
fi
grep -q 'rem: only a nonzero CONSTANT divisor is supported' "$OUT/lean_v.log" \
  || { grep -o '"message":"[^"]*' "$OUT/lean_v.log" | head -2; echo "FAIL: the variable-divisor refusal does not name Rem"; exit 1; }
echo "ok: a variable divisor is refused by name"
echo "PASS: rem_oracle"

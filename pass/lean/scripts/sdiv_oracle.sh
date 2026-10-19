#!/usr/bin/env bash
set -euo pipefail
# RTL differential for DIVISION signedness (D6 Tier 1: a signed Div exports as
# Op_SDiv, not Op_UDiv).
#
# LGraph divides the operands' integer values, truncating toward zero, and its
# signedness is per OPERAND (the importer keeps a signed RTLIL port's own pin
# and wraps an unsigned one in Get_mask). The certificate reads Op_UDiv deps as
# unsigned and Op_SDiv deps as two's complement at each dep's own width, so the
# exporter picks SDiv when any operand is signed and gives an unsigned operand
# a zero bit first. This compares the certificate's `interpretDesign`
# (scripts/cert_trace.lean) against iverilog on the ORIGINAL RTL, step by step:
#   q_ss    signed / signed            q_uu   unsigned / unsigned
#   q_nar   signed, 4-bit result       q_mix  signed / unsigned (Verilog: unsigned)
#   q_cast  $signed() of unsigned inputs
#   q_kneg  negative constant divisor  q_kbig 1-bit quotient, divisor 200
#   q_reg   a registered signed quotient
# including -128 / -1 (the quotient 128 wraps to -128) and truncation toward
# zero on both signs. Zero divisors are excluded: the RTL gives X there.
# Two controls must DIFFER from the RTL:
#   cert_udiv  the same certificate with every Op_SDiv rewritten to Op_UDiv
#              (the exporter's behaviour before this change)
#   rtl_uns    the RTL with every `signed` removed, against the real certificate
#
# The Lean toolchain pin lives in formal/lean; lean runs from there.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/sdiv_oracle}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v iverilog >/dev/null && command -v vvp >/dev/null || { echo "iverilog/vvp not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"
CYCLES=64

cat > "$OUT/sdv.sv" <<'EOF'
module sdv(input clk, input rst, input signed [7:0] a, input signed [3:0] b, input [7:0] ua, input [3:0] ub,
           output signed [7:0] q_ss, output [7:0] q_uu, output signed [3:0] q_nar, output [7:0] q_mix,
           output signed [7:0] q_cast, output signed [7:0] q_kneg, output [7:0] q_kbig, output signed [7:0] q_reg);
  assign q_ss   = a / b;
  assign q_uu   = ua / ub;
  assign q_nar  = a / b;
  assign q_mix  = a / ub;
  assign q_cast = $signed(ua) / $signed({1'b0, ub});
  assign q_kneg = a / -8'sd3;
  assign q_kbig = ua / 8'd200;
  reg signed [7:0] r;
  always @(posedge clk or posedge rst) if (rst) r <= 8'sd0; else r <= a / b;
  assign q_reg = r;
endmodule
EOF
sed 's/\$signed//g; s/\bsigned\b//g; s/-8.sd3/-8'"'"'d3/; s/8.sd0/8'"'"'d0/' "$OUT/sdv.sv" > "$OUT/sdv_uns.sv"
cmp -s "$OUT/sdv.sv" "$OUT/sdv_uns.sv" && { echo "FAIL: the unsigned control did not apply"; exit 1; }

"$LHD" compile "$OUT/sdv.sv" --reader yosys-slang --top sdv --set yosys.setundef=zero --emit-dir "lg:$OUT/lg" \
  --workdir "$OUT/cw" > "$OUT/compile.log" 2>&1 || { tail -5 "$OUT/compile.log"; echo "FAIL: compile"; exit 1; }
"$LHD" compile "lg:$OUT/lg" --top sdv --workdir "$OUT/lw" --emit-dir "lean:$OUT/lean" \
  --set formal.lean.mode=verified_compiler > "$OUT/lean.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$OUT/lean.log" | head -3; echo "FAIL: certificate emission"; exit 1; }
NS=$(grep -c 'LGraphOp.Op_SDiv' "$OUT/lean/sdv_Lgraph.lean" || true)
NU=$(grep -c 'LGraphOp.Op_UDiv' "$OUT/lean/sdv_Lgraph.lean" || true)
[[ $NS -ge 1 && $NU -ge 1 ]] || { echo "FAIL: expected both Op_SDiv and Op_UDiv nodes (got $NS SDiv, $NU UDiv)"; exit 1; }
echo "ok: emitted; $NS Op_SDiv and $NU Op_UDiv nodes"

# The cert_udiv control: DCERT2 node lines are `<opcode> <arg> <width> ...`;
# SDiv is opcode 6, UDiv 5 (CertIOCodec.opCode).
sed -E 's/^6 0 /5 0 /' "$OUT/lean/sdv.dcert" > "$OUT/sdv_udiv.dcert"
NM=$(diff "$OUT/lean/sdv.dcert" "$OUT/sdv_udiv.dcert" | grep -c '^>' || true)
[[ $NM -eq $NS ]] || { echo "FAIL: the cert_udiv control rewrote $NM lines, expected $NS"; exit 1; }

python3 -I - "$OUT" "$CYCLES" <<'PY'
import json, random, sys
out, cycles = sys.argv[1], int(sys.argv[2])
io = json.load(open(f'{out}/lean/sdv_io.json'))
ins = {i['name']: (i['ordinal'], i['width']) for i in io['inputs']}
outs = sorted(io['outputs'], key=lambda o: o['ordinal'])
random.seed(11)
corner = [(-128, -1), (-128, 1), (127, -1), (-7, 2), (7, -2), (-1, -8), (-8, 7), (100, -3)]
rows = []
for c in range(cycles):
    a, b = corner[c - 1] if 1 <= c <= len(corner) else (random.randrange(-128, 128), random.choice([x for x in range(-8, 8) if x]))
    ua = [255, 200, 199, 128][c % 4] if c % 5 == 0 else random.randrange(256)
    ub = 1 if c % 7 == 0 else random.randrange(1, 16)
    rows.append(dict(clk=0, rst=1 if c == 0 else 0, a=a, b=b, ua=ua, ub=ub))
with open(f'{out}/stim.txt', 'w') as f:
    f.write('stim v1\n')
    for c, r in enumerate(rows):
        for name, v in r.items():
            o, w = ins[name]
            f.write(f'at {c} in {o} {w} {v}\n')
with open(f'{out}/tb.v', 'w') as f:
    f.write('`timescale 1ns/1ps\nmodule tb;\n reg clk=0, rst=0; reg [7:0] a=0, ua=0; reg [3:0] b=1, ub=1;\n')
    for o in outs:
        f.write(f' wire [{o["width"]-1}:0] {o["name"]};\n')
    f.write(' sdv u(.clk(clk), .rst(rst), .a(a), .b(b), .ua(ua), .ub(ub)')
    f.write(''.join(f', .{o["name"]}({o["name"]})' for o in outs) + ');\n initial begin\n')
    fmt = ' '.join('%0d' for _ in outs)
    for c, r in enumerate(rows):
        f.write(f'  #({20*c+2} - $time); a={r["a"] & 255}; b={r["b"] & 15}; ua={r["ua"]}; ub={r["ub"]}; rst={r["rst"]};\n')
        f.write(f'  #({20*c+8} - $time); $display("STEP {c} {fmt}", {", ".join(o["name"] for o in outs)});\n')
        f.write(f'  #({20*c+10} - $time); clk=1; #({20*c+15} - $time); clk=0;\n')
    f.write('  $finish;\n end\nendmodule\n')
PY

LP=$(cd "$ROOT/formal/lean" && lake env printenv LEAN_PATH)
rtl() { # <source.sv> <out>
  iverilog -g2012 -o "$OUT/$2.vvp" "$1" "$OUT/tb.v" && vvp "$OUT/$2.vvp" | grep '^STEP' > "$OUT/$2.txt"
}
cert() { # <cert> <out>
  (cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run scripts/cert_trace.lean \
     "$1" "$CYCLES" "$OUT/stim.txt" edges:all > "$OUT/$2.txt" 2> "$OUT/$2.err") \
    || { cat "$OUT/$2.err"; echo "FAIL: cert_trace on $1"; exit 1; }
}
rtl "$OUT/sdv.sv" rtl
grep -q 'x' "$OUT/rtl.txt" && { grep -m3 'x' "$OUT/rtl.txt"; echo "FAIL: the RTL produced X (a zero divisor?)"; exit 1; }
cert "$OUT/lean/sdv.dcert" cert
[[ $(wc -l < "$OUT/rtl.txt") -eq $CYCLES ]] || { echo "FAIL: the RTL run did not produce $CYCLES steps"; exit 1; }
diff "$OUT/rtl.txt" "$OUT/cert.txt" > "$OUT/diff.txt" \
  || { head -5 "$OUT/diff.txt"; echo "FAIL: certificate and RTL disagree"; exit 1; }
echo "ok: $CYCLES steps, RTL and certificate identical on every output"
cert "$OUT/sdv_udiv.dcert" ctl_cert_udiv
rtl "$OUT/sdv_uns.sv" ctl_rtl_uns
for v in cert_udiv rtl_uns; do
  if [[ $v == cert_udiv ]]; then a="$OUT/rtl.txt"; b="$OUT/ctl_$v.txt"; else a="$OUT/ctl_$v.txt"; b="$OUT/cert.txt"; fi
  if diff -q "$a" "$b" > /dev/null; then
    echo "FAIL: the $v control AGREED -- the comparison cannot see signedness"; exit 1
  fi
  echo "ok: control $v differs ($(diff "$a" "$b" | grep -c '^<') of $CYCLES steps)"
done
echo "PASS: sdiv_oracle"

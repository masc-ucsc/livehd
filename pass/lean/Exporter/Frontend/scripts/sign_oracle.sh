#!/usr/bin/env bash
set -euo pipefail
# RTL differential for OPERAND SIGNEDNESS in the exporter.
#
# LiveHD reads every pin as an integer: two's complement at its width when the
# pin is signed, unsigned otherwise. The certificate's operators read their
# operands UNSIGNED and widen by zero extension, except SLT/SGT/SRA/SDiv, which
# read two's complement at the dep's own width. Before this check the exporter
# (1) picked ULT/SLT from the comparison's 1-bit RESULT pin, so
#     `$signed(a) < $signed(b)` became ULT;
# (2) exported `>>` of an UNSIGNED value as SRA on that value, filling with its
#     top bit;
# (3) widened a narrower SIGNED operand by zero extension (LiveHD rewrites
#     4'b1100 as the signed 3-bit -4, which a wider mux then read as 00100).
# This compares the certificate's `interpretDesign` (Futamura_Projection_Scripts/cert_trace.lean)
# against iverilog on the ORIGINAL RTL, step by step, for each of those shapes
# and a registered signed comparison. Two controls must DIFFER from the RTL:
#   cert_ult  the same certificate with every SLT/SGT rewritten to ULT/UGT
#             (case 1 as the exporter used to emit it)
#   rtl_uns   the RTL with every `signed` removed, against the real certificate
#
# The Lean toolchain pin lives in formal/lean; lean runs from there.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/sign_oracle}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v iverilog >/dev/null && command -v vvp >/dev/null || { echo "iverilog/vvp not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"
CYCLES=96
cat > "$OUT/sgn.sv" <<'EOF'
module sgn(input clk, input rst, input signed [7:0] a, input signed [3:0] b, input [7:0] ua, input [3:0] ub,
           input [2:0] k,
           output lt_ss, output lt_su, output gt_ss, output le_mix,
           output [7:0] shr_u, output signed [7:0] sra_s, output [7:0] sra_u, output [7:0] shr_s,
           output [3:0] mux_k, output eq_mix, output [7:0] add_mix, output reg_lt);
  assign lt_ss   = a < b;
  assign lt_su   = a < $signed({1'b0, ua});
  assign gt_ss   = a > b;
  assign le_mix  = a <= $signed({4'b0, ub});
  assign shr_u   = ua >> k;
  assign sra_s   = a >>> k;
  assign sra_u   = ua >>> k;
  assign shr_s   = a >> k;
  assign mux_k   = k[1:0] == 2'd0 ? 4'b1111 : k[1:0] == 2'd1 ? (ua[1] ? 4'b1100 : 4'b0011) : 4'b0001 << ua[1:0];
  assign eq_mix  = b == $signed({1'b0, ub[2:0]});
  assign add_mix = a + $signed({4'b0, ub});
  reg r;
  always @(posedge clk or posedge rst) if (rst) r <= 1'b0; else r <= a < b;
  assign reg_lt = r;
endmodule
EOF
sed 's/\$signed//g; s/\bsigned\b//g' "$OUT/sgn.sv" > "$OUT/sgn_uns.sv"
cmp -s "$OUT/sgn.sv" "$OUT/sgn_uns.sv" && { echo "FAIL: the unsigned control did not apply"; exit 1; }

"$LHD" compile "$OUT/sgn.sv" --reader yosys-slang --top sgn --set yosys.setundef=zero --emit-dir "lg:$OUT/lg" \
  --workdir "$OUT/cw" > "$OUT/compile.log" 2>&1 || { tail -5 "$OUT/compile.log"; echo "FAIL: compile"; exit 1; }
"$LHD" compile "lg:$OUT/lg" --top sgn --workdir "$OUT/lw" --emit-dir "lean:$OUT/lean" \
  --set formal.lean.mode=verified_compiler > "$OUT/lean.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$OUT/lean.log" | head -3; echo "FAIL: certificate emission"; exit 1; }
NS=$(grep -cE 'LGraphOp.Op_S(LT|GT)' "$OUT/lean/sgn_Lgraph.lean" || true)
[[ $NS -ge 3 ]] || { echo "FAIL: expected at least 3 signed comparisons (got $NS)"; exit 1; }
echo "ok: emitted; $NS signed comparison nodes"

# The cert_ult control: DCERT2 node lines are `<opcode> <arg> <width> ...`;
# SLT 16 -> ULT 14, SGT 17 -> UGT 15 (CertIOCodec.opCode).
sed -E 's/^16 0 /14 0 /; s/^17 0 /15 0 /' "$OUT/lean/sgn.dcert" > "$OUT/sgn_ult.dcert"
NM=$(diff "$OUT/lean/sgn.dcert" "$OUT/sgn_ult.dcert" | grep -c '^>' || true)
[[ $NM -eq $NS ]] || { echo "FAIL: the cert_ult control rewrote $NM lines, expected $NS"; exit 1; }

python3 -I - "$OUT" "$CYCLES" <<'PY'
import json, random, sys
out, cycles = sys.argv[1], int(sys.argv[2])
io = json.load(open(f'{out}/lean/sgn_io.json'))
ins = {i['name']: (i['ordinal'], i['width']) for i in io['inputs']}
outs = sorted(io['outputs'], key=lambda o: o['ordinal'])
random.seed(17)
corner = [(-128, 7), (127, -8), (0, -1), (-1, 0), (-8, -8), (64, 3), (-64, -3), (5, 5)]
rows = []
for c in range(cycles):
    a, b = corner[c - 1] if 1 <= c <= len(corner) else (random.randrange(-128, 128), random.randrange(-8, 8))
    ua = [255, 128, 129, 200][c % 4] if c % 3 == 0 else random.randrange(256)
    ub = random.randrange(16)
    rows.append(dict(clk=0, rst=1 if c == 0 else 0, a=a, b=b, ua=ua, ub=ub, k=c % 8))
with open(f'{out}/stim.txt', 'w') as f:
    f.write('stim v1\n')
    for c, r in enumerate(rows):
        for name, v in r.items():
            o, w = ins[name]
            f.write(f'at {c} in {o} {w} {v}\n')
with open(f'{out}/tb.v', 'w') as f:
    f.write('`timescale 1ns/1ps\nmodule tb;\n reg clk=0, rst=0; reg [7:0] a=0, ua=0; reg [3:0] b=0, ub=0; reg [2:0] k=0;\n')
    for o in outs:
        f.write(f' wire [{o["width"]-1}:0] {o["name"]};\n')
    f.write(' sgn u(.clk(clk), .rst(rst), .a(a), .b(b), .ua(ua), .ub(ub), .k(k)')
    f.write(''.join(f', .{o["name"]}({o["name"]})' for o in outs) + ');\n initial begin\n')
    fmt = ' '.join('%0d' for _ in outs)
    for c, r in enumerate(rows):
        f.write(f'  #({20*c+2} - $time); a={r["a"] & 255}; b={r["b"] & 15}; ua={r["ua"]}; ub={r["ub"]}; k={r["k"]}; rst={r["rst"]};\n')
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
rtl "$OUT/sgn.sv" rtl
grep -q 'x' "$OUT/rtl.txt" && { grep -m3 'x' "$OUT/rtl.txt"; echo "FAIL: the RTL produced X"; exit 1; }
cert "$OUT/lean/sgn.dcert" cert
[[ $(wc -l < "$OUT/rtl.txt") -eq $CYCLES ]] || { echo "FAIL: the RTL run did not produce $CYCLES steps"; exit 1; }
diff "$OUT/rtl.txt" "$OUT/cert.txt" > "$OUT/diff.txt" \
  || { head -5 "$OUT/diff.txt"; echo "FAIL: certificate and RTL disagree"; exit 1; }
echo "ok: $CYCLES steps, RTL and certificate identical on every output"
cert "$OUT/sgn_ult.dcert" ctl_cert_ult
rtl "$OUT/sgn_uns.sv" ctl_rtl_uns
for v in cert_ult rtl_uns; do
  if [[ $v == cert_ult ]]; then a="$OUT/rtl.txt"; b="$OUT/ctl_$v.txt"; else a="$OUT/ctl_$v.txt"; b="$OUT/cert.txt"; fi
  if diff -q "$a" "$b" > /dev/null; then
    echo "FAIL: the $v control AGREED -- the comparison cannot see signedness"; exit 1
  fi
  echo "ok: control $v differs ($(diff "$a" "$b" | grep -c '^<') of $CYCLES steps)"
done
echo "PASS: sign_oracle"

#!/usr/bin/env bash
# lhd_dcert_test.sh: the certificates lhd writes, end to end (cases C10 and C13 of the format test
# plan).
#
# lhd (LHD; the full tier builds it from the commit under test) exports three small designs with
# formal.lean.mode=verified_compiler (<top>.dcert, its Lean literal <top>_Lgraph.lean, and
# <top>_io.json):
#   pw     a counter with a power-on value and a 12-word RAM preloaded by $readmemh
#          (ckpt_selftest's): flops and a memory, so every parity control runs
#   ram12  a 12-word RAM whose 4-bit address reaches past it (sym_selftest's): no flop
#   tc     two clocks, an asynchronous active-low reset and a ROM (ckpt_selftest's): no memory
#          (a ROM is a constant source, not a memory record)
# C10, lhd writes DCERT4: each .dcert starts with the 7 bytes `DCERT4\n`; the loader every driver
#   uses admits each (dcert_summary.lean), pw's and ram12's memory with depth 12 of a 16-word
#   table, tc's flops on two clocks; dcert_version_check.lean on ram12's export: it re-renders to
#   its own bytes, admits, and its DCERT3, DCERT2 and DCERT1 forms are refused.
# C13, the C++ writer against the Lean codec: dcert_parity.py prints DCERT4-PARITY OK on each:
#   the bytes are renderCert of the Lean literal (equal bytes are the same certificate), the
#   verified reader re-renders and admits them, and the sensitivity and refusal controls ran. On
#   pw every control runs; ram12 (no flop) and tc (no memory) are run with --allow-skip, and only
#   the block they have nothing for may skip.
# Controls: pw's certificate with one node's origin changed (a field only the byte comparison
#   reads): DCERT4-PARITY FAILED, exit 1; tc without --allow-skip: exit 1.
# Prints `PASS lhd_dcert_test: ...` and exits 0, or a FAIL line and exits 1.
#
# Environment: LHD (default bazel-bin/lhd/lhd), OUT (default generated/lhd_dcert_test). Runs
# interpreted against formal/lean/.olean-dev.
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
L="$ROOT/formal/lean"
OUT="${OUT:-$ROOT/generated/lhd_dcert_test}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
PARITY="$ROOT/pass/lean/Exporter/DesignCert/scripts/dcert_parity.py"
fail() { echo "FAIL: $*"; exit 1; }
[[ -x "$LHD" ]] || { echo "FAIL: no lhd at $LHD (bazel build //lhd:lhd)"; exit 2; }
command -v lean > /dev/null || { echo "FAIL: lean is not on PATH"; exit 2; }
bash "$SCRIPT_DIR/check_fresh.sh" "$ROOT" || exit 1
rm -rf -- "$OUT"
mkdir -p "$OUT"

cat > "$OUT/pw.v" <<'EOF'
module pw(input clk, input we, input [3:0] wa, input [7:0] wd, input [3:0] ra,
          output [7:0] c_o, output [7:0] rd_o);
  reg [7:0] c = 8'd5;
  reg [7:0] m [0:11];
  initial $readmemh("pw.hex", m);
  always @(posedge clk) begin
    c <= c + 8'd1;
    if (we) m[wa] <= wd;
  end
  assign c_o = c;
  assign rd_o = m[ra];
endmodule
EOF
printf '3a c5 00 ff\n10 22 34 46\n00 00 5d 6f\n' > "$OUT/pw.hex"
cat > "$OUT/ram12.v" <<'EOF'
module ram12(input clk, input we, input [1:0] wp, input [1:0] ww, input [7:0] wd,
             input [1:0] rp, input [1:0] rw, output ok_rd, output ok_w0, output bad_rd, output [7:0] rd_o);
  wire [3:0] wa = {wp, ww};
  wire [3:0] ra = {rp, rw};
  reg [7:0] mem [0:11];
  always @(posedge clk) if (we) mem[wa] <= (wa >= 4'd12) ? 8'hff : (wd == 8'hff) ? 8'hfe : wd;
  wire [7:0] rd = mem[ra];
  assign ok_rd = (ra < 4'd12) | (rd == 8'd0);
  assign ok_w0 = mem[0] != 8'hff;
  assign bad_rd = (ra < 4'd11) | (rd == 8'd0);
  assign rd_o = rd;
endmodule
EOF
cat > "$OUT/tc.v" <<'EOF'
module tc(input clka, input clkb, input rst_n, input [2:0] ra, input [7:0] d,
          output [7:0] qa, output [7:0] qb, output [7:0] rom_o);
  reg [7:0] a;
  reg [7:0] b = 8'h3c;
  reg [7:0] rom [0:5];
  initial $readmemh("tc.hex", rom);
  always @(posedge clka or negedge rst_n) if (!rst_n) a <= 8'h11; else a <= a + d;
  always @(posedge clkb) b <= b ^ a;
  assign qa = a;
  assign qb = b;
  assign rom_o = rom[ra];
endmodule
EOF
printf '01 23 45 67 89 ab\n' > "$OUT/tc.hex"
for d in pw ram12 tc; do
  reader=(--reader slang); [[ $d == ram12 ]] && reader=(--reader yosys-slang --set yosys.setundef=zero)
  (cd "$OUT" && "$LHD" compile "$d.v" "${reader[@]}" --top "$d" --emit-dir "lg:${d}_lg" --workdir "${d}_cw" \
     > "$d.compile.log" 2>&1) || { tail -5 "$OUT/$d.compile.log"; fail "lhd compile $d"; }
  (cd "$OUT" && "$LHD" compile "lg:${d}_lg" --top "$d" --workdir "${d}_lw" --emit-dir "lean:${d}_lean" \
     --set formal.lean.mode=verified_compiler > "$d.lean.log" 2>&1) \
    || { grep -o '"message":"[^"]*' "$OUT/$d.lean.log" | head -3; fail "lhd's certificate of $d"; }
  for f in "$d.dcert" "${d}_Lgraph.lean" "${d}_io.json"; do
    [[ -s "$OUT/${d}_lean/$f" ]] || fail "lhd wrote no ${d}_lean/$f"
  done
done
cert() { echo "$OUT/$1_lean/$1.dcert"; }

# C10: DCERT4, admitted, with the depth and clocks of the source
for d in pw ram12 tc; do
  cmp -s <(head -c 7 "$(cert $d)") <(printf 'DCERT4\n') || fail "lhd's certificate of $d is not DCERT4: $(head -c 7 "$(cert $d)" | od -c | head -1)"
done
(cd "$L" && LEAN_PATH="$PWD/.olean-dev" lean --run "$SCRIPT_DIR/dcert_summary.lean" \
   "$(cert pw)" "$(cert ram12)" "$(cert tc)") > "$OUT/summary.log" 2>&1 \
  || { cat "$OUT/summary.log"; fail "a certificate lhd wrote is refused"; }
line() { grep -F "$(cert "$1"): admitted: " "$OUT/summary.log"; }
for d in pw ram12; do
  [[ "$(line $d)" == *"clocks 1; "*"memories [4/8/12 on clock 0 with 0 commits]" ]] \
    || fail "$d: not one 12-word memory of a 16-word table on one clock: $(line $d)"
done
re='clocks 2; flop clocks \[(0, 1|1, 0)\]; memories \[\]$'
[[ "$(line tc)" =~ $re ]] || fail "tc: not two flops on two clocks without a memory: $(line tc)"
(cd "$L" && LEAN_PATH="$PWD/.olean-dev" lean --run "$SCRIPT_DIR/dcert_version_check.lean" "$(cert ram12)") \
  > "$OUT/version_check.log" 2>&1 && grep -q '^PASS dcert_version_check' "$OUT/version_check.log" \
  || { tail -10 "$OUT/version_check.log"; fail "dcert_version_check on lhd's ram12"; }
echo "ok: lhd writes DCERT4 (pw, ram12, tc), each admitted, depth 12 carried; $(tail -1 "$OUT/version_check.log")"

# C13: the C++ bytes against renderCert of the Lean literal
TOOLCHAIN=$(dirname "$(dirname "$(command -v lean)")")
parity() { # <name> <certificate> <log> [--allow-skip]
  python3 -I "$PARITY" --base "$1" --lean "$OUT/$1_lean/$1_Lgraph.lean" --dcert "$2" --out "$OUT/parity_$1" \
    --toolchain "$TOOLCHAIN" "${@:4}" > "$3" 2>&1
}
for d in pw ram12 tc; do
  skip=(); [[ $d != pw ]] && skip=(--allow-skip)
  parity $d "$(cert $d)" "$OUT/parity_$d.log" "${skip[@]}" && grep -q '^DCERT4-PARITY OK$' "$OUT/parity_$d.log" \
    || { tail -15 "$OUT/parity_$d.log"; fail "dcert_parity on $d (log $OUT/parity_$d.log)"; }
done
# only the block a design has nothing for may skip: pw none, ram12 the flop controls, tc the memory ones
[[ $(grep -c '^  --   ' "$OUT/parity_pw.log") == 0 ]] || fail "dcert_parity skipped controls on pw"
[[ $(grep -c '^  --   ' "$OUT/parity_ram12.log") == 1 ]] && grep -q '^  --   no flops' "$OUT/parity_ram12.log" \
  || fail "dcert_parity on ram12 skipped other controls than the flop ones"
[[ $(grep -c '^  --   ' "$OUT/parity_tc.log") == 1 ]] && grep -q '^  --   no memories' "$OUT/parity_tc.log" \
  || fail "dcert_parity on tc skipped other controls than the memory ones"
echo "ok: dcert_parity on pw, ram12 and tc: DCERT4-PARITY OK"

# controls: a field only the bytes show (node 0's origin) changed; a skip that was not allowed
python3 -I - "$(cert pw)" "$OUT/pw_origin.dcert" <<'EOF' || fail "the origin control was not made"
import sys
lines = open(sys.argv[1], 'rb').read().split(b'\n')
ns = int(lines[1])               # sources, one a line, then the node count and node 0
i = 2 + ns + 1
toks = lines[i].split(b' ')
toks[-1] = str(int(toks[-1]) + 1).encode()
lines[i] = b' '.join(toks)
open(sys.argv[2], 'wb').write(b'\n'.join(lines))
EOF
parity pw "$OUT/pw_origin.dcert" "$OUT/parity_ctl_origin.log"; rc=$?
[[ $rc == 1 ]] && grep -q '^DCERT4-PARITY FAILED' "$OUT/parity_ctl_origin.log" \
  && grep -q 'bytes differ at offset' "$OUT/parity_ctl_origin.log" \
  || { tail -5 "$OUT/parity_ctl_origin.log"; fail "dcert_parity passed pw with a changed origin (exit $rc)"; }
parity tc "$(cert tc)" "$OUT/parity_ctl_skip.log"; rc=$?
[[ $rc == 1 ]] && grep -q '^DCERT4-PARITY FAILED' "$OUT/parity_ctl_skip.log" \
  || { tail -5 "$OUT/parity_ctl_skip.log"; fail "dcert_parity passed tc's skipped memory controls without --allow-skip (exit $rc)"; }
echo "ok: controls: a changed origin fails parity; a skip without --allow-skip fails"
echo "PASS lhd_dcert_test: lhd writes DCERT4 (3 designs, admitted, depth carried); parity OK on 3; 2 controls fail as they must"

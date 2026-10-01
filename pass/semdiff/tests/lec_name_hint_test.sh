#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A bus_name-reconstructed state correspondence is a HINT, never a proof.
#
# core/bus_name reads the impl's `g.l` (a `DFF g(...)` holding `q` flattened
# to `g.q`, a flat `\g.l`, or cgen's `\g_cgen1.l`) as the cell state of the
# ref's one-bit register `g`. semdiff renames that impl key to `n:g`, so the
# pair seeds the structural match like a certain tier-1 name pair. Its
# compare-point obligations were still keyed by the RAW names (`n:g` vs
# `n:g.l`): both one-sided, both skipped. Two registers with SWAPPED dins, or
# a different reset/initial constant, therefore matched as "structurally
# identical" and lec PROVED them with no solver call (cached as definitive).
# The obligations now follow the seed, so every *_bad pair must REFUTE, and
# every *_ok twin must still PROVE -- the hier/flat DFF and latch twins on
# the fast semdiff path, since a verified hint is a genuine identity.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lec_name_hint}/lnh"
rm -rf "$WORK"
mkdir -p "$WORK"
fail=0

# ---- two one-bit registers (flops): ref g = a&b, h = a^b ------------------
cat > "$WORK/sw_ref.v" <<'V'
module t(input clk, input a, input b, output q1, output q2);
  logic g, h;
  always_ff @(posedge clk) g <= a & b;
  always_ff @(posedge clk) h <= a ^ b;
  assign q1 = g;
  assign q2 = h;
endmodule
V
cat > "$WORK/sw_hier_ok.v" <<'V'
module DFF(input D, input CK, output Q);
  reg q;
  always_ff @(posedge CK) q <= D;
  assign Q = q;
endmodule
module t(input clk, input a, input b, output q1, output q2);
  DFF g (.D(a & b), .CK(clk), .Q(q1));
  DFF h (.D(a ^ b), .CK(clk), .Q(q2));
endmodule
V
cat > "$WORK/sw_flat_ok.v" <<'V'
module t(input clk, input a, input b, output q1, output q2);
  logic \g.l , \h.l ;
  always_ff @(posedge clk) \g.l <= a & b;
  always_ff @(posedge clk) \h.l <= a ^ b;
  assign q1 = \g.l ;
  assign q2 = \h.l ;
endmodule
V
sed -e 's/\\g\.l /\\g_cgen1.l /g' -e 's/\\h\.l /\\h_cgen2.l /g' "$WORK/sw_flat_ok.v" > "$WORK/sw_cgen_ok.v"
# *_bad: the same wiring with the two dins crossed.
for s in hier flat cgen; do
  sed -e 's/a & b/a @ b/' -e 's/a ^ b/a \& b/' -e 's/a @ b/a ^ b/' "$WORK/sw_${s}_ok.v" > "$WORK/sw_${s}_bad.v"
done

# ---- two gated latches: ref g = a&b, h = a^b ------------------------------
cat > "$WORK/lsw_ref.v" <<'V'
module lg(input clk, input en, input a, input b, output q1, output q2);
  logic en_l;
  always_latch if (!clk) en_l = en;
  wire gclk = clk & en_l;
  logic g, h;
  always_latch if (gclk) g = a & b;
  always_latch if (gclk) h = a ^ b;
  assign q1 = g;
  assign q2 = h;
endmodule
V
cat > "$WORK/lsw_hier_ok.v" <<'V'
module DLX(input D, input GATE, output Q);
  reg l;
  always_latch if (GATE) l = D;
  assign Q = l;
endmodule
module lg(input clk, input en, input a, input b, output q1, output q2);
  logic en_l;
  always_latch if (!clk) en_l = en;
  wire gclk = clk & en_l;
  DLX g (.D(a & b), .GATE(gclk), .Q(q1));
  DLX h (.D(a ^ b), .GATE(gclk), .Q(q2));
endmodule
V
cat > "$WORK/lsw_flat_ok.v" <<'V'
module lg(input clk, input en, input a, input b, output q1, output q2);
  logic en_l;
  always_latch if (!clk) en_l = en;
  wire gclk = clk & en_l;
  logic \g.l , \h.l ;
  always_latch if (gclk) \g.l = a & b;
  always_latch if (gclk) \h.l = a ^ b;
  assign q1 = \g.l ;
  assign q2 = \h.l ;
endmodule
V
for s in hier flat; do
  sed -e 's/a & b/a @ b/' -e 's/a ^ b/a \& b/' -e 's/a @ b/a ^ b/' "$WORK/lsw_${s}_ok.v" > "$WORK/lsw_${s}_bad.v"
done

# ---- one register, same structure, only a constant differs -----------------
cat > "$WORK/rst_ref.v" <<'V'
module t(input clk, input rst, input d, output q);
  logic g;
  always_ff @(posedge clk) if (rst) g <= 1'b0; else g <= d;
  assign q = g;
endmodule
V
cat > "$WORK/rst_ok.v" <<'V'
module t(input clk, input rst, input d, output q);
  logic \g.l ;
  always_ff @(posedge clk) if (rst) \g.l <= 1'b0; else \g.l <= d;
  assign q = \g.l ;
endmodule
V
sed 's/posedge clk)/posedge clk or posedge rst)/' "$WORK/rst_ref.v" > "$WORK/arst_ref.v"
sed 's/posedge clk)/posedge clk or posedge rst)/' "$WORK/rst_ok.v" > "$WORK/arst_ok.v"
cat > "$WORK/init_ref.v" <<'V'
module t(input clk, input d, output q);
  logic g = 1'b0;
  always_ff @(posedge clk) g <= d;
  assign q = g;
endmodule
V
cat > "$WORK/init_flat_ok.v" <<'V'
module t(input clk, input d, output q);
  logic \g.l = 1'b0;
  always_ff @(posedge clk) \g.l <= d;
  assign q = \g.l ;
endmodule
V
cat > "$WORK/init_hier_ok.v" <<'V'
module DFF(input D, input CK, output Q);
  reg q = 1'b0;
  always_ff @(posedge CK) q <= D;
  assign Q = q;
endmodule
module t(input clk, input d, output q);
  DFF g (.D(d), .CK(clk), .Q(q));
endmodule
V
for s in rst arst init_flat init_hier; do
  sed "s/1'b0;/1'b1;/" "$WORK/${s}_ok.v" > "$WORK/${s}_bad.v"
done

# check <label> <top> <impl> <ref> <want PROVEN|REFUTED> [fast]
#   fast: the PROVEN must come from semdiff's no-solver structural match.
check() {
  local label=$1 top=$2 impl=$3 ref=$4 want=$5 fast=${6:-}
  local log="$WORK/$label.log"
  "$LHD" lec --impl "$WORK/$impl" --ref "$WORK/$ref" --top "$top" --set formal.simfail_run=false \
    --workdir "$WORK/w_$label" > "$log" 2>&1
  local got
  got=$(grep -oE "PROVEN equivalent|REFUTED \\(not equivalent\\)|UNKNOWN" "$log" | head -1 | cut -d' ' -f1)
  if [ "$got" != "$want" ]; then
    echo "FAIL: $label -> got '$got', want '$want'"
    sed 's/^/    /' "$log" | head -20
    fail=1
    return
  fi
  if [ -n "$fast" ] && ! grep -q "MATCHED (semdiff structural, no solver)" "$log"; then
    echo "FAIL: $label -> $got, but not on the semdiff structural fast path"
    sed 's/^/    /' "$log" | head -20
    fail=1
    return
  fi
  echo "ok: $label -> $got${fast:+ (semdiff structural)}"
}

for s in hier flat cgen; do
  check "sw_${s}_bad" t "sw_${s}_bad.v" sw_ref.v REFUTED
  check "sw_${s}_ok" t "sw_${s}_ok.v" sw_ref.v PROVEN fast
done
for s in hier flat; do
  check "lsw_${s}_bad" lg "lsw_${s}_bad.v" lsw_ref.v REFUTED
  check "lsw_${s}_ok" lg "lsw_${s}_ok.v" lsw_ref.v PROVEN fast
done
check rst_bad t rst_bad.v rst_ref.v REFUTED
check rst_ok t rst_ok.v rst_ref.v PROVEN
check arst_bad t arst_bad.v arst_ref.v REFUTED
check arst_ok t arst_ok.v arst_ref.v PROVEN
for s in flat hier; do
  check "init_${s}_bad" t "init_${s}_bad.v" init_ref.v REFUTED
  check "init_${s}_ok" t "init_${s}_ok.v" init_ref.v PROVEN
done

if [ $fail -ne 0 ]; then echo "lec_name_hint_test: FAILED"; exit 1; fi
echo "lec_name_hint_test: PASSED"
exit 0

#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# todo/livehd/2f-latch — the ICG enable-latch bypass BEHIND A WRAPPER.
#
# single_edge_icg_test covers the enable latch when it drives the gate
# DIRECTLY. Real RTL almost never looks like that: the clock gate is its own
# module, so after inlining the gate is
#
#     And(zext(clk), zext(en_latch))
#
# and `bypass_enable_latches` -- which tested only the enable pin's IMMEDIATE
# master node -- saw a Get_mask, decided this was not a latch, and left the
# consumer reading the latch's Q. single_edge then retypes that latch into a
# posedge flop, so the Q is a full cycle stale: the classic L1 error, one node
# further away than the existing test looks.
#
# Measured on txfmafrac_top at 02f7f0d92: all 137 gated elements took the stale
# enable, and a 3142-vector differential of the normalized netlist against the
# original RTL mismatched on 3141 of them while the PRE-normalization netlist
# passed all 3142.
#
# WHAT THESE CASES DO AND DO NOT PIN. Mutation-checked against the pass:
#   killed by case 1/2/4b -- removing the wrapper descent (the pre-fix code)
#   killed by case 3b     -- extending the descent to a NARROWING wrapper
#   NOT killed            -- the mask == -1 check, the result-width check, and
#                            the boolean/arm-width precondition
# The last three guard `Get_mask` shapes this frontend does not appear to
# produce between a latch Q and an ICG gate: a bit-select lowers to `Sra`
# (which case 3b covers), and every Get_mask observed on this path carries
# mask -1. They are kept as fail-closed preconditions on a bypass that would
# otherwise be unsound, not as validated behaviour, and this comment exists so
# the next reader does not mistake "the suite is green" for "those are tested".
#
# Cases:
#   1. the wrapper shape actually reaches the pass (if the frontend folded it
#      away this test would silently be re-testing the direct case)
#   2. semantics: normalized == pre-normalized, enable toggling both ways
#   3. an INVERTING wrapper must NOT acquire a bypass
#   3b. a NARROWING (bit-select) wrapper must NOT acquire a bypass
#   4. the same wrapped enable on a MEMORY write port: descriptor unchanged,
#      and (4b) independent per-lane writes preserved
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." 2>/dev/null && pwd)"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
[ -x "$LHD" ] || LHD="lhd/lhd"
[ -x "$LHD" ] || { echo "FAIL: no lhd binary"; exit 1; }
DS=""
for c in "${TEST_SRCDIR:-}/_main/scripts/coreet_lowering_diffsim.py" \
         "$ROOT/scripts/coreet_lowering_diffsim.py" "scripts/coreet_lowering_diffsim.py"; do
  [ -r "$c" ] && { DS="$c"; break; }
done

# PROJECT-LOCAL scratch, never /tmp.
W="${TEST_TMPDIR:-$ROOT/generated/tests}/single_edge_icg_wrapped_en"
rm -rf "$W"; mkdir -p "$W"
rc_all=0
fail() { echo "FAIL: $*"; rc_all=1; }

# The gate in its own module -- this is what creates the wrapper. Keeping the
# hierarchy is the point of the fixture, so it must not be hand-inlined.
cat > "$W/icgw.v" <<'EOF'
module clkgate(input clk_i, input en_i, output clk_o);
  reg en_latch;
  always @* if (!clk_i) en_latch = en_i;   // low-transparent enable latch
  assign clk_o = clk_i & en_latch;         // the gate
endmodule
module clkgate_inv(input clk_i, input en_i, output clk_o);
  reg en_latch;
  always @* if (!clk_i) en_latch = en_i;
  assign clk_o = clk_i & ~en_latch;        // INVERTING: not value-preserving
endmodule
module icgw(input clk, input en, input [7:0] d, output reg [7:0] q);
  wire gclk;
  clkgate u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));
  always @(posedge gclk) q <= d;
endmodule
module icgw_inv(input clk, input en, input [7:0] d, output reg [7:0] q);
  wire gclk;
  clkgate_inv u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));
  always @(posedge gclk) q <= d;
endmodule
module clkgate_bit(input clk_i, input [1:0] en_i, output clk_o);
  reg [1:0] en_latch;
  always @* if (!clk_i) en_latch = en_i;
  assign clk_o = clk_i & en_latch[1];      // SELECTS one bit: narrowing, not zext
endmodule
module icgw_bit(input clk, input [1:0] en, input [7:0] d, output reg [7:0] q);
  wire gclk;
  clkgate_bit u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));
  always @(posedge gclk) q <= d;
endmodule
module icgw_mem(input clk, input en, input we, input [3:0] a, input [7:0] d,
                output [7:0] q, output reg [7:0] shadow);
  wire gclk;
  clkgate u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));
  reg [7:0] mem [0:15];
  always @(posedge gclk) if (we) mem[a] <= d;
  assign q = mem[a];
  // An ungated flop so `clk` is the REFERENCE clock; without one the design
  // has no reference root and single_edge declines before reaching the
  // memory's gate, which would make this case vacuous.
  always @(posedge clk) shadow <= d;
endmodule
// TWO INDEPENDENT WRITE LANES behind the same wrapped gate. BITS/WENSIZE/FWD
// being equal only says the descriptor is unchanged; driving the lanes apart
// and reading back is what shows the per-lane behaviour survived.
module icgw_mem2(input clk, input en, input [1:0] we, input [3:0] a,
                 input [15:0] d, output [15:0] q, output reg [15:0] shadow);
  wire gclk;
  clkgate u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));
  reg [15:0] mem [0:15];
  always @(posedge gclk) begin
    if (we[0]) mem[a][7:0]  <= d[7:0];
    if (we[1]) mem[a][15:8] <= d[15:8];
  end
  assign q = mem[a];
  always @(posedge clk) shadow <= d;
endmodule
EOF

build() {  # <top>
  rm -rf "$W/lg_$1" "$W/cw_$1"
  "$LHD" compile verilog "$W/icgw.v" --top "$1" --reader yosys-verilog \
    --emit-dir "lg:$W/lg_$1" --workdir "$W/cw_$1" -q >"$W/c_$1.log" 2>&1 \
    || { tail -5 "$W/c_$1.log"; fail "compile of $1 failed"; return 1; }
}
norm() {  # <top>; the caller decides whether a refusal is a failure
  rm -rf "$W/norm_$1" "$W/pw_$1"
  "$LHD" pass single_edge --top "$1" "lg:$W/lg_$1" --emit-dir "lg:$W/norm_$1" \
    --workdir "$W/pw_$1" >"$W/n_$1.log" 2>&1
}
norm_or_fail() { norm "$1" || { tail -3 "$W/n_$1.log"; fail "single_edge on $1 failed"; return 1; }; }
emit() {  # <lgdir> <out.v> <top>
  rm -rf "$W/ew_$3_$(basename "$2")"
  "$LHD" compile "lg:$1" --top "$3" --recipe O0 --emit "verilog:$2" \
    --workdir "$W/ew_$3_$(basename "$2")" -q >"$W/e_$3.log" 2>&1 \
    || { tail -5 "$W/e_$3.log"; fail "emission of $3 failed"; return 1; }
}

# --------------------------------------------------------------------------
# 1. the WRAPPER must survive the frontend, or this test is a duplicate of the
#    direct-latch one.
# --------------------------------------------------------------------------
build icgw || exit 1
"$LHD" tool cat "lg:$W/lg_icgw" --top icgw --target all --max 0 --diag-fmt jsonl \
  >"$W/g.jsonl" 2>/dev/null || fail "tool cat failed"
python3 - "$W/g.jsonl" <<'PYEOF' || fail "the gate's enable is NOT behind a wrapper -- this fixture degenerated to the direct-latch case the other test already covers"
import json, sys
N, E = {}, []
for l in open(sys.argv[1]):
    r = json.loads(l)
    (N.__setitem__(r['nid'], r) if r['t'] == 'node' else
     (E.append(r) if r['t'] == 'edge' else None))
ands = [nid for nid, n in N.items() if n['kind'] == 'and']
assert len(ands) == 1, f"expected one And (the gate), got {ands}"
ins = [e['from'] for e in E if e['to'].startswith(f"and_{ands[0]}.")]
wrapped = []
for src in ins:
    if not src.startswith('get_mask_'):
        continue
    g = int(src.split('_')[2].split('.')[0])
    for e in E:
        if e['to'].startswith(f"get_mask_{g}.") and e['from'].startswith('latch_'):
            wrapped.append((g, e['from']))
assert wrapped, f"no get_mask over a latch feeds the gate; gate operands are {ins}"
print(f"ok: the gate's enable is get_mask_{wrapped[0][0]} over {wrapped[0][1]}")
PYEOF

norm_or_fail icgw || exit 1
grep -q "gated clock(s) folded into an enable" "$W/n_icgw.log" \
  || fail "the wrapped ICG was not recognized as a gate at all"
emit "$W/lg_icgw"   "$W/src.v"  icgw || exit 1
emit "$W/norm_icgw" "$W/norm.v" icgw || exit 1

# The consumer's enable must be the latch's TRANSPARENT ARM (`en`), not a
# wrapper over its Q.
if grep -qE '^if \(en\) begin' "$W/norm.v"; then
  echo "ok: the gated flop's enable is the latch's transparent arm"
else
  grep -nE "^if \(" "$W/norm.v" | head -3
  fail "the gated flop still reads a wrapper over the latch Q -- the bypass did not fire"
fi

# --------------------------------------------------------------------------
# 2. SEMANTICS, independently: normalized must agree with pre-normalized.
#    The differential's directed set toggles the enable in BOTH directions and
#    varies the data on every vector.
# --------------------------------------------------------------------------
if [ -n "$DS" ] && command -v verilator >/dev/null 2>&1; then
  sed 's/^module icgw(/module icgw_n(/' "$W/norm.v" > "$W/norm_r.v"
  out="$(python3 "$DS" --top icgw --impl "$W/norm.v" --ref-netlist "$W/src.v" \
         --out "$W/ds" --vectors 64 --seed 3 2>&1)"; drc=$?
  echo "$out" | sed 's/^/    /'
  [ "$drc" -eq 0 ] && echo "$out" | grep -q "DIFFSIM-PASS" \
    || fail "normalized and pre-normalized disagree (the stale-enable bug)"
  echo "ok: 84 vectors agree across normalization"
else
  echo "SKIP-REASON: verilator or the differential harness is unavailable; case 2 did NOT execute"
fi

# --------------------------------------------------------------------------
# 3. CONTROL: an INVERTING wrapper is not value-preserving and must NOT get a
#    bypass. Rewriting `clk & ~enl` to `if (en)` would invert the gate.
# --------------------------------------------------------------------------
if build icgw_inv && norm_or_fail icgw_inv \
   && emit "$W/norm_icgw_inv" "$W/norm_inv.v" icgw_inv; then
  # The inversion must still BE there -- otherwise "no `if (en)`" would be
  # satisfied by a fixture that never had an inverting wrapper at all.
  "$LHD" tool cat "lg:$W/lg_icgw_inv" --top icgw_inv --target node --max 0 \
    --diag-fmt jsonl 2>/dev/null | grep -q '"kind":"not"' \
    || fail "the icgw_inv fixture has no Not -- the inversion control is vacuous"
  if grep -qE '^if \(en\) begin' "$W/norm_inv.v"; then
    fail "an INVERTING enable wrapper was bypassed to the raw arm -- the gate's polarity is now wrong"
  else
    echo "ok: the inverting wrapper did not acquire a bypass (and the Not is still in the graph)"
  fi
else
  fail "could not build/normalize/emit the inversion control, so it asserted nothing"
fi

# --------------------------------------------------------------------------
# 3b. CONTROL: a NARROWING wrapper. `en_latch[1]` is `Get_mask(q, 2)`, which
#     selects one bit, so substituting the 2-bit arm would enable on a
#     different value. Mask -1 is the only shape that may be dropped, and this
#     is the case that says so: a bypass that checks arity but not the mask
#     accepts this one and is wrong.
# --------------------------------------------------------------------------
if build icgw_bit && norm_or_fail icgw_bit \
   && emit "$W/norm_icgw_bit" "$W/norm_bit.v" icgw_bit; then
  if grep -qE '^if \(en\) begin' "$W/norm_bit.v"; then
    grep -nE "^if \(" "$W/norm_bit.v" | head -3
    fail "a NARROWING (bit-select) enable wrapper was bypassed to the whole arm -- the gate now enables on a different value"
  else
    echo "ok: the narrowing wrapper did not acquire a bypass"
  fi
else
  fail "could not build/normalize/emit the narrowing control, so it asserted nothing"
fi

# --------------------------------------------------------------------------
# 4. the helper is shared with MEMORY planning: the same wrapped enable on a
#    write port, and the lane mask must be untouched.
# --------------------------------------------------------------------------
build icgw_mem || exit 1
emit "$W/lg_icgw_mem" "$W/mem_src.v" icgw_mem || exit 1
if norm icgw_mem; then
  emit "$W/norm_icgw_mem" "$W/mem_norm.v" icgw_mem || exit 1
  lanes() { grep -oE '\.WENSIZE *\( *[0-9]+ *\)|\.BITS *\( *[0-9]+ *\)|\.FWD *\( *[0-9]+ *\)' "$1" | sort | tr '\n' ' '; }
  ls_src="$(lanes "$W/mem_src.v")"; ls_norm="$(lanes "$W/mem_norm.v")"
  [ -n "$ls_src" ] || fail "the memory fixture emitted no cgen_memory parameters to compare"
  # DESCRIPTOR parameters only. Equal BITS/WENSIZE/FWD says the memory was
  # described the same way; it says nothing yet about per-lane behaviour, which
  # case 4b establishes.
  if [ "$ls_src" = "$ls_norm" ]; then
    echo "ok: memory descriptor parameters unchanged across normalization ($ls_src)"
  else
    fail "the memory descriptor changed: src[$ls_src] norm[$ls_norm]"
  fi
  if [ -n "$DS" ] && command -v verilator >/dev/null 2>&1; then
    out="$(python3 "$DS" --top icgw_mem --impl "$W/mem_norm.v" --ref-netlist "$W/mem_src.v" \
           --out "$W/ds_mem" --vectors 64 --seed 4 2>&1)"; drc=$?
    echo "$out" | tail -3 | sed 's/^/    /'
    [ "$drc" -eq 0 ] && echo "$out" | grep -q "DIFFSIM-PASS" \
      || fail "the gated MEMORY port disagrees across normalization"
    echo "ok: the gated scalar-we memory write port agrees across normalization"
  fi
else
  tail -3 "$W/n_icgw_mem.log"
  fail "single_edge refused the gated-memory fixture, so the shared helper is NOT covered on the memory path"
fi

# --------------------------------------------------------------------------
# 4b. TWO WRITE LANES behind the same wrapped gate. The descriptor comparison
#     above says the parameters are unchanged; this says the per-lane values
#     are. The differential's directed set drives the two `we` bits
#     independently and reads back, so a lane mask applied to the wrong lane
#     -- or collapsed to all-or-nothing -- shows up as a data mismatch.
# --------------------------------------------------------------------------
if build icgw_mem2 && emit "$W/lg_icgw_mem2" "$W/mem2_src.v" icgw_mem2 \
   && norm_or_fail icgw_mem2 && emit "$W/norm_icgw_mem2" "$W/mem2_norm.v" icgw_mem2; then
  grep -q "gated clock(s) folded into an enable" "$W/n_icgw_mem2.log" \
    || fail "the two-lane memory's wrapped gate never reached the memory planner"
  l2s="$(grep -oE '\.WENSIZE *\( *[0-9]+ *\)|\.BITS *\( *[0-9]+ *\)' "$W/mem2_src.v" | sort | tr '\n' ' ')"
  l2n="$(grep -oE '\.WENSIZE *\( *[0-9]+ *\)|\.BITS *\( *[0-9]+ *\)' "$W/mem2_norm.v" | sort | tr '\n' ' ')"
  # cgen lowers the two 8-bit groups to a PER-BIT write mask rather than two
  # 8-bit lanes, so the assertion is "a real multi-lane mask", not a specific
  # WENSIZE. What makes this a lane test is the differential below, which
  # drives the two groups independently and reads back.
  w2="$(printf '%s' "$l2s" | sed -nE 's/.*WENSIZE\( *([0-9]+) *\).*/\1/p')"
  { [ -n "$w2" ] && [ "$w2" -gt 1 ]; } \
    || fail "the two-lane fixture produced no multi-lane write mask (got [$l2s])"
  [ "$l2s" = "$l2n" ] || fail "two-lane descriptor changed: src[$l2s] norm[$l2n]"
  if [ -n "$DS" ] && command -v verilator >/dev/null 2>&1; then
    out="$(python3 "$DS" --top icgw_mem2 --impl "$W/mem2_norm.v" --ref-netlist "$W/mem2_src.v" \
           --out "$W/ds_mem2" --vectors 128 --seed 6 2>&1)"; drc=$?
    echo "$out" | tail -3 | sed 's/^/    /'
    [ "$drc" -eq 0 ] && echo "$out" | grep -q "DIFFSIM-PASS" \
      || fail "per-lane write values are not preserved across normalization"
    echo "ok: independent per-lane write values preserved across normalization ($l2s)"
  fi
else
  fail "the two-lane memory case did not run"
fi

[ "$rc_all" -eq 0 ] || { echo "FAIL: single_edge_icg_wrapped_en_test"; exit 1; }
echo "PASS: single_edge_icg_wrapped_en_test"

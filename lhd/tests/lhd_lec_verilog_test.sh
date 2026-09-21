#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Verilog and Pyrope equivalence through the default lhd lec solver, including
# reader selection, hierarchy, aggregate ports, reset, and verdict handling.

set -u
LHD=lhd/lhd
PRP=inou/prp/tests/equiv/trivial_if.prp
V0=inou/prp/tests/equiv/trivial_if.v
INV=lhd/tests/merge_demo/inv.v
TOP='trivial_if.fun3'
W="${TEST_TMPDIR:-/tmp/lhd_lec_verilog_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

# 1. Headline case: a Pyrope impl vs a Verilog reference, discharged in-process
#    with cvc5 (the default). The Verilog side elaborates through slang.
"$LHD" lec --impl "$PRP" --ref "$V0" --top "$TOP" --workdir "$W/c1" -q --result-json "$W/r1.json" \
  || fail "lec prp vs verilog (cvc5) not pass: $(cat "$W/r1.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r1.json" || fail "lec cvc5 not pass: $(cat "$W/r1.json")"
echo "PASS: lec prp vs verilog (default cvc5, slang reader)"

# Reader comparison is explicit: Yosys compilation produces graphs, while LEC
# independently elaborates the original source through native Slang.
"$LHD" compile "$INV" --reader yosys --top inv --emit-dir "lg:$W/yosys_inv" \
  --workdir "$W/yosys_inv_compile" || fail "Yosys reference compilation failed"
"$LHD" lec --impl "lg:$W/yosys_inv" --ref "$INV" --top inv \
  --workdir "$W/c2_slang" -q --result-json "$W/r2_slang.json" \
  || fail "Yosys graph differs from native Slang"
grep -q '"verdict":"proven"' "$W/r2_slang.json" || fail "reader comparison was not proven"
echo "PASS: Yosys graphs agree with native Slang LEC"

# Hierarchical default LEC fallback must detect reset per selected module, not by
# grepping the whole concatenated source. A parent may have reset while a child
# does not (as in Dino's DualIssueRegisterFile); constraining that child's
# nonexistent `in_reset` turns an inconclusive/proven result into a setup error.
cat >"$W/reset_ports.il" <<'IL'
module \parent
  wire input 1 \reset
  cell \child \u
  end
end
module \child
  wire input 1 \clock
  wire input 2 \rst_ni
end
IL
PORT_TOOL=inou/yosys/rtlil_children.py
[ "$(python3 "$PORT_TOOL" --rtlil "$W/reset_ports.il" --top parent --has-input reset)" = yes ] \
  || fail "RTLIL reset inspection missed the parent's reset input"
[ "$(python3 "$PORT_TOOL" --rtlil "$W/reset_ports.il" --top child --has-input reset)" = no ] \
  || fail "RTLIL reset inspection inherited an unrelated parent's reset"
[ "$(python3 "$PORT_TOOL" --rtlil "$W/reset_ports.il" --top child --has-input rst_ni)" = yes ] \
  || fail "RTLIL reset inspection missed an active-low child reset"
echo "PASS: RTLIL reset inspection is selected-module local"

# Reset spelling and polarity are semantic. With zero-initialized SAT flops,
# these two encodings disagree before reset (q=0 versus qn=0 => ~qn=1) but are
# identical after an active-low rst_ni pulse. Treating rst_ni as absent creates
# a false bounded counterexample in Minion descendants.
cat >"$W/reset_low_ref.v" <<'V'
module reset_low(input clock, input rst_ni, input d, output o);
  reg q;
  always @(posedge clock or negedge rst_ni)
    if (!rst_ni) q <= 1'b0;
    else q <= d;
  assign o = q;
endmodule
V
cat >"$W/reset_low_impl.v" <<'V'
module reset_low(input clock, input rst_ni, input d, output o);
  reg qn;
  always @(posedge clock or negedge rst_ni)
    if (!rst_ni) qn <= 1'b1;
    else qn <= ~d;
  assign o = ~qn;
endmodule
V
"$LHD" lec --impl "$W/reset_low_impl.v" --ref "$W/reset_low_ref.v" --top reset_low \
  --workdir "$W/c2_reset_low" -q \
  --result-json "$W/r2_reset_low.json" \
  || fail "default LEC did not honor active-low reset: $(cat "$W/r2_reset_low.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r2_reset_low.json" \
  || fail "default LEC active-low reset result not pass: $(cat "$W/r2_reset_low.json")"
echo "PASS: default LEC applies active-low selected-module reset polarity"

# Descending a parameterized hierarchy must compare the two occurrence-
# specialized definitions when both RTLIL caches retain them. Falling back to
# the source module's base name restores its default parameters and can create
# a false interface mismatch (Minion prim_eco_ports: 10-bit occurrence, 4-bit
# default). The helper falls back to the base only when GOLD lacks the exact
# occurrence.
cat >"$W/param_gate.il" <<'IL'
module \parent
  cell \child$parent.u \u
  end
end
module \child$parent.u
  wire width 10 output 1 \eco_o
end
IL
cat >"$W/param_gold.il" <<'IL'
module \parent
  cell \child$parent.u \u
  end
end
module \child$parent.u
  wire width 10 output 1 \eco_o
end
module \child
  wire width 4 output 1 \eco_o
end
IL
mapped=$(python3 "$PORT_TOOL" --rtlil "$W/param_gate.il" --top parent \
  --with-base --map-against "$W/param_gold.il") \
  || fail "RTLIL child specialization mapping failed"
[ "$mapped" = $'child$parent.u\tchild$parent.u' ] \
  || fail "RTLIL child descent discarded the parameterized occurrence: $mapped"
echo "PASS: RTLIL child mapping preserves parameter-specialized child definitions"

# cgen can merge two equal generated occurrences under a different hierarchy
# spelling than yosys-slang (Minion's u_tb/u_tb_cgen1 versus
# gen_thread_buf[0/1].u_tb). Exact-name mapping is then impossible, but falling
# back to the 4-bit source default is still wrong. Select the occurrence whose
# elaborated interface matches the 10-bit gate child; dotted aggregate leaves
# are compared by their packed root width.
cat >"$W/param_renamed_gate.il" <<'IL'
module \parent
  cell \child$parent.renamed \u
  end
end
module \child$parent.renamed
  wire width 6 output 1 \eco.hi
  wire width 4 output 2 \eco.lo
end
IL
cat >"$W/param_renamed_gold.il" <<'IL'
module \child$parent.original_w4
  wire width 4 output 1 \eco
end
module \child$parent.original_w10
  wire width 10 output 1 \eco
end
module \child
  wire width 4 output 1 \eco
end
IL
mapped=$(python3 "$PORT_TOOL" --rtlil "$W/param_renamed_gate.il" --top parent \
  --with-base --map-against "$W/param_renamed_gold.il") \
  || fail "RTLIL renamed-specialization mapping failed"
[ "$mapped" = $'child$parent.renamed\tchild$parent.original_w10' ] \
  || fail "RTLIL child descent restored a default after hierarchy renaming: $mapped"
echo "PASS: RTLIL child mapping maps renamed occurrences by elaborated interface"

# An implementation-only helper generated during the Pyrope/cgen round trip
# has no reference definition to select.  Report an explicit skip marker rather
# than inventing its stripped base name and failing the entire recursive descent
# at source elaboration (Minion's thread-buffer `_p1` clone).
cat >"$W/impl_only_gate.il" <<'IL'
module \parent
  cell \helper_p1$parent.u \u
  end
end
module \helper_p1$parent.u
  wire output 1 \o
end
IL
cat >"$W/impl_only_gold.il" <<'IL'
module \parent
  wire output 1 \o
end
IL
mapped=$(python3 "$PORT_TOOL" --rtlil "$W/impl_only_gate.il" --top parent \
  --with-base --map-against "$W/impl_only_gold.il") \
  || fail "RTLIL implementation-only child mapping failed"
[ "$mapped" = $'helper_p1$parent.u\t-' ] \
  || fail "RTLIL implementation-only child invented a reference top: $mapped"
echo "PASS: RTLIL child mapping marks implementation-only generated children as skips"

# The cached proof itself must select that occurrence too. Give the two cache
# PARENTS opposite behavior while keeping the specialized children identical:
# reusing the parent's active top refutes, selecting the requested child proves.
cat >"$W/cache_gold.il" <<'IL'
module \parent
  wire output 1 \o
  connect \o 1'0
  cell \child$parent.u \u
  end
end
module \child$parent.u
  wire output 1 \o
  connect \o 1'0
end
IL
cat >"$W/cache_gate.il" <<'IL'
module \parent
  wire output 1 \o
  connect \o 1'1
  cell \child$parent.u \u
  end
end
module \child$parent.u
  wire output 1 \o
  connect \o 1'0
end
IL
YOSYS=inou/yosys/yosys2
# Read the cached RTLIL into Verilog; all equivalence checks use default lhd lec.
for side in gold gate; do
  "$YOSYS" -Q -T -p "read_rtlil $W/cache_$side.il; write_verilog $W/cache_$side.v" \
    >"$W/cache_$side.log" 2>&1 || { cat "$W/cache_$side.log"; fail "RTLIL reload failed"; }
done
"$LHD" lec --ref "$W/cache_gold.v" --impl "$W/cache_gate.v" \
  --top 'child$parent.u' \
  --workdir "$W/cache_child" --result-json "$W/cache_child.json" \
  >"$W/cache_child.log" 2>&1 \
  || { cat "$W/cache_child.log"; fail "reloaded descendant proof selected the wrong top"; }
grep -q '"verdict":"proven"' "$W/cache_child.json" || fail "descendant was not proven"
echo "PASS: reloaded descendant proofs select the child rather than the parent"

# A cached occurrence name can end in the base module's own spelling, e.g.
# ClockGate$ExuBlock.ClockGate. Source-side compatibility resolution must not
# replace that authoritative cached name with the standalone source base merely
# because the last dotted component exists in the source.
cat >"$W/cache_source_base.v" <<'V'
module u(output o);
  assign o = 1'b1;
endmodule
V
for side in gold gate; do
  cat "$W/cache_source_base.v" "$W/cache_$side.v" >"$W/cache_named_$side.v"
done
"$LHD" lec --ref "$W/cache_named_gold.v" --impl "$W/cache_named_gate.v" \
  --top 'child$parent.u' \
  --workdir "$W/cache_occurrence_name" --result-json "$W/cache_occurrence_name.json" \
  >"$W/cache_occurrence_name.log" 2>&1 \
  || { cat "$W/cache_occurrence_name.log"; fail "source base replaced the selected occurrence"; }
grep -q '"verdict":"proven"' "$W/cache_occurrence_name.json" || fail "named occurrence was not proven"
echo "PASS: selected occurrence tops are not rewritten to a source base name"

# cgen exposes tuple leaves as escaped dotted top ports, while an original
# packed-struct SystemVerilog top reaches Yosys as one vector port. Exercise the
# opt-in ABI adapter that packs/unpacks those leaves from the two cached RTLIL
# interfaces before the equivalence timer starts. The deliberately asymmetric
# bit arithmetic also checks field ordering; reversing the packing refutes.
cat >"$W/split_ref.sv" <<'SV'
module split_ports(
  input  struct packed { logic [1:0] hi; logic lo; } req,
  output struct packed { logic top; logic [1:0] low; } resp
);
  assign resp.top = req.hi[1] ^ req.lo;
  assign resp.low = req.hi + {1'b0, req.lo};
endmodule
SV
cat >"$W/split_impl.v" <<'V'
module split_ports(
  input [1:0] \req.hi ,
  input       \req.lo ,
  output      \resp.top ,
  output [1:0] \resp.low
);
  assign \resp.top = \req.hi [1] ^ \req.lo ;
  assign \resp.low = \req.hi + {1'b0, \req.lo };
endmodule
V
"$LHD" lec --impl "$W/split_impl.v" --ref "$W/split_ref.sv" --top split_ports \
  --workdir "$W/c2_split_ports" -q --result-json "$W/r2_split_ports.json" \
  || fail "lec default LEC split-port adapter not pass: $(cat "$W/r2_split_ports.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r2_split_ports.json" \
  || fail "lec default LEC split-port adapter not pass: $(cat "$W/r2_split_ports.json")"
echo "PASS: default LEC normalizes packed reference ports against cgen leaf ports"

# A hierarchy-selected module may retain non-monotonic port IDs even though
# write_rtlil keeps the source declaration order.  Aggregate packing follows
# declaration order, not those bookkeeping IDs (XiangShan Mstateen0Module has
# its final C field numbered before the preceding fields).
cat >"$W/split_order_gold.il" <<'IL'
module \ordered
  wire width 3 output 1 \result
end
IL
cat >"$W/split_order_gate.il" <<'IL'
module \ordered
  attribute \src "generated.v:30.1"
  wire output 2 \result.low
  attribute \src "generated.v:20.1"
  wire output 4 \result.middle
  attribute \src "generated.v:10.1"
  wire output 3 \result.high
end
IL
python3 inou/yosys/rtlil_split_port_adapter.py \
  --gold "$W/split_order_gold.il" --gate "$W/split_order_gate.il" \
  --gold-top ordered --gate-top ordered --impl-top ordered_impl \
  --adapter-top ordered_adapter --output "$W/split_order_adapter.v" \
  || fail "split-port adapter rejected non-monotonic port IDs"
grep -Fq '.\result.high (\result [2])' "$W/split_order_adapter.v" \
  || fail "split-port adapter did not preserve the first aggregate field as MSB"
grep -Fq '.\result.middle (\result [1])' "$W/split_order_adapter.v" \
  || fail "split-port adapter permuted the middle aggregate field by port ID"
grep -Fq '.\result.low (\result [0])' "$W/split_order_adapter.v" \
  || fail "split-port adapter did not preserve the final aggregate field as LSB"
echo "PASS: split-port packing ignores non-monotonic hierarchy port IDs"

cat >"$W/relaxed_ref.sv" <<'SV'
module relaxed_ref(input logic a, output logic y);
  typedef enum logic { E0, E1 } mode_t;
  mode_t unused_mode;
  assign y = declared_later;
  assign unused_mode = a;
  logic declared_later;
  assign declared_later = a;
endmodule
SV
cat >"$W/relaxed_impl.v" <<'V'
module relaxed_ref(input a, output y);
  assign y = a;
endmodule
V
# Default Slang enforces the source language: an implicit enum assignment
# is invalid even if the assigned value is unused. Do not inherit lgcheck's
# permissive reader switches merely to make an equivalence check run.
if "$LHD" lec --impl "$W/relaxed_impl.v" --ref "$W/relaxed_ref.sv" --top relaxed_ref \
  --workdir "$W/c2_relaxed" -q --result-json "$W/r2_relaxed.json"; then
  fail "default LEC accepted an invalid implicit enum conversion"
fi
grep -q '"class":"syntax"' "$W/r2_relaxed.json" || fail "invalid enum source lacked a syntax error"
grep -q 'no implicit conversion' "$W/r2_relaxed.json" || fail "invalid enum failed for the wrong reason"
echo "PASS: default LEC diagnoses invalid enum source before equivalence"

# A reader/top/setup failure has no equivalence evidence. Keep it a configuration
# error, never a synthetic REFUTED verdict (only a bounded CEX may use that).
if "$LHD" lec --impl "$INV" --ref "$INV" --top no_such_top \
  --workdir "$W/c2_setup_fail" -q --result-json "$W/r2_setup_fail.json"; then
  fail "default LEC accepted a missing top"
fi
grep -q '"class":"config"' "$W/r2_setup_fail.json" \
  || fail "default LEC setup failure was not classified as config: $(cat "$W/r2_setup_fail.json")"
grep -q 'REFUTED' "$W/r2_setup_fail.json" \
  && fail "default LEC setup failure was mislabeled REFUTED: $(cat "$W/r2_setup_fail.json")"
echo "PASS: default LEC setup failures stay distinct from refutations"

# Default LEC must never invoke an external lgcheck override. Keep this guard
# on a real positive proof, so accidentally restoring that backend fails.
cat >"$W/lgcheck_unexpected.sh" <<'SH'
#!/bin/sh
echo "unexpected lgcheck invocation" >&2
exit 99
SH
chmod +x "$W/lgcheck_unexpected.sh"
LHD_LGCHECK="$W/lgcheck_unexpected.sh" "$LHD" lec --impl "$INV" --ref "$INV" --top inv \
  --workdir "$W/default_backend" --result-json "$W/default_backend.json" \
  || fail "default LEC invoked lgcheck"
grep -q '"verdict":"proven"' "$W/default_backend.json" || fail "default LEC did not prove identity"
echo "PASS: default LEC proves without lgcheck"

# A native proof must not masquerade as an independent lgcheck proof in JSON.
for code in 0 1 2 99; do
  printf '#!/bin/sh\nexit %s\n' "$code" >"$W/lgcheck_result.sh"
  chmod +x "$W/lgcheck_result.sh"
  LHD_LGCHECK="$W/lgcheck_result.sh" "$LHD" lec --impl "$INV" --ref "$INV" --top inv \
    --set formal.solver=lgyosys --workdir "$W/crosscheck_$code" \
    --result-json "$W/crosscheck_$code.json" >"$W/crosscheck_$code.log" 2>&1
  rc=$?
  if [ "$code" -eq 0 ]; then
    [ "$rc" -eq 0 ] || fail "successful crosscheck failed"
    verdict=proven
  elif [ "$code" -eq 1 ]; then
    [ "$rc" -ne 0 ] || fail "disagreeing crosscheck passed"
    verdict=refuted
  else
    [ "$rc" -ne 0 ] || fail "undecided crosscheck passed"
    verdict=unknown
  fi
  grep -q "\"crosscheck\":{\"solver\":\"lgyosys\",\"verdict\":\"$verdict\",\"exit_code\":$code}" "$W/crosscheck_$code.json" \
    || fail "independent crosscheck verdict missing from JSON"
done

# A bounded BMC window is a counterexample search, not an equivalence proof.
# This pair first diverges after more than five clocks: the short window must be
# INCONCLUSIVE (never PROVEN), while a deeper window must find the real CEX.
cat >"$W/deep_ref.v" <<'V'
module deep(input clock, input reset, output o);
  reg [3:0] count;
  always @(posedge clock) if (reset) count <= 0; else count <= count + 1'b1;
  assign o = count == 4'd7;
endmodule
V
cat >"$W/deep_impl.v" <<'V'
module deep(input clock, input reset, output o);
  reg [3:0] count;
  always @(posedge clock) if (reset) count <= 0; else count <= count + 1'b1;
  assign o = 1'b0;
endmodule
V
# Bounds and engine are explicit here because this case tests bounded-result
# classification. Solver selection still follows the default.
"$LHD" lec --impl "$W/deep_impl.v" --ref "$W/deep_ref.v" --top deep \
  --set formal.engine=bmc --set formal.bound=5 --workdir "$W/c2_bounded_short" \
  --result-json "$W/bounded_short.json" >"$W/bounded_short.log" 2>&1 \
  || { cat "$W/bounded_short.log"; fail "short bounded run failed"; }
python3 - "$W/bounded_short.json" <<'PYBOUND'
import json, sys
r = json.load(open(sys.argv[1]))['lec']
assert r['verdict'] == 'proven' and r['bounded'], r
PYBOUND
# A refuted run also writes the counterexample as a Pyrope replay test and then
# BUILDS AND RUNS it, which is a ~5.5s host clang build. This check asserts the
# VERDICT, not the replay, so keep the witness emission and skip only its host
# build (`lhd lec` still writes simfail_*.prp/.json here).
if "$LHD" lec --impl "$W/deep_impl.v" --ref "$W/deep_ref.v" --top deep \
  --set formal.engine=bmc --set formal.bound=10 --set formal.simfail_run=false \
  --workdir "$W/c2_bounded_deep" \
  --result-json "$W/bounded_deep.json" >"$W/bounded_deep.log" 2>&1; then
  fail "deeper BMC missed the delayed mismatch"
fi
grep -q '"verdict":"refuted"' "$W/bounded_deep.json" \
  || { cat "$W/bounded_deep.log"; fail "deeper BMC did not refute the mismatch"; }
echo "PASS: short BMC is explicitly bounded; a deeper counterexample refutes"

# A resetless enabled register and its mapped flop implementation must have
# the same behavior; preparation must not invent a startup mismatch.
cat >"$W/keepdc_gold.v" <<'V'
module keepdc(input clk, input en, input [31:0] data, output [3:0] o);
  reg [35:0] q;
  always @(posedge clk)
    if (en)
      q <= {data, 4'b1111};
  assign o = q[3:0];
endmodule
V
cat >"$W/keepdc_gate.v" <<'V'
module keepdc_dff(input D, input CLK, output Q);
  reg state;
  always @(posedge CLK)
    state <= D;
  assign Q = state;
endmodule
module keepdc(input clk, input en, input [31:0] data, output [3:0] o);
  wire [3:0] d = en ? 4'b1111 : o;
  keepdc_dff q0(.D(d[0]), .CLK(clk), .Q(o[0]));
  keepdc_dff q1(.D(d[1]), .CLK(clk), .Q(o[1]));
  keepdc_dff q2(.D(d[2]), .CLK(clk), .Q(o[2]));
  keepdc_dff q3(.D(d[3]), .CLK(clk), .Q(o[3]));
endmodule
V
"$LHD" lec --ref "$W/keepdc_gold.v" --impl "$W/keepdc_gate.v" --top keepdc \
  --workdir "$W/keepdc" --result-json "$W/keepdc.json" >"$W/keepdc.log" 2>&1 \
  || { cat "$W/keepdc.log"; fail "mapped enabled flops differ from their source"; }
grep -q '"verdict":"proven"' "$W/keepdc.json" || fail "enabled-flop round trip was not proven"
echo "PASS: enabled-flop round trip preserves startup state"

# 3. Bare .v paths on BOTH sides: the verilog kind is inferred from the
#    extension; an identical netlist is trivially PROVEN (in-process cvc5).
"$LHD" lec --impl "$INV" --ref "$INV" --top inv --workdir "$W/c3" -q --result-json "$W/r3.json" \
  || fail "lec verilog identity (cvc5) not pass: $(cat "$W/r3.json" 2>/dev/null)"
echo "PASS: lec verilog vs verilog (bare .v, kind inferred, slang)"

# Reader bypasses are rejected outside explicit compilation.
for reader in yosys yosys-slang yosys-verilog; do
  if "$LHD" lec --impl "$INV" --ref "$INV" --reader "$reader" >"$W/reader_error.log" 2>&1; then
    fail "LEC accepted the non-native reader $reader"
  fi
  grep -q 'only supported by lhd compile' "$W/reader_error.log" || fail "missing reader migration hint"
done
for option in gold_reader gate_reader; do
  if "$LHD" lec --impl "$INV" --ref "$INV" --set "formal.lec.$option=slang" >"$W/option_error.log" 2>&1; then
    fail "LEC accepted the retired reader option $option"
  fi
  grep -qi 'unknown' "$W/option_error.log" || fail "retired reader option did not report unknown option"
done

# 5. The retired `check` command points at the merged `lec`.
out=$("$LHD" check --impl "$V0" --ref "$V0" 2>/dev/null)
echo "$out" | grep -q '"status":"fail"' || fail "check should fail (merged into lec): $out"
echo "$out" | grep -q 'merged into' || fail "check error lacks migration hint: $out"
echo "PASS: check command rejected with lec migration hint"

# 6. An unknown solver is a usage error, never a silent fallthrough.
out=$("$LHD" lec --impl "$PRP" --ref "$V0" --top "$TOP" --set formal.solver=foo -q 2>/dev/null)
echo "$out" | grep -q '"status":"fail"' || fail "bad solver should fail: $out"
echo "$out" | grep -q 'cvc5|bitwuzla|lgyosys' || fail "bad solver error lacks the valid set: $out"
echo "PASS: formal.solver=foo rejected"

echo "ALL PASS: lhd lec verilog inputs + default solver"

# A reset in one cone must not hide the stable identity of unreset state in
# another. Yosys assigns different $procdff IDs after the cgen round trip.
cat >"$W/state_identity.v" <<'V'
module state_identity(input clk, reset, en, input [7:0] d,
                      output reg ready, output reg [7:0] q);
  always @(posedge clk) begin
    if (reset) ready <= 0;
    else ready <= 1;
    if (en) q <= d;
  end
endmodule
V
"$LHD" compile "$W/state_identity.v" --reader yosys-verilog --top state_identity \
  --emit-dir "lg:$W/state_identity_lg" --emit verilog:"$W/state_identity_out.v" --workdir "$W/state_compile" -q \
  || fail "Yosys state identity compile"
"$LHD" lec --impl "lg:$W/state_identity_lg" --ref "$W/state_identity.v" \
  --top state_identity --workdir "$W/state_lec" -q --result-json "$W/state_lec.json" \
  || fail "Yosys state identity round trip: $(cat "$W/state_lec.json")"
grep -q '"verdict":"proven"' "$W/state_lec.json" || fail "state identity proof was inconclusive"
echo "PASS: Yosys state names survive a Verilog round trip with partially reset state"

# Blocking writes see earlier writes in their process; partial/conditional
# writes retain the previous value. Compare against explicit next-state RTL.
cat > "$W/blocking_order.v" <<'V'
module blocking_order(input clk, input rst, input en, input [7:0] d,
                      output reg [7:0] a, b);
  always @(posedge clk) begin
    if (rst) begin a = 0; b = 0; end
    else begin
      if (en) a[3:0] = d[3:0];
      b = a + d;
      a[7:4] = b[3:0];
    end
  end
endmodule
V
cat > "$W/blocking_order_ref.v" <<'V'
module blocking_order(input clk, input rst, input en, input [7:0] d,
                      output reg [7:0] a, b);
  wire [7:0] first = {a[7:4], en ? d[3:0] : a[3:0]};
  wire [7:0] sum = first + d;
  always @(posedge clk) begin
    if (rst) begin a <= 0; b <= 0; end
    else begin a <= {sum[3:0], first[3:0]}; b <= sum; end
  end
endmodule
V
"$LHD" lec --impl "$W/blocking_order.v" --ref "$W/blocking_order_ref.v" \
  --top blocking_order --workdir "$W/blocking_order_lec" -q \
  || fail "blocking assignments lost process order or partial-write state"
sed 's/sum = first + d/sum = first ^ d/' "$W/blocking_order_ref.v" > "$W/blocking_order_bad.v"
# A refuted run also writes the counterexample as a Pyrope replay test and then
# BUILDS AND RUNS it, which is a ~5.5s host clang build. This check asserts the
# VERDICT, not the replay, so keep the witness emission and skip only its host
# build (`lhd lec` still writes simfail_*.prp/.json here).
"$LHD" lec --impl "$W/blocking_order_bad.v" --ref "$W/blocking_order.v" \
  --top blocking_order --workdir "$W/blocking_order_bad_lec" -q \
  --set formal.simfail_run=false \
  --result-json "$W/blocking_order_bad.json" > "$W/blocking_order_bad.log" 2>&1
[ $? -eq 10 ] || fail "blocking assignment corruption was not refuted: $(cat "$W/blocking_order_bad.log")"
echo "PASS: blocking process order, partial writes, and negative control"


# A graph-only refutation still needs its uncapped machine-readable trace,
# although there is no LNAST from which to generate a Pyrope replay test.
for side in ref impl; do
  if [ "$side" = ref ]; then expr=d; else expr='~d'; fi
  cat >"$W/witness_$side.v" <<V
module witness_graph(input clk, rst, d, output reg q);
  always @(posedge clk) if (rst) q <= 0; else q <= $expr;
endmodule
V
  "$LHD" compile "$W/witness_$side.v" --top witness_graph \
    --emit-dir "lg:$W/witness_$side" --workdir "$W/witness_compile_$side" \
    || fail "witness graph compilation failed"
done
"$LHD" lec --ref "lg:$W/witness_ref" --impl "lg:$W/witness_impl" \
  --top witness_graph --set formal.engine=bmc --set formal.bound=2 \
  --workdir "$W/witness_check" --result-json "$W/witness_result.json"
[ "$?" -eq 10 ] || fail "different graph state transitions were not refuted"
python3 - "$W/witness_check/simfail_witness_graph.json" <<'PYTEST'
import json, sys
with open(sys.argv[1]) as f:
    result = json.load(f)
assert result["trace"]["cycles"], result
PYTEST
[ "$?" -eq 0 ] || fail "graph-only counterexample trace was not preserved"
echo "PASS: graph-only refutation retains its full JSON trace"

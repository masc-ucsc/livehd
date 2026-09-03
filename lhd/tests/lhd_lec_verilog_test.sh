#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd lec` is the single logic-equivalence command. The former `lhd check`
# (yosys/lgcheck) is now the `--set formal.solver=lgyosys` backend, and lec accepts
# verilog inputs directly: a .v/.sv side elaborates through the default `slang`
# reader (the direct SV->LNAST front-end) or, with --reader yosys-*, the yosys
# front-end. The --set formal.solver knob selects cvc5 (default) / bitwuzla /
# lgyosys. Fixtures: the committed inou/prp/tests/equiv trivial_if pyrope/verilog
# golden pair, plus the merge_demo inverter (a plainly-named module).

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

# 2. The same pair through the lgyosys backend (the former `lhd check`).
"$LHD" lec --impl "$PRP" --ref "$V0" --top "$TOP" --set formal.solver=lgyosys \
  --workdir "$W/c2" -q --result-json "$W/r2.json" \
  || fail "lec prp vs verilog (lgyosys) not pass: $(cat "$W/r2.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r2.json" || fail "lec lgyosys not pass: $(cat "$W/r2.json")"
echo "PASS: lec prp vs verilog (--set formal.solver=lgyosys)"

# The yosys-slang reference reader is staged in a sibling external-repository
# runfiles directory. Exercise that exact lookup: large V2V tests use it for
# packed-struct SystemVerilog references and must not depend on the caller cwd.
"$LHD" lec --impl "$INV" --ref "$INV" --top inv --set formal.solver=lgyosys \
  --set formal.lec.gold_reader=slang --workdir "$W/c2_slang" -q --result-json "$W/r2_slang.json" \
  || fail "lec lgyosys slang reader not pass: $(cat "$W/r2_slang.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r2_slang.json" \
  || fail "lec lgyosys slang reader not pass: $(cat "$W/r2_slang.json")"
echo "PASS: lgyosys locates the yosys-slang plugin in runfiles"

# The generated/implementation side can independently use yosys-slang. This is
# the scalable path for large cgen Verilog (Minion/XiangShan), while keeping the
# legacy read_verilog reader as the default for compatibility.
"$LHD" lec --impl "$INV" --ref "$INV" --top inv --set formal.solver=lgyosys \
  --set formal.lec.gate_reader=slang --workdir "$W/c2_gate_slang" -q --result-json "$W/r2_gate_slang.json" \
  || fail "lec lgyosys gate slang reader not pass: $(cat "$W/r2_gate_slang.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r2_gate_slang.json" \
  || fail "lec lgyosys gate slang reader not pass: $(cat "$W/r2_gate_slang.json")"
echo "PASS: lgyosys gate-side yosys-slang reader"

# Hierarchical lgyosys fallback must detect reset per selected module, not by
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
echo "PASS: lgyosys reset constraints are selected-module local"

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
  --set formal.solver=lgyosys --workdir "$W/c2_reset_low" -q \
  --result-json "$W/r2_reset_low.json" \
  || fail "lgyosys did not honor active-low reset: $(cat "$W/r2_reset_low.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r2_reset_low.json" \
  || fail "lgyosys active-low reset result not pass: $(cat "$W/r2_reset_low.json")"
echo "PASS: lgyosys applies active-low selected-module reset polarity"

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
echo "PASS: lgyosys descent preserves parameter-specialized child definitions"

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
echo "PASS: lgyosys descent maps renamed occurrences by elaborated interface"

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
echo "PASS: lgyosys descent marks implementation-only generated children as skips"

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
LG=inou/yosys/lgcheck
YOSYS=inou/yosys/yosys2
LGCHECK_EQUIV_TIMEOUT=10 "$LG" --reference "$INV" --implementation "$INV" \
  --yosys "$YOSYS" --reference_top 'child$parent.u' --implementation_top 'child$parent.u' \
  --gold_rtlil "$W/cache_gold.il" --gate_rtlil "$W/cache_gate.il" \
  >"$W/cache_child.log" 2>&1 \
  || { cat "$W/cache_child.log"; fail "cached descendant proof did not select the requested child top"; }
grep -q 'Equivalence successfully proven' "$W/cache_child.log" \
  || { cat "$W/cache_child.log"; fail "cached descendant proof lacked a proof verdict"; }
echo "PASS: cached descendant proofs reload the selected child rather than the parent"

# A cached occurrence name can end in the base module's own spelling, e.g.
# ClockGate$ExuBlock.ClockGate. Source-side compatibility resolution must not
# replace that authoritative cached name with the standalone source base merely
# because the last dotted component exists in the source.
cat >"$W/cache_source_base.v" <<'V'
module u(output o);
  assign o = 1'b1;
endmodule
V
LGCHECK_EQUIV_TIMEOUT=10 "$LG" --reference "$W/cache_source_base.v" \
  --implementation "$W/cache_source_base.v" --yosys "$YOSYS" \
  --reference_top 'child$parent.u' --implementation_top 'child$parent.u' \
  --gold_rtlil "$W/cache_gold.il" --gate_rtlil "$W/cache_gate.il" \
  >"$W/cache_occurrence_name.log" 2>&1 \
  || { cat "$W/cache_occurrence_name.log"; fail "source fallback replaced an authoritative cached occurrence top"; }
grep -q 'Equivalence successfully proven' "$W/cache_occurrence_name.log" \
  || { cat "$W/cache_occurrence_name.log"; fail "cached dotted occurrence lacked a proof verdict"; }
echo "PASS: cached occurrence tops are not rewritten to a source base name"

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
  --set formal.solver=lgyosys --set formal.lec.gold_reader=slang \
  --set formal.lec.gate_reader=slang --set formal.lec.normalize_split_ports=true \
  --set formal.lec.descend_on_inconclusive=true \
  --workdir "$W/c2_split_ports" -q --result-json "$W/r2_split_ports.json" \
  || fail "lec lgyosys split-port adapter not pass: $(cat "$W/r2_split_ports.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r2_split_ports.json" \
  || fail "lec lgyosys split-port adapter not pass: $(cat "$W/r2_split_ports.json")"
echo "PASS: lgyosys normalizes packed reference ports against cgen leaf ports"

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
"$LHD" lec --impl "$W/relaxed_impl.v" --ref "$W/relaxed_ref.sv" --top relaxed_ref \
  --set formal.solver=lgyosys --set formal.lec.gold_reader=slang \
  --workdir "$W/c2_relaxed" -q --result-json "$W/r2_relaxed.json" \
  || fail "lec lgyosys relaxed slang source not pass: $(cat "$W/r2_relaxed.json" 2>/dev/null)"
echo "PASS: lgyosys slang reader accepts project enum and declaration-order idioms"

# A reader/top/setup failure has no equivalence evidence. Keep it a dependency
# error, never a synthetic REFUTED verdict (only a bounded CEX may use that).
if "$LHD" lec --impl "$INV" --ref "$INV" --top no_such_top --set formal.solver=lgyosys \
  --workdir "$W/c2_setup_fail" -q --result-json "$W/r2_setup_fail.json"; then
  fail "lgyosys accepted a missing top"
fi
grep -q '"class":"dependency"' "$W/r2_setup_fail.json" \
  || fail "lgyosys setup failure was not classified as dependency: $(cat "$W/r2_setup_fail.json")"
grep -q 'REFUTED' "$W/r2_setup_fail.json" \
  && fail "lgyosys setup failure was mislabeled REFUTED: $(cat "$W/r2_setup_fail.json")"
echo "PASS: lgyosys setup failures stay distinct from refutations"

# Two engines disagreeing means one of them is WRONG, and the safe reading of
# "an engine holds a concrete counterexample" is never "the designs match".
# Inject exit 1 at the backend seam for an identical raw-Verilog pair: cvc5
# proves the same obligation, so lhd must announce the ENGINE DISAGREEMENT and
# still publish REFUTED with a non-zero exit -- never downgrade it to a pass.
cat >"$W/lgcheck_fake_refute.sh" <<'SH'
#!/bin/sh
exit 1
SH
chmod +x "$W/lgcheck_fake_refute.sh"
if out=$(LHD_LGCHECK="$W/lgcheck_fake_refute.sh" "$LHD" lec --impl "$INV" --ref "$INV" --top inv \
  --set formal.solver=lgyosys --workdir "$W/c2_refute_confirm" --result-json "$W/r2_refute_confirm.json" 2>&1); then
  fail "an lgcheck witness was swallowed into a passing verdict: $out"
fi
echo "$out" | grep -q 'ENGINE DISAGREEMENT' \
  || fail "lgyosys did not disclose the cvc5 disagreement: $out"
grep -q '"verdict":"refuted"' "$W/r2_refute_confirm.json" \
  || fail "a disputed lgyosys witness must stay refuted: $(cat "$W/r2_refute_confirm.json")"
grep -q '"status":"pass"' "$W/r2_refute_confirm.json" \
  && fail "a disputed lgyosys witness was reported as a pass: $(cat "$W/r2_refute_confirm.json")"
echo "PASS: an lgyosys/cvc5 disagreement stays REFUTED instead of becoming a pass"

# A bounded BMC window is a counterexample search, not an equivalence proof.
# This pair first diverges after more than five clocks: the short window must be
# INCONCLUSIVE (never PROVEN), while a deeper window must find the real CEX.
cat >"$W/deep_ref.v" <<'V'
module deep(input clock, output o);
  reg [3:0] count;
  always @(posedge clock) count <= count + 1'b1;
  assign o = count == 4'd7;
endmodule
V
cat >"$W/deep_impl.v" <<'V'
module deep(input clock, output o);
  reg [3:0] count;
  always @(posedge clock) count <= count + 1'b1;
  assign o = 1'b0;
endmodule
V
out=$(LGCHECK_BMC_STEPS=5 "$LHD" lec --impl "$W/deep_impl.v" --ref "$W/deep_ref.v" --top deep \
  --set formal.solver=lgyosys --workdir "$W/c2_bounded_short" 2>&1) \
  || fail "short bounded lgyosys run should be inconclusive, not an error: $out"
echo "$out" | grep -q 'INCONCLUSIVE' \
  || fail "short bounded lgyosys run did not report INCONCLUSIVE: $out"
echo "$out" | grep -q 'PROVEN equivalent' \
  && fail "five counterexample-free steps were mislabeled an equivalence proof: $out"
if out=$(LGCHECK_BMC_STEPS=10 "$LHD" lec --impl "$W/deep_impl.v" --ref "$W/deep_ref.v" --top deep \
  --set formal.solver=lgyosys --workdir "$W/c2_bounded_deep" 2>&1); then
  fail "deeper lgyosys BMC missed the delayed mismatch: $out"
fi
echo "$out" | grep -q 'REFUTED' \
  || fail "deeper lgyosys BMC did not classify the delayed mismatch as REFUTED: $out"
echo "PASS: bounded no-CEX stays inconclusive; a deeper BMC counterexample refutes"

# The definitive lgcheck BMC initializes every surviving flop to zero. Its
# per-side preparation must preserve don't-care startup state until then:
# ordinary `opt` turns GOLD's resetless `if (en) q <= 1` DFFE into constant 1,
# while the same machine behind mapped DFF instances survives as zero-initialized
# state on GATE. That manufactured the br_amba_axil_msi netlist refutation even
# though the next-state functions are equal. Exercise the reduced reproducer
# directly and pin the production BMC to the same DC-preserving preparation.
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
"$YOSYS" -p "
  read_verilog -sv $W/keepdc_gold.v; hierarchy -top keepdc; proc; bmuxmap; memory; opt -keepdc; flatten;
  rename -top gold; prep -top gold; design -stash gold;
  read_verilog -sv $W/keepdc_gate.v; hierarchy -top keepdc; proc; bmuxmap; memory; opt -keepdc; flatten;
  rename -top gate; prep -top gate; design -stash gate;
  design -copy-from gold -as gold gold; design -copy-from gate -as gate gate;
  miter -equiv -flatten -make_outputs -ignore_gold_x gold gate miter;
  async2sync; dffunmap; proc; opt_clean; hierarchy -top miter;
  sat -ignore_unknown_cells -seq 1 -set-at 1 trigger 1 -prove trigger 0 -set-init-zero -set-def-inputs -show-ports miter
" >"$W/keepdc.log" 2>&1 \
  || { cat "$W/keepdc.log"; fail "DC-preserving bounded miter setup failed"; }
grep -q 'SAT proof finished - no model found: SUCCESS' "$W/keepdc.log" \
  || { cat "$W/keepdc.log"; fail "DC-preserving bounded miter manufactured a startup mismatch"; }
grep -Fq 'proc; bmuxmap; memory; opt -keepdc; flatten' "$LG" \
  || fail "lgcheck BMC no longer uses the tested DC-preserving side preparation"
echo "PASS: lgyosys BMC preserves don't-care startup state before zero initialization"

# 3. Bare .v paths on BOTH sides: the verilog kind is inferred from the
#    extension; an identical netlist is trivially PROVEN (in-process cvc5).
"$LHD" lec --impl "$INV" --ref "$INV" --top inv --workdir "$W/c3" -q --result-json "$W/r3.json" \
  || fail "lec verilog identity (cvc5) not pass: $(cat "$W/r3.json" 2>/dev/null)"
echo "PASS: lec verilog vs verilog (bare .v, kind inferred, slang)"

# 4. --reader yosys-verilog override: the verilog side elaborates through the
#    yosys front-end instead of slang (same design => still PROVEN).
"$LHD" lec --impl "$INV" --ref "$INV" --top inv --reader yosys-verilog \
  --workdir "$W/c4" -q --result-json "$W/r4.json" \
  || fail "lec verilog (--reader yosys-verilog) not pass: $(cat "$W/r4.json" 2>/dev/null)"
echo "PASS: lec verilog vs verilog (--reader yosys-verilog override)"

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

echo "ALL PASS: lhd lec verilog inputs + solver backends"

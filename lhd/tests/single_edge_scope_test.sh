#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# todo/livehd/2f-latch M8 steps 1h + 3a — the BLAST-RADIUS argument for
# `pass.single_edge`, made falsifiable.
#
# Edge normalization is the plan of record for latches and negedge state under
# the formal engines, and the reason it could land without re-baselining the
# whole corpus rests on exactly two claims. Neither is self-evident, and both
# are the kind of thing that rots silently:
#
#   1h. IT IS NOT ON THE SYNTHESIS PATH. `lhd compile` must still emit a real
#       `always_latch` and a real `negedge` — the netlist handed to ABC has to
#       contain the actual hardware. (An AIGER/ABC "latch" is an edge-triggered
#       unit-delay register on an implicit global clock, NOT a level-sensitive
#       latch, and ABC's BLIF reader silently DISCARDS the `.latch` control
#       tokens — so a real latch reaching ABC is a silent mismodel, not an
#       error. That is why this direction has to be pinned, not assumed.)
#       Keeping `lhd sim` on the source graph matters for a second reason: it
#       is the INDEPENDENT oracle the transformation itself is validated
#       against, and normalizing it away would cost us that.
#
#   3a. IT IS SKIPPED, NOT RUN AS A NO-OP, on a design with no latch, no
#       negedge state and one clock net. "Skipped" was unfalsifiable prose
#       until this test: here the same design is compiled from the pass's
#       OUTPUT library and from its INPUT library, and the two Verilog
#       emissions must be byte-identical.
#
# The negative half matters as much as the positive: if the trigger under-fires
# the fixtures stop being proven, and if it over-fires every ordinary design
# silently changes time base. Both directions are asserted.

set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then
    LHD=./bazel-bin/lhd/lhd
  else
    echo "FAIL: could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() {
  echo "FAIL: $*"
  exit 1
}

# ---- fixtures ---------------------------------------------------------------
cat > "$W/plain.prp" <<'EOF'
pub mod plain8(d:u8) -> (q:u8@[1]) {
  reg f:u8 = 0
  reg g:u8 = 0
  q = g
  g = f
  f = d
}
EOF

cat > "$W/lat.prp" <<'EOF'
pub mod lat8(en:bool, d:u8) -> (q:u8@[0]) {
  reg l:u8:[latch=true]
  q = l
  if en {
    l = d
  }
}
EOF

cat > "$W/neg.prp" <<'EOF'
pub mod neg8(d:u8) -> (q:u8@[1]) {
  reg a:u8 = 0
  reg b:u8:[posclk=false] = 0
  q = b
  b = a
  a = d
}
EOF

compile_lg() { # <name> <top>
  rm -rf "$W/lg_$1"
  "$LHD" compile "$W/$1.prp" --top "$2" --emit-dir "lg:$W/lg_$1" --workdir "$W/cw_$1" >"$W/c_$1.log" 2>&1 \
    || { tail -5 "$W/c_$1.log"; fail "compile of $1.prp failed"; }
}

emit_v() { # <lgdir> <top> <outdir>
  rm -rf "$3"
  "$LHD" compile "lg:$1" --top "$2" --emit-dir "verilog:$3" --workdir "$W/vw_$(basename "$3")" \
    >"$W/v_$(basename "$3").log" 2>&1 \
    || { tail -5 "$W/v_$(basename "$3").log"; fail "verilog emission from $1 failed"; }
}

compile_lg plain plain8
compile_lg lat   lat8
compile_lg neg   neg8

# ---- 1h: the SYNTHESIS path is untouched ------------------------------------
# `lhd compile` runs the shared graph pipeline. If edge normalization ever crept
# into it (recipe_graph_passes, or graph_pipeline_and_emits), the latch would
# come out as a flop and the negedge as a posedge — silently, and only visible
# in the emitted netlist.
emit_v "$W/lg_lat" lat8 "$W/v_lat"
grep -rq "always_latch" "$W/v_lat" \
  || { grep -rh "always" "$W/v_lat" | head -3; fail "lhd compile no longer emits a real always_latch — edge normalization leaked onto the synthesis path (ABC's BLIF reader DISCARDS latch control tokens, so this is a silent mismodel)"; }
echo "ok: lhd compile still emits always_latch"

emit_v "$W/lg_neg" neg8 "$W/v_neg"
grep -rq "negedge" "$W/v_neg" \
  || { grep -rh "always" "$W/v_neg" | head -3; fail "lhd compile no longer emits a real negedge — edge normalization leaked onto the synthesis path"; }
echo "ok: lhd compile still emits a real negedge"

# ...and the compile recipe must never name the pass at all.
grep -q "pass.single_edge" "$W/c_lat.log" \
  && fail "pass.single_edge appears in the lhd compile recipe: it must run only under formal verify / lec"
echo "ok: pass.single_edge is absent from the lhd compile recipe"

# ---- 3a: SKIPPED on a clean design, and that is falsifiable ------------------
rm -rf "$W/lg_plain_out"
out="$("$LHD" pass single_edge --top plain8 "lg:$W/lg_plain" --emit-dir "lg:$W/lg_plain_out" \
       --workdir "$W/pw_plain" 2>&1)"
[ $? -eq 0 ] || { echo "$out" | tail -5; fail "pass single_edge failed on a plain posedge design"; }
grep -q "single-edge-skipped" <<<"$out" \
  || { echo "$out" | tail -5; fail "the pass did not report SKIPPED on an all-posedge single-clock design"; }
grep -q "single-edge-applied" <<<"$out" \
  && fail "the pass APPLIED to an all-posedge single-clock design — the trigger over-fires and every ordinary design silently changes time base"
echo "ok: an all-posedge single-clock design is reported skipped"

# Skipped must mean the graph is untouched, not 'rewritten to the same thing'.
# Compare through cgen: a byte-identical emission is a much stronger statement
# than a diff of the serialized library (which carries incidental ordering).
emit_v "$W/lg_plain"     plain8 "$W/v_plain_in"
emit_v "$W/lg_plain_out" plain8 "$W/v_plain_out"
diff -r "$W/v_plain_in" "$W/v_plain_out" >"$W/plain.diff" 2>&1 \
  || { head -20 "$W/plain.diff"; fail "the SKIPPED pass still changed the design (see the diff above)"; }
echo "ok: skipped really means untouched (byte-identical Verilog emission)"

# ---- the positive half: it DOES fire where it must --------------------------
# Without this, 'skipped' above could be a pass that never does anything.
rm -rf "$W/lg_lat_out"
out="$("$LHD" pass single_edge --top lat8 "lg:$W/lg_lat" --emit-dir "lg:$W/lg_lat_out" \
       --workdir "$W/pw_lat" 2>&1)"
[ $? -eq 0 ] || { echo "$out" | tail -5; fail "pass single_edge failed on a latch design"; }
grep -q "single-edge-applied" <<<"$out" \
  || { echo "$out" | tail -5; fail "the pass did NOT fire on a latch design (the trigger under-fires)"; }
echo "ok: the pass fires on a latch design"

emit_v "$W/lg_lat_out" lat8 "$W/v_lat_out"
grep -rq "always_latch" "$W/v_lat_out" \
  && { grep -rh "always" "$W/v_lat_out" | head -3; fail "the NORMALIZED graph still holds a latch — the lowering is partial, which reproduces the M4 step-1 counterexample with no diagnostic anywhere"; }
grep -rq "posedge" "$W/v_lat_out" \
  || { grep -rh "always" "$W/v_lat_out" | head -3; fail "the normalized latch did not become a posedge flop"; }
echo "ok: the normalized graph holds a posedge flop, not a latch"

rm -rf "$W/lg_neg_out"
out="$("$LHD" pass single_edge --top neg8 "lg:$W/lg_neg" --emit-dir "lg:$W/lg_neg_out" \
       --workdir "$W/pw_neg" 2>&1)"
[ $? -eq 0 ] || { echo "$out" | tail -5; fail "pass single_edge failed on a negedge design"; }
grep -q "single-edge-applied" <<<"$out" \
  || { echo "$out" | tail -5; fail "the pass did NOT fire on a negedge design"; }
emit_v "$W/lg_neg_out" neg8 "$W/v_neg_out"
grep -rq "negedge" "$W/v_neg_out" \
  && { grep -rh "always" "$W/v_neg_out" | head -3; fail "the NORMALIZED graph still holds a negedge flop — a partial lowering"; }
grep -rq "single_edge_phase" "$W/v_neg_out" \
  || { grep -rh "reg " "$W/v_neg_out" | head -5; fail "the normalized graph has no phase divider: the two edges collapsed into one slot, which is exactly the blindness M8 exists to remove"; }
echo "ok: a negedge design normalizes to posedge flops plus a phase divider"

# ---- rule 4: COINCIDENT COMMIT EDGES are refused, not approximated ----------
# M8 step 1b / condition 5, the US7624363 L1 case. A latch is transparent right
# up to its closing edge, so anything that commits at that SAME instant samples
# the latch's D, not its held Q. The flop-with-enable model hands it the held Q
# instead: a persistent FULL-CYCLE error, silent in Verilog. Slotting does not
# fix this — coincident classes share a slot BY CONSTRUCTION, so it reproduces
# it. Hence an explicit refusal (M3 rule 4's "explicit rejection" arm).
#
# The shape: a transparent-LOW latch closes at the clock's RISE, and a POSEDGE
# flop samples at that same rise.
cat > "$W/l1.prp" <<'EOF'
pub mod l1(clk:bool, d:u8) -> (q:u8@[1]) {
  reg ll:u8:[latch=true]
  reg f:u8:[clock_pin=ref clk] = 0
  q = f
  if !clk {
    ll = d
  }
  f = ll
}
EOF
compile_lg l1 l1
rm -rf "$W/lg_l1_out"
out="$("$LHD" pass single_edge --top l1 "lg:$W/lg_l1" --emit-dir "lg:$W/lg_l1_out" \
       --workdir "$W/pw_l1" 2>&1)"
[ $? -eq 0 ] && { echo "$out" | tail -5; fail "a transparent-low latch feeding a POSEDGE flop on the same clock was accepted: the two commit at the same edge, so the model gives the flop the latch's PREVIOUS value — a persistent full-cycle error"; }
grep -q "coincident-commit-edge" <<<"$out" \
  || { echo "$out" | tail -5; fail "the coincident-edge refusal fired without its named diagnostic (a crash is not a refusal)"; }
echo "ok: a coincident-edge latch/flop pair is refused with a named diagnostic"

# The refusal must be NARROW. A DATA-gated latch feeding a flop is the ordinary
# flop-with-enable abstraction the whole task is scoped to (it is what lhd sim
# and cgen already ship), and inou/prp/tests/equiv/latch_mixed_flop.prp is
# exactly that shape — keyed on the slot instead of the commit CLASS, rule 4
# would reject it and take a live fixture down with it.
cat > "$W/mix.prp" <<'EOF'
pub mod mix8(clk:bool, en:bool, d:u8) -> (q:u8@[1]) {
  reg l:u8:[latch=true]
  reg f:u8 = 0
  q = f
  if en {
    l = d
  }
  f = l
}
EOF
compile_lg mix mix8
rm -rf "$W/lg_mix_out"
out="$("$LHD" pass single_edge --top mix8 "lg:$W/lg_mix" --emit-dir "lg:$W/lg_mix_out" \
       --workdir "$W/pw_mix" 2>&1)"
[ $? -eq 0 ] \
  || { echo "$out" | tail -5; fail "a DATA-gated latch feeding a flop was refused: rule 4 is keyed on the slot instead of the commit class, and it just rejected the latch_mixed_flop shape"; }
echo "ok: a data-gated latch feeding a flop is still accepted (rule 4 is keyed on the commit class)"

# ---- 3h: a free / aperiodic SECOND CLOCK is DECLINED, not slotted -----------
# A clock port the testbench drives as data has no integer ratio to the
# reference, so slotting it would silently change what the design means. It must
# be SKIPPED rather than refused: pass/lec's detected-edge branch REFUTES
# two-clock-vs-one-clock today and sim/multiclock_two_domain.prp pins the value
# semantics, and turning a correct verdict into an unsupported exit is a
# regression, not fail-closed.
cat > "$W/twoclk.prp" <<'EOF'
pub mod twoclk(clk:bool, clkb:bool, d:u8) -> (qa:u8@[1], qb:u8@[2]) {
  reg ra:u8:[clock_pin=ref clk] = 0
  reg rb:u8:[clock_pin=ref clkb] = 0
  qa = ra
  qb = rb
  ra = d
  rb = ra
}
EOF
compile_lg twoclk twoclk
rm -rf "$W/lg_twoclk_out"
out="$("$LHD" pass single_edge --top twoclk "lg:$W/lg_twoclk" --emit-dir "lg:$W/lg_twoclk_out" \
       --workdir "$W/pw_twoclk" 2>&1)"
[ $? -eq 0 ] || { echo "$out" | tail -5; fail "a two-clock design with no latch and no negedge state was REFUSED instead of skipped — that degrades the correct two-clock-vs-one-clock REFUTED into an unsupported exit"; }
grep -q "single-edge-applied" <<<"$out" \
  && fail "a two-clock design was SLOTTED: there is no known ratio between the two clocks, so this silently changes what the design means"
echo "ok: a second clock domain with no known ratio is skipped, not slotted and not refused"

# ...but a second clock domain that ALSO holds a latch cannot be skipped (the
# encoder would refuse the Latch cell), so there it must fail closed loudly.
cat > "$W/twoclk_lat.prp" <<'EOF'
pub mod tcl(clk:bool, clkb:bool, en:bool, d:u8) -> (qa:u8@[1], ql:u8@[0]) {
  reg ra:u8:[clock_pin=ref clk] = 0
  reg rb:u8:[clock_pin=ref clkb] = 0
  reg l:u8:[latch=true]
  qa = rb
  ql = l
  ra = d
  rb = ra
  if en {
    l = d
  }
}
EOF
compile_lg twoclk_lat tcl
rm -rf "$W/lg_tcl_out"
out="$("$LHD" pass single_edge --top tcl "lg:$W/lg_twoclk_lat" --emit-dir "lg:$W/lg_tcl_out" \
       --workdir "$W/pw_tcl" 2>&1)"
[ $? -eq 0 ] && { echo "$out" | tail -5; fail "a two-clock design HOLDING A LATCH was skipped: the encoder refuses the Latch cell, so skipping leaves a design nothing can decide"; }
grep -q "multi-clock-no-ratio" <<<"$out" \
  || { echo "$out" | tail -5; fail "the two-clock-with-latch refusal fired without its named diagnostic"; }
echo "ok: a second clock domain that also holds a latch fails closed with a named diagnostic"

# ---- a top IO ASSUME on a P > 1 design constrains every microstep ------------
# At P>1 the pass rewires each design ASSERT obligation's cond to
# `boundary implies cond`, so asserts are only checked where a whole period has
# settled; assume-kind conds are NOT wrapped (an environment constraint must
# constrain EVERY step, and historically wrapping one silently freed the
# mid-period steps — a FALSE REFUTED factory).
#
# A selected-top input assume has no parent that can discharge it. It therefore
# warns and remains active with assume_nocheck semantics. What this case pins is
# that the constraint is period-independent: it stays in force at every P=2
# microstep while assertions are observed only at settled period boundaries.
cat > "$W/asm.prp" <<'EOF'
pub mod asm_tb(sel:bool) -> (ok:bool@[0]) {
  reg cyc:u3 = 0
  reg a:u8 = 0
  reg b:u8:[posclk=false] = 0     // negedge stage: this is what forces P=2
  const c  = cyc
  const aq = a
  const bq = b
  assume(!sel, "the environment never asserts sel")
  a = if sel { 200 } else { 11 + (16 * c) }
  b = aq
  wrap cyc += 1
  ok = bq#[0] == 0
  assert((c >= 1) implies (aq != 200), "only holds under the sel constraint")
  assert((c >= 1) implies (bq == aq), "the half-cycle transfer alongside an assume")
}
EOF
out="$("$LHD" formal verify "$W/asm.prp" --top asm_tb --workdir "$W/aw" \
        --set formal.bound=12 --set formal.simfail_run=false 2>&1)"
rc=$?
[ "$rc" -eq 0 ] || { echo "$out" | tail -6; fail "the top IO assume must remain active and prove the P=2 assertions (rc=$rc)"; }
grep -q "pass.single_edge slots:2" <<<"$out" \
  || fail "the assume fixture did not reach P=2, so it pins nothing about normalization"
grep -q "formal-top-assume" <<<"$out" \
  || { echo "$out" | tail -6; fail "the selected-top IO assume must emit its cannot-check warning"; }
grep -q "in force (UNCHECKED top-level IO assume cannot be checked; treated as assume_nocheck" <<<"$out" \
  || { echo "$out" | tail -6; fail "the top IO assume must be disclosed as active under P=2"; }
echo "ok: the top IO assume stays active at every P=2 microstep"

# NON-VACUITY: the fixture's asserts are not self-satisfying -- without the
# assume the design still REFUTES (now for the assert, not the assume check).
grep -v 'assume(!sel' "$W/asm.prp" > "$W/asm_novac.prp"
out="$("$LHD" formal verify "$W/asm_novac.prp" --top asm_tb --workdir "$W/awv" \
        --set formal.bound=12 --set formal.simfail_run=false 2>&1)"
grep -q "REFUTED" <<<"$out" \
  || { echo "$out" | tail -5; fail "the same design WITHOUT the assume still passed: the fixture's asserts hold on their own, so the case above pins nothing"; }
echo "ok: the fixture's asserts are load-bearing (removing the assume still REFUTES)"

# ---- a design compared against ITSELF must still prove ----------------------
# `--impl X --ref X` is the vacuity-guard idiom, and both sides then resolve to
# the SAME graph object (the graph library is a singleton per path). Normalizing
# twice would run the second pass over the ALREADY-normalized graph, find
# nothing left to lower, and report "skipped" with no slot count and no
# reference clock -- which the cross-side agreement checks read as a
# disagreement and refuse a design that is trivially equal to itself.
compile_lg lat lat8   # a latch design: normalization definitely fires
out="$("$LHD" lec --impl "lg:$W/lg_lat" --ref "lg:$W/lg_lat" --top lat8 --workdir "$W/selfw" 2>&1)"
[ $? -eq 0 ] || { echo "$out" | tail -5; fail "a latch design compared against ITSELF did not prove -- the miter-wide agreement checks are firing on a single shared graph"; }
grep -qE "'[^']*lat8' PROVEN" <<<"$out" \
  || { echo "$out" | tail -5; fail "self-comparison of a latch design did not report PROVEN"; }
echo "ok: a normalized design still proves against itself"

echo "PASS: single_edge_scope_test"

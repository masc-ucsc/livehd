#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for the sequential `lhd pass abc` knobs (task 2a-abc subtask 5):
# technology-map a colored SEQUENTIAL design to a standard-cell netlist and prove
# it LEC-equivalent to the original logic, exercising both register-mapping modes
# and both memory-mapping modes.
#
#   pass.abc.register=true   flops -> library DFF cells (DFFx1 in the test lib;
#                            the QN-only DFFNx1 + DFFNx2 drive ladder in test_qn.lib)
#   pass.abc.register=false  flops kept native (`always @(posedge)`)
#   pass.abc.memory=true     memory bit-blasted into a DFF array + mux gates (the
#                            default since the mem_lower constant-address rework)
#   pass.abc.memory=false    memory kept as a native boundary instance
#
# Registers cross into ABC as 1-bit latches (so ABC can optimize the
# surrounding logic) with a synchronous reset folded into D (`rst ? rval :
# (en ? din : q)`, reset over enable, exactly cgen's if/else-if); on read-back
# register=true maps each latch to a plain DFF cell under the register's name
# (an asynchronous-reset register or a resetless power-on init stays native so
# its contract survives), while register=false rebuilds a native flop. Sub
# instances are blackbox boundaries; a memory is
# either a boundary (memory=false) or lowered to flops+mux gates (memory=true).
# Each mode's mapped netlist must be sequentially LEC-equivalent (yosys miter +
# BMC/induction, via `lhd lec --set formal.solver=lgyosys`) to its `partition` twin.
#
#   prp -> lg (O1)
#   pass color synth                       (the abc driver coloring)
#   pass abc --set pass.abc.seq=true        (partition + ABC seq tech-map)
#   pass partition                          (same module structure, original logic)
#   pass liberty gensim test.lib            (behavioral model per comb cell)
#   cgen net + models -> impl.v ; cgen re -> ref.v
#   lhd lec --set formal.solver=lgyosys (impl vs ref): must be sequentially LEC-equivalent
#
# LEC soundness of this `lhd lec` / gensim pipeline (a corrupted reference must
# FAIL) is covered by the combinational lhd_abc_test's negative control. A
# sequential negative control is intentionally omitted here: disproving
# sequential inequivalence drives yosys onto a slow temporal-induction search,
# and lgcheck already pins reset to avoid vacuous sequential passes. This test's
# guards against a vacuous pass are the structural checks below (real standard
# cells AND a surviving sequential `always` block per fixture) plus three
# independent positive proofs (flops, memory, hierarchy).
#
# Fixtures (inou/prp/tests/pyrope): abc_seq (two flat registers + comb cones),
# abc_mem (1rd/1wr register-array memory as a blackbox), hier_seq (3-level
# pipeline: top -> stage_unit x2 -> delayer, flops + Sub blackbox boundaries).
# Hermetic: small vendored Liberty (inou/prp/tests/abc/test.lib), not the PDK.

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
W="${TEST_TMPDIR:-/tmp/lhd_abc_seq_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

[ -f "$LIB" ] || fail "missing liberty $LIB"

# run_abc_lec <fix> <top> <register> <memory> [register_max_bits]: tech-map with the given knobs,
# build the original-logic twin + gensim models, and prove the netlist equivalent.
# <memory> = true|false is passed explicitly; `default` leaves pass.abc.memory
# unset so the run exercises whatever the pass defaults to.
# Leaves the netlist verilog dir in the global NETV for the caller's structural asserts.
NETV=""
run_abc_lec() {
  local fix="$1" top="$2" reg="$3" mem="$4" reg_max="${5:-0}"
  local prp="inou/prp/tests/pyrope/${fix}.prp"
  local d="$W/${fix}_r${reg}_m${mem}_x${reg_max}"
  mkdir -p "$d"
  local r="$d/r.json"
  run() { "$LHD" "$@" -q --result-json "$r" || fail "$* -> $(cat "$r" 2>/dev/null)"; }
  local memset=()
  [ "$mem" = "default" ] || memset=(--set "pass.abc.memory=$mem")

  [ -f "$prp" ] || fail "missing fixture $prp"
  run compile "$prp" --top "$top" --recipe O1 --emit-dir lg:"$d/lg" --workdir "$d/w1"
  run pass color synth --top "$top" lg:"$d/lg" --workdir "$d/w2"
  run pass abc --top "$top" lg:"$d/lg" --emit-dir lg:"$d/net" --set abc.library="$LIB" \
      --set pass.abc.register="$reg" --set pass.abc.register_max_bits="$reg_max" \
      "${memset[@]}" --workdir "$d/w3"
  # the original-logic twin (same module structure)
  run pass partition --top "$top" lg:"$d/lg" --emit-dir lg:"$d/re" --workdir "$d/w4"
  run pass liberty gensim "$LIB" --emit-dir lg:"$d/models" --workdir "$d/w5"

  run compile lg:"$d/net" --top "$top" --recipe O0 --emit-dir verilog:"$d/netv" --workdir "$d/w6"
  run compile lg:"$d/models" --recipe O0 --emit-dir verilog:"$d/modelsv" --workdir "$d/w7"
  run compile lg:"$d/re" --top "$top" --recipe O0 --emit-dir verilog:"$d/rev" --workdir "$d/w8"

  # the netlist really is a standard-cell netlist (Sub instances of Liberty cells)
  grep -hq "NAND2x1\|NOR2x1\|INVx1\|XOR2x1\|BUFx1" "$d/netv/"*.v \
    || fail "$fix[reg=$reg,mem=$mem]: no standard cells in the ABC netlist"

  cat "$d/netv/"*.v "$d/modelsv/"*.v > "$d/impl.v"
  cat "$d/rev/"*.v > "$d/ref.v"

  # LEC: the tech-mapped netlist must equal the original logic in every mode
  run lec --set formal.solver=lgyosys --impl verilog:"$d/impl.v" --ref verilog:"$d/ref.v" --top "$top" --workdir "$d/wc"
  NETV="$d/netv"
}

has() { grep -hq "$2" "$1/"*.v; }

# abc_seq/hier_seq declare concrete initial values, which request Pyrope's
# implicit SYNCHRONOUS reset (`reset_pin` + `initial`, no `async`). That reset
# is a D-cone mux, so register=true folds it into the latch and maps every
# register to plain DFFx1 cells named `<reg>_<bit>`; the `initial` is the reset
# value realized on D, never a power-on value, so no native `always` survives.
# The lgyosys LEC (reset pinned, then free) proves the fold; abc_async_reset
# below adds the graph-native cvc5 proof, which seeds power-on state.
run_abc_lec abc_seq abc_seq.abc_seq true false
! has "$NETV" "posedge" || fail "abc_seq register=true: a synchronous-reset register stayed a native flop"
[ "$(grep -h '^DFFx1 ' "$NETV"/*.v | wc -l | tr -d ' ')" = 8 ] \
  || fail "abc_seq register=true: expected 8 DFFx1 cells (p, q x 4 bits), got $(grep -h '^DFFx1 ' "$NETV"/*.v | wc -l)"
for r in p q; do
  for b in 0 1 2 3; do
    has "$NETV" "^DFFx1 ${r}_${b}(" || fail "abc_seq register=true: DFF cell for '${r}[${b}]' is not named ${r}_${b}: $(grep -h '^DFFx1 ' "$NETV"/*.v)"
  done
done
echo "PASS: register=true folds the synchronous reset into D and maps the registers to named DFF cells (abc_seq)"

# hier_seq: six 8-bit registers (delayer.r x4, stage_unit.r x2) -> 48 DFFx1
# cells, each under its register's (hierarchical, `\a.d1.r_<bit>`) name.
run_abc_lec hier_seq hier_seq.top true false
! has "$NETV" "posedge" || fail "hier_seq register=true: a synchronous-reset register stayed a native flop"
[ "$(grep -h '^DFFx1 ' "$NETV"/*.v | wc -l | tr -d ' ')" = 48 ] \
  || fail "hier_seq register=true: expected 48 DFFx1 cells (6 registers x 8 bits), got $(grep -h '^DFFx1 ' "$NETV"/*.v | wc -l)"
grep -hqE "^DFFx1 (\\\\[a-z0-9.]+\.)?r_[0-7][ (]" "$NETV"/*.v \
  || fail "hier_seq register=true: registers did not map to DFFx1 cells under their name: $(grep -h '^DFFx1 ' "$NETV"/*.v | head -8)"
echo "PASS: register=true maps synchronous-reset registers to DFF cells across hierarchy (hier_seq)"

# A synchronous reset expressed in the D cone does NOT specify power-on state.
# ABC is free to choose zero for its internal don't-care latch init, but the
# read-back must recover the source LGraph's absent init rather than turn that
# optimization witness into a hardware guarantee.  The graph-native LEC checks
# arbitrary equal startup state, which the Yosys backend intentionally ignores.
RSD="$W/abc_resetless_sync"
mkdir -p "$RSD"
RSR="$RSD/r.json"
rsrun() { "$LHD" "$@" -q --result-json "$RSR" || fail "$* -> $(cat "$RSR" 2>/dev/null)"; }
rsrun compile lhd/tests/abc_resetless_sync.prp --top abc_resetless_sync --recipe O1 \
  --emit-dir lg:"$RSD/lg" --workdir "$RSD/w1"
rsrun pass color synth --top abc_resetless_sync lg:"$RSD/lg" --workdir "$RSD/w2"
rsrun pass partition --top abc_resetless_sync lg:"$RSD/lg" --emit-dir lg:"$RSD/re" --workdir "$RSD/w3"
rsrun pass abc --top abc_resetless_sync lg:"$RSD/lg" --emit-dir lg:"$RSD/net" --set abc.library="$LIB" \
  --workdir "$RSD/w4"
rsrun pass liberty gensim "$LIB" --emit-dir lg:"$RSD/models" --workdir "$RSD/w5"
rsrun lec --impl lg:"$RSD/net" --ref lg:"$RSD/re" --lib lg:"$RSD/models" --top abc_resetless_sync \
  --set formal.solver=cvc5 --workdir "$RSD/wlec"
rsrun compile lg:"$RSD/net" --top abc_resetless_sync --recipe O0 --emit-dir verilog:"$RSD/netv" --workdir "$RSD/w6"
has "$RSD/netv" "DFFx1 " || fail "abc_resetless_sync: init-less flop was not mapped to DFF cells"
! has "$RSD/netv" "posedge" || fail "abc_resetless_sync: fake ABC init kept the flop native"
echo "PASS: ABC's internal don't-care init does not become a netlist power-on value"

# QN-only DFF cell (test_qn.lib = test.lib + an ASAP7-shaped QN flop family).
# The register pick is the smallest-area plain POSEDGE flop: DFFNx1 (area 5,
# `next_state : "!D"`, lone QN output) over DFFx1 (6); the negedge decoy DFFNLx1
# (area 4, `clocked_on : "!CLK"`) must never win. The cell's QN pin is wired as
# the register's Q and the D side carries the complement: under the built-in
# flow the latch crosses ABC as ~D and the mapper absorbs it (no `__dinv`
# inverter Sub); a user-authored flow may retime, so the AIG stays honest and
# the read-back absorbs the inversion locally -- a NAND2 D-cone root becomes
# its inverting twin AND2x1 (abc_qn_twin), anything it cannot absorb gets one
# inverter `<reg>__dinv` on D. The gensim model is Flop(Not(D)) (the model's
# state is the QN pin, which pass/lec shares with the source register's
# power-on symbol), through which cvc5 proves every netlist against the
# original logic. The drive ladder puts a
# fanout-20 register (abc_qn_fanout) on the second rung DFFNx2 while fanout-1
# registers stay on DFFNx1.
QLIB=inou/prp/tests/abc/test_qn.lib
[ -f "$QLIB" ] || fail "missing liberty $QLIB"
count() { grep -h "$2" "$1/"*.v | wc -l; }
# run_qn <tag> <fixture> <top> [extra --set ...]: map <fixture> under test_qn.lib,
# prove it with cvc5 through the gensim models, leave netv in QNV / models in QNM.
QNV=""
QNM=""
run_qn() {
  local tag="$1" fix="$2" top="$3"
  shift 3
  local d="$W/qn_$tag"
  mkdir -p "$d"
  local r="$d/r.json"
  qrun() { "$LHD" "$@" -q --result-json "$r" || fail "$* -> $(cat "$r" 2>/dev/null)"; }
  qrun compile "lhd/tests/${fix}.prp" --top "$top" --recipe O1 --emit-dir lg:"$d/lg" --workdir "$d/w1"
  qrun pass color synth --top "$top" lg:"$d/lg" --workdir "$d/w2"
  qrun pass partition --top "$top" lg:"$d/lg" --emit-dir lg:"$d/re" --workdir "$d/w3"
  qrun pass abc --top "$top" lg:"$d/lg" --emit-dir lg:"$d/net" --set abc.library="$QLIB" --set abc.qor="$d/abc.json" \
    "$@" --workdir "$d/w4"
  qrun pass liberty gensim "$QLIB" --emit-dir lg:"$d/models" --workdir "$d/w5"
  qrun lec --impl lg:"$d/net" --ref lg:"$d/re" --lib lg:"$d/models" --top "$top" --set formal.solver=cvc5 \
    --workdir "$d/wlec"
  qrun compile lg:"$d/net" --top "$top" --recipe O0 --emit-dir verilog:"$d/netv" --workdir "$d/w6"
  qrun compile lg:"$d/models" --recipe O0 --emit-dir verilog:"$d/modelsv" --workdir "$d/w7"
  QNV="$d/netv"
  QNM="$d/modelsv"
  grep -q '"dff":{"cell":"DFFNx1","q_inverted":true,"ladder":\["DFFNx1","DFFNx2"\]' "$d/abc.json" \
    || fail "qn_$tag: abc.json does not report the QN cell pick: $(grep -o '"dff":{[^}]*}[^}]*}' "$d/abc.json")"
  # register_max_bits defaults to 0 (disabled): every flop maps, as yosys does.
  # The old 4096-bit guard was tripped by one bit-blasted 64x64 memory.
  grep -q '"register_max_bits":0,' "$d/abc.json" \
    || fail "qn_$tag: abc.json does not report register_max_bits=0 as the default: $(grep -o '"register_max_bits":[0-9]*' "$d/abc.json")"
}

run_qn builtin abc_resetless_sync abc_resetless_sync
has "$QNV" "DFFNx1 " || fail "qn: smallest-area QN flop DFFNx1 was not picked"
has "$QNV" "\.QN(" || fail "qn: DFFNx1's QN pin is not wired"
! has "$QNV" "DFFNLx1" || fail "qn: negedge decoy DFFNLx1 was mapped onto a posedge register"
! has "$QNV" "DFFx1 " || fail "qn: the larger Q-only DFFx1 was picked over DFFNx1"
! has "$QNV" "DFFNx2 " || fail "qn: a fanout-1 register left the x1 rung"
! has "$QNV" "__dinv" || fail "qn: built-in flow added a read-back inverter instead of folding ~D into the mapping"
! has "$QNV" "posedge" || fail "qn: init-less flop was kept native"
grep -q "^module DFFNx1" "$QNM"/*.v || fail "qn: gensim emitted no model for DFFNx1"
grep -q "^module DFFNx2" "$QNM"/*.v || fail "qn: gensim emitted no model for the ladder rung DFFNx2"
[ "$(count "$QNV" "^DFFNx1 ")" = 4 ] || fail "qn: expected 4 DFFNx1 cells, got $(count "$QNV" "^DFFNx1 ")"
echo "PASS: QN-only DFF cell picked by area, inversion folded into the D cone, LEC proven via Flop(Not(D)) model"

# A user flow owns its command list (it may retime), so the AIG stays honest
# and the read-back absorbs the inversion: the toy library has no OR2 twin for
# a NOR2 root, so some registers get a `__dinv` inverter on D -- never more
# than one per cell -- and the netlist still proves.
run_qn user abc_resetless_sync abc_resetless_sync --set 'pass.abc.flow=strash; dc2; map'
has "$QNV" "DFFNx1 " || fail "qn user flow: DFFNx1 not mapped"
has "$QNV" "\.QN(" || fail "qn user flow: DFFNx1's QN pin is not wired"
[ "$(count "$QNV" "^INVx1 [a-z_0-9]*__dinv(")" -le 4 ] \
  || fail "qn user flow: more read-back inverters than DFFNx1 cells: $(count "$QNV" "__dinv(")"
echo "PASS: QN-only DFF cell under a user flow absorbs the inversion on read-back, LEC proven"

# Twin swap: a NAND2 next state. Built-in flow: ABC maps ~f = AND2 itself;
# user flow: the read-back swaps the mapped NAND2 root for AND2x1 (3.5 < 3 + 1).
# Both netlists: one AND2x1 into the DFFNx1, no NAND2x1, no inverter at all.
for qflow in builtin user; do
  extra=()
  [ "$qflow" = user ] && extra=(--set 'pass.abc.flow=strash; dc2; map')
  run_qn "twin_$qflow" abc_qn_twin abc_qn_twin "${extra[@]}"
  [ "$(count "$QNV" "^AND2x1 ")" = 1 ] || fail "qn twin ($qflow): expected one AND2x1, got $(count "$QNV" "^AND2x1 ")"
  ! has "$QNV" "NAND2x1 " || fail "qn twin ($qflow): NAND2 root survived next to a QN cell"
  ! has "$QNV" "INVx1 " || fail "qn twin ($qflow): an inverter was minted where the AND2x1 twin absorbs the inversion"
  [ "$(count "$QNV" "^DFFNx1 ")" = 1 ] || fail "qn twin ($qflow): expected one DFFNx1"
done
echo "PASS: QN inversion absorbed into the D-cone root (mapper under the built-in flow, twin swap under a user flow)"

# Drive ladder: the fanout-20 register takes DFFNx2; its port-fed D is the one
# place the D-side inversion cannot be absorbed, so exactly one INVx1 remains.
run_qn fanout abc_qn_fanout abc_qn_fanout
has "$QNV" "DFFNx2 " || fail "qn fanout: fanout-20 register did not move to the DFFNx2 rung"
! has "$QNV" "DFFNx1 " || fail "qn fanout: the fanout-20 register stayed on DFFNx1"
[ "$(count "$QNV" "^INVx1 ")" = 1 ] || fail "qn fanout: expected exactly one INVx1 (the port-fed D), got $(count "$QNV" "^INVx1 ")"
echo "PASS: DFF drive ladder picks the stronger rung for a high-fanout Q net"

# The test Liberty's plain DFFx1 cannot implement an ASYNCHRONOUS reset: it is
# an event, and folding it into D would make it land only on a clock edge
# (lgcheck toggles reset independently of clk, so that miscompile is caught
# directly). It stays native, `always @(posedge clk or posedge rst)`, with its
# XOR data cone still mapped. The SYNCHRONOUS register is a D-cone mux and maps
# to DFFx1 cells `sync_state_<bit>` with its 0ub1111 reset value realized on D;
# the lgyosys LEC (reset pinned, then free) and the graph-native cvc5 LEC (which
# seeds power-on state from the source's `initial` and encodes its `reset_pin`
# as ITE(rst, initial, ...)) both prove the mixed netlist.
ARD="$W/abc_async_reset"
mkdir -p "$ARD"
ARR="$ARD/r.json"
arrun() { "$LHD" "$@" -q --result-json "$ARR" || fail "$* -> $(cat "$ARR" 2>/dev/null)"; }
arrun compile lhd/tests/abc_async_reset.prp --top abc_async_reset --recipe O1 \
  --emit-dir lg:"$ARD/lg" --workdir "$ARD/w1"
arrun pass color synth --top abc_async_reset lg:"$ARD/lg" --workdir "$ARD/w2"
arrun pass partition --top abc_async_reset lg:"$ARD/lg" --emit-dir lg:"$ARD/re" --workdir "$ARD/w3"
arrun pass abc --top abc_async_reset lg:"$ARD/lg" --emit-dir lg:"$ARD/net" --set abc.library="$LIB" \
  --workdir "$ARD/w4"
arrun pass liberty gensim "$LIB" --emit-dir lg:"$ARD/models" --workdir "$ARD/w5"
arrun compile lg:"$ARD/net" --top abc_async_reset --recipe O0 --emit-dir verilog:"$ARD/netv" --workdir "$ARD/w6"
arrun compile lg:"$ARD/models" --recipe O0 --emit-dir verilog:"$ARD/modelsv" --workdir "$ARD/w7"
arrun compile lg:"$ARD/re" --top abc_async_reset --recipe O0 --emit-dir verilog:"$ARD/rev" --workdir "$ARD/w8"
cat "$ARD/netv/"*.v "$ARD/modelsv/"*.v > "$ARD/impl.v"
cat "$ARD/rev/"*.v > "$ARD/ref.v"
arrun lec --set formal.solver=lgyosys --impl verilog:"$ARD/impl.v" --ref verilog:"$ARD/ref.v" \
  --top abc_async_reset --workdir "$ARD/wc"
arrun lec --impl lg:"$ARD/net" --ref lg:"$ARD/re" --lib lg:"$ARD/models" --top abc_async_reset \
  --set formal.solver=cvc5 --workdir "$ARD/wlec"
has "$ARD/netv" "or posedge rst" || fail "abc_async_reset: asynchronous reset edge did not survive mapping"
! grep -h "always @" "$ARD/netv/"*.v | grep -qv "or posedge rst" \
  || fail "abc_async_reset: a synchronous-reset register stayed a native flop: $(grep -h 'always @' "$ARD/netv/"*.v)"
has "$ARD/netv" "XOR2x1\|NAND2x1\|NOR2x1\|INVx1\|BUFx1" \
  || fail "abc_async_reset: surrounding data cone was not mapped"
[ "$(grep -h '^DFFx1 ' "$ARD/netv"/*.v | wc -l | tr -d ' ')" = 4 ] \
  || fail "abc_async_reset: expected 4 DFFx1 cells for sync_state, got $(grep -h '^DFFx1 ' "$ARD/netv"/*.v | wc -l)"
for b in 0 1 2 3; do
  has "$ARD/netv" "^DFFx1 sync_state_${b}(" || fail "abc_async_reset: sync_state[${b}] is not a DFFx1 named sync_state_${b}: $(grep -h '^DFFx1 ' "$ARD/netv"/*.v)"
done
! has "$ARD/netv" "DFFx1 async_state" || fail "abc_async_reset: asynchronous-reset register incorrectly mapped to plain DFFx1"
echo "PASS: asynchronous reset stays native, synchronous reset folds into D and maps to named DFF cells, LEC-equivalent (lgyosys + cvc5)"

# The same fixture under the QN-only cell: the sync-reset register composes with
# the D-side inversion on both paths. Built-in flow: the latch crosses as ~(rst ?
# rval : d^k), the mapper absorbs it, no `__dinv`. User flow: the honest AIG plus
# the read-back absorption (twin swap or at most one INVx1 per cell). The async
# register stays native either way; cvc5 proves both through Flop(Not(D)).
run_qn sreset_builtin abc_async_reset abc_async_reset
[ "$(count "$QNV" "^DFFNx1 sync_state_[0-3](")" = 4 ] \
  || fail "qn sync reset (builtin): expected 4 DFFNx1 cells named sync_state_<bit>, got $(grep -h '^DFFNx1 ' "$QNV"/*.v)"
has "$QNV" "or posedge rst" || fail "qn sync reset (builtin): asynchronous-reset register did not stay native"
! has "$QNV" "__dinv" || fail "qn sync reset (builtin): built-in flow minted a read-back inverter"
! has "$QNV" "DFFNx1 async_state" || fail "qn sync reset (builtin): asynchronous-reset register mapped to a cell"
run_qn sreset_user abc_async_reset abc_async_reset --set 'pass.abc.flow=strash; dc2; map'
[ "$(count "$QNV" "^DFFNx1 sync_state_[0-3](")" = 4 ] \
  || fail "qn sync reset (user): expected 4 DFFNx1 cells named sync_state_<bit>, got $(grep -h '^DFFNx1 ' "$QNV"/*.v)"
has "$QNV" "or posedge rst" || fail "qn sync reset (user): asynchronous-reset register did not stay native"
[ "$(count "$QNV" "^INVx1 sync_state_[0-3]__dinv(")" -le 4 ] \
  || fail "qn sync reset (user): more read-back inverters than cells"
echo "PASS: synchronous reset composes with the QN cell's D-side inversion (built-in fold and read-back absorption), LEC proven"

# register=false: flops kept native (`always @(posedge)`), never a DFF cell.
run_abc_lec abc_seq abc_seq.abc_seq false false
has "$NETV" "posedge" || fail "abc_seq register=false: no native flop survived (flops lost?)"
! has "$NETV" "DFFx1 " || fail "abc_seq register=false: unexpected DFF cell (flop should stay native)"
echo "PASS: register=false keeps flops native (abc_seq)"

# An oversized sequential region takes the same native boundary path without
# disabling register mapping for the rest of the design. A one-bit limit is
# deliberately below abc_seq's state payload.
run_abc_lec abc_seq abc_seq.abc_seq true false 1
has "$NETV" "posedge" || fail "abc_seq register_max_bits: oversized register payload was not kept native"
! has "$NETV" "DFFx1 " || fail "abc_seq register_max_bits: oversized register payload still entered ABC"
echo "PASS: register_max_bits keeps only oversized state regions native (abc_seq)"

# memory=false (must be EXPLICIT now that true is the default): the memory stays
# a native boundary instance (not bit-blasted).
run_abc_lec abc_mem abc_mem.abc_mem true false
has "$NETV" "cgen_memory" || fail "abc_mem memory=false: memory not preserved as a native instance"
echo "PASS: memory=false keeps the memory as a native instance (abc_mem)"

# memory=true: the memory is bit-blasted into gates -- no memory instance remains.
run_abc_lec abc_mem abc_mem.abc_mem true true
! has "$NETV" "cgen_memory" || fail "abc_mem memory=true: memory was not bit-blasted"
echo "PASS: memory=true bit-blasts the memory to gates (abc_mem)"

# The DEFAULT is memory=true: with the knob unset the memory must be bit-blasted
# too (an 8x8 = 64-bit memory, far below the memory_max_bits default of 65536).
run_abc_lec abc_mem abc_mem.abc_mem true default
! has "$NETV" "cgen_memory" || fail "abc_mem default memory mode: memory was not bit-blasted (default should be memory=true)"
echo "PASS: the default memory mode bit-blasts the memory (abc_mem)"

echo "PASS: pass.abc register/memory tech-map LEC-equivalent (DFF cells, native flops, memory bit-blast + boundary)"

#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for the sequential `lhd pass <mapper>` knobs (task 2a-abc subtask 5):
# technology-map a colored SEQUENTIAL design to a standard-cell netlist and prove
# it LEC-equivalent to the original logic, exercising both register-mapping modes
# and both memory-mapping modes.
#
#   pass.abc.register=true   flops -> library DFF cells (DFFx1 in the test lib;
#                            the QN-only DFFNx1 + DFFNx2 drive ladder in test_qn.lib)
#   pass.abc.register=false  flops kept native (`always @(posedge)`)
#   pass.abc.memory=true     memory RTL lowered and mapped inside a child module
#   pass.abc.memory=false    memory kept as a native boundary instance
#   pass.abc.memory=auto     (the default) per memory: fold within memory_max_bits
#                            (1024) or over 3 ports, keep the rest native
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
# BMC/induction, via `lhd lec`) to its `partition` twin.
#
#   prp -> lg
#   pass color synth                       (the abc driver coloring)
#   pass <mapper> --set pass.<mapper>.seq=true        (partition + ABC seq tech-map)
#   pass partition                          (same module structure, original logic)
#   pass liberty gensim test.lib            (behavioral model per comb cell)
#   cgen net + models -> impl.v ; cgen re -> ref.v
#   lhd lec (impl vs ref): must be sequentially LEC-equivalent
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
#
# MAPPER=usyn (native USYN, pass/usyn/README.md) has none of the register/memory
# mapping knobs: it always retains the original registers, which its default
# `tmap=abc` maps to library DFF cells through the same read-back, and it keeps
# every memory as a native barrier. So the USYN leg keeps each semantic claim
# (LEC against the original logic, register count/names/reset, no fabricated
# init, native async reset, QN cell and drive ladder, native memory plus the
# memory negative control) and replaces the ABC-only modes by their analogues:
#   register=false            -> tmap=none: logical CMOS, native flops, no cells
#   memory=false              -> the default: the memory stays native
#   memory=true|auto, memory_max_bits, register_max_bits, user `flow`, and the
#   abc.json `dff`/`register_max_bits` report fields -> ABC-only (gated)
# and checks the schema-5 decision report's preserved register count instead.

set -u

# One script, both technology mappers: MAPPER=abc (default) runs `lhd pass abc`
# and MAPPER=usyn runs `lhd pass usyn`; lhd/tests/BUILD generates the `_usyn`
# twin from this same file. ABC-internal modes are gated on `$MAPPER = abc`.
MAPPER="${MAPPER:-abc}"
case "$MAPPER" in
  abc | usyn) ;;
  *)
    echo "FAIL: bad MAPPER=$MAPPER (expected abc|usyn)" >&2
    exit 1
    ;;
esac

LHD="${LHD:-lhd/lhd}"
LIB=inou/prp/tests/abc/test.lib
W="${TEST_TMPDIR:-/tmp/lhd_abc_seq_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

[ -f "$LIB" ] || fail "missing liberty $LIB"

# Keep independently useful register, QN-cell, and memory regressions below a
# minute in debug builds. A direct invocation still runs all three groups.
group="${1:-all}"
case "$group" in all|registers|qn|memory) ;; *) fail "unknown test group: $group" ;; esac
selected() { [ "$group" = all ] || [ "$group" = "$1" ]; }

# Library models and the original partition do not depend on the ABC mode.
# Reuse them within this test; each mapping still gets a private graph copy and
# its own workdir, and every distinct mapped result is checked by LEC below.
prepare_models() {
  local lib="$1" tag="$2"
  if [ ! -d "$W/models_$tag" ]; then
    "$LHD" pass liberty gensim "$lib" --emit-dir lg:"$W/models_$tag" \
      --emit-dir verilog:"$W/modelsv_$tag" --workdir "$W/models_work_$tag" -q \
      || fail "gensim $lib"
  fi
}

prepare_fixture() {
  local source="$1" top="$2" tag="$3" alg="$4"
  local base="$W/base_$tag"
  local algset=()
  [ "$alg" = cones ] || algset=(--set "color.synth.mode=$alg")
  if [ ! -d "$base/re" ]; then
    "$LHD" compile "$source" --top "$top" --emit-dir lg:"$base/lg" --workdir "$base/compile" -q \
      || fail "compile $source"
    "$LHD" pass color synth --top "$top" ${algset[@]+"${algset[@]}"} lg:"$base/lg" --workdir "$base/color" -q \
      || fail "color $source"
    "$LHD" pass partition --top "$top" lg:"$base/lg" --emit-dir lg:"$base/re" \
      --emit-dir verilog:"$base/rev" --workdir "$base/partition" -q || fail "partition $source"
  fi
}

# usyn_report <qor> <tmap> <register_bits>: the USYN leg's report analogue of
# abc.json. `<qor>.usyn.json` is the schema-5 native decision report and must
# count every preserved register bit; `<qor>` itself is the separate
# technology-map report under tmap=abc, or the logical report under tmap=none.
usyn_report() {
  local qor="$1" tmap="$2" bits="$3"
  [ -f "$qor.usyn.json" ] || fail "usyn report: missing $qor.usyn.json"
  grep -q '^{"schema_version":5,"kind":"usyn",' "$qor.usyn.json" \
    || fail "usyn report: $qor.usyn.json is not a schema-5 USYN report: $(head -c 200 "$qor.usyn.json")"
  grep -q "\"tmap\":\"$tmap\"" "$qor.usyn.json" || fail "usyn report: $qor.usyn.json does not record tmap=$tmap"
  local totals
  totals="$(grep -o '"totals":{[^}]*}' "$qor.usyn.json")"
  printf '%s' "$totals" | grep -q "\"register_bits\":$bits[,}]" \
    || fail "usyn report: expected $bits preserved register bits, got $totals"
  if [ "$tmap" = abc ]; then
    grep -q '"kind":"technology-map"' "$qor" || fail "usyn report: $qor is not the technology-map report"
  else
    grep -q '"kind":"usyn"' "$qor" && grep -q '"output":"logical-cmos"' "$qor" \
      || fail "usyn report: tmap=none $qor is not the logical-CMOS USYN report"
  fi
}

# run_abc_lec <fix> <top> <register> <memory> [register_max_bits] [extra pass.abc --set ...]:
# tech-map with the given knobs, build the original-logic twin + gensim models,
# and prove the netlist equivalent.
# <memory> = true|false|auto is passed explicitly; `default` leaves pass.abc.memory
# unset so the run exercises whatever the pass defaults to.
# MAPPER=usyn has no register/memory knobs: <register>=false selects tmap=none
# (logical CMOS keeps native flops, the register=false analogue), <memory> must be
# false|default (USYN always keeps a memory native), and register_max_bits must be
# 0. Its report lands at $QOR for usyn_report.
# Leaves the netlist verilog dir in the global NETV and the pass.abc diagnostics
# stream in ABCDIAG for the caller's structural asserts.
NETV=""
ABCDIAG=""
QOR=""
run_abc_lec() {
  local fix="$1" top="$2" reg="$3" mem="$4" reg_max="${5:-0}"
  shift $(( $# > 5 ? 5 : $# ))
  local prp="inou/prp/tests/pyrope/${fix}.prp"
  # The extra --set values are part of the run identity: two runs that differ
  # only in them must not share a workdir.
  local extra_tag=""
  [ $# -eq 0 ] || extra_tag="_$(printf '%s' "$*" | tr -c 'A-Za-z0-9' '_')"
  local d="$W/${fix}_r${reg}_m${mem}_x${reg_max}${extra_tag}"
  mkdir -p "$d"
  local r="$d/r.json"
  run() { "$LHD" "$@" -q --result-json "$r" || fail "$* -> $(cat "$r" 2>/dev/null)"; }
  local regset=() regmaxset=()
  local memset=()
  local tmap=abc
  if [ "$MAPPER" = abc ]; then
    [ "$reg" = true ] || regset=(--set "pass.$MAPPER.register=$reg")
    [ "$reg_max" = 0 ] || regmaxset=(--set "pass.$MAPPER.register_max_bits=$reg_max")
    [ "$mem" = "default" ] || [ "$mem" = auto ] || memset=(--set "pass.$MAPPER.memory=$mem")
  else
    case "$mem" in default | false) ;; *) fail "run_abc_lec: USYN has no memory=$mem mode (ABC-only)" ;; esac
    [ "$reg_max" = 0 ] || fail "run_abc_lec: USYN has no register_max_bits (ABC-only)"
    [ "$reg" = true ] || tmap=none
    QOR="$d/qor.json"
    regset=(--set "pass.usyn.tmap=$tmap" --set "pass.usyn.qor=$QOR")
  fi

  [ -f "$prp" ] || fail "missing fixture $prp"
  prepare_models "$LIB" plain
  prepare_fixture "$prp" "$top" "$fix" cones
  cp -R "$W/base_$fix/lg" "$d/lg"
  ABCDIAG="$d/diag.jsonl"
  run pass "$MAPPER" --top "$top" lg:"$d/lg" --emit-dir lg:"$d/net" --emit-dir verilog:"$d/netv" \
      --set synth.liberty="$LIB" \
      ${regset[@]+"${regset[@]}"} ${regmaxset[@]+"${regmaxset[@]}"} \
      ${memset[@]+"${memset[@]}"} "$@" --emit diagnostics:"$ABCDIAG" --workdir "$d/w3"

  if [ "$tmap" = abc ]; then
    # the netlist really is a standard-cell netlist (Sub instances of Liberty cells)
    grep -hq "NAND2x1\|NOR2x1\|INVx1\|XOR2x1\|BUFx1" "$d/netv/"*.v \
      || fail "$fix[reg=$reg,mem=$mem]: no standard cells in the ABC netlist"
  else
    # USYN tmap=none is logical CMOS: no Liberty cell of any kind.
    ! grep -hq "NAND2x1\|NOR2x1\|INVx1\|XOR2x1\|BUFx1\|DFFx1" "$d/netv/"*.v \
      || fail "$fix[tmap=none]: the logical-CMOS netlist instantiates Liberty cells"
  fi

  cat "$d/netv/"*.v "$W/modelsv_plain/"*.v > "$d/impl.v"
  cat "$W/base_$fix/rev/"*.v > "$d/ref.v"

  # The refutation below is an independent solver run over files that already
  # exist, so start it alongside the proof rather than after it. It also skips
  # the counterexample REPLAY build (`lhd lec` writes the witness either way,
  # but building and running the Pyrope replay is ~5.5s of host clang, and this
  # check reads only the exit code).
  # ABC checks its bit-blasted memory; USYN, which never blasts one, checks the
  # native memory its default (mapped) run keeps.
  local neg_pid="" neg_mem=true
  [ "$MAPPER" = abc ] || [ "$tmap" = none ] || neg_mem=false
  if [ "$fix" = abc_mem ] && [ "$mem" = "$neg_mem" ] && [ "$reg_max" = 0 ] && [ $# -eq 0 ]; then
    # Preserve all hierarchy and state names while corrupting one stored bit.
    # The memory/bank relation must verify the write logic, not assume it.
    sed "s/<= wdata;/<= wdata ^ 8'h01;/" "$d/ref.v" > "$d/bad_ref.v"
    cmp -s "$d/ref.v" "$d/bad_ref.v" && fail "memory negative control changed nothing"
    (
      "$LHD" lec --impl "$d/impl.v" --ref "$d/bad_ref.v" --top "$top" \
        --set formal.simfail_run=false \
        --workdir "$d/wc_bad" -q --result-json "$d/bad.json" > "$d/bad.log" 2>&1
      [ $? -eq 10 ]
    ) &
    neg_pid=$!
  fi

  # LEC: the tech-mapped netlist must equal the original logic in every mode.
  # Two modes that emit a byte-identical (impl, ref) pair pose the SAME query --
  # abc_mem folds to the identical blasted netlist under memory=true, under the
  # `auto` default and under memory=true with memory_max_bits=63 -- so prove each
  # distinct pair once and record the identity for the repeats. The key is the
  # content, so a change that ever makes two modes diverge puts the solver back
  # on both of them automatically.
  # `md5sum` is coreutils-only (absent on macOS); shasum ships with both, with
  # sha256sum as the fallback. An empty digest would key every mode to the SAME
  # file and silently skip every proof after the first, so it is a hard failure.
  local digest key
  digest="$(cat "$d/impl.v" "$d/ref.v" <(printf '%s' "$top") \
            | (shasum -a 256 2>/dev/null || sha256sum) | cut -d' ' -f1)"
  [ -n "$digest" ] || fail "no working hasher (shasum/sha256sum) to key the LEC dedup"
  key="$W/proven.$digest"
  if [ -e "$key" ]; then
    echo "LEC: $fix[reg=$reg,mem=$mem] emits the impl+ref already proven by $(cat "$key")"
    # The asserts below read the LEC verdict out of $r, so the recorded run's
    # result JSON stands in for the one this identical query would have produced.
    cp "$key.json" "$r"
  else
    run lec --impl verilog:"$d/impl.v" --ref verilog:"$d/ref.v" --top "$top" --workdir "$d/wc"
    cp "$r" "$key.json"
    printf '%s' "${d##*/}" > "$key"
  fi
  if [ -n "$neg_pid" ]; then
    grep -q '"bounded":false' "$r" || fail "mapped memory only proved to a bounded depth"
    wait "$neg_pid" || fail "mapped memory corruption was not refuted: $(cat "$d/bad.log")"
  fi
  NETV="$d/netv"
}

has() { grep -hq "$2" "$1/"*.v; }

if selected registers; then
  # abc_seq/hier_seq declare concrete initial values, which request Pyrope's
  # implicit SYNCHRONOUS reset (`reset_pin` + `initial`, no `async`). That reset
  # is a D-cone mux, so register=true folds it into the latch and maps every
  # register to plain DFFx1 cells named `<reg>[<bit>]` (core/bus_name.hpp,
  # emitted as the escaped identifier `\<reg>[<bit>] `); the `initial` is the reset
  # value realized on D, never a power-on value, so no native `always` survives.
  # The default LEC LEC (reset pinned, then free) proves the fold; abc_async_reset
  # below adds the graph-native cvc5 proof, which seeds power-on state.
  run_abc_lec abc_seq abc_seq.abc_seq true false
  ! has "$NETV" "posedge" || fail "abc_seq register=true: a synchronous-reset register stayed a native flop"
  [ "$(grep -h '^DFFx1 ' "$NETV"/*.v | wc -l | tr -d ' ')" = 8 ] \
    || fail "abc_seq register=true: expected 8 DFFx1 cells (p, q x 4 bits), got $(grep -h '^DFFx1 ' "$NETV"/*.v | wc -l)"
  for r in p q; do
    for b in 0 1 2 3; do
      has "$NETV" "^DFFx1 \\\\${r}\\[${b}\\] (" || fail "abc_seq register=true: DFF cell for '${r}[${b}]' is not named \\${r}[${b}]: $(grep -h '^DFFx1 ' "$NETV"/*.v)"
    done
  done
  [ "$MAPPER" = abc ] || usyn_report "$QOR" abc 8
  echo "PASS: register=true folds the synchronous reset into D and maps the registers to named DFF cells (abc_seq)"

  # hier_seq: six 8-bit registers (delayer.r x4, stage_unit.r x2) -> 48 DFFx1
  # cells, each under its register's (hierarchical, `\a.d1.r[<bit>] `) name.
  run_abc_lec hier_seq hier_seq.top true false
  ! has "$NETV" "posedge" || fail "hier_seq register=true: a synchronous-reset register stayed a native flop"
  [ "$(grep -h '^DFFx1 ' "$NETV"/*.v | wc -l | tr -d ' ')" = 48 ] \
    || fail "hier_seq register=true: expected 48 DFFx1 cells (6 registers x 8 bits), got $(grep -h '^DFFx1 ' "$NETV"/*.v | wc -l)"
  grep -hqE "^DFFx1 \\\\([a-z0-9.]+\.)?r\\[[0-7]\\] \(" "$NETV"/*.v \
    || fail "hier_seq register=true: registers did not map to DFFx1 cells under their name: $(grep -h '^DFFx1 ' "$NETV"/*.v | head -8)"
  [ "$MAPPER" = abc ] || usyn_report "$QOR" abc 48
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
  rsrun compile lhd/tests/abc_resetless_sync.prp --top abc_resetless_sync \
    --emit-dir lg:"$RSD/lg" --workdir "$RSD/w1"
  rsrun pass color synth --top abc_resetless_sync lg:"$RSD/lg" --workdir "$RSD/w2"
  rsrun pass partition --top abc_resetless_sync lg:"$RSD/lg" --emit-dir lg:"$RSD/re" --workdir "$RSD/w3"
  rsqor=()
  [ "$MAPPER" = abc ] || rsqor=(--set pass.usyn.qor="$RSD/qor.json")
  rsrun pass "$MAPPER" --top abc_resetless_sync lg:"$RSD/lg" --emit-dir lg:"$RSD/net" --set synth.liberty="$LIB" \
    ${rsqor[@]+"${rsqor[@]}"} --workdir "$RSD/w4"
  prepare_models "$LIB" plain
  rsrun lec --impl lg:"$RSD/net" --ref lg:"$RSD/re" --lib lg:"$W/models_plain" --top abc_resetless_sync \
    --workdir "$RSD/wlec"
  rsrun compile lg:"$RSD/net" --top abc_resetless_sync --emit-dir verilog:"$RSD/netv" --workdir "$RSD/w6"
  has "$RSD/netv" "DFFx1 " || fail "abc_resetless_sync: init-less flop was not mapped to DFF cells"
  ! has "$RSD/netv" "posedge" || fail "abc_resetless_sync: fake ABC init kept the flop native"
  [ "$MAPPER" = abc ] || usyn_report "$RSD/qor.json" abc 4
  echo "PASS: ABC's internal don't-care init does not become a netlist power-on value"

fi

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
count() { grep -h "$2" "$1/"*.v | wc -l | tr -d '[:space:]'; }
# run_qn <tag> <fixture> <top> [extra --set ...]: map <fixture> under test_qn.lib,
# prove it with cvc5 through the gensim models, leave netv in QNV / models in QNM.
# The DFF pick report (abc.json `dff`, `register_max_bits`) is pass.abc's; USYN's
# technology-map report has no such fields, so its leg checks the schema-5
# decision report's preserved register bits instead (qn_register_bits).
qn_register_bits() {
  case "$1" in
    abc_resetless_sync) echo 4 ;;
    abc_qn_twin | abc_qn_fanout) echo 1 ;;
    abc_async_reset) echo 8 ;;
    *) fail "qn_register_bits: unknown fixture $1" ;;
  esac
}
QNV=""
QNM=""
run_qn() {
  local tag="$1" fix="$2" top="$3"
  shift 3
  local d="$W/qn_$tag"
  mkdir -p "$d"
  local r="$d/r.json"
  qrun() { "$LHD" "$@" -q --result-json "$r" || fail "$* -> $(cat "$r" 2>/dev/null)"; }
  # mode=synth, not the shipped `cones` default: the DFF drive ladder sizes
  # a register from the fanout of its Q net INSIDE the region ABC mapped, so a
  # coloring that cuts between the register and its loads (cones puts the flop's
  # own cone in one region and the 20 XORs it feeds in another) always sees
  # fanout 1 and always picks the base rung. Cross-boundary drive is the
  # partition-boundary environment's job (pass/abc/abc_boundary.cpp), and it
  # does not re-pick a ladder rung today, so this test maps each fixture as ONE
  # region -- which is what the ladder decision is about.
  prepare_models "$QLIB" qn
  prepare_fixture "lhd/tests/${fix}.prp" "$top" "qn_$fix" synth
  cp -R "$W/base_qn_$fix/lg" "$d/lg"
  local qorset=(--set abc.qor="$d/abc.json")
  [ "$MAPPER" = abc ] || qorset=(--set pass.usyn.qor="$d/abc.json")
  qrun pass "$MAPPER" --top "$top" lg:"$d/lg" --emit-dir lg:"$d/net" --emit-dir verilog:"$d/netv" \
    --set synth.liberty="$QLIB" "${qorset[@]}" "$@" --workdir "$d/w4"
  qrun lec --impl lg:"$d/net" --ref lg:"$W/base_qn_$fix/re" --lib lg:"$W/models_qn" --top "$top" \
    --workdir "$d/wlec"
  QNV="$d/netv"
  QNM="$W/modelsv_qn"
  if [ "$MAPPER" != abc ]; then
    usyn_report "$d/abc.json" abc "$(qn_register_bits "$fix")"
    return 0
  fi
  grep -q '"dff":{"cell":"DFFNx1","q_inverted":true,"ladder":\["DFFNx1","DFFNx2"\]' "$d/abc.json" \
    || fail "qn_$tag: abc.json does not report the QN cell pick: $(grep -o '"dff":{[^}]*}[^}]*}' "$d/abc.json")"
  # register_max_bits defaults to 0 (disabled): every flop maps, as yosys does.
  # The old 4096-bit guard was tripped by one bit-blasted 64x64 memory.
  grep -q '"register_max_bits":0,' "$d/abc.json" \
    || fail "qn_$tag: abc.json does not report register_max_bits=0 as the default: $(grep -o '"register_max_bits":[0-9]*' "$d/abc.json")"
}

if selected qn; then
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
  # USYN has no user ABC flow (its optional tmap is mapping only), so this is
  # pass.abc's alone.
  if [ "$MAPPER" = abc ]; then
    run_qn user abc_resetless_sync abc_resetless_sync --set "pass.$MAPPER.flow=strash; dc2; map"
    has "$QNV" "DFFNx1 " || fail "qn user flow: DFFNx1 not mapped"
    has "$QNV" "\.QN(" || fail "qn user flow: DFFNx1's QN pin is not wired"
    [ "$(count "$QNV" "^INVx1 [a-z_0-9]*__dinv(")" -le 4 ] \
      || fail "qn user flow: more read-back inverters than DFFNx1 cells: $(count "$QNV" "__dinv(")"
    echo "PASS: QN-only DFF cell under a user flow absorbs the inversion on read-back, LEC proven"
  fi
  run_qn timing abc_resetless_sync abc_resetless_sync --set pass.$MAPPER.delay=1000
  has "$QNV" "DFFNx1 " || fail "qn timing flow: DFFNx1 not mapped"
  echo "PASS: timing flow preserves QN register semantics, LEC proven"

  # Twin swap: a NAND2 next state. Built-in flow: ABC maps ~f = AND2 itself;
  # user flow: the read-back swaps the mapped NAND2 root for AND2x1 (3.5 < 3 + 1).
  # Both netlists: one AND2x1 into the DFFNx1, no NAND2x1, no inverter at all.
  # USYN has no user flow, so only its built-in (default tmap) run applies.
  qflows=(builtin)
  [ "$MAPPER" != abc ] || qflows+=(user)
  for qflow in "${qflows[@]}"; do
    extra=()
    [ "$qflow" = user ] && extra=(--set "pass.$MAPPER.flow=strash; dc2; map")
    run_qn "twin_$qflow" abc_qn_twin abc_qn_twin ${extra[@]+"${extra[@]}"}
    [ "$(count "$QNV" "^DFFNx1 ")" = 1 ] || fail "qn twin ($qflow): expected one DFFNx1"
    # Which cell absorbs the inversion is a pass.abc mapping fact; USYN hands its
    # own selected network to tmap, so its cell mix is QoR, not contract (the
    # LEC above still proves it).
    [ "$MAPPER" = abc ] || continue
    [ "$(count "$QNV" "^AND2x1 ")" = 1 ] || fail "qn twin ($qflow): expected one AND2x1, got $(count "$QNV" "^AND2x1 ")"
    ! has "$QNV" "NAND2x1 " || fail "qn twin ($qflow): NAND2 root survived next to a QN cell"
    ! has "$QNV" "INVx1 " || fail "qn twin ($qflow): an inverter was minted where the AND2x1 twin absorbs the inversion"
  done
  if [ "$MAPPER" = abc ]; then
    echo "PASS: QN inversion absorbed into the D-cone root (mapper under the built-in flow, twin swap under a user flow)"
  else
    echo "PASS: QN twin maps to one DFFNx1 and is LEC proven (usyn: the D-cone cell mix is QoR, not asserted)"
  fi

  # Drive ladder: the fanout-20 register takes DFFNx2; its port-fed D is the one
  # place the D-side inversion cannot be absorbed, so exactly one INVx1 remains.
  run_qn fanout abc_qn_fanout abc_qn_fanout
  has "$QNV" "DFFNx2 " || fail "qn fanout: fanout-20 register did not move to the DFFNx2 rung"
  ! has "$QNV" "DFFNx1 " || fail "qn fanout: the fanout-20 register stayed on DFFNx1"
  [ "$MAPPER" != abc ] || [ "$(count "$QNV" "^INVx1 ")" = 1 ] || fail "qn fanout: expected exactly one INVx1 (the port-fed D), got $(count "$QNV" "^INVx1 ")"
  echo "PASS: DFF drive ladder picks the stronger rung for a high-fanout Q net"

fi

# The test Liberty's plain DFFx1 cannot implement an ASYNCHRONOUS reset: it is
# an event, and folding it into D would make it land only on a clock edge
# (lgcheck toggles reset independently of clk, so that miscompile is caught
# directly). It stays native, `always @(posedge clk or posedge rst)`, with its
# XOR data cone still mapped. The SYNCHRONOUS register is a D-cone mux and maps
# to DFFx1 cells `sync_state[<bit>]` with its 0ub1111 reset value realized on D;
# the default LEC LEC (reset pinned, then free) and the graph-native cvc5 LEC (which
# seeds power-on state from the source's `initial` and encodes its `reset_pin`
# as ITE(rst, initial, ...)) both prove the mixed netlist.
if selected registers; then
  ARD="$W/abc_async_reset"
  mkdir -p "$ARD"
  ARR="$ARD/r.json"
  arrun() { "$LHD" "$@" -q --result-json "$ARR" || fail "$* -> $(cat "$ARR" 2>/dev/null)"; }
  arrun compile lhd/tests/abc_async_reset.prp --top abc_async_reset \
    --emit-dir lg:"$ARD/lg" --workdir "$ARD/w1"
  arrun pass color synth --top abc_async_reset lg:"$ARD/lg" --workdir "$ARD/w2"
  arrun pass partition --top abc_async_reset lg:"$ARD/lg" --emit-dir lg:"$ARD/re" --workdir "$ARD/w3"
  arqor=()
  [ "$MAPPER" = abc ] || arqor=(--set pass.usyn.qor="$ARD/qor.json")
  arrun pass "$MAPPER" --top abc_async_reset lg:"$ARD/lg" --emit-dir lg:"$ARD/net" --set synth.liberty="$LIB" \
    ${arqor[@]+"${arqor[@]}"} --workdir "$ARD/w4"
  prepare_models "$LIB" plain
  arrun compile lg:"$ARD/net" --top abc_async_reset --emit-dir verilog:"$ARD/netv" --workdir "$ARD/w6"
  arrun compile lg:"$ARD/re" --top abc_async_reset --emit-dir verilog:"$ARD/rev" --workdir "$ARD/w8"
  cat "$ARD/netv/"*.v "$W/modelsv_plain/"*.v > "$ARD/impl.v"
  cat "$ARD/rev/"*.v > "$ARD/ref.v"
  arrun lec --impl verilog:"$ARD/impl.v" --ref verilog:"$ARD/ref.v" \
    --top abc_async_reset --workdir "$ARD/wc"
  arrun lec --impl lg:"$ARD/net" --ref lg:"$ARD/re" --lib lg:"$W/models_plain" --top abc_async_reset \
    --workdir "$ARD/wlec"
  has "$ARD/netv" "or posedge rst" || fail "abc_async_reset: asynchronous reset edge did not survive mapping"
  ! grep -h "always @" "$ARD/netv/"*.v | grep -qv "or posedge rst" \
    || fail "abc_async_reset: a synchronous-reset register stayed a native flop: $(grep -h 'always @' "$ARD/netv/"*.v)"
  has "$ARD/netv" "XOR2x1\|NAND2x1\|NOR2x1\|INVx1\|BUFx1" \
    || fail "abc_async_reset: surrounding data cone was not mapped"
  [ "$(grep -h '^DFFx1 ' "$ARD/netv"/*.v | wc -l | tr -d ' ')" = 4 ] \
    || fail "abc_async_reset: expected 4 DFFx1 cells for sync_state, got $(grep -h '^DFFx1 ' "$ARD/netv"/*.v | wc -l)"
  for b in 0 1 2 3; do
    has "$ARD/netv" "^DFFx1 \\\\sync_state\\[${b}\\] (" || fail "abc_async_reset: sync_state[${b}] is not a DFFx1 named \\sync_state[${b}]: $(grep -h '^DFFx1 ' "$ARD/netv"/*.v)"
  done
  ! has "$ARD/netv" "DFFx1 async_state" || fail "abc_async_reset: asynchronous-reset register incorrectly mapped to plain DFFx1"
  # Both registers survive: 4 native async bits + 4 mapped sync bits.
  [ "$MAPPER" = abc ] || usyn_report "$ARD/qor.json" abc 8
  echo "PASS: asynchronous reset stays native, synchronous reset folds into D and maps to named DFF cells, LEC-equivalent (default LEC on Verilog and graphs)"

fi

# The same fixture under the QN-only cell: the sync-reset register composes with
# the D-side inversion on both paths. Built-in flow: the latch crosses as ~(rst ?
# rval : d^k), the mapper absorbs it, no `__dinv`. User flow: the honest AIG plus
# the read-back absorption (twin swap or at most one INVx1 per cell). The async
# register stays native either way; cvc5 proves both through Flop(Not(D)).
if selected qn; then
  run_qn sreset_builtin abc_async_reset abc_async_reset
  [ "$(count "$QNV" "^DFFNx1 \\\\sync_state\\[[0-3]\\] (")" = 4 ] \
    || fail "qn sync reset (builtin): expected 4 DFFNx1 cells named sync_state[<bit>], got $(grep -h '^DFFNx1 ' "$QNV"/*.v)"
  has "$QNV" "or posedge rst" || fail "qn sync reset (builtin): asynchronous-reset register did not stay native"
  ! has "$QNV" "__dinv" || fail "qn sync reset (builtin): built-in flow minted a read-back inverter"
  ! has "$QNV" "DFFNx1 async_state" || fail "qn sync reset (builtin): asynchronous-reset register mapped to a cell"
  # The user-flow read-back absorption is pass.abc's alone (USYN has no flow).
  if [ "$MAPPER" = abc ]; then
    run_qn sreset_user abc_async_reset abc_async_reset --set "pass.$MAPPER.flow=strash; dc2; map"
    [ "$(count "$QNV" "^DFFNx1 \\\\sync_state\\[[0-3]\\] (")" = 4 ] \
      || fail "qn sync reset (user): expected 4 DFFNx1 cells named sync_state[<bit>], got $(grep -h '^DFFNx1 ' "$QNV"/*.v)"
    has "$QNV" "or posedge rst" || fail "qn sync reset (user): asynchronous-reset register did not stay native"
    [ "$(count "$QNV" "^INVx1 \\\\sync_state\\[[0-3]\\]__dinv (")" -le 4 ] \
      || fail "qn sync reset (user): more read-back inverters than cells"
  fi
  if [ "$MAPPER" = abc ]; then
    echo "PASS: synchronous reset composes with the QN cell's D-side inversion (built-in fold and read-back absorption), LEC proven"
  else
    echo "PASS: synchronous reset composes with the QN cell's D-side inversion (native logical translation), LEC proven"
  fi

fi

if selected registers; then
  # register=false: flops kept native (`always @(posedge)`), never a DFF cell.
  # USYN analogue: tmap=none, whose logical CMOS keeps the original registers.
  run_abc_lec abc_seq abc_seq.abc_seq false false
  has "$NETV" "posedge" || fail "abc_seq register=false: no native flop survived (flops lost?)"
  ! has "$NETV" "DFFx1 " || fail "abc_seq register=false: unexpected DFF cell (flop should stay native)"
  ! has "$NETV" "set_mask_" || fail "abc_seq register=false: native flop D was rebuilt as a quadratic Set_mask chain"
  [ "$MAPPER" = abc ] || usyn_report "$QOR" none 8
  echo "PASS: register=false (usyn: tmap=none) keeps flops native (abc_seq)"

  # An oversized sequential region takes the same native boundary path without
  # disabling register mapping for the rest of the design. A one-bit limit is
  # deliberately below abc_seq's state payload. pass.abc only: USYN has no
  # register_max_bits (its admission refuses instead, see lhd_abc_memory_test).
  if [ "$MAPPER" = abc ]; then
    run_abc_lec abc_seq abc_seq.abc_seq true false 1
    has "$NETV" "posedge" || fail "abc_seq register_max_bits: oversized register payload was not kept native"
    ! has "$NETV" "DFFx1 " || fail "abc_seq register_max_bits: oversized register payload still entered ABC"
    echo "PASS: register_max_bits keeps only oversized state regions native (abc_seq)"
  fi

fi

if selected memory; then
  # abc_mem's `reg mem:[8]U8 = 0` carries a reset value, i.e. a one-cycle
  # whole-array reset. The default LEC engine compares both graph and Verilog
  # representations against the source memory.

  if [ "$MAPPER" != abc ]; then
    # USYN keeps every memory as a native barrier (it has no memory knob): the
    # default run is the memory=false analogue, and run_abc_lec pairs it with
    # the corrupted-write negative control. The Memory node stays inside its
    # region module (no boundary wrapper), which cgen prints behaviorally -- an
    # address-indexed store and load -- where a bit-blasted realization has
    # neither. The memory is no register either: 0 preserved register bits.
    usyn_native_mem() {
      has "$NETV" "\[waddr\] <= wdata;" && has "$NETV" "\[raddr\];" \
        || fail "abc_mem ($1): memory not preserved as a native address-indexed array"
      ! has "$NETV" "_blasted" || fail "abc_mem ($1): memory was bit-blasted"
      usyn_report "$QOR" "$1" 0
    }
    run_abc_lec abc_mem abc_mem.abc_mem true false
    usyn_native_mem abc
    echo "PASS: USYN keeps the memory native under tmap=abc; a corrupted write is refuted (abc_mem)"
    run_abc_lec abc_mem abc_mem.abc_mem false false
    usyn_native_mem none
    echo "PASS: USYN keeps the memory native in logical CMOS (tmap=none) (abc_mem)"

    # ABC's memory-mapping knob does not exist for USYN: asking for a blasted
    # memory is a usage error, never a silent native keep.
    UMD="$W/usyn_memory_flag"
    mkdir -p "$UMD"
    cp -R "$W/base_abc_mem/lg" "$UMD/lg"
    if "$LHD" pass usyn --top abc_mem.abc_mem lg:"$UMD/lg" --emit-dir lg:"$UMD/net" --set synth.liberty="$LIB" \
        --set pass.usyn.memory=true --workdir "$UMD/w" -q --result-json "$UMD/r.json" 2>/dev/null; then
      fail "pass.usyn accepted the ABC-only memory=true knob"
    fi
    grep -q '"class":"usage"' "$UMD/r.json" || fail "pass.usyn memory=true is not a usage error: $(cat "$UMD/r.json")"
    echo "PASS: USYN memory stays a native barrier and remains equivalent"
  else
    # memory=false: the memory stays
    # a native boundary instance (not bit-blasted).
    run_abc_lec abc_mem abc_mem.abc_mem true false
    has "$NETV" "cgen_memory" || fail "abc_mem memory=false: memory not preserved as a native instance"
    echo "PASS: memory=false keeps the memory as a native instance (abc_mem)"

    # memory=true: the instance remains, with mapped gates inside its module.
    run_abc_lec abc_mem abc_mem.abc_mem true true
    has "$NETV" "cgen_memory_.*_blasted" || fail "abc_mem memory=true: lowered memory module missing"
    ! has "$NETV" '`include.*cgen_memory' || fail "abc_mem memory=true: native memory survived"
    echo "PASS: memory=true bit-blasts the memory to gates (abc_mem)"

    # The default (`auto`) folds this 8 x 8 = 64-bit memory: it is well within
    # memory_max_bits, so flops are the realization a 64-bit array would have anyway.
    run_abc_lec abc_mem abc_mem.abc_mem true default
    has "$NETV" "cgen_memory_.*_blasted" || fail "abc_mem default (auto): a 64-bit memory was not folded"
    ! has "$NETV" '`include.*cgen_memory' || fail "abc_mem default (auto): native memory survived"
    echo "PASS: the default memory mode folds a small memory (abc_mem)"

    # ...and `auto` keeps the SAME memory native once it is over memory_max_bits,
    # with the one-line note naming it. memory=true ignores the threshold entirely.
    run_abc_lec abc_mem abc_mem.abc_mem true auto 0 --set pass.$MAPPER.memory_max_bits=63
    # Native = the shipped wrapper (`include cgen_memory_*.v) for a reset-less
    # memory, or the boundary module a memory with a whole-array reset is enclosed in
    # (the wrappers have no reset port). That boundary module is the macro-instance
    # form: `cgen_memory_<nr>rd_<nw>wr_<hash>` with NO `_blasted` suffix -- the suffix
    # is what marks the bit-blasted realization.
    { has "$NETV" '`include.*cgen_memory' \
      || { has "$NETV" 'cgen_memory_' && ! has "$NETV" 'cgen_memory_.*_blasted'; }; } \
      || fail "abc_mem auto/max_bits=63: memory was folded anyway"
    ! has "$NETV" "cgen_memory_.*_blasted" || fail "abc_mem auto/max_bits=63: unexpectedly lowered"
    grep -q '"code":"memory-max-bits"' "$ABCDIAG" \
      || fail "abc_mem auto/max_bits=63: no memory-max-bits note: $(cat "$ABCDIAG")"
    echo "PASS: auto keeps an over-memory_max_bits memory native with a note (abc_mem)"

    run_abc_lec abc_mem abc_mem.abc_mem true true 0 --set pass.$MAPPER.memory_max_bits=63
    has "$NETV" "cgen_memory_.*_blasted" || fail "abc_mem memory=true: memory_max_bits was consulted"
    echo "PASS: memory=true folds regardless of memory_max_bits (abc_mem)"

    echo "PASS: ABC memory bit-blast and native boundary modes remain equivalent"
  fi

fi

echo "PASS: sequential $MAPPER $group checks"

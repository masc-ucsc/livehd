#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract for `lec.cones`: the register-cone (compare-point) decomposition.
# Cutting at name-matched registers makes each next-state cone an independent
# combinational miter, which is what a bit-level engine is good at; every cone
# ABC proves is SUBTRACTED from the cvc5 obligation, so cvc5 only ever sees the
# residue. The properties that must hold:
#
#   equal netlist   -> cones discharge the cuts; verdict still PROVEN
#   broken netlist  -> the cone pass NEVER hides the bug: still REFUTED (exit 1)
#                      and the residue names the ONE register that differs
#   cones=false     -> same verdict as cones=auto (this is an optimization, and
#                      never a source of verdicts of its own)
#
# The design is the RTL-vs-tech-mapped-netlist shape the mode exists for:
# `pass abc --set abc.register=false` keeps registers as native flops under
# their original names, which is what makes the cut correspondence exist.
# Hermetic: the vendored 6-cell Liberty, never the PDK.

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/leccones}"; mkdir -p "$WORK"; fail=0
LIB=inou/prp/tests/abc/test.lib
SRC=inou/prp/tests/pyrope/abc_seq.prp
TOP=abc_seq.abc_seq
[ -f "$LIB" ] || { echo "FAIL: missing liberty $LIB"; exit 1; }
[ -f "$SRC" ] || { echo "FAIL: missing fixture $SRC"; exit 1; }

# netlist NAME SRCFILE -> lg:$WORK/NAME (native flops, so the cuts pair by name)
netlist() {
  local name=$1 src=$2
  "$LHD" compile "$src" --emit-dir lg:"$WORK/$name.lg" > "$WORK/$name.build" 2>&1 \
    && "$LHD" pass color flat --top "$TOP" lg:"$WORK/$name.lg" >> "$WORK/$name.build" 2>&1 \
    && "$LHD" pass abc --top "$TOP" --workdir "$WORK/$name.wd" lg:"$WORK/$name.lg" \
              --emit-dir lg:"$WORK/$name.net" --set abc.library="$LIB" --set abc.register=false \
              >> "$WORK/$name.build" 2>&1
}

"$LHD" pass liberty gensim "$LIB" --emit-dir lg:"$WORK/models" --workdir "$WORK/gw" > "$WORK/gensim.log" 2>&1 \
  || { echo "FAIL: gensim did not produce cell models"; cat "$WORK/gensim.log"; exit 1; }

netlist good "$SRC" || { echo "FAIL: could not build the good netlist"; cat "$WORK/good.build"; exit 1; }

# The SAME design with ONE flop's cone changed: (a|b) -> (a&b) feeding q.
sed 's/q = (c & d) \^ (a | b)/q = (c \& d) ^ (a \& b)/' "$SRC" > "$WORK/abc_seq_bad.prp"
grep -q 'q = (c & d) \^ (a & b)' "$WORK/abc_seq_bad.prp" || { echo "FAIL: fixture mutation did not apply"; exit 1; }
netlist bad "$WORK/abc_seq_bad.prp" || { echo "FAIL: could not build the broken netlist"; cat "$WORK/bad.build"; exit 1; }

# run NET CONES [ENGINE] -> OUT/RC
run_lec() {
  local eng=${3:-auto}
  "$LHD" lec --ref "$SRC" --impl lg:"$WORK/$1.net" --lib lg:"$WORK/models" --top "$TOP" \
         --set lec.cones="$2" --set lec.engine="$eng" > "$WORK/lec_$1_$2_$eng.txt" 2>&1
  RC=$?; OUT=$(cat "$WORK/lec_$1_$2_$eng.txt")
}

# --- equal: cones discharge the cuts, verdict PROVEN -------------------------
run_lec good true
if [ "$RC" = 0 ] && printf '%s' "$OUT" | grep -q "PROVEN equivalent"; then
  echo "ok: equal netlist under cones -> PROVEN"
else
  echo "FAIL: equal netlist under cones -> rc=$RC"; printf '%s\n' "$OUT" | tail -3; fail=1
fi
if printf '%s' "$OUT" | grep -qE "cones: [1-9][0-9]*/[0-9]+ PROVEN by abc"; then
  echo "ok: the cone pass actually discharged cuts ($(printf '%s' "$OUT" | grep -oE 'cones: [0-9]+/[0-9]+' | head -1))"
else
  echo "FAIL: no cone was proven -- the pass did not engage on its own target shape"; fail=1
fi

# --- cones=false must reach the SAME verdict (cones are an optimization) -----
run_lec good false
if [ "$RC" = 0 ] && printf '%s' "$OUT" | grep -q "PROVEN equivalent"; then
  echo "ok: equal netlist with cones=false -> PROVEN (same verdict)"
else
  echo "FAIL: cones=false changed the verdict on the equal netlist (rc=$RC)"; fail=1
fi

# --- broken: the cone pass must NOT hide a real bug --------------------------
run_lec bad true
if [ "$RC" != 0 ] && printf '%s' "$OUT" | grep -q "REFUTED (not equivalent)"; then
  echo "ok: broken netlist under cones -> REFUTED (exit $RC)"
else
  echo "FAIL: broken netlist under cones -> rc=$RC (a cone pass must never mask a real difference)"
  printf '%s\n' "$OUT" | tail -3; fail=1
fi
# ... and it must localize the mismatch to the ONE register whose cone changed.
# Asserted on the `ind` path, which is where the cone pass runs: under `auto` a
# BMC refutation wins the race and reports its own (reachable) witness instead.
run_lec bad true ind
if printf '%s' "$OUT" | grep -q "cone diffs {.*nxt:q"; then
  echo "ok: the residue localizes the mismatch to nxt:q"
else
  echo "FAIL: the cone residue did not name nxt:q as the differing compare point"
  printf '%s' "$OUT" | grep -oE "cone diffs \{[^}]*\}" | head -1; fail=1
fi

# --- broken with cones=false must ALSO refute (no verdict divergence) --------
run_lec bad false
if [ "$RC" != 0 ] && printf '%s' "$OUT" | grep -q "REFUTED (not equivalent)"; then
  echo "ok: broken netlist with cones=false -> REFUTED (same verdict)"
else
  echo "FAIL: cones=false changed the verdict on the broken netlist (rc=$RC)"; fail=1
fi

if [ "$fail" = 0 ]; then echo "lec_cones_test: PASSED"; else echo "lec_cones_test: FAILED"; fi
exit "$fail"

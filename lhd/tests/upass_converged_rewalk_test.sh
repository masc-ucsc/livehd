#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# REGRESSION: compiling a unit as part of a multi-file tree must produce the
# SAME graph as compiling it alone.
#
# The kernel's import_defer loop (lhd_kernel_compile.cpp) re-runs pass.upass
# over the WHOLE forest each round and restores only the BLOCKED files to their
# pristine bodies. A unit whose imports resolved in round 1 was therefore
# re-walked in every later round, and re-walking an already-elaborated body is
# not idempotent: uPass_ssa demotes its private `<v>___ssa_<N>` names into the
# `<v>__w<N>` namespace that pass.prp_writer ALSO emits into generated sources
# (`lnast_prp_writer.cpp` strip_prefix). Two distinct variables silently became
# one, constprop folded the merged chain to a constant, and DCE deleted the
# real cone — a SILENT MISCOMPILE whose victim set depended only on file walk
# order. lhdsuite //bench:minion_lec's `minion_dcache_miss_handler` refutation
# (fixme.md issue 1i) was this: 1515 nodes standalone, 1476 in-tree, and the
# `new_coh_q` counterexample came from `dcache_cmd_is_write*` folding to a
# constant. 75 of the 179 minion units carried the double-demotion fingerprint.
#
# Fixed by freezing a converged unit (Lnast::set_upass_converged) so later
# rounds skip its walk, plus a collision-safe demotion in uPass_ssa.
#
# The fixture forces the loop into a second round: `blocked.prp` is a SEED and
# `pkg.prp` is discovered on disk, so blocked walks before pkg is stamped and
# pends. `victim.prp` imports nothing, converges in round 1, and was re-walked
# (and corrupted) in round 2. Its `v__w1` is exactly the name round 2's
# demotion of `v___ssa_1` lands on; `q` must keep reading `b`, not `v`.

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/upass_converged_rewalk_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cat > "$W/pkg.prp" <<'EOF'
pub const K = 3
EOF

cat > "$W/blocked.prp" <<'EOF'
const p = import("pkg")
pub mod blocked(a:u4) -> (o:u4@[]) {
  o = a + p.K
}
EOF

# `v` needs a version that SURVIVES into the post-upass body for the demotion
# to have anything to rename: a nested-if write followed by an unconditional
# one is the shape that keeps `v___ssa_1` (a single flat if is renamed away).
cat > "$W/victim.prp" <<'EOF'
pub mod victim(a:u4, b:u4, s:u1, t:u1) -> (o:u4@[], q:u4@[]) {
  mut v__w1 = b
  mut v = a
  if s != 0 { if t != 0 { v = v + 1 } }
  v = v + 3
  o = v
  q = v__w1
}
EOF

# ── 1. victim alone: one upass round, no iterate loop ────────────────────────
"$LHD" compile "$W/victim.prp" --emit-dir lg:"$W/solo" --workdir "$W/w_solo" -q \
  --result-json "$W/r_solo.json" || fail "standalone compile failed: $(cat "$W/r_solo.json" 2>/dev/null)"

# ── 2. victim inside a tree whose OTHER file blocks on an import ─────────────
"$LHD" compile "$W/blocked.prp" "$W/victim.prp" --emit-dir lg:"$W/tree" --workdir "$W/w_tree" -q \
  --result-json "$W/r_tree.json" || fail "in-tree compile failed: $(cat "$W/r_tree.json" 2>/dev/null)"

# The test is only meaningful if the tree compile actually took a SECOND round
# (that is what used to re-walk the converged victim). If a walk-order change
# ever makes blocked.prp resolve in round 1, this fixture stops covering the
# bug — fail loudly rather than passing vacuously.
rounds=$(grep -o 'pass.upass' "$W/r_tree.json" | wc -l | tr -d ' ')
[ "$rounds" -ge 2 ] || fail "fixture no longer forces a second upass round (rounds=$rounds); it cannot see the bug: $(cat "$W/r_tree.json")"

# ── 3. the two graphs must be equivalent ─────────────────────────────────────
"$LHD" lec --impl lg:"$W/tree" --ref lg:"$W/solo" --top victim --workdir "$W/w_lec" \
  > "$W/lec.log" 2>&1 || fail "lec exited non-zero — in-tree victim differs from standalone: $(cat "$W/lec.log")"
grep -q "PROVEN equivalent" "$W/lec.log" \
  || fail "in-tree victim not PROVEN equivalent to the standalone compile: $(cat "$W/lec.log")"

# ── 4. and the IR itself must be identical, not merely equivalent ────────────
# The post-upass LNAST is what the extra round rewrote, so comparing it names
# the defect directly (and catches a corruption the LEC above could not see —
# e.g. one that only makes an output constant-X, which miters vacuously).
# Under the bug the in-tree dump had `v___ssa_1` demoted onto the source's
# `v__w1`, giving that name a second store and making `q` read `v` (== `o`).
"$LHD" compile "$W/victim.prp" --emit-dir lnast-dump:"$W/ln_solo" --workdir "$W/w_lnsolo" -q \
  --result-json "$W/r_lnsolo.json" || fail "standalone lnast dump failed: $(cat "$W/r_lnsolo.json" 2>/dev/null)"
"$LHD" compile "$W/blocked.prp" "$W/victim.prp" --emit-dir lnast-dump:"$W/ln_tree" --workdir "$W/w_lntree" -q \
  --result-json "$W/r_lntree.json" || fail "in-tree lnast dump failed: $(cat "$W/r_lntree.json" 2>/dev/null)"
diff "$W/ln_solo/victim.victim.lnast" "$W/ln_tree/victim.victim.lnast" > "$W/ln.diff" 2>&1 \
  || fail "the converged unit's post-upass LNAST changed when compiled in-tree:
$(cat "$W/ln.diff")"

echo "PASS: a converged unit is not re-walked by a later import_defer round"
exit 0

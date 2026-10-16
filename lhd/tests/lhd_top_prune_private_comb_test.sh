#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# An explicit `--top` does not lower a file-scope helper `comb` that is not
# `pub` and that nothing lowered reaches through a call. The shape that needs
# it: a comptime switch between a looped and a loop-free spelling of a design
# (`if cfg.no_loop { <loop-free, uses helper> } else { <loop> }`). With the
# loop selected, the loop-free arm's helper used to be lowered and emitted as a
# never-instantiated module, so the switch was not zero-impact (lhdtrack
# br_arb_lru `comb transpose`, br_arb_rr `or_up`/`or_down`).
#
# Checked: the switched design emits exactly the plain looped design's modules
# (identical Verilog), the helper comes back when its arm is selected, a `pub`
# unused comb, a helper reached only through another helper, and every unit of
# a compile WITHOUT `--top` (a library compile) are still emitted, and `--top`
# naming the private helper itself keeps it.
set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_top_prune_$$}/top_prune"
rm -rf "$W"
mkdir -p "$W/plain" "$W/dual_false" "$W/dual_true" "$W/lib"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# The looped spelling alone.
cat > "$W/plain/flip.prp" <<'EOF'
pub mod flip(a:U8, b:U4) -> (o:U8@[]) {
  mut t:U8 = a
  for w in 0..<4 {
    if b#[w] == 1 {
      t#[(w*2)..+2] = ~a#[(w*2)..+2]
    }
  }
  o = t
}
EOF

# Both spellings behind a comptime switch; `lane_mask` serves only the
# loop-free arm, `widen` only `lane_mask`.
dual() {
  cat > "$1/sw_cfg.prp" <<EOF
pub comptime const no_loop = $2
EOF
  cat > "$1/flip.prp" <<'EOF'
const cfg = import("sw_cfg")

comb widen(b:U1) -> (m:U2) {
  m = if b == 1 { 3 } else { 0 }
}

comb lane_mask(b:U4) -> (m:U8) {
  m = (widen(b=b#[3]) << 6) | (widen(b=b#[2]) << 4) | (widen(b=b#[1]) << 2) | widen(b=b#[0])
}

pub comb spare(x:U8) -> (y:U8) {
  y = ~x
}

pub mod flip(a:U8, b:U4) -> (o:U8@[]) {
  if cfg.no_loop {
    o = a ^ lane_mask(b=b)
  } else {
    mut t:U8 = a
    for w in 0..<4 {
      if b#[w] == 1 {
        t#[(w*2)..+2] = ~a#[(w*2)..+2]
      }
    }
    o = t
  }
}
EOF
}
dual "$W/dual_false" false
dual "$W/dual_true" true

modules() {
  ls "$1" | grep '\.v$' | tr '\n' ' '
}

compile() {  # dir, extra args...
  local d="$1"
  shift
  (cd "$d" && "$OLDPWD/$LHD" compile flip.prp "$@" --emit-dir verilog:vo --emit-dir lg:lgo --workdir W > c.log 2>&1) \
    || { cat "$d/c.log" >&2; fail "compile in $d ($*)"; }
}

compile "$W/plain" --top flip
compile "$W/dual_false" --top flip
compile "$W/dual_true" --top flip

# 1. The switched-off helpers are gone: the same modules as the plain design,
#    plus only the `pub` spare.
got=$(modules "$W/dual_false/vo")
[[ "$got" == "flip.flip.__loop0.v flip.flip.v flip.spare.v " ]] || fail "dual_false --top emitted: $got"
for m in flip.flip.v flip.flip.__loop0.v; do
  diff -q "$W/plain/vo/$m" "$W/dual_false/vo/$m" > /dev/null || fail "$m differs from the plain looped design"
done
grep -rq --include='*.v' 'lane_mask\|widen' "$W/dual_false/vo" && fail "dual_false Verilog still mentions the dead arm's helpers"
[[ -f "$W/dual_false/lgo/library.txt" ]] || fail "dual_false emitted no lg: library"
grep -q 'graph_io .*\(lane_mask\|widen\)' "$W/dual_false/lgo/library.txt" && fail "dual_false lg: library holds the dead arm's helpers"
grep -q 'graph_io .*flip\.lane_mask' "$W/dual_true/lgo/library.txt" || fail "dual_true lg: library lost lane_mask"

# 2. The other setting keeps both helpers (widen is reached only through lane_mask).
got=$(modules "$W/dual_true/vo")
for m in flip.flip.v flip.lane_mask.v flip.widen.v flip.spare.v; do
  [[ " $got" == *" $m "* ]] || fail "dual_true --top lost $m (emitted: $got)"
done

# 3. No `--top`: a library compile lowers every unit.
cp "$W/dual_false/flip.prp" "$W/dual_false/sw_cfg.prp" "$W/lib/"
compile "$W/lib"
got=$(modules "$W/lib/vo")
for m in flip.flip.v flip.lane_mask.v flip.widen.v flip.spare.v; do
  [[ " $got" == *" $m "* ]] || fail "no --top lost $m (emitted: $got)"
done

# 4. `--top` naming the private helper keeps it (and what it reaches).
rm -rf "$W/lib/vo" "$W/lib/lgo" "$W/lib/W"
compile "$W/lib" --top lane_mask
got=$(modules "$W/lib/vo")
for m in flip.lane_mask.v flip.widen.v; do
  [[ " $got" == *" $m "* ]] || fail "--top lane_mask lost $m (emitted: $got)"
done

echo "PASS: explicit --top drops only unreferenced private combs"

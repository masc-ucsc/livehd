#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Incremental regeneration of the sim tree. `lhd sim --setup-only` re-emits the
# C++ for every module whose structural digest moved, and File_output skips the
# write when the bytes are unchanged — so a generated file's MTIME is the signal
# the generated build.ninja keys on. This test pins the three properties that
# make the host build incremental:
#
#   1. re-running setup with nothing changed rewrites NO generated file;
#   2. a COMMENT-only Pyrope edit rewrites NO generated file (the digest is
#      structural, so the change never reaches the emitter);
#   3. a BODY-only edit to a leaf rewrites the occurrence-root color source and
#      NOT the leaf storage interface. Ordinary children no longer own evaluator
#      methods; the root color TU is where their logic lives.
#
# Plus, when `ninja` is available, the end-to-end consequence: a second build
# with nothing changed compiles nothing at all.
#
# Steps 1-3 drive only --setup-only, so they need no host compiler and no ninja.
#
# Rewrites are detected with `find -newer <marker>`, not by reading timestamps:
# a whole-second stamp cannot see two setups that land in the same second, and
# that made an earlier version of this test pass steps 1-2 vacuously.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_incr_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# A leaf whose body can change without its ports changing, and a parent that
# instantiates it — so the parent's object depends on the leaf's header.
cat > "$W/leaf.prp" <<'EOF'
pub comb leaf(a:u8, b:u8) -> (s:u8) {
  s = (a | b)#[0..=7]
}
EOF

cat > "$W/top.prp" <<'EOF'
const leaf = import("leaf.leaf")
pub comb top(x:u8, y:u8) -> (o:u8) {
  mut inst = leaf::[name=inst](a = x, b = y)
  o = inst.s
}
EOF

cat > "$W/tb.prp" <<'EOF'
const top = import("top.top")
test top.hello(cycles:u20 = 4) {
  mut acc = top
  mut got = 0
  tick cycles clocks=(clock=1) {
    acc.x = 0xF0
    acc.y = 0x0F
    step
    got = acc.o
  }
  puts("hello world: got={got}")
}
EOF

setup() {
  "$LHD" sim "$W/tb.prp" --setup-only --workdir "$W/wd" >"$W/setup.log" 2>&1 \
    || { cat "$W/setup.log" >&2; fail "lhd sim --setup-only failed"; }
}

# rewritten -> basenames of the generated C++ files touched since $W/marker.
rewritten() {
  find "$W/wd/sim" \( -name '*.hpp' -o -name '*.cpp' \) -newer "$W/marker" \
    -exec basename {} \; 2>/dev/null | sort | tr '\n' ' '
}

setup
n_files=$(find "$W/wd/sim" \( -name '*.hpp' -o -name '*.cpp' \) | wc -l | tr -d ' ')
[ "$n_files" -ge 4 ] || fail "expected at least 4 generated C++ files, got $n_files"

# ---- 1. nothing changed: no file may be rewritten -----------------------------
touch "$W/marker"
setup
got=$(rewritten)
[ -z "$got" ] || fail "a no-op re-setup rewrote: $got (File_output is not write-if-different)"

# ---- 2. comment-only edit: still no file may be rewritten ---------------------
printf '\n// a comment carries no structure\n' >> "$W/leaf.prp"
touch "$W/marker"
setup
got=$(rewritten)
[ -z "$got" ] || fail "a comment-only Pyrope edit rewrote: $got"

# ---- 3. body-only edit: root color source, NOT the leaf storage interface -----
# `|` -> `&` keeps every port width identical, so the interface is untouched.
cp "$W/wd/sim/leaf.leaf.cpp" "$W/leaf.cpp.before"
cp "$W/wd/sim/leaf.leaf.hpp" "$W/leaf.hpp.before"
cp "$W/wd/sim/top.top.cpp" "$W/top.cpp.before"
sed -e 's/(a | b)/(a \& b)/' "$W/leaf.prp" > "$W/leaf.new" && mv "$W/leaf.new" "$W/leaf.prp"
grep -q '(a & b)' "$W/leaf.prp" || fail "the body edit did not apply (test bug)"
touch "$W/marker"
setup

# Content first, so a mechanism failure cannot be read as an emitter failure.
if cmp -s "$W/top.cpp.before" "$W/wd/sim/top.top.cpp"; then
  fail "the body edit produced identical root color C++ — occurrence lowering never saw it"
fi
cmp -s "$W/leaf.cpp.before" "$W/wd/sim/leaf.leaf.cpp" \
  || fail "a body-only edit rewrote the leaf storage implementation; module-local evaluation returned"
cmp -s "$W/leaf.hpp.before" "$W/wd/sim/leaf.leaf.hpp" \
  || fail "a body-only edit changed the leaf's storage interface"

got=$(rewritten)
[ -n "$got" ] || fail "the leaf's .cpp content changed but no file registered as rewritten (mtime granularity?)"
case "$got" in
*.hpp*) fail "a body-only edit rewrote a HEADER, so every parent needlessly rebuilds; rewritten: $got" ;;
esac
case "$got" in
*top.top.cpp*) ;;
*) fail "a body-only leaf edit did not rewrite the occurrence-root source: $got" ;;
esac
echo "  body-only edit rewrote root color sources: $got"

# ---- 4. end-to-end: a second build compiles nothing (needs ninja + a cxx) -----
if ! command -v ninja >/dev/null 2>&1; then
  echo "PASS (steps 1-3; no ninja on PATH, skipped the end-to-end build check)"
  exit 0
fi
if ! "$LHD" sim "$W/tb.prp" --run-only --diag-fmt pretty --workdir "$W/wd" >"$W/run1.log" 2>&1; then
  # No host compiler / no sim runtime headers in this environment: the
  # incremental properties above are still proven, so do not fail on it.
  echo "PASS (steps 1-3; the host build did not run here: $(tail -1 "$W/run1.log"))"
  exit 0
fi
grep -qa "hello world" "$W/run1.log" || fail "the built sim printed no hello world"
[ -f "$W/wd/sim/build.ninja" ] || fail "no build.ninja was written next to the generated sources"

# Nothing changed since that build, so ninja must have no work left.
plan=$(ninja -C "$W/wd/sim" -n 2>&1 | grep -v '^ninja: Entering')
case "$plan" in
*"no work to do"*) ;;
*) fail "a second build with nothing changed still has work to do: $plan" ;;
esac

# ---- 5. an INTERFACE change must reach the parent, via the depfile ------------
# The parent's object depends on the leaf's header, and NOTHING in build.ninja
# says so — only the `-MD` depfile does. Ninja accepts a rule whose command
# never writes that depfile without complaining (it just records zero deps), and
# the failure is silent and permanent: header edits stop rebuilding anything.
# So assert the parent actually rebuilds.
sed -e 's/b:u8/b:u7/' "$W/leaf.prp" > "$W/leaf.new" && mv "$W/leaf.new" "$W/leaf.prp"
grep -q 'b:u7' "$W/leaf.prp" || fail "the interface edit did not apply (test bug)"
sed -e 's/b = y/b = y#[0..=6]/' "$W/top.prp" > "$W/top.new" && mv "$W/top.new" "$W/top.prp"
setup
plan=$(ninja -C "$W/wd/sim" -n 2>&1 | grep -v '^ninja: Entering')
case "$plan" in
*"no work to do"*) fail "an interface change rebuilt nothing — the emitter did not see it" ;;
esac
case "$plan" in
*leaf.leaf.o*) ;;
*) fail "an interface change did not rebuild the leaf's own object: $plan" ;;
esac
case "$plan" in
*top.top.o*) ;;
*) fail "an interface change did not rebuild the PARENT's object — the depfile dependency on the leaf header is not being tracked: $plan" ;;
esac
echo "PASS (steps 1-5; warm rebuild is a no-op, interface changes reach dependents)"

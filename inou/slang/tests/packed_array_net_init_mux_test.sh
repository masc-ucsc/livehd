#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Two halves of the packed-2-D-array eligibility gate, pinned together because
# they trade against each other.
#
#  * `wire [N-1:0][W-1:0] x = {lanes};` read at a runtime index MUST split into
#    per-element leaves so the read becomes a lane mux. Two separate gate bugs
#    used to block it: the collector never saw a NET INITIALIZER (slang binds it
#    without an AssignmentExpression), and the eligibility predicate consulted
#    `wire_syms_`, which is empty at that point in the module prologue.
#
#  * A write through a whole-array RANGE select (`x[2:1] <= v`) MUST keep the
#    array flat. Neither split representation can express it: the leaf splitter
#    is guarded on packed STRUCTS so the write lands on the bare undeclared name
#    and DISAPPEARS, and a Memory has no write port for a multi-element store.
#    The flat bus composes it with set_mask.

set -u

LHD=lhd/lhd
SRC=inou/slang/tests/sv/packed_array_net_init_mux.v
TOP=packed_array_net_init_mux
W="${TEST_TMPDIR:-/tmp/packed_array_net_init_mux_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top "$TOP" --recipe O1 \
  --emit-dir pyrope:"$W/prp" --emit verilog:"$W/out.v" --workdir "$W/work" -q 2>/dev/null \
  || fail "compile failed"

prp="$W/prp/$TOP.prp"
[ -s "$prp" ] || fail "Pyrope unit was not emitted"

# ── the net-initialized lane table splits ───────────────────────────────────
grep -q 'wire lanes:(e3:u16, e2:u16, e1:u16, e0:u16)' "$prp" \
  || fail "net-initialized packed array did not split into per-element leaves"
grep -q 'unique if sel_i == 0 { lanes.e0 }' "$prp" \
  || fail "runtime element read did not become a lane mux"
grep -q '(lanes << ' "$prp" && fail "lane table still lowers through a barrel shift"

# ── and the range-written one does NOT ──────────────────────────────────────
grep -q 'reg rng:u64' "$prp" \
  || fail "range-written array did not stay a flat bus"
# The write must SURVIVE. It used to vanish entirely (every lane assigned from
# itself) behind nothing louder than an `unresolved-ref` warning and exit 0.
grep -q 'rng#\[16..=47\] = din_i' "$prp" \
  || fail "whole-array range write was dropped"

# ── and the whole module is still the same circuit ──────────────────────────
[ -s "$W/out.v" ] || fail "Verilog was not emitted"
lec=$("$LHD" lec --impl verilog:"$W/out.v" --ref verilog:"$SRC" --top "$TOP" \
      --workdir "$W/lec" 2>&1)
grep -qa "PROVEN equivalent" <<<"$lec" || fail "packed-array lowering was not PROVEN equivalent: $lec"

echo "PASS: net-initialized lane table muxes, range-written array stays flat, LEC PROVEN"

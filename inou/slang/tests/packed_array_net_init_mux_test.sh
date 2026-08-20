#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A write through a whole-array RANGE select (`x[2:1] <= v`) MUST keep the array
# a FLAT BUS: a Memory has one write port per element store and no write port
# for a multi-element store, so the assignment is silently dropped. Only the
# flat bus composes it (set_mask). That refusal is what gates the packed-2-D
# memory classifier -- see Array_range_write_collector.
#
# The fixture also carries the firtool lane-table shape (`wire [N-1:0][W-1:0] x
# = {lanes};` read at a runtime index). It used to be asserted to SPLIT into
# per-element leaves so the read became a lane mux; packed-array SROA was
# removed in 2026-08, so that array now lowers through the packed bus and only
# the range-written half still makes a claim here.

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

# ── the range-written array stays a FLAT BUS ────────────────────────────────
# This is the surviving correctness claim of this fixture. Packed-array SROA is
# gone, so the net-initialized lane table no longer splits into `lanes.eN` and
# no longer becomes a lane mux -- it lowers through the packed bus, which is
# what the removal traded the mux for. But the range-write refusal was never
# part of SROA: a Memory has one write port per element store and nowhere to
# put `arr[hi:lo] <= v`, so an array written that way must stay flat or the
# whole assignment disappears. That refusal (Array_range_write_collector) is
# what still gates the packed-2D memory classifier.
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

echo "PASS: range-written array stays a flat bus, its write survives, LEC PROVEN"

#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regression for the Type-C whole-net false combinational loop on the
# SIM-emission path (the dominant XiangShan Backend `lhd sim` error family:
# "module X has a combinational loop the single-pass sim schedule cannot
# order", DretEventModule/EnqPolicy/BusyTable et al., and the same family as
# the cvc5 "no encodable driver" LEC inconclusives).
#
# The golden inou/prp/tests/equiv/struct_selfref_pattern.v holds a struct net
# whole-assigned by a '{...}' pattern whose later elements read EARLIER fields
# of the same net, whole-copied to the output port: acyclic per FIELD, cyclic
# at whole-net granularity. Lowered as a flat bus this is a false loop the
# single-pass sim schedule rejects; the slang reader must keep it a per-field
# bundle (is_scalar_struct_var's whole_copied_selfref_pattern relaxation), so
# `--emit-dir sim:` (inou.cgen.sim) succeeds. The v2prp equiv pair guards the
# VALUE correctness of the bundle form; this test guards the SCHEDULABILITY.

set -u
LHD=lhd/lhd
EQUIV=inou/prp/tests/equiv
W="${TEST_TMPDIR:-/tmp/lhd_sim_selfref_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# no --top: the golden's single module has an escaped dotted name — slang
# auto-tops it (same convention as the v2prp harness)
out=$("$LHD" compile "$EQUIV/struct_selfref_pattern.v" \
      --emit-dir "sim:$W/sim" 2>&1) || fail "sim emission errored (false comb loop?):
$out"
echo "$out" | grep -q '"status":"pass"' || fail "compile did not pass:
$out"
ls "$W/sim"/*.hpp >/dev/null 2>&1 || fail "no sim headers emitted"

echo "PASS: struct_selfref_pattern sim-emits without a false comb loop"

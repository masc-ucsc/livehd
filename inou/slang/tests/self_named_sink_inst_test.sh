#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# An instance named after its own module, of a module with NO OUTPUTS, must
# still emit Pyrope. `sink_obs sink_obs (...)` lowers to
# `fcall(dst=sink_obs, callee=sink_obs, …)`: both refs carry the same name, so
# prp_writer's use counter charged the CALLEE reference to the call's own
# result and then refused the module ("result 'X' of zero-output module 'X' is
# read"), taking the whole design's emission down with it. That is bedrock-rtl's
# spelling for every assertion-only observer, and it blocked the Pyrope side of
# 13 lhdtrack tests.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  LHD=lhd/lhd
fi
SRC=inou/slang/tests/sv/self_named_sink_inst.v
W="${TEST_TMPDIR:-/tmp/self_named_sink_inst_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top self_named_sink_inst \
  --emit-dir pyrope:"$W/prp" --workdir "$W/work" -q >"$W/compile.log" 2>&1 \
  || { cat "$W/compile.log"; fail "Pyrope emission of a self-named sink instance failed"; }

prp="$W/prp/self_named_sink_inst.prp"
[ -s "$prp" ] || fail "no Pyrope emitted for the top module"

grep -q 'TODO' "$prp" && { cat "$prp"; fail "the emission left an unimplemented marker"; }

# The sink must be a call STATEMENT, never `mut sink_obs = sink_obs(...)`: the
# Pyrope reader would mint a value-result temp for a call with no result pin and
# resolve it to nil.
# (the callee prints through its import alias — `sink_obs_t` — because the
# instance variable already claims the bare name)
grep -qE '^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*::\[name=sink_obs\]\(' "$prp" \
  || { cat "$prp"; fail "the zero-output instance was not emitted as a call statement"; }
grep -qE '=[[:space:]]*[A-Za-z_][A-Za-z0-9_]*::\[name=sink_obs\]' "$prp" \
  && { cat "$prp"; fail "the zero-output instance bound a result variable"; }

# And the emitted Pyrope is still the same circuit.
lec=$("$LHD" lec --impl pyrope:"$W/prp" --ref verilog:"$SRC" --top self_named_sink_inst \
      --workdir "$W/lec" 2>&1)
grep -qa "PROVEN equivalent\|PASS(" <<<"$lec" || fail "emitted Pyrope not proven equivalent: $lec"

echo "PASS: a self-named zero-output instance emits as a call statement and LECs"

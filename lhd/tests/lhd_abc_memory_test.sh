#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2opt-incr subtask 0: `lhd pass abc` memory admission.
#
# A region is bit-blasted into ABC, so a whole-design region costs millions of
# gates and several network forms at once — a flat XSCore run reached 221 GB on
# a 64 GiB host and was SIGKILLed by the OS, taking the machine down with it.
# pass.abc samples its OWN RSS while translating and refuses a region that will
# not fit BEFORE running any synthesis command.
#
# The guard is host-dependent by nature (the default budget is physical RAM
# minus a reserve), so this test pins it with pass.abc.memory_budget_mb — a 1 MiB
# budget is unsatisfiable on every host, which makes the refusal deterministic
# and CI-safe. Asserts, in order:
#   1. an unsatisfiable budget REFUSES: nonzero exit + the memory-oversize code
#   2. the refusal emits NO partial netlist (the emit-dir stays empty)
#   3. allow_oversize=true overrides it and the map succeeds
#   4. a generous budget does NOT false-positive on the same design
#   5. a malformed budget is a clean error, not a silent default
#
# Hermetic: the small vendored Liberty (inou/prp/tests/abc/test.lib), no PDK.

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
PRP=inou/prp/tests/pyrope/abc_comb.prp
TOP=abc_comb.abc_comb
W="${TEST_TMPDIR:-/tmp/lhd_abc_mem_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

run compile "$PRP" --top "$TOP" --recipe O1 --emit-dir lg:"$W/lg" --workdir "$W/w1"
run pass color synth --top "$TOP" lg:"$W/lg" --workdir "$W/w2"

# ---------------------------------------------------------------------------
# 1. an unsatisfiable budget must refuse
# ---------------------------------------------------------------------------
if "$LHD" pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_refused" \
    --set abc.library="$LIB" --set abc.memory_budget_mb=1 \
    --emit diagnostics:"$W/refused.jsonl" \
    --workdir "$W/w3" -q --result-json "$W/refused.json" 2>"$W/refused.err"; then
  fail "pass.abc accepted a 1 MiB memory budget (the guard did not fire)"
fi
# The `memory-oversize` CODE lives in the diagnostics stream; the result envelope
# carries the class + message. Assert both so a rename of either is caught.
grep -q '"code":"memory-oversize"' "$W/refused.jsonl" \
  || fail "no memory-oversize diagnostic: $(cat "$W/refused.jsonl" 2>/dev/null)"
grep -q "does not fit in memory" "$W/refused.json" \
  || fail "refusal envelope lacks the reason: $(cat "$W/refused.json" 2>/dev/null)"
# The diagnostic must report the real budget it enforced, not a placeholder.
grep -q "budget 1 MiB" "$W/refused.json" \
  || fail "refusal does not name the 1 MiB budget it was given: $(cat "$W/refused.json" 2>/dev/null)"

# ---------------------------------------------------------------------------
# 2. a refusal must emit NO partial result. A half-translated netlist silently
#    passed downstream is worse than the OOM this guard prevents.
# ---------------------------------------------------------------------------
if [ -d "$W/net_refused" ] && [ -n "$(ls -A "$W/net_refused" 2>/dev/null)" ]; then
  fail "refused run left a partial netlist in the emit-dir: $(ls "$W/net_refused")"
fi

# ---------------------------------------------------------------------------
# 3. allow_oversize must override the guard (the documented escape hatch)
# ---------------------------------------------------------------------------
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_forced" \
  --set abc.library="$LIB" --set abc.memory_budget_mb=1 --set abc.allow_oversize=true \
  --workdir "$W/w4"
[ -n "$(ls -A "$W/net_forced" 2>/dev/null)" ] || fail "allow_oversize=true produced no netlist"

# ---------------------------------------------------------------------------
# 4. a generous budget must NOT false-positive on a design that plainly fits
# ---------------------------------------------------------------------------
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_ok" \
  --set abc.library="$LIB" --set abc.memory_budget_mb=65536 --workdir "$W/w5"
[ -n "$(ls -A "$W/net_ok" 2>/dev/null)" ] || fail "a 64 GiB budget produced no netlist"

# ---------------------------------------------------------------------------
# 5. a malformed budget is an error, not a silent fallback to "unlimited"
# ---------------------------------------------------------------------------
if "$LHD" pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_bad" \
    --set abc.library="$LIB" --set abc.memory_budget_mb=lots \
    --workdir "$W/w6" -q --result-json "$W/bad.json" 2>/dev/null; then
  fail "pass.abc accepted memory_budget_mb=lots"
fi

echo "PASS: pass.abc memory admission (refuse + no partial output + allow_oversize + no false positive)"

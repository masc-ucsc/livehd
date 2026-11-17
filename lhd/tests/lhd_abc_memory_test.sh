#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2opt-incr subtask 0: `lhd pass <mapper>` memory admission.
#
# A region is bit-blasted into ABC, so a whole-design region costs millions of
# gates and several network forms at once — a flat XSCore run reached 221 GB on
# a 64 GiB host and was SIGKILLed by the OS, taking the machine down with it.
# The guard is ONE measured ceiling, lhd.ram_max_mb (auto = 80% of physical
# RAM): the sampled footprint check refuses work over it, and the RLIMIT_AS
# backstop armed at the same value turns a failed allocation into a clean
# error. lhd.ram_budget_mb is only a sizing target and never refuses.
#
# Asserts, in order (both mappers):
#   1. a ceiling below the process's own footprint REFUSES: nonzero exit, named
#      TIMEOUT-memory (the sampled refusal or the backstop's caught allocation
#      failure, whichever the host reaches first)
#   2. the refusal emits NO partial netlist (the emit-dir stays empty)
#   3. no escape hatch: the removed allow_oversize option is a usage error
#   4. a generous ceiling does NOT false-positive on the same design
#   5. a malformed ceiling is a clean usage error, not a silent default
#   6. the removed per-mapper growth budget is a usage error naming lhd.ram_max_mb
#
# Hermetic: the small vendored Liberty (inou/prp/tests/abc/test.lib), no PDK.

set -u

# One script, both technology mappers: MAPPER=abc (default) runs `lhd pass abc`
# and MAPPER=usyn runs `lhd pass usyn`; lhd/tests/BUILD generates the `_usyn`
# twin from this same file.
MAPPER="${MAPPER:-abc}"
case "$MAPPER" in
  abc | usyn) ;;
  *)
    echo "FAIL: bad MAPPER=$MAPPER (expected abc|usyn)" >&2
    exit 1
    ;;
esac

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

run compile "$PRP" --top "$TOP" --emit-dir lg:"$W/lg" --workdir "$W/w1"
run pass color synth --top "$TOP" lg:"$W/lg" --workdir "$W/w2"

# 1 + 2. lhd's own footprint is well above 64 MiB, so this ceiling cannot hold.
if "$LHD" pass "$MAPPER" --set lhd.jobs=1 --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_refused" \
    --set synth.liberty="$LIB" --set lhd.ram_max_mb=64 \
    --workdir "$W/w3" -q --result-json "$W/refused.json" 2>"$W/refused.err"; then
  fail "pass.$MAPPER ran under a 64 MiB lhd.ram_max_mb (the guard did not fire)"
fi
cat "$W/refused.json" "$W/refused.err" 2>/dev/null | grep -q "TIMEOUT-memory" \
  || fail "the refusal is not named TIMEOUT-memory: $(cat "$W/refused.json" "$W/refused.err" 2>/dev/null | head -c 2000)"
if [ -d "$W/net_refused" ] && [ -n "$(ls -A "$W/net_refused" 2>/dev/null)" ]; then
  fail "refused run left a partial netlist in the emit-dir: $(ls "$W/net_refused")"
fi

# 3. no escape hatch: the removed allow_oversize option is REJECTED.
"$LHD" pass "$MAPPER" --set lhd.jobs=1 --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_forced" \
  --set synth.liberty="$LIB" --set abc.allow_oversize=true \
  --workdir "$W/w4o" -q --result-json "$W/forced.json" 2>/dev/null
rc=$?
[ "$rc" -eq 2 ] || fail "abc.allow_oversize must be a usage error (exit 2), got rc=$rc: $(cat "$W/forced.json" 2>/dev/null)"
grep -q 'allow_oversize' "$W/forced.json" || fail "the rejection does not name allow_oversize: $(cat "$W/forced.json" 2>/dev/null)"
[ -z "$(ls -A "$W/net_forced" 2>/dev/null)" ] || fail "a rejected allow_oversize run published a netlist"

# 4. a generous ceiling must NOT false-positive on a design that plainly fits.
run pass "$MAPPER" --set lhd.jobs=1 --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_ok" \
  --set synth.liberty="$LIB" --set lhd.ram_max_mb=65536 --workdir "$W/w5"
[ -n "$(ls -A "$W/net_ok" 2>/dev/null)" ] || fail "a 64 GiB ceiling produced no netlist"

# 5. a malformed ceiling is a usage error, not a silent fallback to "unlimited".
for v in lots 0 -1; do
  "$LHD" pass "$MAPPER" --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_bad" --set synth.liberty="$LIB" \
    --set "lhd.ram_max_mb=$v" --workdir "$W/w6" -q --result-json "$W/bad.json" 2>/dev/null
  rc=$?
  [ "$rc" -eq 2 ] && grep -q '"class":"usage"' "$W/bad.json" \
    || fail "lhd.ram_max_mb=$v must be a usage error, got rc=$rc: $(cat "$W/bad.json" 2>/dev/null)"
done

# 6. the per-mapper growth budget is gone: one spelling, lhd.ram_max_mb.
"$LHD" pass "$MAPPER" --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_old" --set synth.liberty="$LIB" \
  --set "pass.$MAPPER.memory_budget_mb=1024" --workdir "$W/w7" -q --result-json "$W/old.json" 2>/dev/null
rc=$?
[ "$rc" -eq 2 ] && grep -q 'lhd.ram_max_mb' "$W/old.json" \
  || fail "pass.$MAPPER.memory_budget_mb must be refused with a hint naming lhd.ram_max_mb, got rc=$rc: $(cat "$W/old.json" 2>/dev/null)"

echo "PASS: pass.$MAPPER memory admission (lhd.ram_max_mb TIMEOUT-memory + no partial output + allow_oversize rejected + no false positive)"

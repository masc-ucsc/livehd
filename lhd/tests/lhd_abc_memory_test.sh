#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2opt-incr subtask 0: `lhd pass <mapper>` memory admission.
#
# A region is bit-blasted into ABC, so a whole-design region costs millions of
# gates and several network forms at once — a flat XSCore run reached 221 GB on
# a 64 GiB host and was SIGKILLed by the OS, taking the machine down with it.
# pass.abc samples its OWN RSS while translating and refuses a region that will
# not fit BEFORE running any synthesis command.
#
# The guard is host-dependent by nature (the default budget is physical RAM
# minus a reserve), so this test pins it with pass.abc.memory_budget_mb — a 1 MiB
# budget is unsatisfiable for this fixture with one worker. Pin lhd.jobs=1
# to test the per-color guard; parallel workers share an aggregate growth budget.
# Asserts, in order:
#   1. an unsatisfiable budget REFUSES: nonzero exit + the memory-oversize code,
#      named TIMEOUT-memory (a memory limit caught before the allocation)
#   2. the refusal emits NO partial netlist (the emit-dir stays empty)
#   3. there is no escape hatch: the removed allow_oversize option is a usage
#      error (exit 2), never silently accepted; the budget itself is the knob
#   4. a generous budget does NOT false-positive on the same design
#   5. a malformed budget is a clean error, not a silent default
#
# MAPPER=usyn runs the same sequence against native USYN's own admission,
# pass.usyn.memory_budget_mb (invocation memory growth; ABC options are not
# inherited, pass/usyn/README.md). Its analogues: 1. the refusal is the
# `synthesis-refused` code naming the exhausted memory-growth budget (as
# TIMEOUT-memory), and the recipe echoes the 1 MiB it enforced; 2. no netlist AND no report is
# published; 3. the removed abc.allow_oversize is a usage error for pass.usyn
# too, and ABC's budget is proven NOT inherited (abc.memory_budget_mb=1 does
# not trigger the USYN refusal); 4. unchanged; 5. a malformed or non-positive
# budget is a clean error.
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

# The admission knob each mapper enforces. pass.abc reads its `abc.` alias;
# USYN reads only its own namespace.
if [ "$MAPPER" = abc ]; then
  BUDGET=abc.memory_budget_mb
else
  BUDGET=pass.usyn.memory_budget_mb
fi

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

run compile "$PRP" --top "$TOP" --emit-dir lg:"$W/lg" --workdir "$W/w1"
run pass color synth --top "$TOP" lg:"$W/lg" --workdir "$W/w2"

# usyn_refuses <tag> [extra --set ...]: a 1 MiB USYN budget must refuse with the
# memory-growth reason and publish nothing (netlist or report).
usyn_refuses() {
  local tag="$1"
  shift
  if "$LHD" pass usyn --set lhd.jobs=1 --set abc.threads=1 --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_$tag" \
      --set synth.liberty="$LIB" --set pass.usyn.memory_budget_mb=1 --set pass.usyn.qor="$W/qor_$tag.json" "$@" \
      --emit diagnostics:"$W/$tag.jsonl" \
      --workdir "$W/w_$tag" -q --result-json "$W/$tag.json" 2>"$W/$tag.err"; then
    fail "$tag: pass.usyn accepted a 1 MiB memory budget (the guard did not fire)"
  fi
  grep -q '"code":"synthesis-refused"' "$W/$tag.jsonl" \
    || fail "$tag: no synthesis-refused diagnostic: $(cat "$W/$tag.jsonl" 2>/dev/null)"
  grep -q '"class":"unsupported"' "$W/$tag.json" \
    || fail "$tag: refusal envelope is not an unsupported-class error: $(cat "$W/$tag.json" 2>/dev/null)"
  grep -q "memory-growth budget exhausted" "$W/$tag.json" \
    || fail "$tag: refusal envelope lacks the memory-growth reason: $(cat "$W/$tag.json" 2>/dev/null)"
  grep -q "TIMEOUT-memory: " "$W/$tag.json" \
    || fail "$tag: refusal is not named TIMEOUT-memory: $(cat "$W/$tag.json" 2>/dev/null)"
  # USYN's message names the exhausted budget kind, not its size; the recipe
  # records the value the invocation enforced.
  grep -q 'memory_budget_mb:1 ' "$W/$tag.json" \
    || fail "$tag: recipe does not record the 1 MiB budget it was given: $(cat "$W/$tag.json" 2>/dev/null)"
  if [ -d "$W/net_$tag" ] && [ -n "$(ls -A "$W/net_$tag" 2>/dev/null)" ]; then
    fail "$tag: refused run left a partial netlist in the emit-dir: $(ls "$W/net_$tag")"
  fi
  # Refusal discards all output: no technology-map or native decision report.
  for f in "$W/qor_$tag.json" "$W/qor_$tag.json.usyn.json"; do
    [ ! -e "$f" ] || fail "$tag: refused run published the report $f"
  done
}

# ---------------------------------------------------------------------------
# 1. an unsatisfiable budget must refuse
# ---------------------------------------------------------------------------
if [ "$MAPPER" = abc ]; then
  if "$LHD" pass "$MAPPER" --set lhd.jobs=1 --set abc.threads=1 --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_refused" \
      --set synth.liberty="$LIB" --set abc.memory_budget_mb=1 \
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
  # A caught memory limit carries the formal tools' word for it.
  grep -q "TIMEOUT-memory: " "$W/refused.json" \
    || fail "refusal is not named TIMEOUT-memory: $(cat "$W/refused.json" 2>/dev/null)"

  # ---------------------------------------------------------------------------
  # 2. a refusal must emit NO partial result. A half-translated netlist silently
  #    passed downstream is worse than the OOM this guard prevents.
  # ---------------------------------------------------------------------------
  if [ -d "$W/net_refused" ] && [ -n "$(ls -A "$W/net_refused" 2>/dev/null)" ]; then
    fail "refused run left a partial netlist in the emit-dir: $(ls "$W/net_refused")"
  fi
else
  # 1 + 2 for USYN (see usyn_refuses).
  usyn_refuses refused
fi

# ---------------------------------------------------------------------------
# 3. no escape hatch: the removed allow_oversize option is REJECTED (a usage
#    error, exit 2, naming it) instead of silently disabling the guard, and the
#    rejected run publishes nothing. Raising memory_budget_mb (step 4) is how a
#    larger region is admitted.
# ---------------------------------------------------------------------------
"$LHD" pass "$MAPPER" --set lhd.jobs=1 --set abc.threads=1 --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_forced" \
  --set synth.liberty="$LIB" --set "$BUDGET=1" --set abc.allow_oversize=true \
  --workdir "$W/w4o" -q --result-json "$W/forced.json" 2>/dev/null
rc=$?
[ "$rc" -eq 2 ] || fail "abc.allow_oversize must be a usage error (exit 2), got rc=$rc: $(cat "$W/forced.json" 2>/dev/null)"
grep -q '"class":"usage"' "$W/forced.json" || fail "abc.allow_oversize: not a usage-class error: $(cat "$W/forced.json" 2>/dev/null)"
grep -q 'allow_oversize' "$W/forced.json" || fail "the rejection does not name allow_oversize: $(cat "$W/forced.json" 2>/dev/null)"
[ -z "$(ls -A "$W/net_forced" 2>/dev/null)" ] || fail "a rejected allow_oversize run published a netlist"
if [ "$MAPPER" = usyn ]; then
  # ABC's admission knobs are not inherited: ABC's budget must not trigger the
  # USYN refusal.
  run pass usyn --set lhd.jobs=1 --set abc.threads=1 --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_abc_budget" \
    --set synth.liberty="$LIB" --set abc.memory_budget_mb=1 --workdir "$W/w4"
  [ -n "$(ls -A "$W/net_abc_budget" 2>/dev/null)" ] || fail "abc.memory_budget_mb=1 was inherited by pass.usyn (no netlist)"
fi

# ---------------------------------------------------------------------------
# 4. a generous budget must NOT false-positive on a design that plainly fits
# ---------------------------------------------------------------------------
run pass "$MAPPER" --set lhd.jobs=1 --set abc.threads=1 --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_ok" \
  --set synth.liberty="$LIB" --set "$BUDGET=65536" --workdir "$W/w5"
[ -n "$(ls -A "$W/net_ok" 2>/dev/null)" ] || fail "a 64 GiB budget produced no netlist"

# ---------------------------------------------------------------------------
# 5. a malformed budget is an error, not a silent fallback to "unlimited"
# ---------------------------------------------------------------------------
bad_budgets=(lots)
# USYN documents the budget as a positive MiB count: zero is not "unlimited".
[ "$MAPPER" = abc ] || bad_budgets+=(0)
for v in "${bad_budgets[@]}"; do
  if "$LHD" pass "$MAPPER" --set lhd.jobs=1 --set abc.threads=1 --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_bad_$v" \
      --set synth.liberty="$LIB" --set "$BUDGET=$v" \
      --workdir "$W/w6_$v" -q --result-json "$W/bad_$v.json" 2>/dev/null; then
    fail "pass.$MAPPER accepted memory_budget_mb=$v"
  fi
  # A clean option refusal, never a crash or an unrelated failure: the result
  # envelope exists and the netlist was not published.
  grep -q '"status":"fail"' "$W/bad_$v.json" || fail "memory_budget_mb=$v: no failure envelope: $(cat "$W/bad_$v.json" 2>/dev/null)"
  if [ "$MAPPER" = usyn ]; then
    grep -q '"class":"syntax"' "$W/bad_$v.json" \
      || fail "memory_budget_mb=$v: expected an option (syntax) refusal: $(cat "$W/bad_$v.json")"
  fi
  [ -z "$(ls -A "$W/net_bad_$v" 2>/dev/null)" ] || fail "memory_budget_mb=$v published a netlist"
done

if [ "$MAPPER" = abc ]; then
  echo "PASS: pass.abc memory admission (TIMEOUT-memory refusal + no partial output + allow_oversize rejected + no false positive)"
else
  echo "PASS: pass.usyn memory admission (refuse + no partial output + allow_oversize rejected + ABC knobs not inherited + no false positive)"
fi

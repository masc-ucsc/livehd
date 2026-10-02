#!/usr/bin/env bash
# Decide whether an `lhd lec` run passes the gate.
#
# Split out of run_coreet_module_lean.sh so the decision can be TESTED against
# PROVEN / REFUTED / UNKNOWN fixtures without running a solver -- see
# lhd/tests/lec_gate_verdict_test.sh.
#
# `lhd lec`'s taxonomy (lhd/lhd_kernel_formal.cpp, "VERDICT TAXONOMY"):
#
#   REFUTED   BMC found a counterexample                      -> exit 10
#   UNKNOWN   the solver timed out / gave up, decided nothing -> exit per formal.strict
#   PASS      proved inductively, holds for all cycles        -> exit 0
#   PASS(n)   BMC exhaustive over inputs for n cycles         -> exit 0
#
# REFUTED is "refuted" and UNKNOWN is "unknown".
#
# A PROOF, HOWEVER, DOES NOT ALWAYS SAY "proven".  Measured on txfma_adder: the
# result JSON carries NO `verdict` field at all, and the log's per-block
# diagnostic says `"verdict":"pass"` -- so the extraction below picked up "pass"
# and strict mode rejected a run whose own log read
#     lec[hier]: 'txfma_adder' PROVEN
#     lec[hier]: 1/1 def(s) proven top-down
# The gate's fixtures had been written from the documented taxonomy rather than
# from a real run, so they agreed with the gate and both were wrong together.
#
# The AUTHORITATIVE signal is therefore `K/N def(s) proven` with K == N > 0:
# every definition in the hierarchy discharged.  A block-level "pass" on its own
# is NOT accepted -- in a multi-block run one block passing says nothing about
# the others.
#
# WHY THIS EXISTS. The gate used to exit only on the literal text REFUTED, so
# UNKNOWN, a solver timeout, a crashed run and a MISSING verdict all printed a
# note and continued -- including with LEC_STRICT=true, which therefore gated
# nothing. In strict mode an absent verdict must fail: "no counterexample was
# printed" is not evidence of equivalence.
#
# usage: lec_gate_verdict.sh <lec_exit_status> <json|''> <log|''> <strict:true|false>
# exit:  0 gate passes (or non-strict and merely inconclusive)
#        4 REFUTED -- the implementation does not match the reference
#        5 strict mode and the run did not come back explicitly proven
set -u

status="${1:?usage: lec_gate_verdict.sh <status> <json> <log> <strict>}"
json="${2:-}"
log="${3:-}"
strict="${4:-false}"

verdict=""
if [[ -n "$json" && -r "$json" ]]; then
  verdict="$(grep -oP '"verdict"\s*:\s*"\K[^"]*' "$json" 2>/dev/null | tail -1)"
fi
if [[ -z "$verdict" && -n "$log" && -r "$log" ]]; then
  verdict="$(grep -oP '"verdict"\s*:\s*"\K[^"]*' "$log" 2>/dev/null | tail -1)"
fi
# Last resort: the human verdict line. Only ever used to RECOGNISE a refutation
# or a proof, never to invent one when the run produced no verdict at all.
if [[ -z "$verdict" && -n "$log" && -r "$log" ]]; then
  if   grep -qE '\bREFUTED\b' "$log" 2>/dev/null; then verdict="refuted"
  elif grep -qE '\bPROVEN\b|\bPASS\([0-9]+\)|\bPASS\b' "$log" 2>/dev/null; then verdict="proven"
  fi
fi

# Corroboration: did the tool say every definition was discharged?
all_defs_proven=false
for _f in "$json" "$log"; do
  [[ -n "$_f" && -r "$_f" ]] || continue
  while read -r _k _n; do
    [[ -n "${_k:-}" && -n "${_n:-}" ]] || continue
    [[ "$_k" == "$_n" && "$_k" -gt 0 ]] && all_defs_proven=true
  done < <(grep -oP '[0-9]+/[0-9]+(?= def\(s\) proven)' "$_f" 2>/dev/null | tr '/' ' ')
done

# What counts as a proof: the machine-readable "proven", or the tool stating
# that every definition was discharged.  Not a bare "pass".
proven=false
if [[ "$verdict" == "proven" || "$all_defs_proven" == "true" ]]; then
  proven=true
fi

echo "lec gate exit=$status verdict=${verdict:-<none>} all_defs_proven=$all_defs_proven strict=$strict"

if [[ "$verdict" == "refuted" ]]; then
  echo "FATAL: LEC gate REFUTED -- the LGraph does not match the RTL; do NOT generate" >&2
  exit 4
fi

if [[ "$strict" == "true" ]]; then
  # Both conditions, not either: a zero exit with no verdict means the run did
  # not get far enough to decide, and a "proven" string from a run that then
  # failed is not a result either.
  if [[ "$status" -ne 0 || "$proven" != "true" ]]; then
    echo "FATAL: LEC_STRICT=true requires a PROVEN result AND exit 0" >&2
    echo "       got exit=$status verdict=${verdict:-<none>} all_defs_proven=$all_defs_proven" >&2
    exit 5
  fi
  exit 0
fi

# Historical non-strict behaviour, unchanged: record and continue.
if [[ "$status" -ne 0 || "$proven" != "true" ]]; then
  echo "  (LEC gate inconclusive; recorded, not fatal -- set LEC_STRICT=true to harden)"
fi
exit 0

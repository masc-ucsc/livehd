#!/usr/bin/env bash
set -uo pipefail
# Controls for coreet_full_flow.sh's completion marker.
#
# The marker used to mean "a non-empty row file exists for every requested
# module", which says nothing about who wrote it. Measured on the frozen
# wrapper: with a PRIOR run's rows present and the current launcher failing
# outright, the sweep printed COMPLETE and exited 0 having run ZERO workers.
# A stale row is not a result.
#
# Usage: coreet_sweep_freshness_controls.sh

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
SWEEP="$R/pass/lean/Lgraph_IR_Semantics_Simulator/scripts/coreet_full_flow.sh"
T="$R/generated/pass_lean_runtime_tmp/sweep_freshness"
rm -rf "$T"; mkdir -p "$T"
rc=0
printf 'mod_one\nmod_two\n' > "$T/list.txt"

# AN ISOLATED FROZEN PROJECT, not a stub on PATH.
#
# The first version of these controls wrote a stub at $T/bin/ and set STUB_FAIL,
# which changed nothing: coreet_full_flow.sh invokes
# "$R/pass/lean/Lgraph_IR_Semantics_Simulator/scripts/run_coreet_module_lean.sh" by ABSOLUTE path, so the stub was never
# reached and the "aborted worker" case was not being exercised at all.
#
# So the controls run against a COPY of the sweep in an isolated tree, whose
# sibling scripts/ holds the stub. Production admission is untouched: the real
# script is not modified, and the copy differs only in where it lives.
PROJ="$T/proj"
mkdir -p "$PROJ/scripts"
cp "$SWEEP" "$PROJ/pass/lean/Lgraph_IR_Semantics_Simulator/scripts/coreet_full_flow.sh"
cat > "$PROJ/pass/lean/Lgraph_IR_Semantics_Simulator/scripts/run_coreet_module_lean.sh" <<'STUB'
#!/usr/bin/env bash
# Stand-in worker. Exits nonzero when STUB_FAIL=1 so the sweep records a
# failure row rather than a pass.
[[ "${STUB_FAIL:-0}" == 1 ]] && exit 3
exit 0
STUB
chmod +x "$PROJ/pass/lean/Lgraph_IR_Semantics_Simulator/scripts/run_coreet_module_lean.sh"
SWEEP="$PROJ/pass/lean/Lgraph_IR_Semantics_Simulator/scripts/coreet_full_flow.sh"
R="$PROJ"

run() {  # <case> [env...] ; echoes "exit=<n> marker=<COMPLETE|INCOMPLETE|none>"
  local case="$1"; shift
  local out="$T/$case"
  mkdir -p "$out"
  ( cd "$R" && env COREET_ROOT="$T" OUT="$out" "$@" \
      bash "$SWEEP" "$T/list.txt" 2 ) > "$out/sweep.log" 2>&1
  local e=$?
  local marker=none
  [[ -f "$out/COMPLETE"   ]] && marker=COMPLETE
  [[ -f "$out/INCOMPLETE" ]] && marker=INCOMPLETE
  echo "exit=$e marker=$marker"
}

# 1. POSITIVE: a real run that produces every row.
res="$(run fresh_ok)"
if [[ "$res" == "exit=0 marker=COMPLETE" ]]; then
  echo "ok: a fresh complete sweep is COMPLETE ($res)"
else
  echo "FAIL: a fresh complete sweep did not report COMPLETE ($res)"; rc=1
fi

# 2. A worker that ABORTS must not be counted. The row still exists (the sweep
#    records EXPORT_FAIL), so this checks the row is honest rather than absent.
res="$(run worker_fail STUB_FAIL=1)"
if [[ "$res" == exit=0* ]] && grep -q 'EXPORT_FAIL' "$T/worker_fail/coreet.tsv" 2>/dev/null; then
  echo "ok: an aborted worker is recorded as a failure row, not silently dropped"
else
  echo "FAIL: an aborted worker was not recorded ($res)"; rc=1
fi

# 3. THE STALE-ROW CASE. Seed a previous run's rows, then make the current
#    launcher fail. Nothing runs now, so the marker must NOT be COMPLETE.
out="$T/stale"
mkdir -p "$out/rows/PREVIOUS-RUN"
printf 'mod_one\tPASS\t1\t1\t0\t\n' > "$out/rows/PREVIOUS-RUN/mod_one.row"
printf 'mod_two\tPASS\t1\t1\t0\t\n' > "$out/rows/PREVIOUS-RUN/mod_two.row"
# `-P nonsense` makes xargs itself fail, so no worker runs at all.
( cd "$R" && env COREET_ROOT="$T" OUT="$out" bash "$SWEEP" "$T/list.txt" nonsense ) \
  > "$out/sweep.log" 2>&1
e=$?
marker=none
[[ -f "$out/COMPLETE"   ]] && marker=COMPLETE
[[ -f "$out/INCOMPLETE" ]] && marker=INCOMPLETE
if [[ "$marker" == COMPLETE ]]; then
  echo "FAIL: a FAILED launcher with stale rows present reported COMPLETE (exit=$e)"; rc=1
else
  echo "ok: stale rows + failed launcher -> $marker, exit=$e"
fi
# The old evidence must still be there: it is evidence, of a different run.
if [[ -s "$out/rows/PREVIOUS-RUN/mod_one.row" ]]; then
  echo "ok: the previous run's rows were preserved"
else
  echo "FAIL: previous evidence was destroyed"; rc=1
fi

[[ $rc -eq 0 ]] && echo "PASS: completion means THIS run finished"
exit $rc

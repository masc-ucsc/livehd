#!/usr/bin/env bash
set -uo pipefail
# Admission controls for replay_orchestrator.py, through its ACTUAL CLI.
#
# The previous version of this file did not test the CLI at all. It imported
# `typecheck` directly, used the LENGTH OF A HASH STRING as its stale-artifact
# gate, and grepped the source for the words "unresolved prerequisite". Its
# False-axiom fixture put `axiom` BEFORE `import`, so it failed on a syntax
# error without ever exercising an axiom dependency -- and "passed" for the
# wrong reason.
#
# Each control below runs the orchestrator binary on a real inventory and reads
# its real result files.
#
# Usage: replay_orchestrator_controls.sh <a-real-verified-artifact.lean> <Top>

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ORCH="$R/scripts/replay_orchestrator.py"
ART="${1:?usage: replay_orchestrator_controls.sh <artifact.lean> <Top>}"
TOP="${2:?}"
T="$R/generated/pass_lean_runtime_tmp/replay_cli_controls"
rm -rf "$T"; mkdir -p "$T"
rc=0

# Fixtures built FROM a real artifact, so each differs from it in exactly one way.
cp "$ART" "$T/good.lean"
echo "THIS IS NOT VALID LEAN AND HAS NO PROOF" > "$T/garbage.lean"
# A type-correct, genuinely USED False axiom: declared after the imports, inside
# the file's own namespace-free scope, and consumed by a replaced obligation.
python3 -I - "$T/good.lean" "$T/false_axiom.lean" "$TOP" <<'PY'
import re, sys
src, dst, top = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(src).read()
# insert AFTER the last import so it parses, and make an obligation depend on it
last = s.rfind("import ")
eol = s.index("\n", last) + 1
s = s[:eol] + f"\naxiom ctlFalse : False\n" + s[eol:]
s = re.sub(rf"theorem {top}_step_legacy_correct[^\n]*\n(.*?)(?=\n#print axioms|\ndef |\ntheorem |\Z)",
           f"theorem {top}_step_legacy_correct : ∀ inp st,\n"
           f"    {top}_step inp st = interpretDesignLegacy {top}_designCert inp st :=\n"
           f"  absurd ctlFalse (by simp)\n", s, count=1, flags=re.S)
open(dst, "w").write(s)
PY

mk_inventory() {  # <path> <case> <mode>
  printf '{"configurations":{"ctl":1},"jobs":[{"family":"CTL","case":"%s","top":"%s","mode":"%s"}]}\n' \
    "$2" "$TOP" "$3" > "$1"
}
verdict() {  # <results-dir> <case> <mode> <field>
  python3 -I -c "
import json,sys
try:
    d=json.load(open(f'{sys.argv[1]}/CTL__{sys.argv[2]}__{sys.argv[3]}.json'))
    print(d.get(sys.argv[4],'-'))
except Exception: print('-')" "$1" "$2" "$3" "$4"
}

# The orchestrator exports via a family adapter; CTL has none, so these drive the
# PROVE stage against a pre-recorded export -- which is also the preseeded-
# artifact scenario, and must therefore be refused on its own.
echo "--- control: a preexisting artifact is not a fresh export ---"
INV="$T/inv_preseed.json"; mk_inventory "$INV" preseed verified_compiler
mkdir -p "$T/replay_preseed"
out="$(cd "$R" && python3 -I "$ORCH" run "$INV" --family CTL --replay-dir "$T/replay_preseed" 2>&1)"
e=$?
if [[ "$e" -ne 0 ]]; then
  echo "ok: an unwired family exits nonzero (exit=$e)"
else
  echo "FAIL: an unwired family exited 0"; rc=1
fi
echo "$out" | grep -q 'SKIPPED' && echo "ok: and is recorded SKIPPED, not passed" \
  || { echo "FAIL: unwired family not SKIPPED"; rc=1; }

# Typecheck-stage controls, driven through the module's real entry point with a
# real mode -- not auto.
probe() {  # <artifact> <mode> -> proof_state
  python3 -I - "$ORCH" "$1" "$T/probe" "$TOP" "$2" <<'PY' 2>/dev/null
import importlib.util, sys
from pathlib import Path
spec = importlib.util.spec_from_file_location("orch", sys.argv[1])
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
out = Path(sys.argv[3]); out.mkdir(parents=True, exist_ok=True)
try:
    print(m.typecheck(Path(sys.argv[2]), sys.argv[4], out, sys.argv[5])["proof_state"])
except Exception:
    print("FAILED")
PY
}
expect() { # <name> <artifact> <mode> <want>
  local got; got="$(probe "$2" "$3")"
  [[ "$got" == "$4" ]] && echo "ok: $1 -> $got" || { echo "FAIL: $1 -> ${got:-none}, want $4"; rc=1; }
}
echo "--- controls: the requested scope decides, not the artifact ---"
expect real_positive           "$T/good.lean"        verified_compiler PROVEN
expect invalid_lean            "$T/garbage.lean"     verified_compiler FAILED
expect used_false_axiom        "$T/false_axiom.lean" verified_compiler FAILED
# THE SCOPE CONTROL: a verified certificate offered for a chunked-WF request.
expect verified_for_chunked_wf "$T/good.lean"        legacy_chunked_wf FAILED
expect verified_for_bridge     "$T/good.lean"        legacy_bridge     FAILED
expect missing_artifact        "$T/does_not_exist.lean" verified_compiler FAILED


# --- controls: --case selects, --redo replaces ------------------------------
#
# A bounded rerun is the only way to correct a record that a gate got wrong, so
# the selection itself has to be trustworthy: a name that matches nothing must
# be an ERROR, not an empty selection reported as a successful rerun.
echo "--- controls: --case selects, and a name that matches nothing is an error ---"
INV2="$T/inv_two.json"
printf '{"configurations":{"ctl":2},"jobs":[{"family":"CTL","case":"alpha","top":"%s","mode":"verified_compiler"},{"family":"CTL","case":"beta","top":"%s","mode":"verified_compiler"}]}\n' \
  "$TOP" "$TOP" > "$INV2"
RES2="$T/replay_two"; mkdir -p "$RES2"

sel_count() {  # args... -> the N from "running N job(s)"
  (cd "$R" && python3 -I "$ORCH" run "$INV2" --replay-dir "$RES2" --family CTL "$@" 2>&1) \
    | sed -n 's/^running \([0-9]*\) job(s).*/\1/p' | head -1
}

n=$(sel_count); [[ "$n" == "2" ]] \
  && echo "ok: no --case selects both jobs" \
  || { echo "FAIL: no --case selected '${n:-none}', want 2"; rc=1; }

rm -rf "$RES2"; mkdir -p "$RES2"
n=$(sel_count --case alpha); [[ "$n" == "1" ]] \
  && echo "ok: --case alpha selects exactly one job" \
  || { echo "FAIL: --case alpha selected '${n:-none}', want 1"; rc=1; }

out="$(cd "$R" && python3 -I "$ORCH" run "$INV2" --replay-dir "$RES2" --case gamma 2>&1)"; e=$?
if [[ "$e" -eq 2 ]] && grep -q 'no such case' <<<"$out"; then
  echo "ok: an unknown --case exits 2 rather than selecting nothing"
else
  echo "FAIL: --case gamma -> exit $e: $(head -1 <<<"$out")"; rc=1
fi

# A case that EXISTS but is excluded by --mode must still be reported as known:
# the check is against the inventory, not the post-filter selection.
out="$(cd "$R" && python3 -I "$ORCH" run "$INV2" --replay-dir "$RES2" --case alpha --mode legacy 2>&1)"
grep -q 'no such case' <<<"$out" \
  && { echo "FAIL: a known case excluded by --mode was reported unknown"; rc=1; } \
  || echo "ok: a known case excluded by --mode is not reported unknown"

# Re-selection: a recorded job is skipped, and --redo brings it back.
n=$(sel_count --case alpha); [[ "$n" == "0" ]] \
  && echo "ok: a recorded case is not re-run without --redo" \
  || { echo "FAIL: recorded case re-selected '${n:-none}', want 0"; rc=1; }
n=$(sel_count --case alpha --redo); [[ "$n" == "1" ]] \
  && echo "ok: --redo re-runs the recorded case" \
  || { echo "FAIL: --redo selected '${n:-none}', want 1"; rc=1; }

# status must report on the tree it was POINTED AT.  The relocation used to sit
# below the status dispatch, so `status --replay-dir X` described the default
# tree while naming X.  This control needs the two trees to DISAGREE, or it
# passes whatever the code does.
echo "--- control: status reports on the relocated tree, not the default ---"
STAT_A="$T/replay_stat_a"; STAT_B="$T/replay_stat_b"
mkdir -p "$STAT_A/results" "$STAT_B/results"
# A: both jobs recorded.  B: neither.  A default-tree read of either is wrong.
for c in alpha beta; do
  printf '{"family":"CTL","case":"%s","top":"%s","mode":"verified_compiler","export_state":"SKIPPED","proof_state":"NOT_RUN"}\n' \
    "$c" "$TOP" > "$STAT_A/results/CTL__${c}__verified_compiler.json"
done
sa="$(cd "$R" && python3 -I "$ORCH" status "$INV2" --replay-dir "$STAT_A" 2>&1)"
sb="$(cd "$R" && python3 -I "$ORCH" status "$INV2" --replay-dir "$STAT_B" 2>&1)"
if [[ "$sa" == "$sb" ]]; then
  echo "FAIL: status gave the same answer for a populated and an empty tree"
  echo "      $(head -3 <<<"$sa" | tr '\n' ' ')"; rc=1
elif grep -qi 'skipped' <<<"$sa"; then
  echo "ok: status reads the relocated tree (populated != empty, and reports SKIPPED)"
else
  echo "FAIL: status on the populated tree did not report its records: $(head -3 <<<"$sa" | tr '\n' ' ')"; rc=1
fi

out="$(cd "$R" && python3 -I "$ORCH" run "$INV2" --replay-dir "$RES2" --redo 2>&1)"; e=$?
[[ "$e" -eq 2 ]] \
  && echo "ok: --redo without --case is refused, not applied to everything" \
  || { echo "FAIL: --redo without --case exited $e"; rc=1; }

[[ $rc -eq 0 ]] && echo "PASS: scope is requested, freshness is enforced, failures exit nonzero"
exit $rc

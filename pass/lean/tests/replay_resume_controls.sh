#!/usr/bin/env bash
set -uo pipefail
# Adversarial controls for --resume in replay_legacy_proofs.py.
#
# Resume exists to skip work whose inputs have not changed.  The failure it
# invites is subtler than a stale artifact: the REQUEST can get stronger while
# the inputs stay identical, and a cache keyed only on inputs will happily
# answer the new question with the old answer.
#
# Both controls below did exactly that before the fix, and neither touches a
# single byte of the artifact.
#
# Usage: replay_resume_controls.sh <positive-bridge.lean> <Top> <env.json>

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
REPLAY="$R/pass/lean/tests/replay_legacy_proofs.py"
SRC="${1:?usage: replay_resume_controls.sh <artifact.lean> <Top> <env.json>}"
TOP="${2:?}"
ENVJSON="${3:?}"
T="$(mktemp -d "${TMPDIR:-$R/generated/pass_lean_runtime_tmp}/resumectl.XXXXXX")"
trap 'rm -rf "$T"' EXIT
rc=0

stage() {  # <runtime> ; lay the artifact out where the replay expects it
  mkdir -p "$1/$TOP/export"
  cp "$SRC" "$1/$TOP/export/${TOP}_Lgraph.lean"
}
inventory() {  # <path> <cert_wf> <scope>
  printf '[{"top":"%s","scope":"%s","replay_mode":"regenerated","export_status":0,"cert_wf":"%s"}]\n' \
    "$TOP" "$3" "$2" > "$1"
}
verdict_of() { python3 -I -c "
import json,sys
d=json.load(open(sys.argv[1]))
print(d[0]['verdict'])" "$1/proof_results.json" 2>/dev/null; }

# --- control 1: bridge -> chunked-WF --------------------------------------
# Record PROVEN as a plain bridge, then ask the STRONGER question (the cert is
# chunked, so graphCert_wf is owed) with --resume.  Reusing the old PROVEN here
# means graphCert_wf is never required of it.
RT="$T/c1"; stage "$RT"
inventory "$T/inv_plain.json" skip bridge
python3 -I "$REPLAY" "$T/inv_plain.json" "$RT" "$ENVJSON" --jobs 4 >"$T/c1a.log" 2>&1
first="$(verdict_of "$RT")"
inventory "$T/inv_chunked.json" chunked bridge
python3 -I "$REPLAY" "$T/inv_chunked.json" "$RT" "$ENVJSON" --jobs 4 --resume >"$T/c1b.log" 2>&1
second="$(verdict_of "$RT")"
if grep -q '^REUSE' "$T/c1b.log" && [[ "$second" == PROVEN ]]; then
  echo "FAIL: c1 bridge->chunked-WF REUSED a verdict across a stronger obligation set"; rc=1
else
  echo "ok: c1 bridge->chunked-WF re-ran (first=$first, then=$second)"
fi

# --- control 2: typecheck -> bridge ---------------------------------------
# A TYPECHECKED row is not a proof.  Promoting the scope to `bridge` asks for
# one; the stored TYPECHECKED verdict must not satisfy it.
RT="$T/c2"; stage "$RT"
inventory "$T/inv_tc.json" skip typecheck
python3 -I "$REPLAY" "$T/inv_tc.json" "$RT" "$ENVJSON" --jobs 4 >"$T/c2a.log" 2>&1
first="$(verdict_of "$RT")"
inventory "$T/inv_bridge.json" skip bridge
python3 -I "$REPLAY" "$T/inv_bridge.json" "$RT" "$ENVJSON" --jobs 4 --resume >"$T/c2b.log" 2>&1
second="$(verdict_of "$RT")"
if grep -q '^REUSE' "$T/c2b.log" && [[ "$second" == TYPECHECKED ]]; then
  echo "FAIL: c2 typecheck->bridge REUSED a TYPECHECKED row to answer a proof request"; rc=1
else
  echo "ok: c2 typecheck->bridge re-ran (first=$first, then=$second)"
fi

# --- control 3: a PRE-POLICY entry ----------------------------------------
# A verdict recorded before the axiom policy existed carries no auditor hash.
# Its artifact is unchanged, so every input-based key still matches.
RT="$T/c3"; stage "$RT"
inventory "$T/inv_plain3.json" skip bridge
python3 -I "$REPLAY" "$T/inv_plain3.json" "$RT" "$ENVJSON" --jobs 4 >"$T/c3a.log" 2>&1
python3 -I - "$RT/$TOP/proof_result.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
for k in ('auditor', 'auditor_sha256', 'axiom_audit', 'axiom_audit_detail', 'required_obligations'):
    d.pop(k, None)          # as a pre-policy run would have left it
json.dump(d, open(p, 'w'), indent=2)
PY
python3 -I "$REPLAY" "$T/inv_plain3.json" "$RT" "$ENVJSON" --jobs 4 --resume >"$T/c3b.log" 2>&1
if grep -q '^REUSE' "$T/c3b.log"; then
  echo "FAIL: c3 a pre-policy entry was REUSED, bypassing the repaired gate"; rc=1
else
  echo "ok: c3 pre-policy entry re-ran"
fi

# --- positive: an UNCHANGED request must still resume ----------------------
# A gate that never reuses anything is not a fix, it is a removal.
RT="$T/c4"; stage "$RT"
inventory "$T/inv_plain4.json" skip bridge
python3 -I "$REPLAY" "$T/inv_plain4.json" "$RT" "$ENVJSON" --jobs 4 >"$T/c4a.log" 2>&1
python3 -I "$REPLAY" "$T/inv_plain4.json" "$RT" "$ENVJSON" --jobs 4 --resume >"$T/c4b.log" 2>&1
if grep -q '^REUSE' "$T/c4b.log"; then
  echo "ok: c4 an identical request still resumes"
else
  echo "FAIL: c4 an identical request did NOT resume -- resume is broken, not fixed"; rc=1
fi

[[ $rc -eq 0 ]] && echo "PASS: resume answers the question it was asked"
exit $rc

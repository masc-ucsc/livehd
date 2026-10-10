#!/usr/bin/env bash
set -uo pipefail
# Controls for reaudit_results.py, through its actual CLI.
#
# A re-audit exists to apply a corrected obligation rule to work already done.
# What it must never do is PROMOTE a run that did not succeed. The first version
# set the verdict purely from axiom_audit.py's exit code, so a record whose
# artifact ends
#
#     example : False := by rfl
#
# -- Lean exit 1, correctly FAILED when it ran -- came back PROVEN, because its
# three bridge theorems printed before the error and the axiom report alone
# looked clean. An axiom audit says what a proof DEPENDS on. It does not say the
# file compiled.
#
# Usage: reaudit_controls.sh <complete-stateful-bridge-artifact.lean> <Top>

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RA="$R/scripts/reaudit_results.py"
ART="${1:?usage: reaudit_controls.sh <artifact.lean> <Top>}"
TOP="${2:?}"
T="$R/generated/pass_lean_runtime_tmp/reaudit_controls"
rm -rf "$T"; mkdir -p "$T"
rc=0

mk_case() {  # <name> <artifact> <lean_exit> <lean_errors> [extra-json]
  local n="$1" art="$2" ex="$3" errs="$4"
  local d="$T/$n"
  mkdir -p "$d"
  cp "$art" "$d/art.lean"
  ( cd "$R/formal/lean" && nice -n19 lake env lean "$d/art.lean" ) > "$d/typecheck.log" 2>&1
  local real_exit=$?
  python3 -I - "$d" "$TOP" "$real_exit" "$n" <<'PY'
import hashlib, json, sys
from pathlib import Path
d, top, ex, name = Path(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()[:16]
log = (d/"typecheck.log").read_text(errors="replace")
json.dump({"family":"CTL","case":name,"top":top,"mode":"legacy_bridge",
           "export_state":"EXPORTED","proof_state":"FAILED",
           "artifact":str(d/"art.lean"),"artifact_sha256":sha(d/"art.lean"),
           "out_dir":str(d),"typecheck_log_sha256":sha(d/"typecheck.log"),
           "lean_exit":ex,"lean_errors":log.count("error:")},
          open(d/"result.json","w"), indent=1)
PY
  echo "$d"
}

run_reaudit() {  # <case-dir> <name> -> proof_state printed
  local d="$1" n="$2"
  local res="$T/res_$n"; mkdir -p "$res"
  cp "$d/result.json" "$res/CTL__${n}__legacy_bridge.json"
  printf '{"configurations":{},"jobs":[{"family":"CTL","case":"%s","top":"%s","mode":"legacy_bridge"}]}\n' \
    "$n" "$TOP" > "$T/inv_$n.json"
  ( cd "$R" && python3 -I "$RA" "$T/inv_$n.json" --family CTL --results "$res" ) >"$T/$n.reaudit.log" 2>&1
  python3 -I -c "
import json,sys
try: print(json.load(open(sys.argv[1]))['proof_state'])
except Exception: print('-')" "$res/CTL__${n}__legacy_bridge.json"
}
expect() {  # <name> <case-dir> <want>
  local got; got="$(run_reaudit "$2" "$1")"
  [[ "$got" == "$3" ]] && echo "ok: $1 -> $got" || { echo "FAIL: $1 -> ${got:-none}, want $3"; rc=1; }
}

# 1. THE LATE ERROR. Three valid printed bridge theorems, then a false example.
python3 -I - "$ART" "$T/late_error.lean" "$TOP" <<'PY'
import sys
src, dst, top = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(src).read().rstrip()
# Append AFTER the obligations and their #print axioms lines, so the reports are
# genuine and the failure comes later.
s += "\n\nexample : False := by rfl\n"
open(dst, "w").write(s)
PY
d="$(mk_case late_error "$T/late_error.lean" 1 1)"
expect late_error "$d" FAILED

# 2. THE COMPLETE POSITIVE must still be promoted.
d2="$(mk_case complete "$ART" 0 0)"
expect complete "$d2" PROVEN

# 3. A CHANGED ARTIFACT after the run: the evidence is no longer the evidence.
d3="$(mk_case tampered "$ART" 0 0)"
printf '\n-- appended after the recorded run\n' >> "$d3/art.lean"
expect tampered "$d3" FAILED

# 4. A record with NO recorded Lean exit cannot be confirmed.
d4="$(mk_case no_exit "$ART" 0 0)"
python3 -I -c "
import json,sys
p=sys.argv[1]; d=json.load(open(p)); d.pop('lean_exit',None)
json.dump(d, open(p,'w'), indent=1)" "$d4/result.json"
expect no_exit "$d4" FAILED

[[ $rc -eq 0 ]] && echo "PASS: a re-audit cannot promote a run that did not succeed"
exit $rc

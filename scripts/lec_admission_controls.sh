#!/usr/bin/env bash
set -uo pipefail
# Admission controls for the LEC gate's RESULT HANDLING.
#
# lec_gate_controls.sh proves the solver can prove and refute. This proves the
# RUNNER cannot be told a lie -- a different question, and the one that was
# actually failing.
#
# The gate used to dispatch on a grepped `verdict` while capturing the process
# exit and never reading it. Controls 1 and 2 below walked straight through
# that: exit 7 with verdict=proven, and status="fail" with verdict=proven, both
# printed PROVEN and returned 0. A verdict is a claim made by a run that may
# itself have failed.
#
# Every fixture here uses the REAL schema -- verdict nested under `lec` -- so
# they test the parser that runs in production rather than a shape it never
# sees. (Writing them top-level is itself a control: control 6.)
#
# Usage: lec_admission_controls.sh

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHECK="$R/scripts/lec_result_check.py"
T="$(mktemp -d "${TMPDIR:-$R/generated/pass_lean_runtime_tmp}/lecadm.XXXXXX")"
trap 'rm -rf "$T"' EXIT
rc=0

mk() {  # <file> <status> <exit_code> <verdict|-> [command]
  local v="$3"
  if [[ "$4" == "-" ]]; then
    printf '{"schema_version":1,"tool":"lhd","command":"%s","status":"%s","exit_code":%s}\n' \
      "${5:-lec}" "$2" "$v" > "$1"
  else
    printf '{"schema_version":1,"tool":"lhd","command":"%s","status":"%s","exit_code":%s,"lec":{"verdict":"%s"}}\n' \
      "${5:-lec}" "$2" "$v" "$4" > "$1"
  fi
}
reject() { # <name> <json> <proc-exit>
  if python3 -I "$CHECK" "$2" "$3" >/dev/null 2>&1; then
    echo "FAIL: '$1' was ACCEPTED"; rc=1
  else
    echo "ok: $1 rejected"
  fi
}
accept() {
  if python3 -I "$CHECK" "$2" "$3" >/dev/null 2>&1; then echo "ok: $1 accepted"
  else echo "FAIL: genuine '$1' was REJECTED"; rc=1; python3 -I "$CHECK" "$2" "$3" | sed 's/^/     /'; fi
}

mk "$T/c1.json" pass 7 proven;     reject "c1 process exit 7 + verdict=proven"       "$T/c1.json" 7
mk "$T/c2.json" fail 0 proven;     reject "c2 status=fail + verdict=proven"          "$T/c2.json" 0
mk "$T/c3.json" pass 0 refuted;    reject "c3 refuted"                               "$T/c3.json" 10
mk "$T/c4.json" pass 0 unknown;    reject "c4 undecided"                             "$T/c4.json" 7
mk "$T/c5.json" pass 0 -;          reject "c5 no lec object at all"                  "$T/c5.json" 0
printf '{"schema_version":1,"tool":"lhd","command":"lec","status":"pass","exit_code":0,"verdict":"proven"}\n' > "$T/c6.json"
reject "c6 verdict at the TOP level (wrong schema)" "$T/c6.json" 0
mk "$T/c7.json" fail 2 - usage;    reject "c7 usage rejection"                       "$T/c7.json" 2
echo 'not json at all'           > "$T/c8.json"; reject "c8 malformed JSON"          "$T/c8.json" 0
reject "c9 missing file"            "$T/does_not_exist.json" 0
mk "$T/c10.json" pass 3 proven;    reject "c10 recorded exit_code disagrees with 0"  "$T/c10.json" 0

mk "$T/good.json" pass 0 proven;   accept "a genuine proven record"                  "$T/good.json" 0

[[ $rc -eq 0 ]] && echo "PASS: the gate cannot be told a lie"
exit $rc

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
  # Produce the SAME evidence a real job does, including the timing file: the
  # guard requires it, and a fixture that omits it tests the fixture rather than
  # the policy. (It did: the complete positive failed on "no retained timing
  # file" until this was added.)
  ( cd "$R/formal/lean" && /usr/bin/time -v -o "$d/typecheck.time" \
      nice -n19 lake env lean "$d/art.lean" ) > "$d/typecheck.log" 2>&1
  local real_exit=$?
  python3 -I - "$d" "$TOP" "$real_exit" "$n" <<'PY'
import hashlib, json, sys
from pathlib import Path
d, top, ex, name = Path(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()[:16]
log = (d/"typecheck.log").read_text(errors="replace")
tt = (d/"typecheck.time").read_text(errors="replace") if (d/"typecheck.time").exists() else ""
def field(label):
    for l in tt.splitlines():
        if label in l:
            return l.split(": ")[-1].strip()
    return None
json.dump({"family":"CTL","case":name,"top":top,"mode":"legacy_bridge",
           "export_state":"EXPORTED","proof_state":"FAILED",
           "artifact":str(d/"art.lean"),"artifact_sha256":sha(d/"art.lean"),
           "out_dir":str(d),"typecheck_log_sha256":sha(d/"typecheck.log"),
           "wall_clock":field("Elapsed (wall clock)"),
           "peak_rss_kib":field("Maximum resident set size"),
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
expect no_exit "$d4" UNVERIFIABLE

# 5..9 EVIDENCE-MUTATION controls. These reuse ONE successful retained run and
# mutate its record, rather than recompiling a full artifact per negative: the
# admission logic is what is under test, not Lean.
mutate() {  # <name> <python-statement operating on `rec` and `d`>
  local n="$1" stmt="$2" d="$T/$1"
  rm -rf "$d"; cp -r "$T/complete" "$d" 2>/dev/null || { echo "FAIL: no complete case"; rc=1; return; }
  # `_rp` and `_d`, not `p`/`d`: a mutation statement that binds `p` would
  # otherwise redirect the json.dump below into whatever file it named. One did
  # -- `p = d / "typecheck.time"` wrote the RESULT JSON into the timing file and
  # left the record unmutated, so the control silently tested the unmodified
  # case and reported a pass.
  STMT="$stmt" DIR="$d" python3 -I -c '
import json, os
from pathlib import Path
_d = Path(os.environ["DIR"])
_rp = _d / "result.json"
rec = json.load(open(_rp))
rec["artifact"] = str(_d / "art.lean")
rec["out_dir"] = str(_d)
d = _d
exec(os.environ["STMT"])
json.dump(rec, open(_rp, "w"), indent=1)
'
}

# A null exit: `not in (0, None)` treated this as acceptable.
mutate null_exit 'rec["lean_exit"] = None'
expect null_exit "$T/null_exit" UNVERIFIABLE

# A deleted hash removed its own check, so the evidence went unverified.
mutate no_art_hash 'rec.pop("artifact_sha256", None)'
expect no_art_hash "$T/no_art_hash" UNVERIFIABLE

mutate no_log_hash 'rec.pop("typecheck_log_sha256", None)'
expect no_log_hash "$T/no_log_hash" UNVERIFIABLE

# A vanished artifact left the old PROVEN standing and exited 0.
mutate gone_artifact 'rec["artifact"] = str(d / "does_not_exist.lean")'
expect gone_artifact "$T/gone_artifact" UNVERIFIABLE

# A killed run: /usr/bin/time records the signal while the log can look clean.
mutate killed 'open(d / "typecheck.time", "a").write("\n\tCommand terminated by signal 9\n")'
expect killed "$T/killed" FAILED

# 10..14 MISSING-CONTENT boundaries. Each is a field that is PRESENT but says
# nothing -- the class the first guards missed, because they rejected only None
# and only lines that happened to exist.

mutate empty_art_hash 'rec["artifact_sha256"] = ""'
expect empty_art_hash "$T/empty_art_hash" UNVERIFIABLE

mutate empty_timing 'open(d / "typecheck.time", "w").write("")'
expect empty_timing "$T/empty_timing" UNVERIFIABLE

# The exit line REMOVED, not made nonzero: absence read as success.
mutate no_exit_line 'tf = d / "typecheck.time"; tf.write_text("\n".join(l for l in tf.read_text().splitlines() if "Exit status" not in l))'
expect no_exit_line "$T/no_exit_line" UNVERIFIABLE

# A timing file from a DIFFERENT run: plausible, successful, and not this one.
mutate timing_substituted 'open(d / "typecheck.time", "w").write("\tCommand being timed: \"lake env lean /somewhere/else/Other_Lgraph.lean\"\n\tElapsed (wall clock) time (h:mm:ss or m:ss): 0:01.00\n\tMaximum resident set size (kbytes): 1234\n\tExit status: 0\n")'
expect timing_substituted "$T/timing_substituted" FAILED

# Recorded wall/RSS that the retained timing does not support.
mutate timing_mismatch 'rec["peak_rss_kib"] = "999999999"'
expect timing_mismatch "$T/timing_mismatch" FAILED

# 10..13 THE PROMISED MEASUREMENTS MUST BE PRESENT AND PARSED.
#
# These three checks were written `if cmdline and ...` and `if line and ...`, so
# DELETING a line skipped its own check. Measured externally on a real-Lean
# validated fast model: removing `Command being timed`, `Elapsed (wall clock)`
# or `Maximum resident set size` separately each still returned TYPECHECKED and
# exit 0. Evidence:
# automate_workspace/master_rebase/audits/timing_identity_18e0d31c0_20261010.
drop_timing_line() {  # <name> <substring to delete>
  mutate "$1" "tf = d / 'typecheck.time'; tf.write_text(''.join(l for l in tf.read_text().splitlines(True) if '$2' not in l))"
}
drop_timing_line no_command_line "Command being timed"
expect no_command_line "$T/no_command_line" UNVERIFIABLE
drop_timing_line no_wall_line "Elapsed (wall clock)"
expect no_wall_line "$T/no_wall_line" UNVERIFIABLE
drop_timing_line no_rss_line "Maximum resident set size"
expect no_rss_line "$T/no_rss_line" UNVERIFIABLE

# A line that is present but carries no value is equally unresolved: the shape
# of the evidence is not the evidence.
mutate blank_wall_value "tf = d / 'typecheck.time'; import re as _re; tf.write_text(_re.sub(r'(Elapsed \\(wall clock\\)[^:]*:).*', r'\\1 ', tf.read_text()))"
expect blank_wall_value "$T/blank_wall_value" UNVERIFIABLE

# And a record that never KEPT a measurement is unresolved rather than
# retroactively measured from the file as it stands now.
mutate no_recorded_rss 'rec.pop("peak_rss_kib", None)'
expect no_recorded_rss "$T/no_recorded_rss" UNVERIFIABLE

# 14..17 THE VALUE IS PARSED, NOT LOOKED FOR INSIDE THE LINE.
#
# `str(rec[key]) not in line` is substring containment.  Measured externally,
# all three of these were accepted as TYPECHECKED: recorded RSS 96028 against a
# retained 796028, recorded wall 0:02.5 against a retained 0:02.58, and a
# retained line reading `not-a-number (796028)`.  A truncated measurement, an
# order-of-magnitude error, and a corrupt line.
#
# A DISAGREEMENT is a contradiction -> FAILED.  An UNREADABLE value says only
# that the file cannot be read as a measurement -> UNVERIFIABLE.
mutate truncated_rss 'rec["peak_rss_kib"] = str(rec["peak_rss_kib"])[1:]'
expect truncated_rss "$T/truncated_rss" FAILED

mutate truncated_wall 'rec["wall_clock"] = str(rec["wall_clock"])[:-1]'
expect truncated_wall "$T/truncated_wall" FAILED

mutate malformed_rss_line "
tf = d / 'typecheck.time'; import re as _re
tf.write_text(_re.sub(r'(Maximum resident set size \\(kbytes\\):).*',
                      r'\\1 not-a-number (796028)', tf.read_text()))"
expect malformed_rss_line "$T/malformed_rss_line" UNVERIFIABLE

mutate malformed_recorded_wall 'rec["wall_clock"] = "banana"'
expect malformed_recorded_wall "$T/malformed_recorded_wall" UNVERIFIABLE

# AND THE VALID RECORD STILL PASSES.  Re-run the untouched complete case after
# every mutation above, so a repair that simply refuses everything is caught.
expect complete_still_valid "$d2" PROVEN

[[ $rc -eq 0 ]] && echo "PASS: a re-audit cannot promote a run that did not succeed"
exit $rc

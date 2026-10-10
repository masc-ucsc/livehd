#!/usr/bin/env bash
# Controls for the DCERT1 delta analyzer.
#
# The measurement this analyzer feeds is a SINGLE NUMBER -- "how much of the
# certificate changed" -- and a wrong number in either direction would be
# invisible without these.  Two failure modes matter and each gets its own
# control:
#
#   TOO SMALL.  An analyzer that skipped records it could not parse, or that
#   collapsed duplicate signatures into a set, would under-report churn --
#   which is the direction that flatters Option B.
#
#   TOO LARGE.  An analyzer whose "structural" metric still saw slot numbers
#   would report a renumbering as churn, and every certificate would look
#   rewritten.
#
# So the load-bearing pair is control 3 against controls 4-6: a consistent
# renumbering must move the ORDINAL metric and leave the STRUCTURAL metric at
# zero, while an operator change, a live dependency change and a duplicated
# node must all move the structural metric.  A single metric cannot pass both.
#
# Every refusal control asserts its OWN diagnostic.  Sixteen guards that all
# exit 2 prove nothing if one of them is doing all the work.
#
# Usage: dcert_delta_controls.sh <a-real.dcert> [workdir]
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DELTA="$SCRIPT_DIR/dcert_delta.py"
MUT="$SCRIPT_DIR/dcert_mutate.py"
PY="${PYTHON:-python3}"

REAL="${1:-}"
WORK="${2:-$(mktemp -d)}"
fail=0
die() { printf 'FATAL: %s\n' "$*" >&2; exit 2; }

[[ -n "$REAL" ]] || die "usage: $0 <a-real.dcert> [workdir]
   A real certificate is REQUIRED: the renumbering and mutation controls must
   run at corpus scale, not only on a hand-written fixture."
[[ -s "$REAL" ]] || die "real certificate $REAL is missing or empty"
[[ -r "$DELTA" ]] || die "missing $DELTA"
[[ -r "$MUT"   ]] || die "missing $MUT"
mkdir -p "$WORK" || die "cannot create $WORK"

ok()  { printf 'ok: %s\n' "$1"; }
bad() { printf 'CONTROL FAILED: %s\n' "$1"; fail=$((fail + 1)); }

# `jq`-free field read from the analyzer's own JSON.
metric() {  # metric <json> <group> <key>
  "$PY" - "$1" "$2" "$3" <<'EOF'
import json, sys
print(json.load(open(sys.argv[1]))[sys.argv[2]][sys.argv[3]])
EOF
}

# --------------------------------------------------------------------------
# A hand-written fixture for the grammar controls: small enough that every
# number below can be checked by eye, and it carries two ADJACENT INDEPENDENT
# nodes so the renumbering control has a legal swap.
# --------------------------------------------------------------------------
TINY="$WORK/tiny.dcert"
printf 'DCERT1\n3\n0 0 8\n0 1 8\n1 8 5\n3\n7 0 8 2 0 1 100\n9 0 8 2 0 2 101\n8 0 8 2 3 4 102\n1\n5 8\n1\n8 5 0 0 0 0 0 0\n0\n' > "$TINY"

run()  { "$PY" "$DELTA" "$1" "$2" --json > "$3" 2>"$3.err"; echo $?; }
mut()  { "$PY" "$MUT" "$1" "$2" --kind "$3" > "$WORK/mut.log" 2>&1; echo $?; }

# --- 1. the real pair, and the fixture, both analyze -----------------------
rc=$(run "$REAL" "$REAL" "$WORK/self.json")
if [[ "$rc" == 0 ]] && [[ -s "$WORK/self.json" ]]; then
  ok "the real certificate analyzes and publishes a result"
else
  bad "the real certificate did not analyze (exit $rc)"
fi

# --- 2. identical input is zero on BOTH metrics ----------------------------
if [[ -s "$WORK/self.json" ]]; then
  o=$(metric "$WORK/self.json" ordinal churn)
  s=$(metric "$WORK/self.json" structural churn)
  if [[ "$o" == 0 && "$s" == 0 ]]; then
    ok "identical input gives zero ordinal AND zero structural churn"
  else
    bad "identical input gave ordinal=$o structural=$s; both must be 0"
  fi
fi

# --- 3. a consistent renumbering: ordinal moves, structural does NOT -------
# This is the control the whole metric rests on.  Run on the REAL certificate,
# because a renumbering that only works on a three-node fixture proves nothing
# about a 35,000-node design.
for spec in "$REAL:renumber:one adjacent swap in the real certificate" \
            "$REAL:renumber-many:EVERY disjoint adjacent swap in the real certificate" \
            "$TINY:renumber:one adjacent swap in the fixture"; do
  src="${spec%%:*}"; rest="${spec#*:}"; k="${rest%%:*}"; tag="${rest#*:}"
  if [[ "$(mut "$src" "$WORK/renum.dcert" "$k")" != 0 ]]; then
    bad "could not build $tag"; continue
  fi
  rc=$(run "$src" "$WORK/renum.dcert" "$WORK/renum.json")
  if [[ "$rc" != 0 ]]; then
    bad "$tag was refused (exit $rc)"; continue
  fi
  o=$(metric "$WORK/renum.json" ordinal churn)
  s=$(metric "$WORK/renum.json" structural churn)
  if [[ "$o" -gt 0 && "$s" == 0 ]]; then
    ok "$tag moves ordinal churn ($o) and leaves structural churn at 0"
  else
    bad "$tag gave ordinal=$o structural=$s; expected ordinal>0 and structural==0"
  fi
done

# --- 4/5/6. real changes DO move the structural metric ---------------------
for k in op dep dup; do
  case "$k" in
    op)  what="an operator change" ;;
    dep) what="a live dependency change" ;;
    dup) what="a duplicated node" ;;
  esac
  if [[ "$(mut "$REAL" "$WORK/$k.dcert" "$k")" != 0 ]]; then
    bad "could not build $what"; continue
  fi
  rc=$(run "$REAL" "$WORK/$k.dcert" "$WORK/$k.json")
  if [[ "$rc" != 0 ]]; then bad "$what was refused (exit $rc)"; continue; fi
  s=$(metric "$WORK/$k.json" structural churn)
  if [[ "$s" -gt 0 ]]; then
    ok "$what produces nonzero structural churn ($s)"
  else
    bad "$what produced structural churn $s; a real change must be visible"
  fi
done

# --- 6b. duplicates are COUNTED, not collapsed -----------------------------
# The duplicate's signature already exists, so a set-based analyzer reports 0.
if [[ -s "$WORK/dup.json" ]]; then
  s=$(metric "$WORK/dup.json" structural churn)
  sb=$(metric "$WORK/dup.json" structural only_in_b)
  if [[ "$s" == 1 && "$sb" == 1 ]]; then
    ok "a duplicate of an EXISTING signature counts as one added record, not zero"
  else
    bad "duplicating one node gave structural churn $s (only_in_b $sb); expected exactly 1"
  fi
fi

# --- 7..14. refusals, each pinning its OWN diagnostic ----------------------
refuse() {  # refuse <label> <file> <needle>
  local label="$1" f="$2" needle="$3"
  local rc out
  out="$WORK/r.json"; rm -f "$out"
  "$PY" "$DELTA" "$TINY" "$f" --json > "$out" 2>"$WORK/r.err"; rc=$?
  if [[ "$rc" == 0 ]]; then
    bad "$label: accepted (exit 0)"; return
  fi
  if [[ -s "$out" ]]; then
    bad "$label: refused but PUBLISHED a result anyway"; return
  fi
  if ! grep -qF -- "$needle" "$WORK/r.err"; then
    bad "$label: refused, but not with '$needle' ($(head -c 90 "$WORK/r.err"))"; return
  fi
  ok "$label is refused by its own guard (exit $rc, no result, '$needle')"
}

for spec in "truncate:truncated:a truncated certificate" \
            "count:declares:a section count with no records behind it" \
            "oor-dep:outside the:a dependency naming a slot outside the design" \
            "token:not a decimal integer:a non-decimal token" \
            "trailing:trailing token:a trailing token after the last record" \
            "cycle:dependency cycle:a dependency cycle"; do
  k="${spec%%:*}"; rest="${spec#*:}"; needle="${rest%%:*}"; label="${rest#*:}"
  if [[ "$(mut "$TINY" "$WORK/$k.dcert" "$k")" != 0 ]]; then
    bad "could not build $label"; continue
  fi
  refuse "$label" "$WORK/$k.dcert" "$needle"
done

# Byte-level shapes the mutator deliberately cannot make.
printf 'NOTDCERT\n0\n0\n0\n0\n0\n' > "$WORK/magic.dcert"
refuse "a file without the DCERT1 magic" "$WORK/magic.dcert" "bad magic"

printf 'DCERT1\n1\n6 0 8\n0\n0\n0\n0\n' > "$WORK/tag.dcert"
refuse "an unknown source tag" "$WORK/tag.dcert" "unknown tag"

printf 'DCERT1\n1\n0 0 8\n1\n29 0 8 1 0 100\n0\n0\n0\n' > "$WORK/op.dcert"
refuse "an operator code the format does not define" "$WORK/op.dcert" "is not an operator"

refuse "a certificate that is not there at all" "$WORK/no-such-file.dcert" "cannot read"

# --- NON-CANONICAL RECORDS -------------------------------------------------
# These are files that PARSE as integers but that `renderCert` could never have
# written.  Accepting one lets the structural metric separate two records that
# denote the same design -- or, for a negative Nat payload, two records that
# `opOfCode` would push through `Int.toNat` to the SAME operator.  Review found
# the first three accepted; each is now a control with its own diagnostic.

printf 'DCERT1\n1\n0 -1 8\n0\n0\n0\n0\n' > "$WORK/negnat.dcert"
refuse "a Nat field written negative" "$WORK/negnat.dcert" "is a Nat"

printf 'DCERT1\n1\n0 0 8\n1\n7 99 8 1 0 100\n0\n0\n0\n' > "$WORK/stray.dcert"
refuse "a payload on an operator that carries none" "$WORK/stray.dcert" "carries no payload"

printf 'DCERT1\n1\n0 0 8\n1\n1 -1 8 1 0 100\n0\n0\n0\n' > "$WORK/negarg.dcert"
refuse "a negative payload on a Nat-typed operator" "$WORK/negarg.dcert" "is a Nat"

printf 'DCERT1\n1\n0 0 08\n0\n0\n0\n0\n' > "$WORK/lead.dcert"
refuse "a field with a leading zero" "$WORK/lead.dcert" "leading zero"

printf 'DCERT1\n1\n1 8 -0\n0\n0\n0\n0\n' > "$WORK/negzero.dcert"
refuse "a value spelled -0" "$WORK/negzero.dcert" "not canonical"

printf 'DCERT1\n1\n0 0 8\n0\n0\n1\n8 0 2 0 0 0 0 0\n0\n' > "$WORK/bit.dcert"
refuse "a boolean field that is not 0 or 1" "$WORK/bit.dcert" "boolean field"

printf 'DCERT1\n2\n0 0 8\n0 1 8\n0\n0\n1\n8 0 0 1 0 0 0 0\n0\n' > "$WORK/absent.dcert"
refuse "an absent pin whose slot is not the canonical 0" "$WORK/absent.dcert" "absent pin"

# A separator the writer does not emit: every field rule above could pass and
# this would still not be a file `renderCert` produced.  The byte-exact
# re-render is what closes that, and it is the only guard that catches this.
printf 'DCERT1\n1\n0  0 8\n0\n0\n0\n0\n' > "$WORK/space.dcert"
refuse "a record with a separator renderCert never writes" "$WORK/space.dcert" "not canonical DCERT1"

# --------------------------------------------------------------------------
if [[ "$fail" -eq 0 ]]; then
  echo "PASS: dcert_delta_controls"
  exit 0
fi
echo "FAIL: $fail control(s)"
exit 1

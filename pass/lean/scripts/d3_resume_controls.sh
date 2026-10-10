#!/usr/bin/env bash
# Controls for INPUT BINDING on d3_sweep.py's exploratory (--certs) path.
#
# WHAT THIS GUARDS. The exploratory path built every target with an EMPTY
# `sha256`, so a row recorded no binding to the certificate it measured. The
# resume test is `row["cert_sha256"] != target.sha256`, and with both empty
# that is equal -- so `--resume` reused a row for a certificate that had since
# changed. The manifest path has always hashed; this one now does too.
#
# Four legs on the ACTUAL CLI, on the smallest pilot design:
#   1. a fresh run records a non-empty certificate hash;
#   2. `--resume` with the certificate UNCHANGED reuses the row;
#   3. `--resume` with the certificate CHANGED re-runs it;
#   4. a malformed certificate does not reach `agree`.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SWEEP="$ROOT/pass/lean/scripts/d3_sweep.py"
SRC="${D3_CONTROL_CERT:-$ROOT/generated/master_port/d3_pilot/certs/tiny_sync_ram_Lgraph.lean}"

fails=0
ok()  { printf '  ok   %s\n' "$1"; }
bad() { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }
[[ -r "$SWEEP" ]] || { echo "FATAL: missing $SWEEP" >&2; exit 2; }
if [[ ! -s "$SRC" ]]; then
  echo "SKIP: no control certificate at $SRC -- unresolved prerequisite, not a pass" >&2
  exit 3
fi

W="$ROOT/generated/d3_resume_controls"
rm -rf "$W"; mkdir -p "$W/certs"
cp "$SRC" "$W/certs/"
CERT="$W/certs/$(basename "$SRC")"
sweep() { nice -n19 ionice -c3 timeout 1800 python3 -I "$SWEEP" --certs "$W/certs" \
            --out "$W/rows.tsv" --reifier legacy --jobs 1 --timeout 900 "$@"; }
field() { awk -F'\t' -v c="$1" 'NR==1{for(i=1;i<=NF;i++) if($i==c) k=i; next} {print $k; exit}' "$W/rows.tsv"; }

sweep --force > "$W/run1.log" 2>&1
h1="$(field cert_sha256)"
if [[ "$h1" =~ ^[0-9a-f]{64}$ ]]; then
  ok "a fresh exploratory row records the certificate hash"
else
  bad "cert_sha256 is '${h1:-<empty>}' -- the row does not bind its input"
fi
v1="$(field verdict)"
[[ "$v1" == "agree" ]] && ok "the control design reaches agree" \
                       || bad "control design verdict '$v1'; later legs are then vacuous"

sweep --resume > "$W/run2.log" 2>&1
reused="$(grep -oE '[0-9]+ reused' "$W/run2.log" | head -1 | cut -d' ' -f1)"
[[ "${reused:-0}" -ge 1 ]] && ok "--resume reuses the row when the certificate is unchanged" \
                           || bad "--resume did not reuse an unchanged row (reused=${reused:-?})"

# A CHANGED certificate. A trailing comment changes the bytes and nothing else,
# so a run that reuses the row is reusing it for a different input.
# A CHANGED certificate. A trailing comment changes the bytes and nothing else,
# so a run that reuses the row is reusing it for a different input.
#
# EITHER outcome is a correct refusal of stale reuse: the sweep may re-run the
# target, or refuse the whole resume because the selection digest moved. What
# must not happen is a silent reuse. (With the certificate hash populated the
# digest DOES move, so the observed behaviour is the refusal -- which is the
# stronger of the two. My first version of this leg demanded the re-run and
# called the refusal a failure.)
printf '\n-- control: certificate changed\n' >> "$CERT"
sweep --resume > "$W/run3.log" 2>&1
rc3=$?
reused3="$(grep -oE 'reusing [0-9]+ terminal' "$W/run3.log" | grep -oE '[0-9]+' | head -1)"
if grep -q 'REFUSED: configuration differs' "$W/run3.log" && [[ "$rc3" -ne 0 ]]; then
  ok "--resume REFUSES a changed certificate, naming the moved selection digest"
elif [[ "${reused3:-1}" -eq 0 ]]; then
  ok "--resume re-runs a changed certificate rather than reusing its row"
else
  bad "a changed certificate was REUSED (reused=${reused3:-?}, rc=$rc3)"
fi

# A malformed certificate must not reach agree.
printf 'this is not Lean\n' > "$CERT"
sweep --force > "$W/run4.log" 2>&1
v4="$(field verdict)"
[[ "$v4" != "agree" ]] && ok "a malformed certificate does not reach agree (verdict '$v4')" \
                       || bad "a malformed certificate reached agree"

if [[ $fails -eq 0 ]]; then echo "D3-RESUME CONTROLS OK"; else echo "D3-RESUME CONTROLS FAILED ($fails)"; fi
exit $fails

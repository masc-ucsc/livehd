#!/usr/bin/env bash
# Controls for `scripts/verify_d4_run.sh`.
#
# The defects these pin, all found by review:
#
#   1  THE LIVE-SCOPE QUERY FAILED OPEN.  It was
#      `systemctl ... 2>/dev/null | wc -l`, so when the query itself failed
#      ("Operation not permitted") the pipeline still produced 0 and the
#      verifier printed "no scopes left behind" and exited 0.  An unavailable
#      query is UNKNOWN, which is a verification FAILURE.
#   2  A nonzero emitter rc did not stop a row counting as coverage.
#   3  The retained artifacts' static gates were never run.
#
# The controls run the verifier against a real run directory and compare
# behaviour; they do not need that run to pass, only to react.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
VERIFY="$ROOT/scripts/verify_d4_run.sh"
LIST="$HERE/cva6_30.list"
RUN="${D4_VERIFY_RUN:-$ROOT/temp/cva6_30_auth}"

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }

[[ -x "$VERIFY" ]] || { echo "FATAL: no $VERIFY" >&2; exit 2; }
if [[ ! -s "$RUN/rows.tsv" ]]; then
  echo "SKIP: no run directory at $RUN (set D4_VERIFY_RUN=<dir>)"; exit 0
fi

mkdir -p "$ROOT/temp"
TMP="$(mktemp -d "$ROOT/temp/verify_ctl.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# --- baseline -------------------------------------------------------------
"$VERIFY" "$RUN" "$LIST" > "$TMP/base.txt" 2>&1
base_rc=$?
if grep -q 'live-scope query succeeded' "$TMP/base.txt"; then
  ok "baseline: the live-scope query ran and was reported as having succeeded"
else
  bad "baseline did not report a successful live-scope query: $(grep -i 'live-scope' "$TMP/base.txt" | head -1)"
fi

# --- 1. a FAILING scope query must fail verification ----------------------
mkdir -p "$TMP/bin"
cat > "$TMP/bin/systemctl" <<'EOF'
#!/usr/bin/env bash
echo "Failed to list units: Operation not permitted" >&2
exit 1
EOF
chmod +x "$TMP/bin/systemctl"
PATH="$TMP/bin:$PATH" "$VERIFY" "$RUN" "$LIST" > "$TMP/fake.txt" 2>&1
fake_rc=$?
if [[ "$fake_rc" -eq 0 ]]; then
  bad "verification PASSED while the live-scope query was failing"
elif ! grep -q 'live-scope query FAILED' "$TMP/fake.txt"; then
  bad "a failing live-scope query was not reported as a failure: $(grep -i 'live-scope' "$TMP/fake.txt" | head -1)"
elif grep -q 'no d4emit scopes left behind' "$TMP/fake.txt"; then
  bad "a failing query still claimed zero leftovers"
else
  ok "failing live-scope query: reported as a verification failure, never as zero leftovers"
fi

# --- 2. a nonzero emitter rc must be rejected -----------------------------
# Only meaningful when the run actually contains one; otherwise it is reported
# as not applicable rather than silently passing.
nz="$(for d in "$RUN"/blocks/*/; do
        r="$(grep -m1 '^emit_rc=' "$d/emit.cgroup" 2>/dev/null | cut -d= -f2)"
        [[ -n "$r" && "$r" != "0" ]] && basename "$d"
      done | wc -l)"
if [[ "$nz" -eq 0 ]]; then
  printf 'N/A   no nonzero emitter rc in %s; rc rejection not exercised here\n' "$RUN"
elif ! grep -q 'a nonzero emitter result is never accepted coverage' "$TMP/base.txt"; then
  bad "$nz block(s) have a nonzero emitter rc but the verifier did not reject them"
else
  got="$(grep -c 'a nonzero emitter result is never accepted coverage' "$TMP/base.txt")"
  [[ "$got" -eq "$nz" ]] && ok "all $nz nonzero-rc block(s) rejected from accepted coverage" \
                         || bad "$nz nonzero-rc blocks but $got rejections"
fi

# --- 2b. MISSING and MALFORMED emit_rc must both fail ---------------------
# The verifier checked only for a PRESENT NONZERO value, so deleting `emit_rc=0`
# from one record left that row counted as accepted coverage.
mirror() {  # mirror <dest>: rows, provenance and cgroup records only
  local d="$1" mm
  mkdir -p "$d/blocks"
  cp "$RUN/rows.tsv" "$RUN/provenance.txt" "$d/" 2>/dev/null
  while IFS=$'\t' read -r _c mm _rest; do
    [[ -s "$RUN/blocks/$mm/emit.cgroup" ]] || continue
    mkdir -p "$d/blocks/$mm"
    cp "$RUN/blocks/$mm/emit.cgroup" "$d/blocks/$mm/"
  done < <(tail -n +2 "$RUN/rows.tsv")
}
victim="$(awk -F'\t' 'NR>1 && $13=="RAN"{print $2; exit}' "$RUN/rows.tsv")"
if [[ -z "$victim" ]]; then
  bad "no RAN row to mutate; the emit_rc controls cannot run"
else
  base_acc="$(grep -m1 'ACCEPTED COVERAGE:' "$TMP/base.txt" | grep -oE '[0-9]+ of [0-9]+')"

  mirror "$TMP/m_missing"
  grep -v '^emit_rc=' "$RUN/blocks/$victim/emit.cgroup" > "$TMP/m_missing/blocks/$victim/emit.cgroup"
  "$VERIFY" "$TMP/m_missing" "$LIST" > "$TMP/missing.txt" 2>&1
  rc=$?
  acc="$(grep -m1 'ACCEPTED COVERAGE:' "$TMP/missing.txt" | grep -oE '[0-9]+ of [0-9]+')"
  if [[ "$rc" -eq 0 ]]; then
    bad "a MISSING emit_rc still passed verification"
  elif ! grep -q "$victim: no emit_rc recorded" "$TMP/missing.txt"; then
    bad "a missing emit_rc was not reported: $(grep -m1 "$victim" "$TMP/missing.txt")"
  elif [[ "$acc" == "$base_acc" ]]; then
    bad "a missing emit_rc did not reduce accepted coverage (still $acc)"
  else
    ok "missing emit_rc on '$victim': reported and excluded ($base_acc -> $acc)"
  fi

  mirror "$TMP/m_bad"
  sed 's/^emit_rc=.*/emit_rc=banana/' "$RUN/blocks/$victim/emit.cgroup" > "$TMP/m_bad/blocks/$victim/emit.cgroup"
  "$VERIFY" "$TMP/m_bad" "$LIST" > "$TMP/malformed.txt" 2>&1
  rc=$?
  acc="$(grep -m1 'ACCEPTED COVERAGE:' "$TMP/malformed.txt" | grep -oE '[0-9]+ of [0-9]+')"
  if [[ "$rc" -eq 0 ]]; then
    bad "a MALFORMED emit_rc still passed verification"
  elif ! grep -q "is not an integer" "$TMP/malformed.txt"; then
    bad "a malformed emit_rc was not reported: $(grep -m1 "$victim" "$TMP/malformed.txt")"
  elif [[ "$acc" == "$base_acc" ]]; then
    bad "a malformed emit_rc did not reduce accepted coverage (still $acc)"
  else
    ok "malformed emit_rc on '$victim': reported and excluded ($base_acc -> $acc)"
  fi
fi

# --- 2c. the per-artifact differential half must not be claimed as run ----
if grep -q 'retained artifacts pass vc_gates.py and cert_lgraph_diff.py$' "$TMP/base.txt"; then
  bad "an invariant-only invocation is reported as a cert_lgraph_diff pass"
elif ! grep -qE 'INVARIANT half|DIFFERENTIAL half' "$TMP/base.txt"; then
  bad "the verifier does not say which half of cert_lgraph_diff ran"
else
  ok "the verifier distinguishes the invariant half from the differential half"
fi

# --- 3. the static gates must actually run --------------------------------
if ! grep -qE 'retained artifacts pass vc_gates|REJECTED by the static gates' "$TMP/base.txt"; then
  bad "the verifier did not run the retained-artifact static gates at all"
else
  ok "retained-artifact gates are run and their verdict is reported"
fi

# --- 4. accepted coverage is reported apart from raw RAN ------------------
if ! grep -q 'ACCEPTED COVERAGE:' "$TMP/base.txt" || ! grep -q 'RAW RUNTIME OBSERVATIONS:' "$TMP/base.txt"; then
  bad "the verifier does not separate raw runtime observations from accepted coverage"
else
  raw="$(grep -m1 'RAW RUNTIME OBSERVATIONS:' "$TMP/base.txt" | grep -oE '[0-9]+ of [0-9]+' | head -1)"
  acc="$(grep -m1 'ACCEPTED COVERAGE:' "$TMP/base.txt" | grep -oE '[0-9]+ of [0-9]+' | head -1)"
  ok "raw observations ($raw) reported apart from accepted coverage ($acc)"
fi

# --- 5. truncated diagnostics are not called exact ------------------------
if grep -qi 'exact diagnostics' "$TMP/base.txt"; then
  bad "the verifier still calls the truncated diagnostic column 'exact'"
else
  ok "the truncated diagnostic column is labelled as truncated, not exact"
fi

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1

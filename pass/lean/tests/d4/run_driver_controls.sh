#!/usr/bin/env bash
# Controls for the three REUSABLE result drivers, invoked as the real
# entrypoints with real arguments.
#
# WHY.  These three decide whether a corpus run is acceptable, so each one's
# EXIT STATUS is its product. Review found all three exiting 0 on inputs they
# were supposed to reject: a candidate that failed a semantic gate, a cgroup
# verdict outside the supported states, a 19-field runtime row reported as "18
# on every row" because `dict(zip(...))` silently drops the extra, and a gate
# row deleted for a formerly unaccepted block quietly becoming "ran-ungated".
# A driver that cannot fail is not evidence, so every case below requires a
# NONZERO exit, and the positive cases require zero.
#
# Nothing here re-runs a corpus. The fixtures are tiny and synthetic except the
# positive legs, which read the committed R2 tables.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
GATES="$HERE/coreet_run_gates.sh"
CGROUP="$HERE/coreet_run_cgroup_records.sh"
RECON="$HERE/coreet_run_reconcile.py"

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }
for f in "$GATES" "$CGROUP" "$RECON"; do
  [[ -r "$f" ]] || { echo "FATAL: missing $f" >&2; exit 2; }
done

mkdir -p "$ROOT/temp"
W="$(mktemp -d "$ROOT/temp/drvctl.XXXXXX")"
trap 'rm -rf "$W"' EXIT

R2_ROWS="$HERE/coreet_122_r2_rows.tsv"
R2_GATES="$HERE/coreet_122_r2_gates.tsv"
B_ROWS="$HERE/coreet_122_fresh_rows.tsv"
B_GATES="$HERE/coreet_122_fresh_gates.tsv"
LIST="$HERE/coreet_122.list"

# ---------------------------------------------------------------------------
# A synthetic one-block run directory. `kind` picks what is wrong with it.
# ---------------------------------------------------------------------------
mk_run() { # mk_run <dir> <emit> <parse> <compiles> <runst> <diag> <cert_sha>
  local d="$1" emit="$2" parse="$3" comp="$4" runst="$5" diag="$6" sha="$7"
  mkdir -p "$d/blocks/m1/lean" "$d/blocks/m1/lgdb_norm"
  printf 'corpus\tmodule\temit_status\tcert_path\tcert_sha256\tsources\tnodes\tflops\tmemories\tparse_status\tparse_ms\tcompiles_ok\truntime_status\trun_ms\tdigest\tdiagnostic\twall_s\tpeak_rss_kb\n' > "$d/rows.tsv"
  printf 'coreet\tm1\t%s\t%s\t%s\t1\t1\t0\t0\t%s\t1\t%s\t%s\t1\t0\t%s\t1\t1\n' \
    "$emit" "$d/blocks/m1/lean/m1.dcert" "$sha" "$parse" "$comp" "$runst" "$diag" >> "$d/rows.tsv"
}
mk_cgroup() { # mk_cgroup <dir> <verdict> <cr> <ts> <oom_kill> <ogk> <rc> <cleanup>
  printf 'counters_readable=%s\nterminal_snapshot=%s\noom_kill=%s\noom_group_kill=%s\nmemory_peak_bytes=1\nmemory_max_kb=8388608\nsystemd_result=success\nemit_rc=%s\nemit_peak_rss_kb=1\nverdict=%s\ncleanup=%s\n' \
    "$3" "$4" "$5" "$6" "$7" "$2" "$8" > "$1/blocks/m1/emit.cgroup"
}

run_rc() { "$@" >"$W/out.log" 2>&1; echo $?; }

expect_nonzero() { # expect_nonzero <label> <needle> <cmd...>
  local label="$1" needle="$2"; shift 2
  local rc; rc="$(run_rc "$@")"
  if [[ "$rc" -eq 0 ]]; then
    bad "$label: driver exited 0"
  elif ! grep -q "$needle" "$W/out.log"; then
    bad "$label: nonzero but without the expected diagnostic -- $(grep -m1 'FAILED\|!' "$W/out.log")"
  else ok "$label (exit $rc)"; fi
}
expect_zero() { # expect_zero <label> <cmd...>
  local label="$1"; shift
  local rc; rc="$(run_rc "$@")"
  [[ "$rc" -eq 0 ]] && ok "$label (exit 0)" || bad "$label: exited $rc -- $(tail -n2 "$W/out.log" | tr '\n' ' ')"
}

# ===== 1. GATES: a candidate that fails a semantic gate ====================
d="$W/g_fail"; mk_run "$d" EMITTED PARSED true RAN - deadbeef
printf 'not a DesignCert\n' > "$d/blocks/m1/lean/m1_Lgraph.lean"
printf 'x\n' > "$d/blocks/m1/lean/m1.dcert"
expect_nonzero "GATES: a candidate that fails cgen/a semantic gate is REJECTED" \
               "GATES FAILED" "$GATES" "$d" "$W/g_fail.tsv"
[[ -s "$W/g_fail.tsv" ]] && ok "GATES: the per-block table is retained on failure" \
                         || bad "GATES: the table was not written on failure"

# ===== 2. GATES: no candidates at all ======================================
d="$W/g_empty"; mk_run "$d" EMIT_FAILED NOT_EMITTED - NOT_RUN "front-end" -
expect_nonzero "GATES: an EMPTY candidate set is REJECTED (0 of 0 is not a pass)" \
               "no candidate RAN rows" "$GATES" "$d" "$W/g_empty.tsv"

# ===== 3. CGROUP: an unsupported verdict ===================================
d="$W/c_unknown"; mk_run "$d" EMITTED PARSED true RAN - deadbeef
mk_cgroup "$d" "UNKNOWN no counters" 0 0 0 0 0 clean
expect_nonzero "CGROUP: verdict=UNKNOWN is REJECTED" \
               "outside the supported OK/LIMIT_HIT states" "$CGROUP" "$d" "$W/c_unknown.tsv" 1

# ===== 4. CGROUP: an OK that carries a kill counter ========================
d="$W/c_okkill"; mk_run "$d" EMITTED PARSED true RAN - deadbeef
mk_cgroup "$d" "OK oom_kill=1" 1 1 1 0 0 clean
expect_nonzero "CGROUP: an OK verdict carrying a kill counter is REJECTED" \
               "counters that are missing, non-terminal, or non-zero" "$CGROUP" "$d" "$W/c_okkill.tsv" 1

# ===== 5. CGROUP: a LIMIT_HIT whose runtime row still RAN ==================
d="$W/c_limitran"; mk_run "$d" EMITTED PARSED true RAN - deadbeef
mk_cgroup "$d" "LIMIT_HIT" 1 1 1 0 137 clean
expect_nonzero "CGROUP: a LIMIT_HIT whose runtime row still RAN is REJECTED" \
               "disagree with their runtime row" "$CGROUP" "$d" "$W/c_limitran.tsv" 1

# ===== 6. CGROUP: a LIMIT_HIT with no positive kernel counter ==============
d="$W/c_limit0"; mk_run "$d" NO_CERT NOT_EMITTED - NOT_RUN "resource-limit: claimed" -
mk_cgroup "$d" "LIMIT_HIT" 1 1 0 0 137 clean
expect_nonzero "CGROUP: a LIMIT_HIT with zero kernel counters is REJECTED" \
               "no positive kernel counter" "$CGROUP" "$d" "$W/c_limit0.tsv" 1

# ===== 7. RECONCILE: a 19-field runtime row ================================
head -1 "$R2_ROWS" > "$W/rows19.tsv"
awk -F'\t' 'NR>1{print $0"\tEXTRA"}' "$R2_ROWS" >> "$W/rows19.tsv"
expect_nonzero "RECONCILE: a 19-field runtime row is REJECTED (not silently truncated)" \
               "fields against a 18-field header" \
               python3 "$RECON" "$W/rows19.tsv" "$R2_GATES" "$B_ROWS" "$B_GATES" "$LIST" "$W/r19.txt"

# ===== 8. RECONCILE: a gate row MISSING for a formerly unaccepted block ====
grep -v $'^minion_tlb\t' "$R2_GATES" > "$W/gates_missing.tsv"
expect_nonzero "RECONCILE: a MISSING gate row for a candidate is REJECTED" \
               "have no gate row" \
               python3 "$RECON" "$R2_ROWS" "$W/gates_missing.tsv" "$B_ROWS" "$B_GATES" "$LIST" "$W/rmiss.txt"
grep -q 'ran-ungated' "$W/rmiss.txt" 2>/dev/null \
  && bad "RECONCILE: the missing gate row still degraded to a block-level 'ran-ungated' outcome" \
  || ok "RECONCILE: a missing gate row does not degrade to a block-level outcome"

# ===== 9. RECONCILE: duplicate / omitted gate substitution =================
awk -F'\t' -v OFS='\t' '/^#/{print;next} $1=="module"{print;next}
  $1=="intpipe_alu"{alu=$0; print; next} $1=="minion_tlb"{print alu; next} {print}' \
  "$R2_GATES" > "$W/gates_dup.tsv"
expect_nonzero "RECONCILE: a duplicate/omitted gate substitution is REJECTED" \
               "duplicate gate row" \
               python3 "$RECON" "$R2_ROWS" "$W/gates_dup.tsv" "$B_ROWS" "$B_GATES" "$LIST" "$W/rdup.txt"

# ===== 10. RECONCILE: reordered gate identity ==============================
awk -F'\t' '/^#/{print;next} $1=="module"{print;next} $1!="vpu_trans"{print}' "$R2_GATES" > "$W/gates_ord.tsv"
awk -F'\t' '$1=="vpu_trans"' "$R2_GATES" >> "$W/gates_ord.tsv"
expect_nonzero "RECONCILE: a REORDERED gate table is REJECTED" \
               "not identical to the runtime rows" \
               python3 "$RECON" "$R2_ROWS" "$W/gates_ord.tsv" "$B_ROWS" "$B_GATES" "$LIST" "$W/rord.txt"

# ===== 11. RECONCILE: an UNKNOWN gate identity =============================
awk -F'\t' -v OFS='\t' '/^#/{print;next} $1=="module"{print;next}
  {if ($1=="vpu_trans") $1="vpu_trans_v2"; print}' "$R2_GATES" > "$W/gates_unk.tsv"
expect_nonzero "RECONCILE: an UNKNOWN gate module is REJECTED" \
               "absent from the runtime rows" \
               python3 "$RECON" "$R2_ROWS" "$W/gates_unk.tsv" "$B_ROWS" "$B_GATES" "$LIST" "$W/runk.txt"

# ===== 12. CGROUP: a MALFORMED kill counter on an OK record ================
# `oom_kill=not-an-integer` is not "not positive", it is NO EVIDENCE. A bare
# positivity test reads it as zero and lets the OK through.
d="$W/c_badok"; mk_run "$d" EMITTED PARSED true RAN - deadbeef
mk_cgroup "$d" "OK" 1 1 "not-an-integer" 0 0 clean
expect_nonzero "CGROUP: a malformed kill counter on an OK record is REJECTED" \
               "not a canonical nonnegative integer" "$CGROUP" "$d" "$W/c_badok.tsv" 1

# ===== 13. CGROUP: a MALFORMED kill counter on a LIMIT_HIT record ==========
# "at least one positive" must not be decided from a field that was never read.
d="$W/c_badlimit"; mk_run "$d" NO_CERT NOT_EMITTED - NOT_RUN "resource-limit: claimed" -
mk_cgroup "$d" "LIMIT_HIT" 1 1 2 "-1" 137 clean
expect_nonzero "CGROUP: a malformed kill counter on a LIMIT_HIT record is REJECTED" \
               "not a canonical nonnegative integer" "$CGROUP" "$d" "$W/c_badlimit.tsv" 1

# ===== 14. RECONCILE: the cert_hash_matches_row COLUMN deleted =============
# Not a row: the whole authentication column. The outcome function used to
# default a missing value to "yes", so the table still reconciled and still
# reported its coverage.
awk -F'\t' '/^#/{print;next}{ out=""; for(i=1;i<=NF;i++) if(i!=10) out = out (out==""?"":"\t") $i; print out }' \
  "$R2_GATES" > "$W/gates_nocol.tsv"
expect_nonzero "RECONCILE: deleting the cert_hash_matches_row COLUMN is REJECTED" \
               "missing the required column 'cert_hash_matches_row'" \
               python3 "$RECON" "$R2_ROWS" "$W/gates_nocol.tsv" "$B_ROWS" "$B_GATES" "$LIST" "$W/rcol.txt"
grep -q 'ACCEPTED COVERAGE' "$W/rcol.txt" 2>/dev/null \
  && bad "RECONCILE: a coverage figure was still printed over an unauthenticated gate table" \
  || ok "RECONCILE: no coverage figure is printed when the gate schema is incomplete"

# ===== 15. RECONCILE: a BASELINE row substituted ===========================
# 122 rows of 18 fields preserved: one ungated row replaced by a duplicate of
# another. The dropped module then read as "absent" and fabricated a twelfth
# transition.
awk -F'\t' -v OFS='\t' 'NR==1{print;next} $2=="core_top"{ct=$0; print; next}
  $2=="intpipe_csr_file"{print ct; next} {print}' "$B_ROWS" > "$W/brows_dup.tsv"
rows=$(( $(wc -l < "$W/brows_dup.tsv") - 1 ))
[[ "$rows" -eq 122 ]] || bad "the baseline substitution fixture has $rows rows, not 122; it would not test identity"
expect_nonzero "RECONCILE: a duplicate/omitted BASELINE row is REJECTED" \
               "baseline rows: duplicate module row" \
               python3 "$RECON" "$R2_ROWS" "$R2_GATES" "$W/brows_dup.tsv" "$B_GATES" "$LIST" "$W/bdup.txt"
grep -q 'blocks that moved' "$W/bdup.txt" 2>/dev/null \
  && bad "RECONCILE: transitions were still computed over a baseline that does not reconcile" \
  || ok "RECONCILE: no transition is computed when the baseline identity fails"

# ===== POSITIVE 12-14: valid fixtures and the committed R2 tables ==========
d="$W/c_good"; mk_run "$d" EMITTED PARSED true RAN - deadbeef
mk_cgroup "$d" "OK oom_kill=0" 1 1 0 0 0 clean
expect_zero "CGROUP: a well-formed OK record is ACCEPTED" "$CGROUP" "$d" "$W/c_good.tsv" 1

d="$W/c_good_limit"; mk_run "$d" NO_CERT NOT_EMITTED - NOT_RUN "resource-limit: cgroup memory.events reports a kill" -
mk_cgroup "$d" "LIMIT_HIT" 1 1 2 0 137 clean
expect_zero "CGROUP: a well-formed LIMIT_HIT record is ACCEPTED" "$CGROUP" "$d" "$W/c_gl.tsv" 1

expect_zero "RECONCILE: the committed R2 tables still reconcile" \
            python3 "$RECON" "$R2_ROWS" "$R2_GATES" "$B_ROWS" "$B_GATES" "$LIST" "$W/r2.txt"
# ...and the genuine result is unchanged by the hardening.
grep -q 'ACCEPTED COVERAGE      93 of 122' "$W/r2.txt" \
  && ok "RECONCILE: the R2 result is still 93 of 122" \
  || bad "RECONCILE: the R2 coverage figure moved -- $(grep -m1 'ACCEPTED COVERAGE' "$W/r2.txt")"
# Check the REPORTED COUNTS, not a raw arrow tally: the memory ten are listed a
# second time in their own section, so an arrow count conflates the two.
miss=""
for want in "blocks that moved      11" \
            "newly ACCEPTED         11" \
            "no longer ACCEPTED     0" \
            "of the ten, ACCEPTED   10"; do
  grep -q "$want" "$W/r2.txt" || miss="$miss; $want"
done
[[ -z "$miss" ]] \
  && ok "RECONCILE: the same eleven named gains, ten of them the memory designs, nothing regressed" \
  || bad "RECONCILE: the transition summary moved -- missing$miss"

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1

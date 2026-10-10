#!/usr/bin/env bash
# Controls for the D4 CGROUP COUNTER DOMAIN, across every path that reads one.
#
# THE DEFECT THESE PIN.  A cgroup kill counter is an UNBOUNDED decimal written
# by the kernel, but Bash's `[[ -gt ]]` / `[[ -ne ]]` are fixed width.
# `18446744073709551616` (2^64) is canonical, nonzero, and evaluates to EXACTLY
# ZERO in Bash arithmetic -- `$((18446744073709551616))` prints 0. Every
# positivity test built on `-gt 0` therefore read that value as "no kill", so a
# huge positive counter could be lost taking a maximum at collection,
# classified OK at source, accepted by the reusable result driver, and accepted
# by final verification. Four places, one domain, one policy.
#
# THE POLICY (scripts/d4_scope_verdict.sh): validate the canonical-decimal
# SYNTAX first, then decide zero/nonzero TEXTUALLY, and compare magnitudes by
# length then lexicographic order. No counter is handed to Bash arithmetic.
# Ordinary loop and tally arithmetic is untouched.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
POLICY="$ROOT/scripts/d4_scope_verdict.sh"
CGROUP="$HERE/coreet_run_cgroup_records.sh"

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }
[[ -r "$POLICY" ]] || { echo "FATAL: no $POLICY" >&2; exit 2; }
# shellcheck source=../../../../scripts/d4_scope_verdict.sh
. "$POLICY" || { echo "FATAL: cannot source $POLICY" >&2; exit 2; }

mkdir -p "$ROOT/temp"
W="$(mktemp -d "$ROOT/temp/ctrdom.XXXXXX")"
trap 'rm -rf "$W"' EXIT

BIG=18446744073709551616          # 2^64: canonical, nonzero, wraps to 0 in Bash
BIGGER=18446744073709551617
HUGE=99999999999999999999999999999999

# ---- the arithmetic trap itself, stated as a fact -------------------------
[[ "$(( BIG ))" -eq 0 ]] \
  && ok "Bash arithmetic really does wrap 2^64 to zero (the defect is real)" \
  || bad "2^64 no longer wraps in Bash arithmetic; these controls need rechecking"

# ---- 1. the policy primitives --------------------------------------------
pol=0
d4_counter_is_canonical "$BIG"   || { bad "policy: 2^64 is not accepted as canonical"; pol=1; }
d4_counter_is_nonzero   "$BIG"   || { bad "policy: 2^64 is not TEXTUALLY nonzero"; pol=1; }
d4_counter_is_zero      "$BIG"   && { bad "policy: 2^64 was called zero"; pol=1; }
d4_counter_is_zero      "0"      || { bad "policy: \"0\" is not zero"; pol=1; }
d4_counter_is_nonzero   "0"      && { bad "policy: \"0\" was called nonzero"; pol=1; }
for s in "" " " "-1" "+1" "007" "1 " "0x10" "1e3" "not-an-integer" "１"; do
  d4_counter_is_canonical "$s" && { bad "policy: accepted non-canonical ${s@Q}"; pol=1; }
done
d4_counter_gt "$BIGGER" "$BIG" || { bad "policy: 2^64+1 not greater than 2^64"; pol=1; }
d4_counter_gt "$BIG" "$BIGGER" && { bad "policy: 2^64 reported greater than 2^64+1"; pol=1; }
d4_counter_gt "$HUGE" "$BIG"   || { bad "policy: a longer decimal is not greater"; pol=1; }
d4_counter_gt "9" "10"         && { bad "policy: 9 reported greater than 10 (lexicographic without length)"; pol=1; }
d4_counter_gt "10" "9"         || { bad "policy: 10 not greater than 9"; pol=1; }
d4_counter_gt "5" "5"          && { bad "policy: 5 reported greater than itself"; pol=1; }
d4_counter_gt "x" "1"; [[ $? -eq 2 ]] || { bad "policy: a malformed operand did not return the distinct 'not well formed' status"; pol=1; }
[[ "$pol" -eq 0 ]] && ok "policy primitives: canonical syntax, textual zero, unbounded magnitude"

# ---- 2. SOURCE verdict: 2^64 must be a LIMIT_HIT --------------------------
v="$(d4_scope_verdict "$BIG" 0 0 success 1 1)"
[[ "$v" == LIMIT_HIT* ]] \
  && ok "d4_scope_verdict: oom_kill=2^64 is LIMIT_HIT at source" \
  || bad "d4_scope_verdict: oom_kill=2^64 gave '${v%% *}', not LIMIT_HIT"
v="$(d4_scope_verdict 0 "$BIG" 0 success 1 1)"
[[ "$v" == LIMIT_HIT* ]] \
  && ok "d4_scope_verdict: oom_group_kill=2^64 is LIMIT_HIT at source" \
  || bad "d4_scope_verdict: oom_group_kill=2^64 gave '${v%% *}'"
v="$(d4_scope_verdict 0 0 0 success 1 1)"
[[ "$v" == OK* ]] || bad "d4_scope_verdict: a terminal zero reading is no longer OK"
v="$(d4_scope_verdict "007" 0 0 success 1 1)"
[[ "$v" == UNKNOWN* ]] \
  && ok "d4_scope_verdict: a non-canonical counter is UNKNOWN, not zero and not a kill" \
  || bad "d4_scope_verdict: '007' gave '${v%% *}', expected UNKNOWN"

# ---- 3. MERGE must not lose a 2^64 snapshot -------------------------------
printf 'source=in-scope\nphase=exit\noom_kill=%s\noom_group_kill=0\nmemory_peak_bytes=5\n' "$BIG" > "$W/s1"
printf 'source=sampler\nphase=sample\noom_kill=3\noom_group_kill=0\nmemory_peak_bytes=%s\n' "$HUGE" > "$W/s2"
read -r mk mg mp have term _ < <(scope_merge "$W/s1" "$W/s2")
[[ "$mk" == "$BIG" ]] \
  && ok "scope_merge: a 2^64 kill snapshot survives the maximum (not replaced by 3)" \
  || bad "scope_merge: kept oom_kill=$mk, losing the 2^64 snapshot"
[[ "$mp" == "$HUGE" ]] \
  && ok "scope_merge: an unbounded memory_peak survives the maximum" \
  || bad "scope_merge: kept memory_peak=$mp"
[[ "$have" == "1" && "$term" == "1" ]] || bad "scope_merge: lost have/terminal while merging"
# ...and the merged value must still reach a LIMIT_HIT at source.
v="$(d4_scope_verdict "$mk" "$mg" 0 success "$have" "$term")"
[[ "$v" == LIMIT_HIT* ]] \
  && ok "scope_merge -> d4_scope_verdict: the merged 2^64 still declares a LIMIT_HIT" \
  || bad "scope_merge -> d4_scope_verdict: gave '${v%% *}'"

# ---- 4. the RESULT DRIVER, real entrypoint --------------------------------
mk_run() { # mk_run <dir> <runst> <diag>
  mkdir -p "$1/blocks/m1"
  printf 'corpus\tmodule\temit_status\tcert_path\tcert_sha256\tsources\tnodes\tflops\tmemories\tparse_status\tparse_ms\tcompiles_ok\truntime_status\trun_ms\tdigest\tdiagnostic\twall_s\tpeak_rss_kb\n' > "$1/rows.tsv"
  if [[ "$2" == RAN ]]; then
    printf 'coreet\tm1\tEMITTED\tp\tdeadbeef\t1\t1\t0\t0\tPARSED\t1\ttrue\tRAN\t1\t0\t%s\t1\t1\n' "$3" >> "$1/rows.tsv"
  else
    printf 'coreet\tm1\tNO_CERT\t-\t-\t-\t-\t-\t-\tNOT_EMITTED\t-\t-\tNOT_RUN\t-\t-\t%s\t1\t1\n' "$3" >> "$1/rows.tsv"
  fi
}
mk_cg() { # mk_cg <dir> <verdict> <oom_kill> <ogk> <rc>
  printf 'counters_readable=1\nterminal_snapshot=1\noom_kill=%s\noom_group_kill=%s\nmemory_peak_bytes=1\nsystemd_result=success\nemit_rc=%s\nemit_peak_rss_kb=1\nverdict=%s\ncleanup=clean\n' \
    "$3" "$4" "$5" "$2" > "$1/blocks/m1/emit.cgroup"
}
drv() { "$CGROUP" "$1" "$2" "${3:-1}" >"$W/d.log" 2>&1; echo $?; }

d="$W/ok_big"; mk_run "$d" RAN -; mk_cg "$d" OK "$BIG" 0 0
rc="$(drv "$d" "$W/ok_big.tsv")"
if [[ "$rc" -eq 0 ]]; then bad "DRIVER: an OK record with oom_kill=2^64 was ACCEPTED"
elif ! grep -q 'non-zero' "$W/d.log"; then bad "DRIVER: rejected the 2^64 OK for another reason -- $(grep -m1 FAILED "$W/d.log")"
else ok "DRIVER: an OK record with oom_kill=2^64 is REJECTED (exit $rc)"; fi

d="$W/lim_big"; mk_run "$d" NOT_RUN "resource-limit: cgroup memory.events reports a kill"
mk_cg "$d" LIMIT_HIT "$BIG" 0 137
rc="$(drv "$d" "$W/lim_big.tsv")"
[[ "$rc" -eq 0 ]] \
  && ok "DRIVER: a LIMIT_HIT with a 2^64 counter on a non-running resource-limit row is ACCEPTED (exit 0)" \
  || bad "DRIVER: the 2^64 LIMIT_HIT was rejected -- $(grep -m1 FAILED "$W/d.log")"

d="$W/lim_zero"; mk_run "$d" NOT_RUN "resource-limit: claimed"; mk_cg "$d" LIMIT_HIT 0 0 137
rc="$(drv "$d" "$W/lim_zero.tsv")"
[[ "$rc" -ne 0 ]] \
  && ok "DRIVER: a LIMIT_HIT with both counters zero is still REJECTED" \
  || bad "DRIVER: a zero-counter LIMIT_HIT was accepted"

# ---- 5. the genuine retained evidence still passes ------------------------
RUN="${D4_R2_RUN:-$ROOT/temp/coreet_122_run_49cf}"
if [[ -s "$RUN/rows.tsv" ]]; then
  rc="$(drv "$RUN" "$W/r2.tsv" "$(( $(wc -l < "$RUN/rows.tsv") - 1 ))")"
  if [[ "$rc" -ne 0 ]]; then
    bad "DRIVER: the genuine R2 evidence no longer passes -- $(grep -m1 FAILED "$W/d.log")"
  elif ! diff -q <(grep -v '^#' "$W/r2.tsv") <(grep -v '^#' "$HERE/coreet_122_r2_cgroup_records.tsv") >/dev/null; then
    bad "DRIVER: the R2 record rows changed under the new counter policy"
  else
    ok "DRIVER: the genuine R2 evidence still passes, rows byte-identical to the committed table"
  fi
else
  printf 'N/A   the retained R2 run is not present; the genuine-evidence leg was SKIPPED\n'
fi

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1

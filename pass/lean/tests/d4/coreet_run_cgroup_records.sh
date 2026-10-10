#!/usr/bin/env bash
# One complete kernel/emitter record per block of a D4 runtime sweep, plus the
# assertions that make a verdict mean something.
#
# THE RULE THIS ENFORCES.  `memory.events` `oom_kill` / `oom_group_kill` are the
# ONLY authoritative limit-hit signal.  systemd's `Result` and the child's exit
# code are recorded as diagnostics and never decide a limit hit -- measured 5/5,
# an OOM-killed emission reports `rc=137 Result=success`.  So:
#
#   * a LIMIT_HIT verdict MUST rest on a positive kernel counter;
#   * an OK verdict MUST have readable counters AND a TERMINAL (post-command)
#     snapshot.  An entry-only zero is not evidence that nothing was killed
#     later, and authorising OK from it is fail-open;
#   * every block MUST have an explicit integer emitter status and a clean
#     scope teardown, or its row cannot be reconciled at all.
#
# Usage: coreet_run_cgroup_records.sh <run-outdir> <records.tsv> [declared-count]
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
# ONE counter policy for the whole of D4, from the sourceable file the sweep
# itself uses: canonical-decimal syntax, TEXTUAL zero/nonzero, length-then-
# lexicographic magnitude. Bash arithmetic wraps 2^64 to zero, so a positivity
# test on a raw counter would read a huge kill count as a clean run.
# shellcheck source=../../../../scripts/d4_scope_verdict.sh
. "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)/scripts/d4_scope_verdict.sh" \
  || { echo "FATAL: cannot source the shared D4 counter policy" >&2; exit 2; }

RUN="${1:?usage: coreet_run_cgroup_records.sh <run-outdir> <records.tsv> [declared]}"
OUT="${2:?missing output records.tsv}"
DECL="${3:-0}"

[[ -s "$RUN/rows.tsv" ]] || { echo "FATAL: no rows at $RUN/rows.tsv" >&2; exit 2; }
val() { grep -m1 "^$2=" "$1" 2>/dev/null | cut -d= -f2-; }

tmp="$(mktemp "$ROOT/temp/cgrec.XXXXXX")"
printf 'module\tverdict\tcounters_readable\tterminal_snapshot\toom_kill\toom_group_kill\tmemory_peak_bytes\temit_peak_rss_kb\tsystemd_result\temit_rc\tcleanup\n' > "$tmp"

n=0; missing=0; bad_ok=0; bad_limit=0; dirty=0; no_rc=0; bad_verdict=0; bad_limit_row=0; dup=0
bad_counters=0
seen=""
while IFS=$'\t' read -r corpus m emit f4 f5 f6 f7 f8 f9 parse f11 compiles runst f14 f15 diag rest; do
  [[ "$corpus" == "corpus" || "$corpus" == \#* || -z "$m" ]] && continue
  n=$((n + 1))
  # IDENTITY, not a count. Two records for one module, or a count that happens
  # to match while the names do not, is not "one record per block".
  case " $seen " in *" $m "*) dup=$((dup + 1)) ;; esac
  seen="$seen $m"
  f="$RUN/blocks/$m/emit.cgroup"
  if [[ ! -s "$f" ]]; then
    missing=$((missing + 1))
    printf '%s\tNO_RECORD\t0\t0\t-\t-\t-\t-\t-\t-\t-\n' "$m" >> "$tmp"
    continue
  fi
  v="$(val "$f" verdict)"; v="${v%% *}"
  cr="$(val "$f" counters_readable)"; ts="$(val "$f" terminal_snapshot)"
  ok="$(val "$f" oom_kill)";  ogk="$(val "$f" oom_group_kill)"
  pk="$(val "$f" memory_peak_bytes)"; rss="$(val "$f" emit_peak_rss_kb)"
  sr="$(val "$f" systemd_result)";    rc_v="$(val "$f" emit_rc)"; cl="$(val "$f" cleanup)"
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$m" "${v:--}" "${cr:-0}" "${ts:-0}" "${ok:--}" "${ogk:--}" "${pk:--}" "${rss:--}" "${sr:--}" "${rc_v:--}" "${cl:--}" >> "$tmp"

  [[ "$rc_v" =~ ^-?[0-9]+$ ]] || no_rc=$((no_rc + 1))
  [[ "$cl" == "clean" ]]      || dirty=$((dirty + 1))
  # A COUNTER MUST BE AUTHENTICATED, NOT MERELY TESTED FOR POSITIVITY.
  # `oom_kill=not-an-integer` is not "not positive", it is NO EVIDENCE: a
  # positivity test alone reads it as zero and lets an OK through. Canonical
  # nonnegative decimal only -- no sign, no leading zeros, no whitespace, no
  # empty -- so nothing arithmetic-ambiguous can stand in for a kill count.
  nat() { d4_counter_is_canonical "${1-}"; }
  pos() { d4_counter_is_nonzero  "${1-}"; }
  case "$v" in
    OK)
      # Readable counters AND a terminal snapshot, or the zero proves nothing --
      # an entry-only zero cannot say a kill did not happen later.
      [[ "$cr" == "1" && "$ts" == "1" ]] || bad_ok=$((bad_ok + 1))
      # BOTH kill counters must be well formed AND exactly zero. An OK that
      # carries a kill is a contradiction; an OK whose counter cannot be read
      # is an unverified block, not a clean one.
      if ! nat "$ok" || ! nat "$ogk"; then
        bad_counters=$((bad_counters + 1))
      elif ! d4_counter_is_zero "$ok" || ! d4_counter_is_zero "$ogk"; then
        bad_ok=$((bad_ok + 1))
      fi
      ;;
    LIMIT_HIT)
      # Must rest on a POSITIVE kernel counter, never on rc or Result -- an
      # OOM-killed emission reports rc=137 Result=success, measured 5/5. Both
      # fields must be well formed first, or "at least one positive" is being
      # decided from a value that was never read.
      if ! nat "$ok" || ! nat "$ogk"; then
        bad_counters=$((bad_counters + 1))
      elif ! pos "$ok" && ! pos "$ogk"; then
        bad_limit=$((bad_limit + 1))
      fi
      # ...and the runtime row must AGREE: a limit hit that still produced a
      # RAN row would mean the kill did not actually stop the measurement.
      if [[ "$runst" == "RAN" || "$diag" != resource-limit:* ]]; then
        bad_limit_row=$((bad_limit_row + 1))
      fi
      ;;
    *)
      # UNKNOWN, NO_VERDICT, empty, or anything a future change invents: there
      # is no rule here that says what it means, so it cannot be reconciled.
      # Falling through silently is how an unverified block gets counted.
      bad_verdict=$((bad_verdict + 1))
      ;;
  esac
done < "$RUN/rows.tsv"

{
  echo "#"
  echo "# records                       $n"
  echo "# duplicate module identities   $dup"
  echo "# blocks with NO record         $missing"
  echo "# unsupported verdict states    $bad_verdict"
  echo "# malformed kill counters        $bad_counters"
  echo "# OK verdicts lacking readable terminal counters or carrying a kill   $bad_ok"
  echo "# LIMIT_HIT verdicts without a positive kernel counter   $bad_limit"
  echo "# LIMIT_HIT verdicts whose runtime row is not a non-running resource-limit   $bad_limit_row"
  echo "# non-integer emit_rc           $no_rc"
  echo "# non-clean scope teardown      $dirty"
} >> "$tmp"
mv -f "$tmp" "$OUT"
echo "wrote $OUT"
tail -n 10 "$OUT"

rc=0
fail() { echo "CGROUP RECORDS FAILED: $*"; rc=1; }
[[ "$missing"       -eq 0 ]] || fail "$missing block(s) have no cgroup/emitter record"
[[ "$dup"           -eq 0 ]] || fail "$dup duplicate module identit(ies) in the runtime rows"
[[ "$bad_verdict"   -eq 0 ]] || fail "$bad_verdict record(s) carry a verdict outside the supported OK/LIMIT_HIT states"
[[ "$bad_counters"  -eq 0 ]] || fail "$bad_counters record(s) have a kill counter that is not a canonical nonnegative integer; a counter that cannot be read is evidence failure, not zero"
[[ "$bad_ok"        -eq 0 ]] || fail "$bad_ok OK verdict(s) rest on counters that are missing, non-terminal, or non-zero"
[[ "$bad_limit"     -eq 0 ]] || fail "$bad_limit LIMIT_HIT verdict(s) have no positive kernel counter"
[[ "$bad_limit_row" -eq 0 ]] || fail "$bad_limit_row LIMIT_HIT verdict(s) disagree with their runtime row"
[[ "$no_rc"         -eq 0 ]] || fail "$no_rc block(s) have no explicit integer emitter status"
[[ "$dirty"         -eq 0 ]] || fail "$dirty block(s) left a scope behind"
[[ "$n" -gt 0 ]]             || fail "no runtime rows at all; nothing was checked"
[[ "$DECL" -eq 0 || "$n" -eq "$DECL" ]] || fail "$n records for $DECL declared modules"
if [[ "$rc" -ne 0 ]]; then
  echo "(the per-block table above is retained at $OUT)"
  exit "$rc"
fi
[[ "$rc" -eq 0 ]] && echo "CGROUP RECORDS OK: $n complete records, every verdict backed by its own evidence"
exit $rc

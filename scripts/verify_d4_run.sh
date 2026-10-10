#!/usr/bin/env bash
# READ-ONLY reconciliation of one sweep run against its declared module list,
# its persisted cgroup records, and the certificates on disk.
#
# Everything here is RECOMPUTED.  Nothing is quoted from the run's own summary:
# a run that miscounted itself would otherwise be audited by its own mistake.
# This touches no harness file, regenerates nothing and runs no design.
#
#   scripts/verify_d4_run.sh <run-dir> <declared-module-list>
set -uo pipefail
RUN="${1:?usage: verify_d4_run.sh <run-dir> <declared-module-list>}"
LIST="${2:?missing declared module list}"
ROWS="$RUN/rows.tsv"
PROV="$RUN/provenance.txt"
ROOT_REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The SHARED D4 counter policy: canonical-decimal syntax, TEXTUAL zero/nonzero,
# length-then-lexicographic magnitude. Bash arithmetic is fixed width and wraps
# 2^64 to zero, so a counter must never reach `-gt` / `-eq`.
# shellcheck source=d4_scope_verdict.sh
. "$ROOT_REPO/scripts/d4_scope_verdict.sh" \
  || { echo "FATAL: cannot source the shared D4 counter policy" >&2; exit 2; }

fails=0
ok()   { printf '  OK    %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; fails=$((fails + 1)); }
head2() { printf '\n== %s ==\n' "$1"; }

[[ -s "$ROWS" ]] || { echo "FATAL: no rows at $ROWS" >&2; exit 2; }
[[ -s "$LIST" ]] || { echo "FATAL: no module list at $LIST" >&2; exit 2; }

# ---------------------------------------------------------------- 1. shape
head2 "1. ROW CONTRACT AND DECLARED ORDER (recomputed)"
mapfile -t DECLARED < <(grep -v '^[[:space:]]*#' "$LIST" | grep -v '^[[:space:]]*$' | sed 's/[[:space:]]*$//')
mapfile -t ROWMODS  < <(awk -F'\t' 'NR>1{print $2}' "$ROWS")
n_dec="${#DECLARED[@]}"; n_row="${#ROWMODS[@]}"
n_uniq="$(printf '%s\n' "${ROWMODS[@]}" | sort -u | wc -l)"
bad_shape="$(awk -F'\t' 'NR>1 && NF!=18' "$ROWS" | wc -l)"

[[ "$n_row" -eq "$n_dec" ]] && ok "row count $n_row equals declared count $n_dec" \
                            || bad "row count $n_row != declared count $n_dec"
[[ "$n_uniq" -eq "$n_row" ]] && ok "all $n_uniq module names unique" \
                             || bad "$n_row rows but only $n_uniq unique names"
[[ "$bad_shape" -eq 0 ]] && ok "every data row has exactly 18 fields" \
                         || bad "$bad_shape rows do not have 18 fields"
# EXACT SEQUENCE equality, not set equality: order is part of the contract.
if [[ "$(printf '%s\n' "${DECLARED[@]}")" == "$(printf '%s\n' "${ROWMODS[@]}")" ]]; then
  ok "rows are in exact declared-list order, no extra or missing names"
else
  bad "rows do not match the declared list in order"
  diff <(printf '%s\n' "${DECLARED[@]}") <(printf '%s\n' "${ROWMODS[@]}") | head -10 | sed 's/^/        /'
fi
hdr_nf="$(head -1 "$ROWS" | awk -F'\t' '{print NF}')"
[[ "$hdr_nf" == "18" ]] && ok "header carries 18 columns" || bad "header has $hdr_nf columns"

# ---------------------------------------------------------- 2. provenance
head2 "2. RUN PROVENANCE"
# Two kinds of line, labelled so they cannot be confused:
#   RECORDED    read from the run's persisted provenance, not re-derived
#   RECOMPUTED  the named artifact was re-hashed from disk and compared
pf() { grep -m1 "^$1=" "$PROV" 2>/dev/null | cut -d= -f2-; }
want() { local l="$1" g="$2" w="$3"; [[ "$g" == "$w" ]] && ok "RECORDED   $l = $g" || bad "RECORDED   $l = '${g:-<absent>}', expected '$w'"; }
want "d4_tracked_dirty"   "$(pf d4_tracked_dirty)"   "no"

# WHICH CORPUS THIS RUN DECLARES, AND ONLY ITS BINDINGS.
#
# These checks were written against a CVA6 run and applied unconditionally, so
# a CORE-ET run failed four of them for having no `cva6_*` fields at all, and
# -- worse -- a CVA6 run was the only thing whose corpus binding was ever
# verified. The corpus comes from the run's own provenance; an absent or
# unknown value is fatal, because a run that will not say what it measured
# cannot have its inputs re-derived.
CORPUS_RUN="$(pf corpus)"
case "$CORPUS_RUN" in
  coreet|cva6) ok "RECORDED   corpus = $CORPUS_RUN" ;;
  selftest)    bad "RECORDED   corpus = selftest: these rows exercise the DRIVER and are not corpus evidence" ;;
  "")          bad "RECORDED   corpus is absent; the run does not say what it measured" ;;
  *)           bad "RECORDED   corpus = '$CORPUS_RUN' is not a corpus this verifier knows" ;;
esac

if [[ "$CORPUS_RUN" == "cva6" ]]; then
  # The pinned revision stays the expectation. `D4_EXPECT_CVA6_REV` exists so a
  # deliberately different source state can be verified, and the override is
  # PRINTED, so a run checked against another revision says so in its own log.
  EXPECT_CVA6_REV="${D4_EXPECT_CVA6_REV:-6cb200105fb9441d170e45786125a737fab98e91}"
  [[ "$EXPECT_CVA6_REV" == "6cb200105fb9441d170e45786125a737fab98e91" ]] \
    || printf '  NOTE  the expected CVA6 revision was overridden to %s\n' "$EXPECT_CVA6_REV"
  want "cva6_tracked_clean" "$(pf cva6_tracked_clean)" "yes"
  want "cva6_root_rev"      "$(pf cva6_root_rev)"      "$EXPECT_CVA6_REV"
elif [[ "$CORPUS_RUN" == "coreet" ]]; then
  # The CORE-ET binding, checked with the same force: the revision and tech the
  # run names, the cleanliness of exactly the paths the filelist flow reads,
  # and the derived sources -- which are RE-DERIVED below, not quoted.
  want "coreet_relevant_inputs_clean" "$(pf coreet_relevant_inputs_clean)" "yes"
  want "coreet_outside_scope_tracked" "$(pf coreet_outside_scope_tracked)" "0"
  want "coreet_derived_count"         "$(pf coreet_derived_count)"         "4"
  # THE CANONICAL TECH. `generic` and `xilinx` pick different `prim_clk_gate`
  # sources and BOTH succeed, so nothing downstream notices the difference.
  # A run on the other tech is a run on a different corpus and is refused
  # here rather than recorded as an equivalent one.
  want "coreet_tech"                  "$(pf coreet_tech)"                  "generic"
  for k in coreet_root_canonical coreet_rev coreet_patch_script_sha256 \
           coreet_derived_dir coreet_derived_map_sha256 coreet_derived_aggregate_sha256; do
    v="$(pf "$k")"; [[ -n "$v" ]] && ok "RECORDED   $k = $v" || bad "RECORDED   $k is absent"
  done

  # EVERY ONE OF THOSE IS RECOMPUTED BELOW.
  #
  # Presence was all this checked, so each field could carry any value and
  # pass: a wrong `coreet_rev`, a wrong `coreet_patch_script_sha256` and a
  # wrong `coreet_derived_map_sha256` each exited 0 and printed VERIFICATION
  # PASSED. Measured against this CLI. A recorded identity that is never
  # re-derived is a label, not a binding.
  croot_v="$(pf coreet_root_canonical)"; crev_v="$(pf coreet_rev)"
  if [[ -z "$croot_v" ]]; then
    bad "RECOMPUTED CORE-ET revision: no root recorded"
  elif [[ ! -d "$croot_v/.git" ]]; then
    bad "RECOMPUTED CORE-ET revision: $croot_v is not a git checkout, so the recorded revision cannot be checked"
  else
    rnow="$(git -C "$croot_v" rev-parse HEAD 2>/dev/null)"
    [[ "$rnow" == "$crev_v" ]] \
      && ok "RECOMPUTED CORE-ET revision matches $rnow" \
      || bad "RECOMPUTED CORE-ET revision DIFFERS: recorded '${crev_v:-<absent>}', checkout at '${rnow:-<unreadable>}'"
    # The cleanliness the preflight asserted, re-asked of the SAME paths it
    # scoped: `hw dv mk`, tracked AND untracked, and nothing outside them.
    dirty="$(git -C "$croot_v" status --porcelain -- hw dv mk 2>/dev/null | wc -l)"
    [[ "$dirty" == "0" ]] \
      && ok "RECOMPUTED CORE-ET hw/dv/mk clean" \
      || bad "RECOMPUTED CORE-ET hw/dv/mk has $dirty change(s); the recorded run claimed clean"
  fi
fi
want "jobs_requested"     "$(pf jobs_requested)"     "1"
want "execution"          "$(pf execution)"          "serial"
want "stack_limit_kb"     "$(pf stack_limit_kb)"     "8192"
want "emit_timeout_s"     "$(pf emit_timeout_s)"     "3600"
want "run_timeout_s"      "$(pf run_timeout_s)"      "1800"
want "emit_max_rss_kb"    "$(pf emit_max_rss_kb)"    "8388608"
want "reconciled"         "$(pf reconciled)"         "YES"
for k in d4_head lake lake_version; do
  v="$(pf "$k")"; [[ -n "$v" ]] && ok "RECORDED   $k = $v" || bad "RECORDED   $k is absent"
done

# rehash <label> <recorded-hash> <file> <fatal-on-mismatch>
rehash() {
  local l="$1" rec="$2" f="$3" fatal="$4" now
  if [[ -z "$rec" ]]; then bad "RECOMPUTED $l: no hash recorded"; return; fi
  if [[ ! -r "$f" ]]; then bad "RECOMPUTED $l: artifact absent at $f, so the recorded hash cannot be checked"; return; fi
  now="$(sha256sum "$f" | cut -d' ' -f1)"
  if [[ "$now" == "$rec" ]]; then ok "RECOMPUTED $l matches $now"
  elif [[ "$fatal" == "note" ]]; then
    printf '  NOTE  RECOMPUTED %s DIFFERS: recorded %s, on disk %s\n' "$l" "$rec" "$now"
    printf '        The artifact has changed since the run, so this run cannot be\n'
    printf '        re-derived from the tree as it stands.  The run itself is unaffected.\n'
  else bad "RECOMPUTED $l DIFFERS: recorded $rec, on disk $now"; fi
}
rehash "module list"     "$(pf module_list_sha256)"            "$LIST" fatal
# The patcher that produced the derived sources. Called HERE, not up in the
# provenance block: `rehash` is defined below that point, and a function used
# before its definition is a "command not found" that `set -uo pipefail`
# prints and walks past -- so the check silently did nothing and a tampered
# hash passed.
if [[ "$CORPUS_RUN" == "coreet" ]]; then
  rehash "CORE-ET patcher" "$(pf coreet_patch_script_sha256)" \
         "$ROOT_REPO/scripts/coreet_patch_srcs.py" fatal
fi
if [[ "$CORPUS_RUN" == "cva6" ]]; then
  rehash "foundation"      "$(pf cva6_foundation_sha256)"        "$(pf cva6_foundation)" fatal
  rehash "filelist"        "$(pf cva6_filelist_sha256)"          "$(pf cva6_filelist)" fatal
  rehash "config package"  "$(pf cva6_config_pkg_sha256)"        "$(pf cva6_config_pkg)" fatal
fi
# The lhd binary is legitimately rebuilt between runs; a difference is reported
# loudly but does not retroactively invalidate rows already produced.
rehash "lhd binary"      "$(pf lhd_sha256)"                    "$(pf lhd)" note
# The declared wrapper corpus hash is re-derived the way the sweep derives it.
wrec="$(pf cva6_wrappers_declared_sha256)"; wdir="$(pf cva6_wrap_dir)"
if [[ "$CORPUS_RUN" == "coreet" ]]; then
  # THE DERIVED SOURCES, RE-DERIVED. The run recorded an aggregate over the
  # four outputs; this regenerates them from the recorded CORE-ET root with
  # the recorded generator, into fresh staging, and recomputes the aggregate
  # the same way. Quoting the recorded value back would audit the run by its
  # own arithmetic. Read-only: staging is a temporary directory and the
  # consumed one is never written.
  cdir="$(pf coreet_derived_dir)"; crec="$(pf coreet_derived_aggregate_sha256)"
  croot="$(pf coreet_root_canonical)"; cpatch="$ROOT_REPO/scripts/coreet_patch_srcs.py"
  if [[ -z "$crec" || -z "$croot" || ! -r "$cpatch" ]]; then
    bad "RECOMPUTED derived CORE-ET sources: cannot re-derive (aggregate '${crec:-<absent>}', root '${croot:-<absent>}')"
  elif [[ ! -d "$croot" ]]; then
    bad "RECOMPUTED derived CORE-ET sources: the recorded root $croot is not present, so the aggregate cannot be checked"
  else
    cstage="$(mktemp -d "$ROOT_REPO/temp/d4verify.XXXXXX")"
    if COREET_ROOT="$croot" COREET_PATCHED_OUTDIR="$cstage" python3 -B "$cpatch" >/dev/null 2>&1; then
      cnow="$(for f in intpipe_mul_div_ctl.sv txfmactl_top.sv txfmaexp_top.sv vpu_defs_pkg.sv; do
                printf '%s %s\n' "$f" "$(sha256sum < "$cstage/$f" | cut -d' ' -f1)"; done \
              | sha256sum | cut -d' ' -f1)"
      [[ "$cnow" == "$crec" ]] \
        && ok "RECOMPUTED derived CORE-ET sources match $cnow" \
        || bad "RECOMPUTED derived CORE-ET sources DIFFER: recorded $crec, re-derived $cnow"
      # THE MAP, AND THE ASSOCIATIONS IN IT.
      #
      # The four-file aggregate says the OUTPUTS are this generator's. It says
      # nothing about which SOURCE each came from -- that is `map.tsv`, and a
      # recorded `coreet_derived_map_sha256` that is never re-derived is a
      # label. Measured: a wrong one exited 0 and printed VERIFICATION PASSED.
      # Column 2 is the ABSOLUTE output path, so a staging derivation's map
      # can never be byte-equal to the consumed one. Two checks, each asking
      # what it can actually answer:
      #   * the recorded hash against the CONSUMED map -- has it changed since
      #     the run;
      #   * the ASSOCIATIONS, source path to output BASENAME, consumed against
      #     freshly derived -- is each output still the one this generator
      #     makes from that source.
      mrec="$(pf coreet_derived_map_sha256)"
      if [[ -z "$mrec" ]]; then
        bad "RECOMPUTED derived map: no hash recorded"
      elif [[ ! -r "$cdir/map.tsv" ]]; then
        bad "RECOMPUTED consumed map absent at $cdir/map.tsv"
      else
        mdisk="$(sha256sum < "$cdir/map.tsv" | cut -d' ' -f1)"
        [[ "$mdisk" == "$mrec" ]] \
          && ok "RECOMPUTED consumed map matches $mdisk" \
          || bad "RECOMPUTED consumed map DIFFERS: recorded $mrec, on disk $mdisk"
        assoc() { awk -F'\t' '{n=split($2,a,"/"); print $1 "\t" a[n]}' "$1" | sort; }
        if diff -q <(assoc "$cdir/map.tsv") <(assoc "$cstage/map.tsv") >/dev/null 2>&1; then
          ok "RECOMPUTED source/output associations match a fresh derivation"
        else
          bad "RECOMPUTED source/output associations DIFFER from a fresh derivation: an output is not the one this generator makes from that source"
          diff <(assoc "$cdir/map.tsv") <(assoc "$cstage/map.tsv") | head -6 | sed 's/^/        /'
        fi
      fi
      # The directory the run CONSUMED must still hold those same bytes.
      if [[ -d "$cdir" ]]; then
        cdisk="$(for f in intpipe_mul_div_ctl.sv txfmactl_top.sv txfmaexp_top.sv vpu_defs_pkg.sv; do
                   printf '%s %s\n' "$f" "$(sha256sum < "$cdir/$f" 2>/dev/null | cut -d' ' -f1)"; done \
                 | sha256sum | cut -d' ' -f1)"
        [[ "$cdisk" == "$crec" ]] \
          && ok "RECOMPUTED consumed derived directory still matches $cdisk" \
          || bad "RECOMPUTED consumed derived directory DIFFERS: recorded $crec, on disk $cdisk"
      else
        bad "RECOMPUTED consumed derived directory absent at '${cdir:-<absent>}'"
      fi
    else
      bad "RECOMPUTED derived CORE-ET sources: the recorded generator failed against $croot"
    fi
    rm -rf "$cstage"
  fi
elif [[ -n "$wrec" && -d "$wdir" ]]; then
  wnow="$(for dm in "${DECLARED[@]}"; do
            f="$wdir/${dm}_gate.sv"
            if [[ -r "$f" ]]; then echo "$dm $(sha256sum < "$f" | cut -d' ' -f1)"; else echo "$dm -"; fi
          done | sha256sum | cut -d' ' -f1)"
  [[ "$wnow" == "$wrec" ]] && ok "RECOMPUTED declared wrapper corpus matches $wnow" \
                           || bad "RECOMPUTED declared wrapper corpus DIFFERS: recorded $wrec, now $wnow"
else
  bad "RECOMPUTED declared wrapper corpus: cannot re-derive (hash '${wrec:-<absent>}', dir '${wdir:-<absent>}')"
fi

# ------------------------------------------- 3. per-module cgroup records
head2 "3. CGROUP EVIDENCE, ONE RECORD PER MODULE (recomputed from the records)"
n_rec=0; n_okrec=0; n_limit=0; n_infra=0
while IFS=$'\t' read -r _c m emit _cp sha _s _n _f _mm parse _pm compok rt _rm digest diag _w _p; do
  f="$RUN/blocks/$m/emit.cgroup"
  if [[ ! -s "$f" ]]; then bad "$m: no persisted cgroup record"; n_infra=$((n_infra+1)); continue; fi
  n_rec=$((n_rec+1))
  g() { grep -m1 "^$1=" "$f" | cut -d= -f2-; }
  verdict="$(g verdict | awk '{print $1}')"
  creadable="$(g counters_readable)"; term="$(g terminal_snapshot)"
  okk="$(g oom_kill)"; ogk="$(g oom_group_kill)"; clean="$(g cleanup)"
  # Malformed counters are an evidence failure, never a zero -- and the test is
  # the shared D4 policy, not Bash arithmetic: `-gt 0` wraps 2^64 to zero.
  if ! d4_counter_is_canonical "$okk" || ! d4_counter_is_canonical "$ogk"; then
    bad "$m: malformed counters in the record (oom_kill='$okk' oom_group_kill='$ogk')"; n_infra=$((n_infra+1)); continue
  fi
  if [[ "$clean" != "clean" ]]; then
    bad "$m: scope did not clean up ($clean)"; n_infra=$((n_infra+1)); continue
  fi
  case "$verdict" in
    OK)
      if [[ "$creadable" != "1" || "$term" != "1" ]]; then
        bad "$m: verdict OK without well-formed TERMINAL counters (readable=$creadable terminal=$term)"; n_infra=$((n_infra+1))
      elif ! d4_counter_is_zero "$okk" || ! d4_counter_is_zero "$ogk"; then
        bad "$m: verdict OK but the record shows a kill (oom_kill=$okk oom_group_kill=$ogk)"; n_infra=$((n_infra+1))
      elif [[ "$diag" == resource-limit:* ]]; then
        bad "$m: verdict OK but the row claims a resource limit"; n_infra=$((n_infra+1))
      else n_okrec=$((n_okrec+1)); fi ;;
    LIMIT_HIT)
      if d4_counter_is_zero "$okk" && d4_counter_is_zero "$ogk"; then
        bad "$m: LIMIT_HIT with no positive kernel counter"; n_infra=$((n_infra+1))
      elif [[ "$rt" == "RAN" || "$digest" != "-" ]]; then
        bad "$m: a certificate was run or counted after a positive limit hit"; n_infra=$((n_infra+1))
      else n_limit=$((n_limit+1)); fi ;;
    UNKNOWN|"")
      bad "$m: verdict '$verdict' -- infrastructure failure, not coverage"; n_infra=$((n_infra+1)) ;;
    *) bad "$m: unrecognised verdict '$verdict'"; n_infra=$((n_infra+1)) ;;
  esac
done < <(tail -n +2 "$ROWS")
[[ "$n_rec" -eq "$n_row" ]] && ok "$n_rec/$n_row modules have exactly one persisted record" \
                            || bad "$n_rec records for $n_row modules"
[[ "$n_infra" -eq 0 ]] && ok "no infrastructure failures: $n_okrec authoritative clean, $n_limit limit hits" \
                       || bad "$n_infra infrastructure failures"
# THE LIVE-SCOPE QUERY MUST NOT FAIL OPEN.
# This was `systemctl ... 2>/dev/null | wc -l`: with the query failing
# ("Operation not permitted") the pipeline still printed 0 and the check
# reported "no scopes left behind".  An unavailable query is UNKNOWN, which is a
# verification failure -- it is not proof of zero leftovers.
# ...AND IT MUST ASK ABOUT **THIS** RUN'S SCOPES.
#
# The pattern was every `d4emit-*` unit on the machine, so a DIFFERENT sweep
# running concurrently made this fail -- reported as leftovers from the run
# being verified, which is a false accusation about the wrong run. Each unit
# name ends in the sweep's own pid, and every record names its unit, so the
# question is asked about exactly those. A run whose records name no unit at
# all is still refused: that is missing evidence, not a clean result.
mapfile -t RUN_UNITS < <(grep -h '^unit=' "$RUN"/blocks/*/emit.cgroup 2>/dev/null \
                         | cut -d= -f2- | sort -u)
scope_out="$(systemctl --user list-units --all --no-legend 'd4emit-*' 2>&1)"
scope_rc=$?
if [[ "$scope_rc" -ne 0 ]]; then
  bad "live-scope query FAILED (rc=$scope_rc): $(head -1 <<<"$scope_out") -- cannot show that no scope was left behind"
elif [[ "${#RUN_UNITS[@]}" -eq 0 ]]; then
  bad "no emit.cgroup record names a scope unit, so this run's scopes cannot be asked about"
else
  mine=""
  for u in "${RUN_UNITS[@]}"; do
    line="$(grep -F -- "$u" <<<"$scope_out" | head -1)"
    [[ -n "$line" ]] && mine+=" $line"
  done
  others="$(( $(grep -c . <<<"${scope_out//[[:space:]]/}") ))"
  if [[ -n "${mine//[[:space:]]/}" ]]; then
    bad "scopes from THIS run left behind:$(head -c 300 <<<"$mine")"
  else
    ok "live-scope query succeeded; none of this run's ${#RUN_UNITS[@]} scope unit(s) is still loaded"
    [[ -n "${scope_out//[[:space:]]/}" ]] \
      && printf '  NOTE  other d4emit scopes are live on this machine (another sweep); not this run\n'
  fi
fi

# -------------------------------------- 4. certificates, hashes, outcomes
head2 "4. CERTIFICATES RECOMPUTED, AND OUTCOME TOTALS"
n_cert=0; n_hash_ok=0; n_hash_bad=0; n_missing=0
while IFS=$'\t' read -r _c m _e cert sha _rest; do
  [[ "$cert" == "-" ]] && continue
  n_cert=$((n_cert+1))
  if [[ ! -s "$cert" ]]; then bad "$m: recorded certificate is missing at $cert"; n_missing=$((n_missing+1)); continue; fi
  now="$(sha256sum "$cert" | cut -d' ' -f1)"
  if [[ "$now" == "$sha" ]]; then n_hash_ok=$((n_hash_ok+1))
  else bad "$m: certificate SHA-256 differs (recorded $sha, on disk $now)"; n_hash_bad=$((n_hash_bad+1)); fi
done < <(tail -n +2 "$ROWS")
[[ "$n_hash_bad" -eq 0 && "$n_missing" -eq 0 ]] \
  && ok "all $n_hash_ok/$n_cert retained certificates recomputed to their recorded SHA-256" \
  || bad "$n_hash_bad hash mismatches, $n_missing missing, of $n_cert"

head2 "4b. EMITTER RESULT AND RETAINED-ARTIFACT GATES"
# A row is ACCEPTED COVERAGE only if the emitter returned 0 AND the retained
# artifact passes both static gates.  Clean cgroup counters prove only that
# MemoryMax did not kill the emission; they do not make a nonzero emitter result
# a success, and they say nothing about the certificate's contents.
GATE_VC="$ROOT_REPO/pass/lean/scripts/vc_gates.py"
GATE_DIFF="$ROOT_REPO/pass/lean/scripts/cert_lgraph_diff.py"
[[ -r "$GATE_VC" ]]   || bad "missing $GATE_VC"
[[ -r "$GATE_DIFF" ]] || bad "missing $GATE_DIFF"
n_rc_bad=0; n_gate_bad=0; n_gate_ok=0; n_nolean=0; n_inv=0
declare -A REJECTED=()
while IFS=$'\t' read -r _c m _e cert _sha _s _n _f _mm _p _pm _co rt _rm _dg _d _w _pk; do
  rec="$RUN/blocks/$m/emit.cgroup"
  # ACCEPTED COVERAGE REQUIRES AN EXPLICIT, WELL-FORMED ZERO.
  # This read `[[ -n "$erc" && "$erc" != "0" ]]`, so an ABSENT field passed
  # silently: deleting `emit_rc=0` from one record left the row counted as
  # accepted.  Absence and malformation are evidence failures, not successes.
  if ! grep -q '^emit_rc=' "$rec" 2>/dev/null; then
    bad "$m: no emit_rc recorded; the emitter's result is unknown, which is not accepted coverage"
    n_rc_bad=$((n_rc_bad+1)); REJECTED["$m"]="emit_rc=<absent>"
  else
    erc="$(grep -m1 '^emit_rc=' "$rec" | cut -d= -f2-)"
    if ! [[ "$erc" =~ ^-?[0-9]+$ ]]; then
      bad "$m: emit_rc='$erc' is not an integer; malformed evidence is not accepted coverage"
      n_rc_bad=$((n_rc_bad+1)); REJECTED["$m"]="emit_rc=malformed"
    # TEXTUAL ZERO, NOT ARITHMETIC ZERO.
    #
    # `[[ "$erc" -ne 0 ]]` evaluates in Bash arithmetic, which is 64-bit and
    # WRAPS: `emit_rc=18446744073709551616` is 2^64, compares equal to 0, and
    # was accepted as a clean emitter result. Measured against this CLI. The
    # only string the writer ever emits for success is the one character `0`,
    # so that is what is required -- no leading zeros, no sign, no width.
    elif [[ "$erc" != "0" ]]; then
      bad "$m: emitter returned rc=$erc; runtime status is '$rt' -- a nonzero emitter result is never accepted coverage"
      n_rc_bad=$((n_rc_bad+1)); REJECTED["$m"]="emit_rc=$erc"
    fi
  fi
  [[ "$cert" == "-" ]] && continue
  lean="${cert%.dcert}_Lgraph.lean"
  if [[ ! -s "$lean" ]]; then
    bad "$m: no retained _Lgraph.lean beside the certificate, so the static gates cannot be run"
    n_nolean=$((n_nolean+1)); REJECTED["$m"]="${REJECTED[$m]:+${REJECTED[$m]}, }no-lean"; continue
  fi
  top="$(basename "${cert%.dcert}")"
  g1="$(python3 "$GATE_VC" "$lean" "$top" --quiet 2>&1)";   r1=$?
  # WITHOUT a cgen artifact this is the INVARIANT half only -- the tool says so
  # itself ("no --verilog given; the differential half did NOT run").  A Verilog
  # file bound to this module makes it the full gate; otherwise the result is
  # labelled invariant-only and never reported as a differential pass.
  vfile=""
  [[ -n "${D4_VERILOG_DIR:-}" && -s "${D4_VERILOG_DIR}/${m}.v" ]] && vfile="${D4_VERILOG_DIR}/${m}.v"
  if [[ -n "$vfile" ]]; then
    g2="$(python3 "$GATE_DIFF" "$lean" --verilog "$vfile" --require-verilog --quiet 2>&1)"; r2=$?
    gkind="differential"
  else
    g2="$(python3 "$GATE_DIFF" "$lean" --quiet 2>&1)"; r2=$?
    gkind="invariant-only"
    n_inv=$((n_inv+1))
  fi
  if [[ "$r1" -eq 0 && "$r2" -eq 0 ]]; then
    n_gate_ok=$((n_gate_ok+1))
  else
    local_reason="$(grep -h '^FAIL:' <<<"$g1"$'\n'"$g2" | head -2 | tr '\n' ';' )"
    bad "$m: retained artifact REJECTED by the static gates -- ${local_reason:-vc_gates rc=$r1, cert_lgraph_diff rc=$r2}"
    n_gate_bad=$((n_gate_bad+1)); REJECTED["$m"]="${REJECTED[$m]:+${REJECTED[$m]}, }gates"
  fi
done < <(tail -n +2 "$ROWS")
[[ "$n_rc_bad" -eq 0 ]] && ok "every block's emitter returned 0" \
                        || bad "$n_rc_bad block(s) with a nonzero emitter rc"
if [[ "$n_gate_bad" -eq 0 && "$n_nolean" -eq 0 ]]; then
  ok "all $n_gate_ok retained artifacts pass vc_gates.py and the cert_lgraph_diff INVARIANT half"
else
  bad "$n_gate_bad gate-rejected, $n_nolean without a retained .lean, of $((n_gate_ok+n_gate_bad+n_nolean))"
fi
# WHICH HALF RAN IS REPORTED UNCONDITIONALLY.  An invariant-only invocation
# exits 0 and must never be read as a differential pass, whatever else failed.
n_full=$((n_gate_ok + n_gate_bad - n_inv))
printf '  NOTE  cert_lgraph_diff: DIFFERENTIAL half ran for %s artifact(s), INVARIANT half only for %s.\n' \
  "$n_full" "$n_inv"
[[ "$n_inv" -gt 0 ]] && printf '        Set D4_VERILOG_DIR=<dir with <module>.v> to bind an independent cgen\n        artifact and make it the full gate for those.\n'
true

# Every RAN row must carry a certificate, a hash and a digest; nothing else may.
while IFS=$'\t' read -r _c m _e cert sha _s _n _f _mm parse _pm compok rt _rm digest _d _w _p; do
  if [[ "$rt" == "RAN" ]]; then
    [[ "$parse" == "PARSED" && "$compok" == "true" ]] || bad "$m: RAN without PARSED/compiles_ok=true ($parse/$compok)"
    [[ "$cert" != "-" && "$sha" != "-" ]] || bad "$m: RAN with no retained certificate or hash"
    [[ "$digest" != "-" ]] || bad "$m: RAN with no digest"
  else
    [[ "$digest" == "-" ]] || bad "$m: not RAN yet carries digest $digest"
  fi
done < <(tail -n +2 "$ROWS")

printf '\n  EMISSION STATUS (recomputed over all %s declared)\n' "$n_row"
awk -F'\t' 'NR>1{c[$3]++} END{for(k in c) printf "    %-14s %s\n", k, c[k]}' "$ROWS" | sort
printf '\n  RUNTIME STATUS (recomputed over all %s declared)\n' "$n_row"
awk -F'\t' 'NR>1{c[$13]++} END{for(k in c) printf "    %-14s %s\n", k, c[k]}' "$ROWS" | sort
printf '\n  DIAGNOSTIC CLASS (recomputed)\n'
awk -F'\t' 'NR>1{
  if ($16 ~ /^resource-limit:/) k="resource-limit";
  else if ($16 ~ /^resource-evidence-unavailable:/) k="evidence-unavailable";
  else if ($16 ~ /emission timeout/) k="emission-timeout";
  else if ($16 ~ /runtime timeout/) k="runtime-timeout";
  else if ($16 == "-") k="(none)";
  else k="front-end-or-other";
  c[k]++ } END{for(k in c) printf "    %-22s %s\n", k, c[k]}' "$ROWS" | sort

n_ran="$(awk -F'\t' 'NR>1 && $10=="PARSED" && $12=="true" && $13=="RAN"' "$ROWS" | wc -l)"
n_rej="${#REJECTED[@]}"
n_acc=0
while IFS=$'\t' read -r _c m _e _ce _sh _s _n _f _mm p _pm co rt _rest; do
  [[ "$p" == "PARSED" && "$co" == "true" && "$rt" == "RAN" ]] || continue
  [[ -n "${REJECTED[$m]:-}" ]] && continue
  n_acc=$((n_acc+1))
done < <(tail -n +2 "$ROWS")
printf '\n  RAW RUNTIME OBSERVATIONS: %s of %s reached PARSED / compiles_ok=true / RAN\n' "$n_ran" "$n_row"
printf '  ACCEPTED COVERAGE:        %s of %s (RAN, emitter rc explicitly 0, vc_gates clean,\n' "$n_acc" "$n_row"
printf '                            and cert_lgraph_diff clean at the half that was run)\n'
if [[ "$n_rej" -gt 0 ]]; then
  printf '  REJECTED despite running:\n'
  for k in "${!REJECTED[@]}"; do printf '    %-26s %s\n' "$k" "${REJECTED[$k]}"; done
fi
[[ "$n_acc" -eq "$n_row" ]] || printf '  NOT all %s are accepted coverage; the remainder are classified above.\n' "$n_row"

# The row's diagnostic column is CUT TO 120 CHARACTERS by the sweep, so it is a
# truncated summary, not the compiler's exact message.  Saying "exact" here
# would be false; the full text lives in the block's own logs.
printf '\n  NON-RAN ROWS (diagnostic column, truncated to 120 chars by the sweep):\n'
awk -F'\t' 'NR>1 && !($10=="PARSED" && $12=="true" && $13=="RAN"){
  printf "    %-26s %-11s %-12s %-9s %s\n", $2, $3, $10, $13, $16 }' "$ROWS"

printf '\n'
[[ "$fails" -eq 0 ]] && { echo "VERIFICATION PASSED ($n_row rows, $n_acc accepted of $n_ran raw RAN)"; exit 0; }
echo "VERIFICATION FAILED: $fails problem(s)"; exit 1

# long_tier.sh: the long tier (weekly) of run_tier.sh, items L1 to L5. Sourced by run_tier.sh,
# whose record helper writes each item's one PASS, FAIL or SKIP line; not run on its own.
#
# The long tier answers "what changed over the week?". Its tables are numbers compared with the
# previous week's: a correctness check inside an item (a trace, an admission) fails the run; a
# number more than 10% from last week's (slower, faster, larger) is reported in the item's note
# and its compare table, never failed (the host is shared).
#
#   L1 census_speed   cert_trace and cert_run, routes ir and vc, on the census speed rows
#                     (CENSUS_ROWS: <family>/rows/<design>/{lean/*.dcert,stim.txt}): 0 and 32 cycles
#                     best of 3, 4,096 cycles once (the census stimulus's 32 cycles repeated), measured
#                     as the census's Lean_speed_history.tsv rows were (steps 20-25): one run at a time,
#                     memory capped at 20 GB with no swap, nice 19, `ulimit -s unlimited`,
#                     /usr/bin/time; best = the least wall and CPU time and the largest peak RSS of
#                     the runs. Table $OUT/speed.tsv, one row per design, tool, route and cycles
#                     (wall_s, cpu_s = user + sys, peak_rss_mb, the sha256 of the STEP lines, first 16
#                     hex digits, as the census's trace_sha256_16). FAIL: a 32-cycle trace (any run)
#                     differs from Stage 0 (STAGE0/out_<design>_<route>_32.txt), the four 4,096-cycle
#                     traces of a design differ, or a trace changed since PREV_SPEED for the same
#                     certificate. Compare table $OUT/speed_compare.tsv.
#   L2 fp_census      route:fp of cert_trace and dcert2_memroute (`auto auto 32 init:reset nocompare
#                     outputs stim:<stim> edges:third`, its current words), 32 cycles, against
#                     cert_trace route:ir, on the same rows. Table $OUT/fp.tsv: wall, CPU, peak RSS of
#                     each run, and dcert2_memroute's own phase times (specialize_s is fp's
#                     specialization: cert_trace's route:fp runs the same specialization, at the same
#                     auto budget and retry, before its first step). FAIL: a trace differs from
#                     route:ir's, or a run fails. Compare table $OUT/fp_compare.tsv (PREV_FP).
#   L3 sym_survey     scripts/sym_survey.py (sym_probe: one symbolic step per certificate and route fp,
#                     ir, vc, one at a time, 20 GB cap, 30 min timeout) on SURVEY_INPUTS. Table
#                     $OUT/survey.tsv. FAIL: a status that was OK in PREV_SURVEY is now REFUSED,
#                     DRIFT, TIMEOUT, KILLED or MISSING. Compare table $OUT/survey_compare.tsv.
#   L4 cert_corpus    every certificate of SURVEY_INPUTS, of the ROUNDTRIP tables (the converted
#                     corpus) and matched by EXTRA_CERTS is admitted (cert_trace 0 cycles route:ref:
#                     admitFile and nothing else) and, with a stimulus (its ROUNDTRIP row's, or a
#                     stim.txt in its row folder), run 32 cycles on route:ir. Each ROUNDTRIP row (a
#                     DCERT3 original converted to DCERT4) is converted again by this commit's
#                     dcert_upgrade, and its trace compared with the original's (recorded when it
#                     was converted, by a cert_trace that still read DCERT3; this commit reads
#                     DCERT4 only); a reconversion to other bytes is traced too. Table
#                     $OUT/corpus.tsv. FAIL: a DCERT4 certificate refused, or a trace (stored or
#                     reconverted) that differs from the original's. The SURVEY_INPUTS lines with no
#                     certificate are counted in the note and listed in $OUT/l4/missing.txt.
#   L5 cold_build     the commit under test in a new tree under $OUT/l5 (a shared clone at HEAD, plus
#                     the working tree's uncommitted changes to tracked files, none in a CI clone):
#                     run_tier.sh quick there (its .build-proj.sh builds into an empty .olean-dev,
#                     then every quick check runs on that build), and build-opt.sh of every native
#                     driver into an empty ROOT ($OUT/l5/natroot), check_fresh.sh on them, and their
#                     census traces (cert_trace and cert_run, ir and vc, 32 cycles) against Stage 0.
#                     FAIL: anything that does not build or pass from nothing, i.e. what builds only
#                     from a stale cache. The shared .olean-dev and NATROOT are not touched.
#
# Environment (besides run_tier.sh's OUT, LHD, STRICT, NATROOT):
#   LONG_ITEMS    the items to run (default "L1 L2 L3 L4 L5"); the others are SKIP (so STRICT=1 fails
#                 a partial run)
#   CENSUS_ROWS, STAGE0   the census speed rows and their Stage 0 traces (L1, L2, L5)
#   PREV_LONG     last week's long-tier OUT folder: the default of PREV_SPEED ($PREV_LONG/speed.tsv),
#                 PREV_FP ($PREV_LONG/fp.tsv) and PREV_SURVEY ($PREV_LONG/survey.tsv)
#   PREV_SPEED, PREV_FP, PREV_SURVEY   last week's tables; unset or missing: the first week, nothing
#                 compared
#   SURVEY_INPUTS the certificate list (`family=<path>/<Top>_Lgraph.lean` or `family=<path>.dcert`
#                 lines) of L3 and L4
#   SURVEY_SIZES  a census TSV with family, block, nodes (sym_survey.py --sizes: smallest first, fp
#                 skipped above 150,000 nodes); SURVEY_ONLY a regex (--only: a subset, said so in
#                 the note); SURVEY_TIMEOUT seconds per probe (default 5400: the user chose three times the
#                 first 1800, so the largest designs get a chance to finish)
#   ROUNDTRIP     TSV files (space-separated) with columns converted, original, stim,
#                 original_trace (`-` where there is none): the converted corpus (L4)
#   EXTRA_CERTS   a glob of more certificates to admit, read only (L4)
#   LONG_KEEP=1   keep L5's tree and builds after a pass (by default only its logs stay; a failure
#                 keeps everything)
#   LONG_MEM      the memory cap of each run (default 20G; scripts/memcap.py: a systemd user scope,
#                 from cron too, or a watchdog where there is no user manager)
#   PYTHON        the python for the scripts (default python3), as the other self-tests take it

PY="${PYTHON:-python3}"
MEMCAP="$SCRIPT_DIR/../scripts/memcap.py"
LONG_MEM="${LONG_MEM:-20G}"
LONG_ITEMS="${LONG_ITEMS:-L1 L2 L3 L4 L5}"
if [[ -n "${PREV_LONG:-}" ]]; then
  PREV_SPEED="${PREV_SPEED:-$PREV_LONG/speed.tsv}"
  PREV_FP="${PREV_FP:-$PREV_LONG/fp.tsv}"
  PREV_SURVEY="${PREV_SURVEY:-$PREV_LONG/survey.tsv}"
fi
COMMIT="$(cd "$ROOT" && git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
# every capped run uses the way the first one found (a systemd scope, or the watchdog)
MEMCAP_METHOD="$("$PY" "$MEMCAP" --detect)"; export MEMCAP_METHOD
echo "long tier: items $LONG_ITEMS; memory cap $LONG_MEM per run ($MEMCAP_METHOD)"

# long_natives <log> <driver>...: build these native drivers into NATROOT (build-opt.sh, incremental)
# and check that each is a build of the current sources
long_natives() {
  local log=$1; shift
  local srcs=() d
  for d in "$@"; do
    case $d in
      cert_trace|dcert2_memroute) srcs+=("Futamura_Projection_Scripts/$d.lean") ;;
      cert_run|lhd_ckpt|dcert_upgrade) srcs+=("../../pass/lean/tools/sim/$d.lean") ;;
      *) srcs+=("../../pass/lean/tools/proof/$d.lean") ;;
    esac
  done
  (cd "$L" && OPT=-O2 ROOT="$NATROOT" bash Futamura_Projection_Scripts/build-opt.sh "${srcs[@]}") > "$log" 2>&1 \
    && bash "$SCRIPT_DIR/check_fresh.sh" "$ROOT" "$NATROOT/nat" "$@" >> "$log" 2>&1
}

# long_measure <out prefix> <binary> <args...>: one run under the memory cap, nice 19, unlimited
# stack, timed by /usr/bin/time; stdout in <prefix>.txt, stderr in <prefix>.err. Sets M_RC, M_WALL,
# M_CPU (user + sys) and M_RSS (MB); a value /usr/bin/time did not write is `-`.
long_measure() {
  local o=$1 bin=$2; shift 2
  local q="" a
  for a in "$@"; do q+=" $(printf %q "$a")"; done
  rm -f "$o.time"
  "$PY" "$MEMCAP" --mem "$LONG_MEM" -- bash -c "ulimit -s unlimited; exec /usr/bin/time \
-f 'wall %e s user %U s sys %S s rss %M KB' -o $(printf %q "$o.time") nice -n19 $(printf %q "$bin")$q \
> $(printf %q "$o.txt") 2> $(printf %q "$o.err")" < /dev/null
  M_RC=$?
  M_WALL=$(grep -o 'wall [0-9.]*' "$o.time" 2>/dev/null | cut -d' ' -f2); M_WALL=${M_WALL:--}
  local u s k
  u=$(grep -o 'user [0-9.]*' "$o.time" 2>/dev/null | cut -d' ' -f2)
  s=$(grep -o 'sys [0-9.]*' "$o.time" 2>/dev/null | cut -d' ' -f2)
  k=$(grep -o 'rss [0-9]*' "$o.time" 2>/dev/null | cut -d' ' -f2)
  if [[ -n "$u" && -n "$s" ]]; then M_CPU=$(awk -v a="$u" -v b="$s" 'BEGIN{printf "%.2f", a+b}'); else M_CPU=-; fi
  if [[ -n "$k" ]]; then M_RSS=$((k / 1024)); else M_RSS=-; fi
}

sha16() { sha256sum "$1" | cut -c1-16; }
load() { cut -d' ' -f1-3 /proc/loadavg; }

# the census speed rows: <family>/rows/<design> folders with a certificate and a stimulus
census_rows() {
  local d
  for d in "$CENSUS_ROWS"/*/rows/*; do
    compgen -G "$d/lean/*.dcert" > /dev/null && [[ -f "$d/stim.txt" ]] && echo "$d"
  done
}

# ---- L1: census speed
l1_run() { # <dir> <row> <tool> <route> <cycles> <round> <stim>: one measured run, one runs.tsv row
  local d=$1 rd=$2 tool=$3 rt=$4 cyc=$5 round=$6 stim=$7
  local r cert o steps sha check ref
  r=$(basename "$rd"); cert=$(ls "$rd"/lean/*.dcert | head -1)
  o="$d/out_${r}_${tool}_${rt}_${cyc}_$round"
  if [[ $tool == cert_trace ]]; then
    long_measure "$o" "$NATROOT/nat/cert_trace" "$cert" "$cyc" "$stim" edges:third "route:$rt"
    grep '^STEP' "$o.txt" > "$o.steps"; rm -f "$o.txt"
  else
    rm -rf "$o.d"
    long_measure "$o" "$NATROOT/nat/cert_run" "$cert" "stim:$stim" edges:third "route:$rt" "max:$cyc" "trace:$o.d"
    grep '^STEP' "$o.d/steps.trace" > "$o.steps" 2>/dev/null; rm -rf "$o.d"
  fi
  steps=$(grep -c '^STEP' "$o.steps")
  sha=-; [[ $cyc -gt 0 ]] && sha=$(sha16 "$o.steps")
  check="$steps steps"
  if [[ $M_RC -ne 0 || $steps -ne $cyc ]]; then
    check="FAILED (exit $M_RC, $steps steps)"
  elif [[ $cyc == 32 ]]; then
    ref="$STAGE0/out_${r}_${rt}_32.txt"
    if [[ -f "$ref" ]] && cmp -s "$o.steps" <(grep '^STEP' "$ref"); then check="same as Stage 0"; else check="DIFFERS from Stage 0"; fi
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$r" "$tool" "$rt" "$cyc" "$round" "$M_RC" \
    "$M_WALL" "$M_CPU" "$M_RSS" "$sha" "$check" "$(sha16 "$cert")" >> "$d/runs.tsv"
  echo "$r $tool $rt $cyc (round $round): exit $M_RC, wall $M_WALL s, cpu $M_CPU s, rss $M_RSS MB, $check"
}

L1_census_speed() {
  local d="$OUT/l1" rd r tool rt cyc round
  local -a rows
  mkdir -p "$d"
  long_natives "$d/natives.log" cert_trace cert_run \
    || { echo "RESULT FAIL the natives did not build or are stale ($d/natives.log)"; return 1; }
  mapfile -t rows < <(census_rows)
  [[ ${#rows[@]} -gt 0 ]] || { echo "RESULT FAIL no census rows in $CENSUS_ROWS"; return 1; }
  printf 'design\ttool\troute\tcycles\tround\texit\twall_s\tcpu_s\tpeak_rss_mb\ttrace_sha256_16\ttrace_check\tcert_sha256_16\n' \
    > "$d/runs.tsv"
  # the 4,096-cycle stimulus: the census stimulus's 32 cycles repeated (cycle k is cycle k mod 32)
  for rd in "${rows[@]}"; do
    r=$(basename "$rd")
    awk 'NR == 1 { print; next } { line[NR] = $0; n = NR }
      END { for (rep = 0; rep < 128; rep++) for (i = 2; i <= n; i++) {
              k = split(line[i], a, " ")
              if (a[1] == "at") { a[2] += 32 * rep; s = a[1]; for (j = 2; j <= k; j++) s = s " " a[j]; print s }
              else if (rep == 0) print line[i] } }' "$rd/stim.txt" > "$d/stim_${r}_4096.txt"
  done
  echo "long runs (4,096 cycles, once), load $(load)"
  for rd in "${rows[@]}"; do
    r=$(basename "$rd")
    for tool in cert_trace cert_run; do
      for rt in ir vc; do l1_run "$d" "$rd" "$tool" "$rt" 4096 1 "$d/stim_${r}_4096.txt"; done
    done
  done
  echo "short runs (0 and 32 cycles, best of 3), load $(load)"
  for round in 1 2 3; do
    for rd in "${rows[@]}"; do
      for cyc in 0 32; do
        for rt in ir vc; do
          for tool in cert_trace cert_run; do l1_run "$d" "$rd" "$tool" "$rt" "$cyc" "$round" "$rd/stim.txt"; done
        done
      done
    done
  done
  echo "load at the end $(load)"
  "$PY" "$SCRIPT_DIR/../scripts/long_compare.py" best "$d/runs.tsv" "$d/best.tsv" --keys design,tool,route,cycles
  # speed.tsv: the best runs, the commit and the date
  awk -F'\t' -v OFS='\t' -v c="$COMMIT" -v t="$(date +%F)" \
    'NR == 1 { print $0, "livehd_commit", "date"; next } { print $0, c, t }' "$d/best.tsv" > "$OUT/speed.tsv"
  local nfail n32 bad32 bad4k=0 cmp
  nfail=$(awk -F'\t' 'NR > 1 && $11 ~ /^FAILED/' "$d/runs.tsv" | wc -l)
  n32=$(awk -F'\t' 'NR > 1 && $4 == 32' "$d/runs.tsv" | wc -l)
  bad32=$(awk -F'\t' 'NR > 1 && $4 == 32 && $11 != "same as Stage 0"' "$d/runs.tsv" | wc -l)
  for rd in "${rows[@]}"; do
    r=$(basename "$rd")
    if [[ $(awk -F'\t' -v r="$r" 'NR > 1 && $1 == r && $4 == 4096 { print $10 }' "$d/runs.tsv" | sort -u | wc -l) -ne 1 ]]; then
      echo "DIFFER: the 4,096-cycle traces of $r (cert_trace and cert_run, ir and vc)"; bad4k=$((bad4k + 1))
    fi
  done
  cmp=$("$PY" "$SCRIPT_DIR/../scripts/long_compare.py" table "$OUT/speed.tsv" "${PREV_SPEED:--}" "$OUT/speed_compare.tsv" \
        --keys design,tool,route,cycles --metrics cpu_s,wall_s,peak_rss_mb --trace trace_sha256_16 --same cert_sha256_16)
  local crc=$?
  echo "$cmp"
  awk -F'\t' 'NR > 1 && $NF != "" && $0 ~ /REPORT|TRACE CHANGED/' "$OUT/speed_compare.tsv" | head -50
  local res="${#rows[@]} designs, $(($(wc -l < "$OUT/speed.tsv") - 1)) measurements ($OUT/speed.tsv); $bad32 of $n32 32-cycle traces differ from Stage 0; $bad4k designs whose 4,096-cycle traces disagree; $nfail runs failed; against last week: $cmp ($OUT/speed_compare.tsv)"
  if [[ $nfail -eq 0 && $bad32 -eq 0 && $bad4k -eq 0 && $crc -eq 0 ]]; then echo "RESULT PASS $res"
  else echo "RESULT FAIL $res"; return 1; fi
}

# ---- L2: fp census
L2_fp_census() {
  local d="$OUT/l2" rd r cert ir n=0 bad=0 failed=0 steps sha eq ms spec chk exe
  local -a rows
  mkdir -p "$d"
  long_natives "$d/natives.log" cert_trace dcert2_memroute \
    || { echo "RESULT FAIL the natives did not build or are stale ($d/natives.log)"; return 1; }
  mapfile -t rows < <(census_rows)
  [[ ${#rows[@]} -gt 0 ]] || { echo "RESULT FAIL no census rows in $CENSUS_ROWS"; return 1; }
  printf 'design\ttool\tcycles\texit\twall_s\tcpu_s\tpeak_rss_mb\tspecialize_s\tcheck_s\texecute_s\tsteps\tequals_ir\ttrace_sha256_16\tcert_sha256_16\tlivehd_commit\tdate\n' \
    > "$OUT/fp.tsv"
  row() { # <tool> <prefix> <spec> <check> <execute> <equals>
    steps=$(grep -c '^STEP' "$2.steps")
    printf '%s\t%s\t32\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$r" "$1" "$M_RC" "$M_WALL" "$M_CPU" \
      "$M_RSS" "$3" "$4" "$5" "$steps" "$6" "$(sha16 "$2.steps")" "$(sha16 "$cert")" "$COMMIT" "$(date +%F)" >> "$OUT/fp.tsv"
    echo "$r $1: exit $M_RC, wall $M_WALL s, cpu $M_CPU s, rss $M_RSS MB, specialize $3 s, $steps steps, equals ir: $6"
  }
  echo "load at the start $(load)"
  for rd in "${rows[@]}"; do
    r=$(basename "$rd"); cert=$(ls "$rd"/lean/*.dcert | head -1)
    # route:ir, the reference
    ir="$d/ir_$r"
    long_measure "$ir" "$NATROOT/nat/cert_trace" "$cert" 32 "$rd/stim.txt" edges:third route:ir
    grep '^STEP' "$ir.txt" > "$ir.steps"
    eq="reference"
    [[ -f "$STAGE0/out_${r}_ir_32.txt" ]] && ! cmp -s "$ir.steps" <(grep '^STEP' "$STAGE0/out_${r}_ir_32.txt") \
      && eq="reference, DIFFERS from Stage 0"
    if [[ $M_RC -ne 0 || $(grep -c '^STEP' "$ir.steps") -ne 32 ]]; then eq="FAILED"; failed=$((failed + 1)); fi
    row cert_trace_ir "$ir" - - - "$eq"
    # cert_trace route:fp
    local fp="$d/fp_$r"
    long_measure "$fp" "$NATROOT/nat/cert_trace" "$cert" 32 "$rd/stim.txt" edges:third route:fp
    grep '^STEP' "$fp.txt" > "$fp.steps"
    n=$((n + 1))
    if [[ $M_RC -ne 0 ]]; then eq="FAILED (exit $M_RC)"; failed=$((failed + 1))
    elif cmp -s "$fp.steps" "$ir.steps" && [[ $(grep -c '^STEP' "$fp.steps") -eq 32 ]]; then eq=yes
    else eq=DIFFERS; bad=$((bad + 1)); fi
    row cert_trace_fp "$fp" - - - "$eq"
    # dcert2_memroute, its own words: auto fuel, 32 cycles from init:reset, outputs only (the default
    # encoded loop; `nocompare`: the STEP lines are compared with route:ir's here)
    local mr="$d/memroute_$r"
    long_measure "$mr" "$NATROOT/nat/dcert2_memroute" "$cert" auto auto 32 init:reset nocompare outputs \
      "stim:$rd/stim.txt" edges:third
    grep '^STEP' "$mr.txt" > "$mr.steps"
    n=$((n + 1))
    ms=$(grep -m1 '  ms: support' "$mr.txt")
    spec=$(sed -n 's/.*specialize \([0-9]*\).*/\1/p' <<< "$ms"); chk=$(sed -n 's/.*check \([0-9]*\).*/\1/p' <<< "$ms")
    exe=$(sed -n 's/.*execute \([0-9]*\).*/\1/p' <<< "$ms")
    s2() { [[ -n "$1" ]] && awk -v m="$1" 'BEGIN{printf "%.2f", m / 1000}' || echo -; }
    if [[ $M_RC -ne 0 ]]; then eq="FAILED (exit $M_RC: $(grep -m1 'RESULT:' "$mr.txt" | sed 's/^ *//'))"; failed=$((failed + 1))
    elif cmp -s "$mr.steps" "$ir.steps" && [[ $(grep -c '^STEP' "$mr.steps") -eq 32 ]]; then eq=yes
    else eq=DIFFERS; bad=$((bad + 1)); fi
    row dcert2_memroute "$mr" "$(s2 "$spec")" "$(s2 "$chk")" "$(s2 "$exe")" "$eq"
  done
  echo "load at the end $(load)"
  local cmp crc
  cmp=$("$PY" "$SCRIPT_DIR/../scripts/long_compare.py" table "$OUT/fp.tsv" "${PREV_FP:--}" "$OUT/fp_compare.tsv" \
        --keys design,tool --metrics cpu_s,wall_s,peak_rss_mb,specialize_s --trace trace_sha256_16 --same cert_sha256_16)
  crc=$?
  echo "$cmp"
  local res="${#rows[@]} designs, $n fp runs ($OUT/fp.tsv): $bad traces differ from route:ir, $failed runs failed; \
specialization $(awk -F'\t' '$2 == "dcert2_memroute" && $8 != "-" { s = s sep $1 " " $8 " s"; sep = ", " } END { print s }' "$OUT/fp.tsv"); \
largest peak RSS $(awk -F'\t' 'NR > 1 && $7 != "-" && $7 + 0 > m { m = $7 + 0 } END { print m + 0 }' "$OUT/fp.tsv") MB; against last week: $cmp"
  if [[ $bad -eq 0 && $failed -eq 0 && $crc -eq 0 ]]; then echo "RESULT PASS $res"
  else echo "RESULT FAIL $res"; return 1; fi
}

# ---- L3: symbolic survey
L3_sym_survey() {
  local d="$OUT/l3"
  mkdir -p "$d"
  long_natives "$d/natives.log" sym_probe \
    || { echo "RESULT FAIL the natives did not build or are stale ($d/natives.log)"; return 1; }
  local args=(--probe "$NATROOT/nat/sym_probe" --inputs "$SURVEY_INPUTS" --out "$d" --mem "$LONG_MEM"
              --timeout "${SURVEY_TIMEOUT:-5400}")
  [[ -n "${SURVEY_SIZES:-}" ]] && args+=(--sizes "$SURVEY_SIZES")
  [[ -n "${SURVEY_ONLY:-}" ]] && args+=(--only "$SURVEY_ONLY")
  echo "load at the start $(load)"
  "$PY" "$SCRIPT_DIR/../scripts/sym_survey.py" "${args[@]}" || { echo "RESULT FAIL sym_survey.py stopped (exit $?)"; return 1; }
  echo "load at the end $(load)"
  cp "$d/survey.tsv" "$OUT/survey.tsv"
  local counts cmp crc
  counts=$(awk -F'\t' 'NR > 1 { n[$5]++ } END { for (s in n) printf "%s %s ", s, n[s] }' "$OUT/survey.tsv")
  cmp=$("$PY" "$SCRIPT_DIR/../scripts/long_compare.py" survey "$OUT/survey.tsv" "${PREV_SURVEY:--}" "$OUT/survey_compare.tsv")
  crc=$?
  echo "$cmp"
  awk -F'\t' 'NR > 1 && $NF ~ /WORSE|better|changed/' "$OUT/survey_compare.tsv"
  local res="$(($(wc -l < "$OUT/survey.tsv") - 1)) probes${SURVEY_ONLY:+ (subset --only '$SURVEY_ONLY')}: ${counts% } ($OUT/survey.tsv); $cmp"
  if [[ $crc -eq 0 ]]; then echo "RESULT PASS $res"; else echo "RESULT FAIL $res"; return 1; fi
}

# ---- L4: certificate corpus
L4_cert_corpus() {
  local d="$OUT/l4" c
  mkdir -p "$d/reconv" "$d/tr"
  long_natives "$d/natives.log" cert_trace dcert_upgrade \
    || { echo "RESULT FAIL the natives did not build or are stale ($d/natives.log)"; return 1; }
  printf 'stim v1\n' > "$d/empty.stim"
  # every certificate, with where it came from, and its round-trip row
  declare -A SRC=() STIM=() ORIG=() REF=()
  local order=() nmiss=0
  add() { [[ -n "${SRC[$1]:-}" ]] || order+=("$1"); SRC[$1]="${SRC[$1]:+${SRC[$1]},}$2"; }
  : > "$d/missing.txt"
  if [[ -n "${SURVEY_INPUTS:-}" ]]; then
    while IFS= read -r line; do
      [[ -z "$line" || "$line" == \#* ]] && continue
      local p=${line#*=}
      if [[ $p == *.dcert ]]; then c=$p; else c="$(dirname "$(realpath "$p")")/$(basename "$(realpath "$p")" _Lgraph.lean).dcert"; fi
      if [[ -f "$c" ]]; then add "$c" survey; else echo "$line" >> "$d/missing.txt"; nmiss=$((nmiss + 1)); fi
    done < "$SURVEY_INPUTS"
  fi
  local t conv orig stim ref
  for t in ${ROUNDTRIP:-}; do
    while IFS=$'\t' read -r conv orig stim ref; do
      [[ $conv == converted || -z $conv ]] && continue
      add "$conv" roundtrip
      [[ $stim != - ]] && STIM[$conv]=$stim
      [[ $orig != - ]] && ORIG[$conv]=$orig
      [[ $ref != - ]] && REF[$conv]=$ref
    done < "$t"
  done
  if [[ -n "${EXTRA_CERTS:-}" ]]; then
    for c in $EXTRA_CERTS; do [[ -f "$c" ]] && add "$c" extra; done
  fi
  printf 'certificate\tsources\tversion\tadmission\tstim\ttrace\treconversion\tseconds\n' > "$OUT/corpus.tsv"
  local n=0 nd4=0 nadm=0 refused=0 notd4=0 ntr=0 trbad=0 trstop=0 nrc=0 rcdiff=0 i=0
  for c in "${order[@]}"; do
    i=$((i + 1)); n=$((n + 1))
    local t0=$SECONDS ver adm tr="-" rc="-" o="$d/tr/$i" s
    ver=$(head -c 7 "$c" | tr -d '\n' | tr -c 'A-Z0-9' '?')
    [[ $ver == DCERT4 ]] && nd4=$((nd4 + 1))
    # the stimulus: the round-trip row's, else the row folder's stim.txt (<row>/lean/<top>.dcert)
    s=${STIM[$c]:-}
    [[ -z "$s" && $(basename "$(dirname "$c")") == lean && -f "$(dirname "$(dirname "$c")")/stim.txt" ]] \
      && s="$(dirname "$(dirname "$c")")/stim.txt"
    # admission: admitFile, and nothing else (0 cycles of route:ref)
    "$PY" "$MEMCAP" --mem "$LONG_MEM" -- bash -c "ulimit -s unlimited; exec '$NATROOT/nat/cert_trace' '$c' 0 '$d/empty.stim' route:ref" \
      > "$o.adm" 2>&1 < /dev/null
    if [[ $? -eq 0 ]]; then adm=admitted; nadm=$((nadm + 1))
    else
      adm="refused: $(head -1 "$o.adm" | cut -c1-160)"
      if [[ $ver == DCERT4 ]]; then refused=$((refused + 1)); else notd4=$((notd4 + 1)); fi
    fi
    # route:ir, 32 cycles, against the original's trace when there is one
    if [[ -n "$s" && $adm == admitted ]]; then
      ntr=$((ntr + 1))
      "$PY" "$MEMCAP" --mem "$LONG_MEM" -- bash -c "ulimit -s unlimited; exec nice -n19 '$NATROOT/nat/cert_trace' '$c' 32 '$s' edges:third route:ir" \
        > "$o.ir" 2> "$o.ir.err" < /dev/null
      local e=$? k
      k=$(grep -c '^STEP' "$o.ir")
      if [[ -n "${REF[$c]:-}" ]]; then
        if cmp -s <(grep '^STEP' "$o.ir") <(grep '^STEP' "${REF[$c]}"); then tr="same as the original ($k steps)"
        else tr="DIFFERS from the original"; trbad=$((trbad + 1)); fi
      elif [[ $e -eq 0 && $k -eq 32 ]]; then tr="ran (32 steps, no reference)"
      else tr="stopped (exit $e, $k steps, no reference)"; trstop=$((trstop + 1)); fi
    fi
    # the round trip: this commit's dcert_upgrade on the original, the same bytes and the same trace
    if [[ -n "${ORIG[$c]:-}" ]]; then
      nrc=$((nrc + 1))
      rm -f "$d/reconv/$i.dcert"
      if "$NATROOT/nat/dcert_upgrade" "${ORIG[$c]}" "$d/reconv/$i.dcert" > "$o.up" 2>&1 < /dev/null; then
        if cmp -s "$d/reconv/$i.dcert" "$c"; then rc="the same bytes"; rm -f "$d/reconv/$i.dcert"
        else
          rcdiff=$((rcdiff + 1)); rc="DIFFERENT bytes ($d/reconv/$i.dcert)"
          if [[ -n "$s" && -n "${REF[$c]:-}" ]]; then
            "$PY" "$MEMCAP" --mem "$LONG_MEM" -- bash -c "ulimit -s unlimited; exec nice -n19 '$NATROOT/nat/cert_trace' '$d/reconv/$i.dcert' 32 '$s' edges:third route:ir" \
              > "$o.re" 2> "$o.re.err" < /dev/null
            if cmp -s <(grep '^STEP' "$o.re") <(grep '^STEP' "${REF[$c]}"); then rc+=", the same trace"
            else rc+=", a DIFFERENT trace"; trbad=$((trbad + 1)); fi
          fi
        fi
      else
        rc="REFUSED by dcert_upgrade: $(head -1 "$o.up" | cut -c1-120)"; rcdiff=$((rcdiff + 1))
      fi
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$c" "${SRC[$c]}" "$ver" "$adm" "${s:--}" "$tr" "$rc" $((SECONDS - t0)) >> "$OUT/corpus.tsv"
    echo "$c [${SRC[$c]}] $ver: $adm; trace: $tr; reconversion: $rc"
  done
  local res="$n certificates ($nd4 DCERT4): $nadm admitted, $refused DCERT4 refused, $notd4 of another version refused; $ntr traced on route:ir, $trbad differ from the original, $trstop stopped without a reference; $nrc reconverted by this commit, $rcdiff not to the same bytes; $nmiss list lines with no certificate ($d/missing.txt) ($OUT/corpus.tsv)"
  if [[ $n -gt 0 && $refused -eq 0 && $trbad -eq 0 ]]; then echo "RESULT PASS $res"
  else echo "RESULT FAIL $res"; return 1; fi
}

# ---- L5: cold build
L5_cold_build() {
  local d="$OUT/l5" head
  rm -rf "$d"; mkdir -p "$d"
  head=$(git -C "$ROOT" rev-parse HEAD) || { echo "RESULT FAIL $ROOT is not a git clone"; return 1; }
  git clone -q --shared --no-checkout "$ROOT" "$d/src" && git -C "$d/src" checkout -q --detach "$head" \
    || { echo "RESULT FAIL could not make a tree of $head"; return 1; }
  # the working tree's uncommitted changes to tracked files (a CI clone has none)
  git -C "$ROOT" diff HEAD --binary > "$d/local.diff"
  if [[ -s "$d/local.diff" ]]; then
    git -C "$d/src" apply "$d/local.diff" || { echo "RESULT FAIL the uncommitted changes do not apply ($d/local.diff)"; return 1; }
    echo "tree: $head and the working tree's uncommitted changes ($(git -C "$d/src" status --porcelain | wc -l) files)"
  else
    echo "tree: $head"
  fi
  local qrc nrc trc=0 nbad=0 n=0 t0=$SECONDS
  # quick, in the new tree: .build-proj.sh into its empty .olean-dev, then every quick check
  env OUT="$d/quick" LHD="$LHD" STRICT="$STRICT" BUILD=1 bash "$d/src/pass/lean/tools/tests/run_tier.sh" quick \
    > "$d/quick.log" 2>&1
  qrc=$?
  cat "$d/quick.log"
  echo "quick on the cold build: exit $qrc ($((SECONDS - t0)) s)"
  # every native driver into an empty ROOT
  t0=$SECONDS
  local drivers=(cert_trace dcert2_memroute cert_run lhd_ckpt dcert_upgrade sym_verify sym_refmap sym_probe ckpt_reach)
  (cd "$d/src/formal/lean" && OPT=-O2 ROOT="$d/natroot" bash Futamura_Projection_Scripts/build-opt.sh \
    Futamura_Projection_Scripts/cert_trace.lean Futamura_Projection_Scripts/dcert2_memroute.lean \
    ../../pass/lean/tools/sim/cert_run.lean ../../pass/lean/tools/sim/lhd_ckpt.lean ../../pass/lean/tools/sim/dcert_upgrade.lean \
    ../../pass/lean/tools/proof/sym_verify.lean ../../pass/lean/tools/proof/sym_refmap.lean \
    ../../pass/lean/tools/proof/sym_probe.lean ../../pass/lean/tools/proof/ckpt_reach.lean) > "$d/natives.log" 2>&1 \
    && bash "$d/src/pass/lean/tools/tests/check_fresh.sh" "$d/src" "$d/natroot/nat" "${drivers[@]}" >> "$d/natives.log" 2>&1
  nrc=$?
  echo "natives from an empty ROOT: exit $nrc ($((SECONDS - t0)) s; $(grep -c '^  lean ' "$d/natives.log") modules; log $d/natives.log)"
  tail -3 "$d/natives.log"
  # the cold natives on the census traces
  if [[ $nrc -eq 0 && -n "${CENSUS_ROWS:-}" && -n "${STAGE0:-}" ]]; then
    local rd r cert rt o
    while IFS= read -r rd; do
      r=$(basename "$rd"); cert=$(ls "$rd"/lean/*.dcert | head -1)
      for rt in ir vc; do
        [[ -f "$STAGE0/out_${r}_${rt}_32.txt" ]] || continue
        o="$d/census_${r}_$rt"
        "$PY" "$MEMCAP" --mem "$LONG_MEM" -- bash -c "ulimit -s unlimited; exec '$d/natroot/nat/cert_trace' '$cert' 32 '$rd/stim.txt' edges:third route:$rt" > "$o.ct" 2>&1 < /dev/null
        "$PY" "$MEMCAP" --mem "$LONG_MEM" -- bash -c "ulimit -s unlimited; exec '$d/natroot/nat/cert_run' '$cert' 'stim:$rd/stim.txt' edges:third route:$rt max:32 'trace:$o.d'" > "$o.cr" 2>&1 < /dev/null
        n=$((n + 2))
        cmp -s <(grep '^STEP' "$o.ct") <(grep '^STEP' "$STAGE0/out_${r}_${rt}_32.txt") || { echo "DIFFERS cert_trace $r $rt"; nbad=$((nbad + 1)); }
        cmp -s <(grep '^STEP' "$o.d/steps.trace" 2>/dev/null) <(grep '^STEP' "$STAGE0/out_${r}_${rt}_32.txt") || { echo "DIFFERS cert_run $r $rt"; nbad=$((nbad + 1)); }
      done
    done < <(census_rows)
    echo "cold natives on the census traces: $n compared, $nbad differ"
    [[ $n -gt 0 && $nbad -eq 0 ]] || trc=1
  elif [[ $nrc -eq 0 ]]; then
    echo "cold natives on the census traces: not run (CENSUS_ROWS and STAGE0 are not set)"
    [[ "$STRICT" == 1 ]] && trc=1
  fi
  local qsum="quick: $(tail -1 "$d/quick.log" | sed 's/^summary: //; s/ (results.*//')"
  local res="$qsum ($d/quick); natives: ${#drivers[@]} drivers $([[ $nrc -eq 0 ]] && echo "built from an empty ROOT and fresh" || echo "FAILED") ($d/natives.log); census traces on them: $n compared, $nbad differ"
  if [[ $qrc -eq 0 && $nrc -eq 0 && $trc -eq 0 ]]; then
    # a pass needs the tree and the builds no more (about 1.7 GB); the logs stay. LONG_KEEP=1 keeps them.
    [[ "${LONG_KEEP:-0}" == 1 ]] || rm -rf "$d/src" "$d/natroot"
    echo "RESULT PASS $res"
  else echo "RESULT FAIL $res"; return 1; fi
}

# long_item <name> <function> <why it cannot run, or empty>: one PASS, FAIL or SKIP line; the note
# is the function's `RESULT` line (its numbers and the tables it wrote) and its log
long_item() {
  local name=$1 fn=$2 why=$3 item=${2%%_*} log="$OUT/$1.log" t0=$SECONDS rc res
  if [[ " $LONG_ITEMS " != *" $item "* ]]; then record "$name" SKIP 0 "$item is not in LONG_ITEMS ($LONG_ITEMS)"; return; fi
  if [[ -n "$why" ]]; then record "$name" SKIP 0 "$why"; return; fi
  "$fn" > "$log" 2>&1; rc=$?
  res=$(grep '^RESULT ' "$log" | tail -1)
  if [[ $rc -eq 0 && $res == "RESULT PASS "* ]]; then
    record "$name" PASS $((SECONDS - t0)) "${res#RESULT PASS }; log $log"
  else
    res=${res#RESULT FAIL }
    record "$name" FAIL $((SECONDS - t0)) "${res:-exit $rc, no RESULT line}; log $log"
  fi
}

need_census=""; [[ -n "${CENSUS_ROWS:-}" && -n "${STAGE0:-}" ]] || need_census="CENSUS_ROWS and STAGE0 are not set"
need_survey=""; [[ -n "${SURVEY_INPUTS:-}" && -f "${SURVEY_INPUTS:-}" ]] || need_survey="SURVEY_INPUTS is not set or not a file"
need_corpus=""
[[ -n "${SURVEY_INPUTS:-}${ROUNDTRIP:-}${EXTRA_CERTS:-}" ]] \
  || need_corpus="none of SURVEY_INPUTS, ROUNDTRIP, EXTRA_CERTS is set"
long_item L1_census_speed L1_census_speed "$need_census"
long_item L2_fp_census L2_fp_census "$need_census"
long_item L3_sym_survey L3_sym_survey "$need_survey"
long_item L4_cert_corpus L4_cert_corpus "$need_corpus"
long_item L5_cold_build L5_cold_build ""

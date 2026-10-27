#!/usr/bin/env bash
# Controls for RUNTIME-CHILD EXIT ADMISSION in the two restored D4 drivers.
#
# WHAT THIS GUARDS. Both drivers captured the runtime child's status and then
# IGNORED it, admitting a row on SHAPE alone. (The status was available: these
# scripts set `-o pipefail`, so the `| tail -1` substitution does carry a
# failing `lake` through. An earlier note here said `tail` forced it to zero;
# that is wrong with pipefail set.) A child that printed a correct 11-field
# `PARSED/true/RAN` row and exited 17 was published WITH ITS DIGEST, and
# `replay` reported `runtime_complete=YES`. The preflight had the same hole one
# step earlier: a correctly shaped `READ_ERROR` row followed by a nonzero exit
# blessed a toolchain that cannot run.
#
# Each case below drives the ACTUAL CLI with a stub `LAKE`, so what is tested
# is the driver's admission decision, not a restatement of it. A control that
# cannot fail is not a control: the "success" legs require exit 0 AND an
# accepted digest, and the failure legs require the digest to be ABSENT.
#
# This is a harness guard. No Lean semantics, checker policy or resource
# verdict is involved, and no row here is evidence about any design.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
REPLAY="$ROOT/scripts/replay_d4_certs.sh"
SWEEP="$ROOT/scripts/run_d4_runtime_sweep.sh"
CERT="${D4_CONTROL_CERT:-$ROOT/generated/master_port/d4_corpus/tiny.dcert}"

fails=0
ok()  { printf '  ok   %s\n' "$1"; }
bad() { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }

for f in "$REPLAY" "$SWEEP"; do
  [[ -r "$f" ]] || { echo "FATAL: missing $f" >&2; exit 2; }
done
if [[ ! -r "$CERT" ]]; then
  echo "SKIP: no control certificate at $CERT -- unresolved prerequisite, not a pass" >&2
  exit 3
fi

W="$ROOT/generated/d4_exit_controls"
rm -rf "$W"; mkdir -p "$W"

HEADER=$'corpus\tmodule\temit_status\tcert_path\tcert_sha256\tsources\tnodes\tflops\tmemories\tparse_status\tparse_ms\tcompiles_ok\truntime_status\trun_ms\tdigest\tdiagnostic\twall_s\tpeak_rss_kb'
SHA="$(sha256sum "$CERT" | cut -d' ' -f1)"
ROWS="$W/recorded.tsv"
printf '%s\n' "$HEADER" > "$ROWS"
printf 'selftest\tm1\tEMITTED\t%s\t%s\t4\t2\t1\t0\tPARSED\t1\ttrue\tRAN\t1\t111\t-\t1\t1\n' \
  "$CERT" "$SHA" >> "$ROWS"

# A stub `lake`. ROW/RC/DELAY say what the RUNTIME child does; the PREFLIGHT
# call -- the driver's own probe with `/nonexistent.dcert` -- answers with the
# shape the real driver would produce, under its own `pre_rc`, so a control
# aimed at the runtime leg is not decided by the preflight. `--version` answers
# the driver's binding check so nothing else has to be faked.
mk_lake() { # mk_lake <path> <row> <rc> [delay] [pre_rc] [pre_row]
  cat > "$1" <<STUB
#!/usr/bin/env bash
if [[ "\${1:-}" == "--version" ]]; then echo "Lake version 5.0.0 (stub)"; exit 0; fi
for a in "\$@"; do last="\$a"; done
if [[ "\$last" == "/nonexistent.dcert" ]]; then
  printf '%s\n' '${6:-$READ_ROW}'
  exit ${5:-0}
fi
printf '%s\n' '$2'
sleep ${4:-0}
exit $3
STUB
  chmod +x "$1"
}

GOOD_ROW=$'4\t2\t1\t0\tPARSED\t1\ttrue\tRAN\t1\t111\t-'
READ_ROW=$'-\t-\t-\t-\tREAD_ERROR\t0\t-\tNOT_RUN\t0\t-\tcannot read'

run_replay() { # run_replay <tag> <lake>
  LAKE="$2" RUN_TIMEOUT=5 bash "$REPLAY" "$ROWS" "$W/$1" > "$W/$1.log" 2>&1
  echo $?
}

# 1. SUCCESS: exit 0 and a valid row -- the leg that must still pass.
mk_lake "$W/lake_ok" "$GOOD_ROW" 0
rc="$(run_replay ok "$W/lake_ok")"
if [[ "$rc" -eq 0 ]] && grep -q 'runtime_complete=YES' "$W/ok/"*.txt "$W/ok.log" 2>/dev/null; then
  ok "replay accepts exit 0 with a valid row (runtime_complete=YES)"
else
  bad "replay rejected a clean run (rc=$rc)"; sed -n '1,6p' "$W/ok.log"
fi

# 2. THE DEFECT: the same valid row, then exit 17.
mk_lake "$W/lake_17" "$GOOD_ROW" 17
rc="$(run_replay e17 "$W/lake_17")"
if [[ "$rc" -ne 0 ]] && ! grep -q 'runtime_complete=YES' "$W/e17.log" "$W/e17/"*.txt 2>/dev/null; then
  ok "replay refuses a valid row behind exit 17 (nonzero, runtime_complete not YES)"
else
  bad "replay ACCEPTED a row from a child that exited 17 (rc=$rc)"
fi
if [[ "$(awk -F'\t' 'NR==2{print $15}' "$W/e17/rows.tsv" 2>/dev/null)" == "-" ]]; then
  ok "no runtime digest is published for the failed child (column 15 is '-')"
else
  bad "the refused run still published a runtime digest"
fi

# 3. FAILED PREFLIGHT: the right shape, then a nonzero exit.
mk_lake "$W/lake_pre" "$GOOD_ROW" 0 0 17
rc="$(run_replay pre "$W/lake_pre")"
if [[ "$rc" -ne 0 ]] && grep -q 'preflight exited 17' "$W/pre.log"; then
  ok "replay refuses a preflight that exits nonzero, and names the status"
else
  bad "replay accepted a failing preflight (rc=$rc)"; sed -n '1,4p' "$W/pre.log"
fi

# 4. TIMEOUT AFTER OUTPUT: a complete row, then the child outlives the limit.
mk_lake "$W/lake_slow" "$GOOD_ROW" 0 9
rc="$(run_replay slow "$W/lake_slow")"
slow_dig="$(awk -F'\t' 'NR==2{print $15}' "$W/slow/rows.tsv" 2>/dev/null)"
slow_diag="$(awk -F'\t' 'NR==2{print $16}' "$W/slow/rows.tsv" 2>/dev/null)"
if [[ "$rc" -ne 0 && "$slow_dig" == "-" ]] && grep -qi 'timeout' <<<"$slow_diag"; then
  ok "replay refuses a run that printed a row and then timed out, and says so"
else
  bad "timed-out run: rc=$rc digest=[$slow_dig] diag=[$slow_diag]"
fi

# 5. THE SWEEP driver, same two legs, through its selftest corpus.
cat > "$W/emit.sh" <<EMIT
#!/usr/bin/env bash
# selftest emission helper: \$1 = work dir, \$2 = module.
mkdir -p "\$1/lean"
cp "$CERT" "\$1/lean/\$2.dcert"
EMIT
chmod +x "$W/emit.sh"
printf 'm1\n' > "$W/mods.list"

run_sweep() { # run_sweep <tag> <lake>
  LAKE="$2" SELFTEST_EMIT_CMD="$W/emit.sh" RUN_TIMEOUT=5 EMIT_TIMEOUT=30 \
    bash "$SWEEP" selftest "$W/mods.list" "$W/$1" > "$W/$1.log" 2>&1
  echo $?
}
mk_lake "$W/lake_ok2" "$GOOD_ROW" 0
run_sweep sok "$W/lake_ok2" > /dev/null
if grep -qE $'\tRAN\t[0-9]+\t111\t' "$W/sok/rows.tsv" 2>/dev/null; then
  ok "sweep publishes the digest when the runtime child exits 0"
else
  bad "sweep did not publish a clean selftest row"; tail -3 "$W/sok.log"
fi
mk_lake "$W/lake_172" "$GOOD_ROW" 17
run_sweep s17 "$W/lake_172" > /dev/null
if [[ "$(awk -F'\t' 'NR==2{print $15}' "$W/s17/rows.tsv" 2>/dev/null)" == "-" ]]; then
  ok "sweep publishes no digest when the runtime child exits 17"
else
  bad "sweep PUBLISHED a digest from a child that exited 17"
fi
if grep -q 'exited 17 after printing a complete row' "$W/s17/rows.tsv" 2>/dev/null; then
  ok "the failure row names the cause"
else
  bad "the failure row does not say why the row was refused"
  grep -m1 'm1' "$W/s17/rows.tsv" 2>/dev/null | cut -c1-160
fi

# ---------------------------------------------------------------------------
# PREFLIGHT ADMISSION, in BOTH drivers.
#
# Two legs the eight above do not reach:
#
#   6. the SWEEP's preflight ignored its child's status entirely, so a
#      correctly shaped row followed by exit 17 blessed a toolchain that
#      cannot run;
#   7. BOTH drivers looked for the string `READ_ERROR` anywhere in the row. A
#      perfectly successful `PARSED / true / RAN` row satisfies that as soon as
#      the words appear in its diagnostic PROSE -- so the preflight passed
#      without ever exercising the failure path it exists to test. The check is
#      now on the parsed FIELDS: READ_ERROR, NOT_RUN, no compile verdict, no
#      digest.
# ---------------------------------------------------------------------------
DECEPTIVE_PRE=$'4\t2\t1\t0\tPARSED\t1\ttrue\tRAN\t1\t111\tnote: a READ_ERROR row would be NOT_RUN'

mk_lake "$W/lake_spre" "$GOOD_ROW" 0 0 17
run_sweep spre "$W/lake_spre" > /dev/null
if grep -q 'preflight exited 17' "$W/spre.log"; then
  ok "sweep refuses a preflight that exits nonzero, and names the status"
else
  bad "sweep accepted a failing preflight"; sed -n '1,4p' "$W/spre.log"
fi

mk_lake "$W/lake_sdec" "$GOOD_ROW" 0 0 0 "$DECEPTIVE_PRE"
run_sweep sdec "$W/lake_sdec" > /dev/null
if grep -q 'did not report READ_ERROR/NOT_RUN' "$W/sdec.log"; then
  ok "sweep refuses a PARSED/true/RAN preflight that only MENTIONS READ_ERROR"
else
  bad "sweep accepted a successful row as its missing-file preflight"
  sed -n '1,4p' "$W/sdec.log"
fi

mk_lake "$W/lake_rdec" "$GOOD_ROW" 0 0 0 "$DECEPTIVE_PRE"
rc="$(run_replay rdec "$W/lake_rdec")"
if [[ "$rc" -ne 0 ]] && grep -q 'did not report READ_ERROR/NOT_RUN' "$W/rdec.log"; then
  ok "replay refuses a PARSED/true/RAN preflight that only MENTIONS READ_ERROR"
else
  bad "replay accepted a successful row as its missing-file preflight (rc=$rc)"
  sed -n '1,4p' "$W/rdec.log"
fi

if [[ $fails -eq 0 ]]; then echo "D4-EXIT-ADMISSION CONTROLS OK"; else echo "D4-EXIT-ADMISSION CONTROLS FAILED ($fails)"; fi
exit $fails

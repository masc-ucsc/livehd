#!/usr/bin/env bash
# Controls for the A/B churn PRODUCER.
#
# The analyzer has its own controls (`dcert_delta_controls.sh`); these are about
# the thing that turns a measurement into published evidence.  The failure that
# matters here is not a wrong number -- it is a RIGHT number attached to the
# wrong experiment.  Review found two of exactly that kind in the first version
# and both are controls below:
#
#   * the wrapper hashes were PRINTED and never compared, so replacing one with
#     `deadbeef` published happily;
#   * the gate tables were GUESSED as `$(dirname RUN)/gates.tsv`, so the real
#     finalization published gate evidence about a different run under a
#     different binary without noticing.
#
# Recording a hash is not checking it, and a gate table that passes is not a
# gate table about the artifact being measured.
#
# Usage: cva6_csr_regfile_dg_controls.sh <A-run> <A-gates> <B-run> <B-gates> [workdir]
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROD="$SCRIPT_DIR/cva6_csr_regfile_dg.sh"
die() { printf 'FATAL: %s\n' "$*" >&2; exit 2; }

A_RUN="${1:-}"; A_GATES="${2:-}"; B_RUN="${3:-}"; B_GATES="${4:-}"; WORK="${5:-$(mktemp -d)}"
[[ -n "$A_RUN" && -n "$A_GATES" && -n "$B_RUN" && -n "$B_GATES" ]] \
  || die "usage: $0 <A-run> <A-gates> <B-run> <B-gates> [workdir]"
[[ -s "$A_RUN/rows.tsv" && -s "$B_RUN/rows.tsv" ]] || die "both run dirs need a rows.tsv"
[[ -s "$A_GATES" && -s "$B_GATES" ]] || die "both gate tables must exist"
[[ -x "$PROD" ]] || die "missing producer $PROD"
mkdir -p "$WORK" || die "cannot create $WORK"
# ABSOLUTE.  A gate table names its run by absolute path and the producer
# resolves the run dir the same way, so a relative workdir would make every
# staged table look like it was about a different run.
WORK="$(cd "$WORK" && pwd)" || die "cannot resolve $WORK"
fail=0
ok()  { printf 'ok: %s\n' "$1"; }
bad() { printf 'CONTROL FAILED: %s\n' "$1"; fail=$((fail + 1)); }

# A writable copy of both sides.  The gate table names its run by ABSOLUTE path,
# so the staged copy's `# run` line is repointed at the staged location -- that
# is staging, and every negative below perturbs from this known-good baseline.
mk() {  # mk <dest-root>
  local d="$1"; rm -rf "$d"; mkdir -p "$d" || return 1
  cp -r "$A_RUN" "$d/Aout" && cp -r "$B_RUN" "$d/Bout" || return 1
  cp "$A_GATES" "$d/Agates.tsv" && cp "$B_GATES" "$d/Bgates.tsv" || return 1
  sed -i "s|^# run .*|# run             = $d/Aout|" "$d/Agates.tsv"
  sed -i "s|^# run .*|# run             = $d/Bout|" "$d/Bgates.tsv"
  return 0
}

run_prod() {  # run_prod <root> -> writes <root>/t.tsv, echoes exit
  local d="$1"
  "$PROD" "$d/Aout" "$d/Agates.tsv" "$d/Bout" "$d/Bgates.tsv" "$d/t.tsv" > "$d/log" 2>&1
  echo $?
}

refuse() {  # refuse <label> <root> <needle>
  local label="$1" d="$2" needle="$3" rc
  rc="$(run_prod "$d")"
  if [[ "$rc" == 0 ]]; then bad "$label: published (exit 0)"; return; fi
  if [[ -e "$d/t.tsv" ]]; then bad "$label: refused but left a table behind"; return; fi
  if ! grep -qF -- "$needle" "$d/log"; then
    bad "$label: refused, but not with '$needle' ($(head -c 110 "$d/log"))"; return
  fi
  ok "$label is refused by its own guard (exit $rc, no table, '$needle')"
}

# --- 1. the real pair publishes -------------------------------------------
mk "$WORK/pos" || die "cannot stage the positive"
if [[ "$(run_prod "$WORK/pos")" == 0 ]] && [[ -s "$WORK/pos/t.tsv" ]] \
   && grep -q '^structural_churn	' "$WORK/pos/t.tsv"; then
  ok "the real adjacent pair publishes a table carrying both churn metrics"
else
  bad "the real pair did not publish ($(tail -c 170 "$WORK/pos/log"))"
fi

# --- 2. a side that did not RAN is a failed EXPERIMENT, not a 100 % delta --
mk "$WORK/notran" || die "stage"
awk -F'\t' 'BEGIN{OFS="\t"} NR>1 && $2=="csr_regfile"{$13="NOT_RUN"} {print}' \
  "$WORK/notran/Bout/rows.tsv" > "$WORK/notran/x" && mv "$WORK/notran/x" "$WORK/notran/Bout/rows.tsv"
refuse "a side whose block never ran" "$WORK/notran" "did not independently pass"

# --- 3. a row whose certificate hash does not match the bytes it names -----
mk "$WORK/rowhash" || die "stage"
awk -F'\t' 'BEGIN{OFS="\t"} NR>1 && $2=="csr_regfile"{$5="0000000000000000000000000000000000000000000000000000000000000000"} {print}' \
  "$WORK/rowhash/Aout/rows.tsv" > "$WORK/rowhash/x" && mv "$WORK/rowhash/x" "$WORK/rowhash/Aout/rows.tsv"
refuse "a row whose cert_sha256 does not match the retained bytes" "$WORK/rowhash" "does not match the retained bytes"

# --- 4..7. THE GATE BINDING, which review found entirely absent -----------
mk "$WORK/grun" || die "stage"
sed -i "s|^# run .*|# run             = /unrelated/stale/run|" "$WORK/grun/Bgates.tsv"
refuse "a gate table about a different run" "$WORK/grun" "about a different run"

mk "$WORK/glhd" || die "stage"
sed -i "s|^# lhd_sha256 .*|# lhd_sha256      = $(printf 'd%.0s' {1..64})|" "$WORK/glhd/Bgates.tsv"
refuse "a gate table produced with a different lhd" "$WORK/glhd" "but the run records"

mk "$WORK/gtool" || die "stage"
sed -i "s|^# gate_tool_sha256 .*|# gate_tool_sha256 = $(printf 'e%.0s' {1..64})|" "$WORK/gtool/Bgates.tsv"
refuse "the two sides gated by different gate tools" "$WORK/gtool" "different gate tools"

mk "$WORK/gcert" || die "stage"
awk -F'\t' 'BEGIN{OFS="\t"} $1=="csr_regfile"{$7="1111111111111111111111111111111111111111111111111111111111111111"} {print}' \
  "$WORK/gcert/Bgates.tsv" > "$WORK/gcert/x" && mv "$WORK/gcert/x" "$WORK/gcert/Bgates.tsv"
refuse "a gate table that gated a different certificate" "$WORK/gcert" "gated a different certificate"

mk "$WORK/gfail" || die "stage"
sed -i 's/\tPASS\tPASS\tPASS\t/\tPASS\tPASS\tFAIL\t/' "$WORK/gfail/Bgates.tsv"
refuse "a side that did not pass all three gates" "$WORK/gfail" "all three gates"

mk "$WORK/gnohdr" || die "stage"
sed -i "/^# run /d" "$WORK/gnohdr/Bgates.tsv"
refuse "a gate table with no run line at all" "$WORK/gnohdr" "predates the explicit binding"

# --- 8..10. INPUT IDENTITY, which review found recorded but not enforced --
mk "$WORK/wrap" || die "stage"
sed -i 's/^cva6_wrappers_declared_sha256=.*/cva6_wrappers_declared_sha256=deadbeef/' \
  "$WORK/wrap/Bout/provenance.txt"
refuse "the two sides built from different wrappers" "$WORK/wrap" "the wrapper corpus"

mk "$WORK/cfg" || die "stage"
sed -i 's/^cva6_config_pkg_sha256=.*/cva6_config_pkg_sha256=deadbeef/' \
  "$WORK/cfg/Bout/provenance.txt"
refuse "the two sides built from different config packages" "$WORK/cfg" "the config package"

# TWO guards cover this and they fire in order, so each gets its own control.
# Corrupting only the provenance is caught by the GATE binding, because the gate
# table still names the real binary.
mk "$WORK/bin1" || die "stage"
sed -i 's/^lhd_sha256=.*/lhd_sha256=deadbeef/' "$WORK/bin1/Bout/provenance.txt"
refuse "a run whose provenance disagrees with its own gate table about the binary" \
  "$WORK/bin1" "but the run records"

# Moving BOTH consistently on one side gets past that and must then be caught by
# the cross-side check -- otherwise the two halves of the experiment could have
# been produced by different compilers.
mk "$WORK/bin2" || die "stage"
FAKE="$(printf 'f%.0s' {1..64})"
sed -i "s|^lhd_sha256=.*|lhd_sha256=$FAKE|" "$WORK/bin2/Bout/provenance.txt"
sed -i "s|^# lhd_sha256 .*|# lhd_sha256      = $FAKE|" "$WORK/bin2/Bgates.tsv"
refuse "the two sides built by different binaries" "$WORK/bin2" "the lhd binary"

mk "$WORK/lim" || die "stage"
sed -i 's/^emit_max_rss_kb=.*/emit_max_rss_kb=99999999/' "$WORK/lim/Bout/provenance.txt"
refuse "the two sides run at different emission ceilings" "$WORK/lim" "the emission ceiling"

# --- 11..13. THE RETAINED SOURCE STATE ------------------------------------
mk "$WORK/root" || die "stage"
sed -i "s|^cva6_root=.*|cva6_root=$WORK/root/nosuchroot|" "$WORK/root/Bout/provenance.txt"
refuse "a retained root that is gone" "$WORK/root" "is gone"

mk "$WORK/rev" || die "stage"
sed -i 's/^cva6_root_rev=.*/cva6_root_rev=0000000000000000000000000000000000000000/' \
  "$WORK/rev/Aout/provenance.txt"
refuse "a retained root that is not at its recorded revision" "$WORK/rev" "not the recorded"

mk "$WORK/same" || die "stage"
cp "$WORK/same/Aout/provenance.txt" "$WORK/same/Bout/provenance.txt"
refuse "both sides naming the same revision" "$WORK/same" "no delta to measure"

# --- 14. the filelist must differ ONLY by its source path -----------------
mk "$WORK/fl" || die "stage"
FL_B="$(grep -m1 '^cva6_filelist=' "$WORK/fl/Bout/provenance.txt" | cut -d= -f2-)"
cp "$FL_B" "$WORK/fl/flist.f" && echo "/an/extra/source.sv" >> "$WORK/fl/flist.f"
sed -i "s|^cva6_filelist=.*|cva6_filelist=$WORK/fl/flist.f|" "$WORK/fl/Bout/provenance.txt"
refuse "filelists differing by more than their source path" "$WORK/fl" "more than their source path"

# --- 15. a failing run leaves NO table at the requested path --------------
mk "$WORK/stale" || die "stage"
run_prod "$WORK/stale" >/dev/null
if [[ -s "$WORK/stale/t.tsv" ]]; then
  sed -i 's/^lhd_sha256=.*/lhd_sha256=deadbeef/' "$WORK/stale/Bout/provenance.txt"
  run_prod "$WORK/stale" >/dev/null
  if [[ -e "$WORK/stale/t.tsv" ]]; then
    bad "a failing run left the PREVIOUS table at the requested path"
  else
    ok "a failing run leaves no stale table at the requested path"
  fi
else
  bad "could not establish a previous table for the stale-table control"
fi

# --- 16. a non-publishable destination ------------------------------------
mk "$WORK/dest" || die "stage"
mkdir -p "$WORK/dest/adir"
"$PROD" "$WORK/dest/Aout" "$WORK/dest/Agates.tsv" "$WORK/dest/Bout" "$WORK/dest/Bgates.tsv" \
  "$WORK/dest/adir" > "$WORK/dest/log" 2>&1
rc=$?
if [[ "$rc" != 0 ]] && grep -qF "is a directory" "$WORK/dest/log"; then
  ok "a directory destination is refused by the destination contract"
else
  bad "a directory destination was accepted (exit $rc)"
fi

# --- 17. the analyzer's own failure is FATAL to the producer --------------
mk "$WORK/tool" || die "stage"
# The producer invokes the analyzer THROUGH $PY, so the stand-in has to be a
# Python program -- a shell script would exit 1 on a syntax error and the
# control would pass for the wrong reason.
printf 'import sys\nsys.exit(9)\n' > "$WORK/tool/fake.py"
D4_DG_TOOL="$WORK/tool/fake.py" "$PROD" "$WORK/tool/Aout" "$WORK/tool/Agates.tsv" \
  "$WORK/tool/Bout" "$WORK/tool/Bgates.tsv" "$WORK/tool/t.tsv" > "$WORK/tool/log" 2>&1
rc=$?
if [[ "$rc" != 0 ]] && [[ ! -e "$WORK/tool/t.tsv" ]] && grep -qF "analyzer exited 9" "$WORK/tool/log"; then
  ok "an analyzer that exits nonzero is fatal to the producer (no table)"
else
  bad "the producer survived an analyzer failure (exit $rc)"
fi

if [[ "$fail" -eq 0 ]]; then echo "PASS: cva6_csr_regfile_dg_controls"; exit 0; fi
echo "FAIL: $fail control(s)"
exit 1

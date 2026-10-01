#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Pins the helpers in scripts/proc_ifx_scc_compare.sh against the bash `local`
# trap that silently blanked a full 11-row evidence table.
#
#     local top="$1" d="$OUTROOT/$top/plain"
#
# Bash expands EVERY word of a `local` command before performing ANY of its
# assignments, so `$top` is still unset when `$OUTROOT/$top/plain` is expanded.
# The script runs under `set -u`, so the helper aborted with
# "top: unbound variable" on every row -- and because each row's output was
# captured into a field, the table still PRINTED, just with an empty column.
# A silent blank column in a results table is the dangerous shape here: it reads
# as "measured nothing unusual" rather than as a crash.
#
# This test calls the helper the way the sweep does, with set -u active, and
# requires a nonblank verdict.
set -u

# Under bazel the script arrives through runfiles (//scripts:cycle_provenance_tools);
# run by hand it is found relative to this file.
SCRIPT=""
for c in "${TEST_SRCDIR:-}/_main/scripts/proc_ifx_scc_compare.sh" \
         "${TEST_SRCDIR:-}/scripts/proc_ifx_scc_compare.sh" \
         "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." 2>/dev/null && pwd)/scripts/proc_ifx_scc_compare.sh" \
         "scripts/proc_ifx_scc_compare.sh"; do
  if [ -r "$c" ]; then SCRIPT="$c"; break; fi
done
[ -n "$SCRIPT" ] || { echo "FAIL: cannot find proc_ifx_scc_compare.sh"; exit 1; }

OUTDIR="${OUTDIR:-${TEST_TMPDIR:-$(cd "$(dirname "$SCRIPT")/.." && pwd)/generated/tests}/proc_ifx_helper}"
rm -rf "$OUTDIR"; mkdir -p "$OUTDIR"

# Source for the helpers only -- no sweep, no lhd, no yosys.
#
# These are PLAIN assignments, not a `VAR=x . script` prefix: bash restores
# prefix assignments when the builtin returns, so the script's own
# `OUTROOT="${OUTROOT:-...}"` would be reverted the moment sourcing finished and
# the helper would then see OUTROOT unset. (Found by this test.)
PROC_IFX_LIB_ONLY=1
OUTROOT="$OUTDIR"
LHD=/bin/true
export PROC_IFX_LIB_ONLY OUTROOT LHD
. "$SCRIPT" || { echo "FAIL: sourcing the script errored"; exit 1; }

# Sourcing derives the plain-proc script from the real inou_yosys_read.ys and
# hard-fails if `proc -ifx` is no longer there to substitute, so reaching this
# line also means that substitution still matches the shipped script.
echo "ok: the proc -ifx -> proc substitution still applies to inou_yosys_read.ys"

type lean_on_plain >/dev/null 2>&1 || { echo "FAIL: lean_on_plain not defined"; exit 1; }
type elaborate     >/dev/null 2>&1 || { echo "FAIL: elaborate not defined"; exit 1; }

# No lg/ directory exists for this module, so the helper must return its
# defined compile-failure sentinel. Before the fix it died on the unbound $top.
out="$(lean_on_plain fake_module_with_no_lg 2>"$OUTDIR/stderr.txt")"
rc=$?

if grep -q "unbound variable" "$OUTDIR/stderr.txt"; then
  echo "FAIL: the bash local-expansion trap is back:"; sed 's/^/    /' "$OUTDIR/stderr.txt"; exit 1
fi
[ -n "$out" ] || { echo "FAIL: helper returned a BLANK verdict (rc=$rc) -- a blank table column reads as 'measured, nothing odd'"; exit 1; }
# The sentinel must say COMPILE, not anything that could be read as a pass.lean
# verdict: the plain-proc elaboration really can fail on its own (measured:
# intpipe_csr_file and minion_dcache_top abort with latch-contract rule C under
# plain proc while compiling cleanly under -ifx), and reporting that as a Lean
# result would misattribute it.
case "$out" in
  PLAIN-COMPILE-FAIL:*) ;;
  *) echo "FAIL: expected a PLAIN-COMPILE-FAIL:* sentinel for a module with no graph, got '$out'"; exit 1 ;;
esac
echo "ok: lean_on_plain returns '$out' instead of dying on an unbound variable"

echo "PASS: proc_ifx_compare_helper_test"

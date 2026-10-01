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

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="$ROOT/../scripts/proc_ifx_scc_compare.sh"
[ -r "$SCRIPT" ] || SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/scripts/proc_ifx_scc_compare.sh"
[ -r "$SCRIPT" ] || { echo "FAIL: cannot find proc_ifx_scc_compare.sh"; exit 1; }

OUTDIR="${OUTDIR:-$(cd "$(dirname "$SCRIPT")/.." && pwd)/generated/tests/proc_ifx_helper}"
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

type lean_on_plain >/dev/null 2>&1 || { echo "FAIL: lean_on_plain not defined"; exit 1; }
type elaborate     >/dev/null 2>&1 || { echo "FAIL: elaborate not defined"; exit 1; }

# No lg/ directory exists for this module, so the helper must return its
# defined "no-lg" sentinel. Before the fix it died on the unbound $top instead.
out="$(lean_on_plain fake_module_with_no_lg 2>"$OUTDIR/stderr.txt")"
rc=$?

if grep -q "unbound variable" "$OUTDIR/stderr.txt"; then
  echo "FAIL: the bash local-expansion trap is back:"; sed 's/^/    /' "$OUTDIR/stderr.txt"; exit 1
fi
[ -n "$out" ] || { echo "FAIL: helper returned a BLANK verdict (rc=$rc) -- a blank table column reads as 'measured, nothing odd'"; exit 1; }
[ "$out" = "no-lg" ] || { echo "FAIL: expected 'no-lg' for a module with no graph, got '$out'"; exit 1; }
echo "ok: lean_on_plain returns '$out' instead of dying on an unbound variable"

echo "PASS: proc_ifx_compare_helper_test"

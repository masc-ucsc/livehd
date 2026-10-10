#!/usr/bin/env bash
set -uo pipefail
# Controls for the trace-count admission in mem_lean_diffsim.py.
#
# The historical script compared `min(len(rtl_rows), len(lean_rows))` against the
# expected period count, so the LONGER trace was truncated away. Measured on it:
#
#   rtl=13 lean=12 expecting 12  -> PASS   (should fail)
#   rtl=12 lean=13 expecting 12  -> PASS   (should fail)
#   rtl=12 lean=12 expecting 12  -> PASS   (correct)
#   rtl=11 lean=12 expecting 12  -> FAIL   (correct: min falls below)
#
# What `min` catches is a trace SHORT on either side; its blind spot is extra
# rows, on either side -- a simulator emitting one period too many passed.
#
# These call the TOOL'S OWN `shape_error`, not a copy of it, so they test the
# rule that actually runs. They do not exercise the simulator: a full run needs
# iverilog and pinned Lean, and what was wrong here was the admission rule.
#
# Usage: mem_lean_diffsim_controls.sh

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rc=0
HELPER="$R/generated/pass_lean_runtime_tmp/diffsim_probe.py"
mkdir -p "$(dirname "$HELPER")"
cat > "$HELPER" <<'PYEOF'
import importlib.util, sys
spec = importlib.util.spec_from_file_location("ds", sys.argv[1])
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
n_rtl, n_lean, want, cols, short_col = (int(x) for x in sys.argv[2:7])
names = [f"o{i}" for i in range(cols)]
rtl = [["1"] * cols for _ in range(n_rtl)]
lean = [["1"] * cols for _ in range(n_lean)]
if short_col and rtl:
    rtl[-1] = rtl[-1][:-1]          # one row a column short
print("FAIL" if m.shape_error(rtl, lean, want, names) else "PASS")
PYEOF

probe() { python3 -I "$HELPER" "$R/scripts/mem_lean_diffsim.py" "$1" "$2" "$3" "${4:-2}" "${5:-0}"; }
expect() {  # <name> <rtl> <lean> <want> <expect> [cols] [short-col]
  local got; got="$(probe "$2" "$3" "$4" "${6:-2}" "${7:-0}")"
  [[ "$got" == "$5" ]] && echo "ok: $1 -> $got" \
    || { echo "FAIL: $1 -> ${got:-none}, expected $5"; rc=1; }
}

expect exact_match    12 12 12 PASS
expect rtl_one_extra  13 12 12 FAIL
expect lean_one_extra 12 13 12 FAIL
expect rtl_one_short  11 12 12 FAIL
expect lean_one_short 12 11 12 FAIL
expect both_extra     13 13 12 FAIL
expect short_column   12 12 12 FAIL 2 1

[[ $rc -eq 0 ]] && echo "PASS: both traces must have the expected length and shape"
exit $rc

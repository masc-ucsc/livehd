#!/usr/bin/env bash
# sym_observe_test.sh: scripts/sym_observe.py, which adds observer outputs to a copy of a Verilog
# module (Python only, about a second). Checks: a module with a parameter list and a comment in
# its ports gets two observer outputs and a declaration, in the ANSI header and before
# endmodule, and its source file is not modified; refused: a module that is not in the file, a
# module with no port list, an observer that is not name:width=expr, and a non-ANSI header
# (ports declared in the body), which the added outputs would turn into mixed-style Verilog.
set -euo pipefail
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
S="$R/pass/lean/tools/scripts/sym_observe.py"
T="${OUT:-$R/generated/master_port/sym_observe_test}"
PY="${PYTHON:-python3}"
rm -rf -- "$T"; mkdir -p "$T"
fail() { echo "FAIL: $*"; exit 1; }
printf 'module m #(parameter W = 4) (input clk, input [W-1:0] a, output y);\n  reg [W-1:0] r; // (not a port)\n  always @(posedge clk) r <= a;\n  assign y = r[0];\nendmodule\n' > "$T/m.v"
cp "$T/m.v" "$T/m.orig"
out=$("$PY" -I "$S" "$T/m.v" m "$T/m_obs.v" 'r_o:4=r' 'lo:1=r[0]' --decl 'reg seen; always @(posedge clk) seen <= 1'"'"'b1;')
[[ "$out" == "$T/m_obs.v: m + 2 observer output(s), 1 declaration(s)" ]] || fail "report: $out"
grep -q 'output \[3:0\] r_o /\* sym_observe \*/' "$T/m_obs.v" || fail "no r_o port"
grep -q 'output \[0:0\] lo /\* sym_observe \*/' "$T/m_obs.v" || fail "no lo port"
grep -q 'assign lo = r\[0\];  // sym_observe' "$T/m_obs.v" || fail "no assign of lo"
grep -q 'reg seen; always @(posedge clk) seen <= 1.b1;  // sym_observe' "$T/m_obs.v" || fail "no declaration"
[[ $(grep -n 'sym_observe \*/' "$T/m_obs.v" | head -1 | cut -d: -f1) -lt $(grep -n 'assign y' "$T/m_obs.v" | cut -d: -f1) ]] \
  || fail "the ports are not in the header"
cmp -s "$T/m.v" "$T/m.orig" || fail "the source was modified"
echo "ok: two observer outputs and a declaration added; the source is unchanged"
refused() { # <what> <message> args...
  local what=$1 msg=$2; shift 2
  if "$PY" -I "$S" "$@" > "$T/err.log" 2>&1; then cat "$T/err.log"; fail "$what was accepted"; fi
  grep -qF -- "$msg" "$T/err.log" || { cat "$T/err.log"; fail "$what: no '$msg'"; }
  [[ ! -e "$T/x.v" ]] || fail "$what wrote a file"
  echo "ok: refused: $what"
}
refused "a module that is not in the file" 'no module nosuch' "$T/m.v" nosuch "$T/x.v" 'a:1=1'
printf 'module n;\nendmodule\n' > "$T/n.v"
refused "a module with no port list" 'module n: no ANSI port list' "$T/n.v" n "$T/x.v" 'a:1=1'
refused "an observer that is not name:width=expr" 'expected name:width=expr' "$T/m.v" m "$T/x.v" 'bad'
printf 'module p(clk, /* the data */ d);\n  input clk; input [3:0] d;\nendmodule\n' > "$T/p.v"
refused "a non-ANSI header" 'module p: a non-ANSI port list' "$T/p.v" p "$T/x.v" 'a:1=1'
echo "PASS sym_observe_test"

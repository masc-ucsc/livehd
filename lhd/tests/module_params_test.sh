#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A parameterized CORE-ET top is elaborated with its tracked parameters, on BOTH
# sides.
#
# `txfma_adder` declares `parameter int unsigned Width = 0` and is correct when
# instantiated; as a TOP it elaborates to `logic [-1:0]` ports and slang refuses
# it -- the whole of its "compile-failed" census row.  pass/lean/MODULE_PARAMS.tsv
# supplies `-G Width=10`.
#
# Three things are checked:
#   * the tracked table says what it is supposed to say, and every row has all
#     three columns (a missing tab makes `why` part of the FLAGS and word-splits
#     prose onto the slang command line);
#   * the parameters reach BOTH reads -- the implementation and the LEC
#     reference. Giving them to only one elaborates two different designs and
#     refutes them against each other with nothing wrong in the tool, which is a
#     very expensive way to learn about a missing flag. Structural, because
#     running the real flow needs core-et, which is not a runfile;
#   * `-G` really does what it is being trusted to do: on a self-contained
#     fixture of the same shape, elaboration is REFUSED without it and produces
#     the intended port widths with it.
#
# What this does NOT check is core-et's own txfma_adder, which is not reachable
# from a sandbox. That it compiles, passes a strict LEC gate and is ACCEPTED for
# 4 cycles is separate, recorded evidence -- census run
# e292add9f_20261001-220602_3277308_13522 and pass/lean/COVERAGE_LEDGER.md.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." 2>/dev/null && pwd)"
RUNNER=""
for c in "${TEST_SRCDIR:-}/_main/scripts/run_coreet_module_lean.sh" \
         "$ROOT/scripts/run_coreet_module_lean.sh" "scripts/run_coreet_module_lean.sh"; do
  [ -r "$c" ] && { RUNNER="$c"; break; }
done
TSV=""
for c in "${TEST_SRCDIR:-}/_main/pass/lean/MODULE_PARAMS.tsv" \
         "$ROOT/pass/lean/MODULE_PARAMS.tsv" "pass/lean/MODULE_PARAMS.tsv"; do
  [ -r "$c" ] && { TSV="$c"; break; }
done
[ -n "$RUNNER" ] || { echo "FAIL: cannot find run_coreet_module_lean.sh"; exit 1; }
[ -n "$TSV" ]    || { echo "FAIL: cannot find MODULE_PARAMS.tsv"; exit 1; }
rc=0

# ---- the table says what it is supposed to say ----------------------------
row="$(grep -v '^#' "$TSV" | grep -P '^txfma_adder\t' | head -1)"
if [ -z "$row" ]; then
  echo "FAIL: MODULE_PARAMS.tsv has no txfma_adder row, so the module is unelaborable again"
  rc=1
elif ! printf '%s' "$row" | grep -q -- '-G Width=10'; then
  echo "FAIL: txfma_adder's parameters are $(printf '%s' "$row" | cut -f2), not -G Width=10"
  rc=1
else
  echo "ok: MODULE_PARAMS.tsv pins txfma_adder at -G Width=10"
fi
# Every row must have all three columns, or `why` silently becomes part of the
# flags and gets word-split onto the slang command line.
while IFS= read -r line; do
  [ -z "$line" ] && continue
  n="$(printf '%s' "$line" | awk -F'\t' '{print NF}')"
  if [ "$n" -ne 3 ]; then
    echo "FAIL: MODULE_PARAMS.tsv row has $n tab-separated fields, not 3: ${line:0:60}"
    rc=1
  fi
done < <(grep -v '^#' "$TSV" | grep -v '^[[:space:]]*$')

# ---- BOTH elaborations receive them ---------------------------------------
# Structural, because running the real flow here needs core-et and a yosys read.
# The LEC leg is the one that was missing in the plan's own description, so it
# is pinned by name rather than by "the variable is mentioned somewhere".
if grep -q 'MODULE_PARAMS\[@\]' "$RUNNER"; then
  n="$(grep -c 'MODULE_PARAMS\[@\]' "$RUNNER")"
  if [ "$n" -lt 2 ]; then
    echo "FAIL: the module parameters reach only $n elaboration; the implementation and"
    echo "      the LEC reference must both get them or LEC compares two different designs"
    rc=1
  else
    echo "ok: the module parameters reach $n elaborations (implementation + LEC reference)"
  fi
else
  echo "FAIL: run_coreet_module_lean.sh does not use MODULE_PARAMS at all"
  rc=1
fi
if grep -A3 'declare -a ref_slang=' "$RUNNER" | grep -q 'MODULE_PARAMS'; then
  echo "ok: the LEC reference read gets them specifically"
else
  echo "FAIL: ref_slang does not pick up MODULE_PARAMS, so the reference elaborates"
  echo "      the design WITHOUT its parameters"
  rc=1
fi

# ---- `-G` actually works, on a fixture of the same shape --------------------
LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  echo "FAIL: no lhd binary, so the elaboration half of this test cannot run"
  rc=1
else
  LHD="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
  if [ -n "${TEST_TMPDIR:-}" ]; then T="$TEST_TMPDIR/module_params_runtime"
  else T="$ROOT/generated/module_params_test/runtime_tmp"; fi
  rm -rf "$T"; mkdir -p "$T"
  FIX="$HERE/param_width_leaf.v"
  [ -r "$FIX" ] || { echo "FAIL: missing fixture $FIX"; rc=1; }

  # WITHOUT the parameter: `logic [-1:0]` ports, and slang must refuse.
  # Without this half, a `-G` that was silently dropped would still "pass".
  ( cd "$T" && "$LHD" compile verilog "$FIX" --top param_width_leaf --reader yosys-slang \
      --workdir w0 --emit-dir lg:lg0 ) > "$T/no_param.log" 2>&1
  if [ $? -eq 0 ]; then
    echo "FAIL: the fixture elaborated WITHOUT -G, so it no longer reproduces the"
    echo "      unparameterized-top failure and the other half proves nothing"
    rc=1
  else
    echo "ok: the fixture is refused without -G (Width=0 gives [-1:0] ports)"
  fi

  # WITH it: elaborates, and the PORTS are the width asked for.
  #
  # The ports, not "the widest pin in the graph": this design's internal carry
  # chain is 11 and 12 bits wide, so a max-over-all-pins check reports 12 and
  # says nothing about the parameter. The Lean IO sidecar names each port and
  # its width, which is exactly the claim.
  ( cd "$T" && "$LHD" compile verilog "$FIX" --top param_width_leaf --reader yosys-slang \
      --workdir w1 --emit-dir lg:lg1 -- -G Width=10 ) > "$T/with_param.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "FAIL: the fixture did not elaborate with -G Width=10"
    tail -3 "$T/with_param.log" | sed 's/^/      /'
    rc=1
  elif ! "$LHD" compile "lg:$T/lg1" --top param_width_leaf --workdir "$T/wl" \
         --emit-dir "lean:$T/lean" --set formal.lean.mode=verified_compiler \
         --set formal.lean.strict=true > "$T/lean.log" 2>&1; then
    echo "FAIL: no certificate from the parameterized fixture"
    tail -3 "$T/lean.log" | sed 's/^/      /'
    rc=1
  else
    SIDE="$T/lean/param_width_leaf_io.json"
    SIDECAR="$SIDE" python3 - <<'PYW' || rc=1
import json, os, sys
d = json.load(open(os.environ["SIDECAR"]))
got = {i["name"]: i["width"] for i in d["inputs"]}
got.update({o["name"]: o["width"] for o in d["outputs"]})
want = {"a_i": 10, "b_i": 10, "cin_i": 1, "sum_o": 10}
bad = {k: (want[k], got.get(k)) for k in want if got.get(k) != want[k]}
if bad:
    print("FAIL: -G Width=10 did not set the parameter; port widths are wrong:")
    for k, (w, g) in sorted(bad.items()):
        print(f"      {k}: want {w}b, got {g}b")
    sys.exit(1)
print("ok: -G Width=10 produces the intended PORT widths "
      + ", ".join(f"{k}={want[k]}b" for k in ("a_i", "b_i", "cin_i", "sum_o")))
PYW
  fi
fi

[ "$rc" -eq 0 ] || { echo "FAIL: module_params_test"; exit 1; }
echo "PASS: module_params_test"

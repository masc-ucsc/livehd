#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# REGRESSION: `formal.min_timeout`'s DEFAULT must be the one pass/lec registers.
#
# The knob has exactly ONE registration (pass/lec/pass_lec.cpp, "20") and the
# struct default agrees (pass/lec/query.hpp, `int min_timeout = 20`), but both
# `lhd` kernel entry points independently re-spell the fallback and had drifted
# to "1" -- the value query.hpp itself calls unusable: "a 1s floor cannot earn a
# real verdict on a non-trivial def" and "mints Unknowns that read as design
# problems when they are pure scheduling".  On a wide design that silently
# turned scheduling starvation into UNKNOWN verdicts (measured: lhdsuite
# //verif:genprp_xs_exu reported `3 on the 1s floor` and went INCONCLUSIVE with
# 494/500 defs proven).
#
# Nothing caught it because EVERY other budget test pins `formal.min_timeout`
# explicitly, so the default was never exercised.  This test asserts the
# reported floor with NO override, which is the only thing that pins it.
set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecmintimeout}"
mkdir -p "$WORK"
fail=0

# Two DISTINCT hard leaf defs + a top, so the soft total is spent before the DAG
# drains and at least one def is dispatched onto the floor.  Same masked-mul
# reassoc shape lec_budget_test.sh uses: equivalent, but the bit-blast freezes,
# so a leaf burns whatever grant it is given (and `& 16'hF0F0` keeps int_blast
# from finishing it either).
mkhier() {  # $1=basename $2=ref|impl
  local e i; if [ "$2" = ref ]; then e='(a*b)*c'; else e='a*(b*c)'; fi
  { for ((i = 0; i < 2; i++)); do
      echo "module mul3_$i(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);"
      echo "  assign z = (($e) & 16'hF0F0) ^ 16'd$i;"
      echo "endmodule"
    done
    echo -n "module top(input [15:0] a, input [15:0] b, input [15:0] c"
    for ((i = 0; i < 2; i++)); do echo -n ", output [15:0] z$i"; done
    echo ");"
    for ((i = 0; i < 2; i++)); do
      echo "  mul3_$i u$i(.a(a ^ 16'd$i), .b(b), .c(c), .z(z$i));"
    done
    echo "endmodule"
  } > "$WORK/${1}_$2.v"
}
mkhier wide ref; mkhier wide impl

# $1=label  $2=expected floor seconds  $3...=extra --set flags
check_floor() {
  local label=$1 want=$2; shift 2
  local log="$WORK/$label.log"
  "$LHD" lec --impl "verilog:$WORK/wide_impl.v" --ref "verilog:$WORK/wide_ref.v" \
    --top top --workdir "$WORK/wd_$label" \
    --set formal.timeout=1 "$@" >"$log" 2>&1
  # The budget line only prints once the soft total is spent (or a def floored),
  # which is exactly the situation this test constructs.
  if ! grep -q "lec\[hier\]: budget " "$log"; then
    echo "FAIL($label): no budget report -- the soft total was never spent, so the floor is untested" >&2
    tail -5 "$log" >&2
    return 1
  fi
  if ! grep -q "on the ${want}s floor" "$log"; then
    echo "FAIL($label): expected 'on the ${want}s floor', got:" >&2
    grep -o "lec\[hier\]: budget .*" "$log" >&2
    return 1
  fi
  echo "PASS($label): floor reported as ${want}s"
  return 0
}

# 1) THE REGRESSION: no override -> the registered default (20), not 1.
check_floor default 20 || fail=1

# 2) Control: an explicit override still wins, so (1) is pinning the DEFAULT and
#    not merely a hardcoded constant in the report.
check_floor override 3 --set formal.min_timeout=3 || fail=1

[ "$fail" -eq 0 ] && echo "PASS: formal.min_timeout default matches its registration" \
                 || echo "FAIL: formal.min_timeout default drifted from its registration"
exit "$fail"

#!/usr/bin/env bash
# Controls for cert_shape_check.py, on mutations of real certificates.
#
# The checker's whole value is that an out-of-range reference becomes a named
# failure in milliseconds instead of a Lean error minutes later, so what has to
# be controlled is that it actually FAILS on one -- and that a failure to PARSE
# a certificate is reported as a parser failure, not as a shape failure in the
# design.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CHK="$ROOT/pass/lean/scripts/cert_shape_check.py"
J="$ROOT/generated/master_port/replay/jobs"
WORK="$ROOT/generated/master_port/cert_shape_controls"
PLAIN="$J/CORE-ET/verified_compiler/txfma_e4/lean/txfma_e4_Lgraph.lean"
ZERO="$J/CORE-ET/verified_compiler/minion_dcache_texsend/lean/minion_dcache_texsend_Lgraph.lean"
FLOPS="$J/CORE-ET/verified_compiler/intpipe_csr_replay/lean/intpipe_csr_replay_Lgraph.lean"
MEMS="$J/CORE-ET/verified_compiler/intpipe_csr_msgs/lean/intpipe_csr_msgs_Lgraph.lean"

for f in "$PLAIN" "$ZERO" "$FLOPS" "$MEMS"; do
  [ -f "$f" ] || { echo "REFUSE: missing fixture $f" >&2; exit 2; }
done
rm -rf "$WORK"; mkdir -p "$WORK"
fails=0

case_run() { # name want_exit file [grep-for]
  local name="$1" want="$2" f="$3" want_txt="${4:-}" got
  python3 -I "$CHK" "$f" > "$WORK/$name.log" 2>&1; got=$?
  if [ "$got" -ne "$want" ]; then
    echo "MISS: $name -> exit $got, wanted $want"; sed -n '1,5p' "$WORK/$name.log"
    fails=$((fails+1)); return
  fi
  if [ -n "$want_txt" ] && ! grep -q "$want_txt" "$WORK/$name.log"; then
    echo "MISS: $name -> exit $got but did not say '$want_txt'"; sed -n '1,5p' "$WORK/$name.log"
    fails=$((fails+1)); return
  fi
  echo "ok: $name -> exit $got  $(grep -m1 -E '^(ok|FAIL|note)' "$WORK/$name.log" | cut -c1-62)"
}

mut() { # src out python-over-s
  python3 -I -c "
import sys, re
s = open(sys.argv[1]).read()
$3
open(sys.argv[2],'w').write(s)" "$1" "$2"
}

# ---- accepted shapes ------------------------------------------------------
case_run plain_ok      0 "$PLAIN" "all references resolve"
case_run zero_node_ok  0 "$ZERO"  "zero-node design"
case_run flops_ok      0 "$FLOPS" "all references resolve"
case_run mems_ok       0 "$MEMS"  "all references resolve"

# ---- out-of-range references ---------------------------------------------
mut "$PLAIN" "$WORK/out_slot.lean" "s = s.replace('slot := 2', 'slot := 99', 1)"
case_run out_of_range_output 1 "$WORK/out_slot.lean" "output slot 99 is out of range"

mut "$PLAIN" "$WORK/out_dep.lean" "s = s.replace('deps := #[0, 1]', 'deps := #[0, 77]', 1)"
case_run out_of_range_dep 1 "$WORK/out_dep.lean" "node dep 77 is out of range"

mut "$FLOPS" "$WORK/out_din.lean" "s = re.sub(r'din := \d+', 'din := 999999', s, count=1)"
case_run out_of_range_flop_din 1 "$WORK/out_din.lean" "flop din 999999 is out of range"

mut "$MEMS" "$WORK/out_next.lean" "s = re.sub(r'nextImg := \d+', 'nextImg := 999999', s, count=1)"
case_run out_of_range_mem 1 "$WORK/out_next.lean" "memory nextImg 999999 is out of range"

# ---- a certificate that computes nothing ---------------------------------
mut "$PLAIN" "$WORK/no_obs.lean" "
import re
s = re.sub(r'outputs  := #\[.*?\]', 'outputs  := #[]', s, count=1, flags=re.S)"
case_run no_observable 1 "$WORK/no_obs.lean" "computes nothing observable"

# ---- PARSER drift must say so, not blame the design ----------------------
# Every reference would look out of range once the node count silently falls to
# zero; the checker has to distinguish "I cannot read this" from "this is bad".
mut "$PLAIN" "$WORK/drift.lean" "s = s.replace('origin :=', 'provenance :=')"
case_run parser_drift_nodes 1 "$WORK/drift.lean" "PARSER failure"
if grep -q 'out of range' "$WORK/parser_drift_nodes.log"; then
  echo " -- MISS: parser drift was reported as an out-of-range shape failure"
  fails=$((fails+1))
fi

mut "$PLAIN" "$WORK/drift2.lean" "s = s.replace('SourceDesc.', 'SrcDesc.')"
case_run parser_drift_sources 1 "$WORK/drift2.lean" "PARSER failure"

mut "$PLAIN" "$WORK/no_sec.lean" "
import re
s = re.sub(r'^\s*outputs  :=', '    outs     :=', s, count=1, flags=re.M)"
case_run missing_section 1 "$WORK/no_sec.lean" "PARSER failure"

case_run unreadable 1 "$WORK/does_not_exist.lean" "cannot read certificate"

if [ "$fails" -eq 0 ]; then
  echo "PASS: references must resolve, and a parse failure says so"
else
  echo "FAIL: $fails control(s) missed"
fi
exit $((fails > 0))

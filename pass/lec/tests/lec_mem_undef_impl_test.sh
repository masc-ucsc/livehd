#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# ordering="none" leaves a read that collides with a write UNDEFINED. On the
# reference side that is a don't-care; on the IMPLEMENTATION side it is an
# arbitrary value, which cannot match the defined "old" value a reference
# promises. LEC used to model the undefined read only on the reference, so an
# ordering="none" implementation of an ordering="old" reference came back
# PROVEN-full -- while LiveHD's own emitted Verilog of the same "none" design
# (the collision read is 8'b????????) was REFUTED-cex. Both must refute now;
# the refinement direction ("none" reference, "old" implementation) proves.
set -euo pipefail
LHD="${LHD:-./bazel-bin/lhd/lhd}"
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
WORK="${TEST_TMPDIR:-$(mktemp -d)}"
LHD="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"

for ord in old none; do
  mkdir -p "$WORK/$ord"
  cat > "$WORK/$ord/top.prp" <<P
pub mod top(we:Bool, wa:U2, d:U8, ra:U2) -> (q:U8@[0]) {
  reg mem:[4]U8:[ordering="$ord"] = 0
  q = mem[ra]
  if we { mem[wa] = d }
}
P
done
"$LHD" compile "$WORK/none/top.prp" --top top --workdir "$WORK/emit" --emit verilog:"$WORK/none.v" > "$WORK/emit.log" 2>&1 \
  || { echo "FAIL: cannot emit the ordering=none design"; tail -5 "$WORK/emit.log"; exit 1; }
grep -q "????" "$WORK/none.v" || { echo "FAIL: the emitted read has no undefined collision value"; exit 1; }

check() {
  local tag=$1 ref=$2 impl=$3 expected=$4 code=0
  shift 4
  "$LHD" lec --set formal.effort=proof --ref "$ref" --impl "$impl" --top top --set formal.timeout=20 \
    --set formal.simfail=false "$@" --workdir "$WORK/$tag" --result-json "$WORK/$tag.json" > "$WORK/$tag.log" 2>&1 || code=$?
  case "$expected" in proven-*) test "$code" = 0 ;; refuted-cex) test "$code" = 10 ;; not-proven) true ;; *) false ;; esac || {
    echo "FAIL: $tag: exit $code (expected $expected)"; grep "^lec: " "$WORK/$tag.log" | tail -3; exit 1; }
  python3 - "$WORK/$tag.json" "$expected" <<'PYRESULT' || { echo "FAIL: $tag"; grep "^lec: " "$WORK/$tag.log" | tail -3; exit 1; }
import json, sys
verdict = json.load(open(sys.argv[1]))["lec"]["verdict"]
want = sys.argv[2]
assert not verdict.startswith("proven") if want == "not-proven" else verdict == want, verdict
PYRESULT
  echo "ok: $tag -> $expected"
}
check old_vs_none "$WORK/old/top.prp" "$WORK/none/top.prp" refuted-cex
check old_vs_none_ind "$WORK/old/top.prp" "$WORK/none/top.prp" not-proven --set formal.engine=ind
check old_vs_emitted_none "$WORK/old/top.prp" "verilog:$WORK/none.v" refuted-cex
check none_vs_old "$WORK/none/top.prp" "$WORK/old/top.prp" proven-full
echo "lec_mem_undef_impl_test: PASSED"

#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Soundness corner cases of cross-version reuse.
# 1. Houdini early return: the stored invariant must not seed PDR unfiltered.
#    v0 counts to 15 (c[4] stays 0, so c != 20 is proven); v1 counts to 31 and
#    hits 20 past the bound. LHD_TEST_HOUDINI_ABORT forces Houdini's early
#    return; an unfiltered v0 invariant would hide the failure (proven unbounded).
# 2. Same target, different assumptions: v1 adds an assume that only breaks
#    after the failure. Reuse must give the same verdict as a cold run.

set -u
LHD="${LHD:-lhd/lhd}"
case "$LHD" in /*) ;; *) LHD="$PWD/$LHD" ;; esac
W="${TEST_TMPDIR:-/tmp/lhd_formal_reuse_sound_$$}"
rm -rf "$W"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

# run STEP WORKDIR REUSE FILE
run() {
  (cd "$W" && "$LHD" formal verify "$4" --top t --workdir "$2" --set formal.bound=12 --set formal.engine=bmc \
    --set formal.assume_check=false --set formal.simfail_run=false --set compile.formal.on_refute=warn \
    --set formal.reuse="$3" --set formal.pdr=true --diag-fmt pretty) >"$W/$1.out" 2>&1
  cp "$W/$2/formal_report.json" "$W/$1.json" 2>/dev/null || fail "$1: no formal_report.json: $(tail -5 "$W/$1.out")"
}

# get STEP -> "verdict unbounded reuse_by" of the only assert
get() {
  python3 - "$W/$1.json" <<'PYEOF'
import json, sys
d = json.load(open(sys.argv[1]))
o = [o for o in d["obligations"] if o["kind"] == "assert"][0]
print(o["verdict"].split("-")[0], str(o.get("unbounded", False)).lower(), o.get("reuse_by", "") or "-")
PYEOF
}

# ---- 1. Houdini early return
counter() {  # $1 = saturation value
  cat >"$W/c$1.sv" <<EOF
module t(input logic clk, input logic rst_n, output logic [4:0] q);
  logic [4:0] c;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) c <= 5'd0; else if (c != 5'd$1) c <= c + 5'd1;
  assign q = c;
  NotTwenty: assert property (@(posedge clk) disable iff (!rst_n) c != 5'd20);
endmodule
EOF
}
counter 15
counter 31
run h0 wh inv c15.sv
[ "$(get h0 | cut -d' ' -f1-2)" = "proven true" ] || fail "h0: v0 not proven unbounded: $(get h0)"
[ -s "$W/wh/reuse/$(ls "$W/wh/reuse" | head -1)/inv.txt" ] || fail "h0: no stored invariant"
run h1c wc none c31.sv
cold=$(get h1c)
LHD_TEST_HOUDINI_ABORT=1 run h1 wh inv c31.sv
got=$(get h1)
echo "houdini abort: cold [$cold] reuse [$got]"
[ "$(echo "$got" | cut -d' ' -f1-2)" = "$(echo "$cold" | cut -d' ' -f1-2)" ] \
  || fail "h1: unfiltered seed: reuse [$got] vs cold [$cold]"

# ---- 2. Same target, different assumptions
assume() {  # $1 = extra assume line (or empty)
  cat >"$W/a$2.sv" <<EOF
module t(input logic clk, input logic rst_n, output logic [3:0] q);
  logic [3:0] c;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) c <= 4'd0; else if (c != 4'd15) c <= c + 4'd1;
  assign q = c;
  NotThree: assert property (@(posedge clk) disable iff (!rst_n) c != 4'd3);
  $1
endmodule
EOF
}
assume "" 0
assume "Late: assume property (@(posedge clk) disable iff (!rst_n) c != 4'd6);" 1
for r in cex equiv all; do
  run "a0_$r" "wa_$r" "$r" a0.sv
  run "a1_$r" "wa_$r" "$r" a1.sv
done
run a1c wac none a1.sv
cold=$(get a1c)
for r in cex equiv all; do
  got=$(get "a1_$r")
  echo "late assume, reuse=$r: cold [$cold] reuse [$got]"
  [ "$(echo "$got" | cut -d' ' -f1)" = "$(echo "$cold" | cut -d' ' -f1)" ] \
    || fail "a1 reuse=$r: verdict [$got] differs from cold [$cold]"
done

echo "PASS: reuse soundness corner cases (houdini early return, late assume)"

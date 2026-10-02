#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A reset's NAME carries no polarity (docs 04b "Implicit clock and reset"):
# `rst_n:Reset` without `negreset=true` is ACTIVE-HIGH, and cgen emits
# `if (rst_n)` for it. `lhd lec` reads both sides with slang, whose reset
# recognition must keep that polarity: a Verilog `if (rst_n)` (sync or async
# `posedge rst_n`) is an active-high reset. It used to fall to tolg's
# Verilog-origin `_n` naming default and be read ACTIVE-LOW on both sides --
# a false PROVEN against a golden that computes something else once running.
# Each design below is checked PROVEN against its twin and REFUTED against a
# mutated golden (the running arm differs), natively and from emitted Verilog.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lec_reset_n_polarity_$$}"
mkdir -p "$WORK"
fail=0

# The Pyrope design: a synchronous, active-high `rst_n` (no negreset).
cat > "$WORK/m.prp" <<'EOF'
pub mod m(clk:Clock, rst_n:Reset, d:U8) -> (q:U8@[0]) {
  reg r:U8 = 5
  wrap r = r + d
  q = r
}
EOF
# Its golden, and a golden whose running arm differs.
cat > "$WORK/gold.v" <<'EOF'
module m(input clk, input rst_n, input [7:0] d, output [7:0] q);
  reg [7:0] r;
  always @(posedge clk) begin
    if (rst_n) r <= 8'd5;
    else r <= r + d;
  end
  assign q = r;
endmodule
EOF
cat > "$WORK/gold_mut.v" <<'EOF'
module m(input clk, input rst_n, input [7:0] d, output [7:0] q);
  reg [7:0] r;
  always @(posedge clk) begin
    if (rst_n) r <= 8'd5;
    else r <= r - d;
  end
  assign q = r;
endmodule
EOF
# The same register reset asynchronously, active-high on an `_n` name.
cat > "$WORK/gold_async.v" <<'EOF'
module m(input clk, input rst_n, input [7:0] d, output [7:0] q);
  reg [7:0] r;
  always @(posedge clk or posedge rst_n) begin
    if (rst_n) r <= 8'd5;
    else r <= r + d;
  end
  assign q = r;
endmodule
EOF
cat > "$WORK/gold_async_mut.v" <<'EOF'
module m(input clk, input rst_n, input [7:0] d, output [7:0] q);
  reg [7:0] r;
  always @(posedge clk or posedge rst_n) begin
    if (rst_n) r <= 8'd5;
    else r <= r - d;
  end
  assign q = r;
endmodule
EOF

# This checks proof/refutation polarity; witness replay has separate tests.
# Avoid compiling a witness simulator for every deliberately broken twin.
# verdict <ref.v> <impl kind:path> -> proven | refuted | <other>
verdict() {
  $LHD lec --ref "verilog:$WORK/$1" --ref-top m --impl "$2" --impl-top m \
       --set formal.timeout=30 --set formal.simfail_run=false --workdir "$WORK/q_$$_$RANDOM" 2>&1 \
    | grep -oE '"verdict":"(proven|refuted|unknown)"' | head -1 | sed -E 's/"verdict":"([a-z]+)"/\1/'
}
expect() {  # $1=label $2=got $3=want
  if [ "$2" != "$3" ]; then echo "FAIL: $1 -> got '$2', want '$3'"; fail=1
  else echo "ok: $1 -> $2"; fi
}

if ! $LHD compile "$WORK/m.prp" --emit-dir "verilog:$WORK/v" --workdir "$WORK/w_m" >/dev/null 2>&1; then
  echo "FAIL: compile m.prp"; exit 1
fi
MV=$(ls "$WORK"/v/*.v | head -1)
grep -q 'if (rst_n)' "$MV" || { echo "FAIL: cgen did not emit an active-high rst_n guard: $(cat "$MV")"; fail=1; }

expect "native vs gold"            "$(verdict gold.v "pyrope:$WORK/m.prp")"           proven
expect "native vs mutated gold"    "$(verdict gold_mut.v "pyrope:$WORK/m.prp")"       refuted
expect "cgen vs gold"              "$(verdict gold.v "verilog:$MV")"                  proven
expect "cgen vs mutated gold"      "$(verdict gold_mut.v "verilog:$MV")"              refuted
expect "cgen vs async gold"        "$(verdict gold_async.v "verilog:$MV")"            proven
expect "cgen vs mutated async"     "$(verdict gold_async_mut.v "verilog:$MV")"        refuted

[ "$fail" -eq 0 ] && echo "PASS" || exit 1

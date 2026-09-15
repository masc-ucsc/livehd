#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Native Slang retains scalar blocking-assigned state across clock edges.
# Check both asynchronous-reset feedback and a separate-process pipeline
# against explicit nonblocking RTL, plus local temporaries and loop controls.

set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then
    LHD=./bazel-bin/lhd/lhd
  else
    echo "ERROR: no lhd binary found (cwd $(pwd))"
    exit 1
  fi
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0

note() { echo "  $*"; }

# --- 1. State: blocking-written scalar read by a continuous assign ----------
cat >"$TMP/bad_assign.v" <<'EOF'
module bad_assign(input rst, input pulse, output [31:0] tick_count);
  reg [31:0] ms_counter;
  always @(posedge pulse or posedge rst) begin
    if (rst) ms_counter = 0;
    else     ms_counter = ms_counter + 1;
  end
  assign tick_count = ms_counter;
endmodule
EOF

# --- 2. State: blocking-written scalar read by ANOTHER process --------------
cat >"$TMP/bad_proc.v" <<'EOF'
module bad_proc(input clk, input [7:0] d, output reg [7:0] q);
  reg [7:0] stage;
  always @(posedge clk) stage = d;
  always @(posedge clk) q <= stage;
endmodule
EOF

for t in bad_assign bad_proc; do
  "$LHD" compile "$TMP/$t.v" --emit-dir "pyrope:$TMP/$t/" \
    --emit verilog:"$TMP/$t-out.v" --workdir "$TMP/w_$t" -q > "$TMP/$t.log" 2>&1 \
    || { cat "$TMP/$t.log"; exit 1; }
  # These fixtures have one assignment per written register in each branch;
  # replacing = with <= gives an independently expressed state machine.
  sed -e 's/ms_counter =/ms_counter <=/g' -e 's/stage =/stage <=/g' \
    "$TMP/$t.v" > "$TMP/$t-ref.v"
  "$LHD" lec --impl "$TMP/$t-out.v" --ref "$TMP/$t-ref.v" --top "$t" \
    --workdir "$TMP/lec_$t" -q > "$TMP/$t-lec.log" 2>&1 \
    || { cat "$TMP/$t-lec.log"; exit 1; }
  echo "ok[$t]: blocking state survives emission and proves against nonblocking RTL"
done

# --- 3. ACCEPTED: a process-LOCAL blocking temp -------------------------------
# `acc` never leaves the process, so it is an ordinary temp and the lowering is
# combinational-within-the-cycle, which is correct.
cat >"$TMP/ok_temp.v" <<'EOF'
module ok_temp(input clk, input [7:0] a, input [7:0] b, output reg [7:0] q);
  reg [7:0] acc;
  always @(posedge clk) begin
    acc = a + b;
    q  <= acc;
  end
endmodule
EOF

# --- 4. ACCEPTED: the ordinary non-blocking register --------------------------
cat >"$TMP/ok_nb.v" <<'EOF'
module ok_nb(input clk, input [7:0] d, output reg [7:0] q);
  always @(posedge clk) q <= d;
endmodule
EOF

# --- 5. ACCEPTED: a module-scope for-LOOP INDEX shared by two processes -------
# `n` is blocking-written by the edge process's loop control and referenced by
# the comb process, which must not become state: a loop
# index is not hardware state, elaboration unrolls it away. Refusing this
# rejected two real corpus designs (a cache tag array and an AXIS SRL register),
# so the loop control is excluded from the blocking-write set.
cat >"$TMP/ok_loopvar.v" <<'EOF'
module ok_loopvar(input clk, input [3:0] d, output reg [3:0] q, output reg [3:0] c);
  integer n;
  always @(posedge clk)
    for (n = 0; n < 4; n = n + 1) q[n] <= d[n];
  always @(*) begin
    c = 0;
    for (n = 0; n < 4; n = n + 1) c = c + d[n];
  end
endmodule
EOF

for t in ok_temp ok_nb ok_loopvar; do
  out=$("$LHD" compile "$TMP/$t.v" --reader slang --emit-dir "pyrope:$TMP/$t/" \
        --workdir "$TMP/w_$t" -q 2>&1)
  rc=$?
  if [ $rc -ne 0 ]; then
    echo "FAIL[$t]: legitimate design refused (rc=$rc)"
    note "$out"
    fail=1
  else
    echo "ok[$t]: accepted"
  fi
done

# --- 6. the ordinary register must still be REAL state, not folded away -------
if ! grep -q "^  reg q" "$TMP/ok_nb/ok_nb.prp" 2>/dev/null; then
  echo "FAIL[ok_nb]: emitted Pyrope has no \`reg q\` -- the register was dropped"
  cat "$TMP/ok_nb/ok_nb.prp" 2>/dev/null
  fail=1
else
  echo "ok[ok_nb]: register survives as \`reg q\`"
fi

exit $fail

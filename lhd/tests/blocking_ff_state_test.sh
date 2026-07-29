#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# LIVE fail-closed test: a variable BLOCKING-assigned inside an EDGE-sensitive
# process and read outside it is persistent flop state.
#
# `--reader slang` models state from non-blocking (`<=`) writes only, and
# collect_state_vars documents the rest: "one written there but read elsewhere
# has flop semantics this reader does not model yet -> diagnosed". That
# diagnostic only ever fired for module OUTPUTS (`blocking-ff-output`) and
# unpacked arrays (`blocking-ff-array`). An INTERNAL scalar fell through and was
# declared a plain `mut`, so the register VANISHED:
#
#     always @(posedge pulse or posedge rst)
#       if (rst) ms_counter = 0; else ms_counter = ms_counter + 1;
#     assign tick_count = ms_counter;
#
# lowered to a stateless `pub comb` whose output folded to the constant 1 -- it
# compiled clean, exit 0, zero warnings, and lgcheck REFUTED it against the
# source. That is the worst possible outcome, so this pins the refusal.
#
# The ACCEPTANCE half is what makes it a real test: a checker that rejects every
# blocking write in an edge process would also pass the rejection cases. A
# process-LOCAL blocking temp (written and read only inside the one process) is
# legitimate and must still compile, and so must the ordinary `<=` register.

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

# --- 1. REFUSED: blocking-written scalar read by a continuous assign ----------
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

# --- 2. REFUSED: blocking-written scalar read by ANOTHER process --------------
cat >"$TMP/bad_proc.v" <<'EOF'
module bad_proc(input clk, input [7:0] d, output reg [7:0] q);
  reg [7:0] stage;
  always @(posedge clk) stage = d;
  always @(posedge clk) q <= stage;
endmodule
EOF

for t in bad_assign bad_proc; do
  out=$("$LHD" compile "$TMP/$t.v" --reader slang --emit-dir "pyrope:$TMP/$t/" \
        --workdir "$TMP/w_$t" -q 2>&1)
  rc=$?
  if [ $rc -eq 0 ]; then
    echo "FAIL[$t]: compiled clean -- the register was silently dropped"
    note "$out"
    fail=1
  elif ! echo "$out" | grep -q "blocking-assigned in an edge-sensitive process and read outside it"; then
    # Match the MESSAGE, not the `blocking-ff-state` code: `-q` prints only the
    # result envelope, which carries the message but not the diagnostic code.
    echo "FAIL[$t]: exited $rc but without the blocking-ff-state diagnostic"
    note "$out"
    fail=1
  else
    echo "ok[$t]: refused with blocking-ff-state"
  fi
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
# the comb process, which looks exactly like the refused shape -- but a loop
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

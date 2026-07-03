#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Stage 0 of the cgen_sim false-combinational-loop fix (see
# inou/cgen/tests/comb_loop/README.md): a combinational sub-instance whose output
# feeds back -- through PARENT comb logic -- into one of its own inputs is a FALSE
# comb loop. inou.cgen.sim schedules a module with ONE forward_class pass and
# calls a Sub atomically, so the fed-back input has no valid emission order and
# operand() used to silently emit create_integer(0) -> WRONG sim, exit 0.
#
# Stage 0 turned that silent miscompile into a LOUD, located build failure
# (diag code comb-loop-through-instance / combinational-loop, category
# unsupported, non-deferred so run_step's has_halting_errors() gate blocks the
# sim build).
#
# Stage 1 (flatten_false_loop_subs in cgen_sim.cpp) RESOLVES the common case: a
# single PURE-COMB sub-instance whose output feeds back into its OWN input is
# inlined into the parent before scheduling, so forward_class orders the now-flat
# DAG and the sim is correct (verified end-to-end by prp-simfail/simeq-
# sim_lg_atomic_feedback, the same design). Still an ERROR: a GENUINE comb loop
# (flattens to a real bit-level cycle) and a cross-coupled MUTUAL false loop
# through TWO subs (the detector stops at the sibling instance). This test guards
# all three: the single-instance false loop compiles clean, the genuine loop and
# the mutual loop still fail with a comb-loop diagnostic, and a legitimate
# hierarchical/comb design still compiles clean.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_cgen_comb_loop_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

# Compile a Verilog design to sim C++ with the hierarchy-preserving slang reader.
# Echoes the exit code; the JSON envelope (incl. any diagnostic) lands in $W/out.
compile_sim() {  # <file> <top>
  "$LHD" compile "$1" --reader slang --top "$2" \
        --emit-dir "sim:$W/sim_$2/" --workdir "$W/wd_$2" >"$W/out" 2>&1
  echo $?
}

# A bug design must (a) exit non-zero and (b) name a comb-loop diagnostic.
expect_loop_error() {  # <file> <top> <label>
  local rc; rc=$(compile_sim "$1" "$2")
  [ "$rc" -ne 0 ] || fail "$3: expected non-zero exit (silent wrong-sim not blocked)"
  grep -qE '"code":"(comb-loop-through-instance|combinational-loop)"' "$W/out" \
    || fail "$3: expected a comb-loop diagnostic; got: $(cat "$W/out")"
  echo "ok: $3 blocked with a comb-loop diagnostic"
}

# A legitimate design must compile clean (no false-positive diagnostic).
expect_clean() {  # <file> <top> <label>
  local rc; rc=$(compile_sim "$1" "$2")
  [ "$rc" -eq 0 ] || fail "$3: expected clean compile but rc=$rc: $(cat "$W/out")"
  ! grep -qE '"code":"(comb-loop-through-instance|combinational-loop)"' "$W/out" \
    || fail "$3: false-positive comb-loop diagnostic on a legitimate design"
  echo "ok: $3 compiled clean"
}

# --- bug: pure-comb sub, output x fed back into input c through parent logic ---
cat > "$W/base.v" <<'EOF'
module subadd(input [7:0] a, input [7:0] b, input [7:0] c, input [7:0] d,
              output [8:0] x, output [8:0] y);
  assign x = a + b;
  assign y = c + d;
endmodule
module top(input [7:0] a, input [7:0] b, input [7:0] d, input [7:0] topx,
           output [8:0] x_out, output [8:0] y_out);
  wire [8:0] x; wire [7:0] c;
  subadd u(.a(a), .b(b), .c(c), .d(d), .x(x), .y(y_out));
  assign c = x[7:0] + topx;
  assign x_out = x;
endmodule
EOF
# Stage 1: a single-instance self-feedback false loop is now FLATTENED and
# computes correctly (golden a=10,b=20,d=5,topx=3 -> x_out=30, y_out=38).
expect_clean "$W/base.v" top "base false loop (flattened)"

# --- bug: two pure-comb subs cross-coupled (u1.x->u2.c, u2.x->u1.c) ---
cat > "$W/cross.v" <<'EOF'
module subadd(input [7:0] a, input [7:0] b, input [7:0] c, input [7:0] d,
              output [8:0] x, output [8:0] y);
  assign x = a + b;
  assign y = c + d;
endmodule
module top(input [7:0] a1, input [7:0] b1, input [7:0] d1,
           input [7:0] a2, input [7:0] b2, input [7:0] d2,
           output [8:0] o1, output [8:0] o2);
  wire [8:0] x1, x2;
  subadd u1(.a(a1), .b(b1), .c(x2[7:0]), .d(d1), .x(x1), .y(o1));
  subadd u2(.a(a2), .b(b2), .c(x1[7:0]), .d(d2), .x(x2), .y(o2));
endmodule
EOF
expect_loop_error "$W/cross.v" top "cross-coupled false loop"

# --- bug: GENUINE comb loop through a sub (must stay an error) ---
cat > "$W/genuine.v" <<'EOF'
module passthru(input [7:0] a, output [8:0] x);
  assign x = a + 1;
endmodule
module top(input [7:0] din, output [8:0] o);
  wire [8:0] x; wire [7:0] b;
  passthru u(.a(b), .x(x));
  assign b = x[7:0] + din;
  assign o = x;
endmodule
EOF
expect_loop_error "$W/genuine.v" top "genuine comb loop through sub"

# --- negative: a hierarchical sub with NO feedback compiles clean ---
cat > "$W/ok.v" <<'EOF'
module subadd(input [7:0] a, input [7:0] b, input [7:0] c, input [7:0] d,
              output [8:0] x, output [8:0] y);
  assign x = a + b;
  assign y = c + d;
endmodule
module top(input [7:0] a, input [7:0] b, input [7:0] c, input [7:0] d,
           output [8:0] x_out, output [8:0] y_out);
  subadd u(.a(a), .b(b), .c(c), .d(d), .x(x_out), .y(y_out));
endmodule
EOF
expect_clean "$W/ok.v" top "no-feedback hierarchy"

# --- crash regression: comb cycle feeding a comb-array READ ADDRESS ----------
# The mem-operand prefetch walker (ensure_ready_impl, cgen_sim.cpp) used to
# self-recurse with no visited guard: a word-level false cycle in the address
# cone stack-overflowed (SIGSEGV rc=139, NO diagnostic) instead of any orderly
# outcome (BusyTable_1/Dispatch; equiv/sim_loop_mem_prefetch). Since the
# generalized split_packed_selfref_wires the FALSE word-level cycle is also
# RESOLVED, so the correct outcome today is a clean compile; the crash guard
# is the rc<128 check (a regression may legitimately re-error with the loud
# comb-loop diagnostic, but must never die by signal).
cat > "$W/memp.prp" <<'EOF'
pub mod memp::[lg="memp", hdl](a:u2, en:bool, d:u8) -> (z:u8@[0]) {
  wire io:u4
  wire hi:u2
  wire low:u2
  hi  = io#[2..=3]
  low = hi & 3
  io  = low + (a << 2)
  mut t:[4]u8 = (10,20,30,40)
  if en {
    t[a] = d
  }
  z = t[low]
}
EOF
"$LHD" compile "$W/memp.prp" --top memp \
      --emit-dir "sim:$W/sim_memp/" --workdir "$W/wd_memp" >"$W/out" 2>&1
rc=$?
[ "$rc" -lt 128 ] || fail "mem-prefetch cycle: died by signal (rc=$rc) instead of a diagnostic"
[ "$rc" -eq 0 ] || fail "mem-prefetch cycle: expected clean compile (false loop resolved) but rc=$rc: $(cat "$W/out")"
echo "ok: mem-prefetch address-cone cycle resolves cleanly (no crash, no false loop)"

echo "PASS: cgen_sim comb-loop Stage 0 safety net"

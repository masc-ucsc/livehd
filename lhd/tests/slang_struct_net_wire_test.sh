#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regression — a module-scope STRUCT net that the dataflow sort places AFTER one
# of its readers must be `wire`-classified (2c-wire), exactly like the scalar in
# the same position. Before the fix it could not be: lower_module pre-declared
# every struct Variable BEFORE lower_members computed `wire_syms_`, so
# declare_struct_leaves always read an empty set and locked the leaves into
# `mut`. A `mut` binds nothing in tolg until its first store, so the early read
# resolved to nil — `unresolved ref '<net>.<leaf>' — wiring nil (0sb?)` — and the
# CONNECTION WAS SILENTLY SEVERED: the consumer instance's port came out tied to
# a bare X const and the producer instance's output ports vanished from the
# netlist entirely.
#
# It was not a niche shape. On lhdsuite //bench:minion this was 448 warnings over
# 104 sites and 85 X-tied instance ports, including the whole VPU->core ID-stage
# control bundle (`id_vpu_core_ctrl`, all 19 leaves) and the transcendental-ROM
# coefficient bus into txfma (`f8_trans_rom_response`).
#
# WHY THE READER CAN LEGALLY SORT FIRST — no combinational cycle is needed.
# collect_registered_outputs marks a net driven by a purely-REGISTERED instance
# output as order-free (seq_out_nets) and pass 2 drops that dependency edge, so
# Kahn is free to emit the consumer first. Case 1 is exactly that shape.
#
# WHAT IS DELIBERATELY NOT PROMOTED (case 3): the wire SPLIT device (mut
# accumulator + single bridge) is scalar-only — it skips structs at its
# `ct.isStruct()` guard — so a procedurally / partially / multiply written struct
# has no accumulator for its own RMW reads and must KEEP `mut`. That case still
# warns; promoting it needs a per-leaf split, which does not exist yet. This test
# pins the boundary so a future change does not silently cross it.

set -u

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD="$(dirname "$0")/../lhd"
[ -x "$LHD" ] || { echo "FAIL: cannot find the lhd binary"; exit 1; }

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() { echo "FAIL: $*"; exit 1; }

# ── the shape ───────────────────────────────────────────────────────────────
# rom5's output is PURELY REGISTERED, so `coef` lands in seq_out_nets and the
# reader->writer edge is dropped. fma5 is combinational, so rom5's read of
# `res.exc` is a real dependency: Kahn must emit fma5 (the READER of coef) first.
cat >"$W/sn.sv" <<'EOF'
package p5;
  typedef struct packed { logic [3:0] c0; logic [3:0] c2; logic exc; } coef_t;
  typedef struct packed { logic [7:0] data; logic exc; } res_t;
endpackage

module rom5 (input logic clk, input logic exc_i, input logic [3:0] x, output p5::coef_t o);
  always_ff @(posedge clk) begin
    o.c0  <= x;
    o.c2  <= {3'b0, exc_i};
    o.exc <= exc_i;
  end
endmodule

module fma5 (input p5::coef_t coef_i, input logic [3:0] x, output p5::res_t o);
  always_comb begin
    o.data = {coef_i.c0, coef_i.c2} ^ {4'b0, x};
    o.exc  = coef_i.exc;
  end
endmodule

module sn (input logic clk, input logic [3:0] x, output p5::res_t res, output logic [1:0] dbg);
  p5::coef_t coef;
  rom5 u_rom (.clk(clk), .exc_i(res.exc), .x(x), .o(coef));  // writer, source-FIRST
  fma5 u_fma (.coef_i(coef), .x(x), .o(res));                // reader, source-SECOND
  always_comb dbg = {coef.exc, coef.c0[0]};
endmodule
EOF

# The SCALAR control: identical topology, three scalar nets instead of the
# struct. This one was always handled correctly (its declare is lazy, so it runs
# after the classification) — it is what the struct case must now match.
cat >"$W/sn_scalar.sv" <<'EOF'
module rom6 (input logic clk, input logic exc_i, input logic [3:0] x,
             output logic [3:0] c0, output logic [3:0] c2, output logic exc);
  always_ff @(posedge clk) begin
    c0  <= x;
    c2  <= {3'b0, exc_i};
    exc <= exc_i;
  end
endmodule
module fma6 (input logic [3:0] c0, input logic [3:0] c2, input logic exc_i, input logic [3:0] x,
             output logic [7:0] data, output logic exc);
  always_comb begin
    data = {c0, c2} ^ {4'b0, x};
    exc  = exc_i;
  end
endmodule
module sn_scalar (input logic clk, input logic [3:0] x,
                  output logic [7:0] res_data, output logic res_exc, output logic [1:0] dbg);
  logic [3:0] coef_c0, coef_c2; logic coef_exc;
  rom6 u_rom (.clk(clk), .exc_i(res_exc), .x(x), .c0(coef_c0), .c2(coef_c2), .exc(coef_exc));
  fma6 u_fma (.c0(coef_c0), .c2(coef_c2), .exc_i(coef_exc), .x(x), .data(res_data), .exc(res_exc));
  always_comb dbg = {coef_exc, coef_c0[0]};
endmodule
EOF

# ── (1) no unresolved-ref, and the struct matches its scalar twin ────────────
for m in sn sn_scalar; do
  "$LHD" compile "$W/$m.sv" --top "$m" --emit "verilog:$W/$m.v" \
    --workdir "$W/w_$m" --result-json "$W/$m.json" >"$W/$m.log" 2>&1 \
    || fail "compile of $m failed: $(cat "$W/$m.log")"
  if grep -q '"code":"unresolved-ref"' "$W/$m.log"; then
    fail "$m emitted unresolved-ref (struct net locked into 'mut'?):
$(grep -o '"message":"unresolved[^"]*"' "$W/$m.log")"
  fi
done
echo "PASS: a forward-read struct net produces no unresolved-ref (matches the scalar twin)"

# ── (2) the CONNECTION survives into the netlist ────────────────────────────
# The regression signature is a port tied to a bare all-? const, e.g.
#   ,.\coef_i.c0 (65'sb1????????…)
# and rom5 losing its output ports altogether. Both must be gone.
if grep -Eq "\.\\\\?coef_i\.[a-z0-9]+ *\(\s*[0-9]+'sb1\?" "$W/sn.v"; then
  fail "u_fma's coefficient inputs are tied to a don't-care const — the connection is severed:
$(grep -E "coef_i\." "$W/sn.v")"
fi
grep -Eq "\.\\\\?coef_i\.c0 *\([a-zA-Z_]" "$W/sn.v" \
  || fail "u_fma's .coef_i.c0 is not driven by a real net:
$(sed -n '/fma5 u_fma/,/);/p' "$W/sn.v")"
grep -Eq "\.\\\\?o\.c0 *\([a-zA-Z_]" "$W/sn.v" \
  || fail "u_rom's struct output ports are missing from the netlist:
$(sed -n '/rom5 u_rom/,/);/p' "$W/sn.v")"
# dbg reads the same net through a third driver; a severed net folds it to a
# pure X constant.
grep -Eq "dbg *= *\(?[0-9]+'sb[01]\?+\)? *;" "$W/sn.v" \
  && fail "dbg folded to a pure don't-care — the struct net carries no value:
$(grep -E '\bdbg *=' "$W/sn.v")"
echo "PASS: the producer->consumer struct connection survives into the netlist"

# ── (3) BOUNDARY: a procedurally-written struct still keeps `mut` ────────────
# Same forward-read shape, but the struct is written by an always_comb that also
# READS it (an RMW). There is no per-leaf split device, so promoting this to a
# wire would make the RMW read bind the wire's own buffer output — a
# combinational self-loop. It must stay `mut` (and therefore still warn).
cat >"$W/sn_proc.sv" <<'EOF'
package p7;
  typedef struct packed { logic [3:0] a; logic [3:0] b; } st_t;
endpackage
module snk7 (input p7::st_t i, output logic [3:0] o);
  always_comb o = i.a ^ i.b;
endmodule
// COMBINATIONAL, so `fb` is NOT order-free (seq_out_nets) and the always_comb
// genuinely depends on it: snk7 -> src7 -> always_comb -> snk7 is a real SCC,
// and the cyclic list emits in source order, i.e. the READER first.
module src7 (input logic [3:0] k, output logic [3:0] q);
  always_comb q = k + 4'h1;
endmodule
module sn_proc (input logic [3:0] x, output logic [3:0] o);
  p7::st_t nxt;
  logic [3:0] fb;
  snk7 u_snk (.i(nxt), .o(o));          // READER, emitted first
  src7 u_src (.k(o), .q(fb));
  always_comb begin                     // WRITER, RMW on nxt
    nxt.a = fb;
    nxt.b = nxt.a ^ x;
  end
endmodule
EOF
"$LHD" compile "$W/sn_proc.sv" --top sn_proc --workdir "$W/w_proc" >"$W/proc.log" 2>&1 \
  || fail "compile of sn_proc failed: $(cat "$W/proc.log")"
grep -q '"code":"unresolved-ref"' "$W/proc.log" \
  || echo "NOTE: sn_proc no longer warns — if a per-leaf split landed, retire this boundary case"
# What must NOT happen either way: a comb loop or a hard error.
grep -q '"severity":"error"' "$W/proc.log" \
  && fail "a procedurally-written struct must not be promoted to a wire (hard error):
$(grep -o '"message":"[^"]*"' "$W/proc.log")"
grep -q '"code":"unresolved-cycle"' "$W/proc.log" \
  && fail "a procedurally-written struct was promoted to a wire and formed a combinational self-loop:
$(grep -o '"message":"[^"]*"' "$W/proc.log")"
echo "PASS: a procedurally-written (RMW) struct keeps 'mut' — no self-loop, no error"

# ── (4) a WIRE-classified scalar OUTPUT PORT gets its buffer ─────────────────
# An output port is declared from io_meta, so `declared_` already holds it and
# the lazy declare never fires; the comb-output poison loop then deliberately
# skips a wire-classified output. Net effect before the fix: the name bound
# NOTHING and an early read resolved to nil (prim_mul_div's `req_ready`).
cat >"$W/sn_out.sv" <<'EOF'
module a11 (input logic clk, input logic r, output logic q);
  always_ff @(posedge clk) q <= r;
endmodule
module b11 (input logic clk, input logic q, output logic r);
  always_ff @(posedge clk) r <= ~q;
endmodule
module sn_out (input logic clk, output logic req_ready, output logic o);
  logic mid;
  a11 ua (.clk(clk), .r(req_ready), .q(mid));   // READS the output port first
  b11 ub (.clk(clk), .q(mid), .r(req_ready));   // drives it later
  assign o = mid;
endmodule
EOF
"$LHD" compile "$W/sn_out.sv" --top sn_out --emit "verilog:$W/sn_out.v" \
  --workdir "$W/w_out" >"$W/out.log" 2>&1 \
  || fail "compile of sn_out failed: $(cat "$W/out.log")"
grep -q '"code":"unresolved-ref"' "$W/out.log" \
  && fail "a wire-classified OUTPUT port bound nothing:
$(grep -o '"message":"unresolved[^"]*"' "$W/out.log")"
grep -Eq "\.r *\(\s*[0-9]+'sb1\?" "$W/sn_out.v" \
  && fail "ua's .r is tied to a don't-care const — the output port carries no value:
$(sed -n '/a11 ua/,/);/p' "$W/sn_out.v")"
echo "PASS: a wire-classified scalar output port binds its buffer"

# ── (5) member reads create a dependency on the aggregate root ───────────────
# A MemberAccessExpression names the FIELD symbol in Slang, but the child
# output connection records a write to the ROOT `child_o` symbol. If the
# dependency collector compares those spellings directly, this always_ff is
# incorrectly considered ready before u_child: Pyrope snapshots child_o's
# poison initializer into q's next-state mux and the real child result is lost.
cat >"$W/member_dep.sv" <<'EOF'
typedef struct packed {
  logic [7:0] data;
  logic       flag;
} member_dep_payload_t;

typedef struct packed {
  member_dep_payload_t to_s1;
} member_dep_t;

module member_dep_child(
  input  logic [7:0] d_i,
  output member_dep_t o
);
  always_comb begin
    o.to_s1.data = d_i + 8'd3;
    o.to_s1.flag = ^d_i;
  end
endmodule

module member_dep(
  input  logic        clk,
  input  logic        en,
  input  logic [7:0]  d_i,
  output member_dep_t q_o
);
  member_dep_t child_o;
  member_dep_t q;

  // Deliberately source-before the producer instance: dataflow ordering must
  // move u_child ahead of this consumer. The automatic locals mirror the
  // FIRRTL-generated shape: their initializer cone is part of this process
  // even though the local VariableSymbol is not a module-level driver.
  always_ff @(posedge clk) begin
    automatic logic [7:0] next_data = child_o.to_s1.data;
    automatic logic       next_flag = child_o.to_s1.flag;
    if (en) begin
      q.to_s1.data <= next_data;
      q.to_s1.flag <= next_flag;
    end
  end

  member_dep_child u_child (.d_i(d_i), .o(child_o));
  assign q_o = q;
endmodule
EOF
"$LHD" compile "$W/member_dep.sv" --top member_dep \
  --emit-dir "pyrope:$W/member_dep_prp" --workdir "$W/w_member_dep" >"$W/member_dep.log" 2>&1 \
  || fail "compile of member_dep failed: $(cat "$W/member_dep.log")"
"$LHD" lec --impl "pyrope:$W/member_dep_prp/member_dep.prp" \
  --ref "verilog:$W/member_dep.sv" --top member_dep \
  --set formal.strict=true --workdir "$W/lec_member_dep" >"$W/member_dep_lec.log" 2>&1 \
  || fail "aggregate member dependency was lost on the Pyrope round trip: $(cat "$W/member_dep_lec.log")"
echo "PASS: aggregate member reads order their producer before the consumer"

# ── (6) a nonuniform packed-array reset survives the Pyrope round trip ──────
# Slang emits this state as one array reg with an exact packed `init=` value and
# a whole-array next-state update. The Pyrope reader used to recognize only the
# importer's `initial=` spelling in that whole-array path, so it kept the reset
# condition but silently dropped the value: cgen reset all eight entries to 0.
# The reset-edge marker also has to survive; making an asynchronous source reset
# synchronous is a separate semantic loss even when edge-stepped LEC cannot see
# it.
cat >"$W/nonuniform_array_reset.sv" <<'EOF'
module nonuniform_array_reset(
  input  logic       clock,
  input  logic       reset,
  input  logic [2:0] index,
  input  logic [2:0] step,
  output logic [6:0] data
);
  logic [7:0][6:0] ptr;
  wire [7:0][6:0] ptr_next = {
    ptr[7] + {4'b0, step}, ptr[6] + {4'b0, step},
    ptr[5] + {4'b0, step}, ptr[4] + {4'b0, step},
    ptr[3] + {4'b0, step}, ptr[2] + {4'b0, step},
    ptr[1] + {4'b0, step}, ptr[0] + {4'b0, step}
  };
  always_ff @(posedge clock or posedge reset) begin
    if (reset)
      ptr <= {7'd7, 7'd6, 7'd5, 7'd4, 7'd3, 7'd2, 7'd1, 7'd0};
    else
      ptr <= ptr_next;
  end
  assign data = ptr[index];
endmodule
EOF
"$LHD" compile "$W/nonuniform_array_reset.sv" --top nonuniform_array_reset \
  --emit-dir "pyrope:$W/nonuniform_array_reset_prp" \
  --workdir "$W/w_nonuniform_array_reset" >"$W/nonuniform_array_reset.log" 2>&1 \
  || fail "compile of nonuniform_array_reset failed: $(cat "$W/nonuniform_array_reset.log")"
grep -Fq 'reg ptr:[8]u7:[ordering="old", reset_pin=ref reset, async=true] = (0, 1, 2, 3, 4, 5, 6, 7)' \
  "$W/nonuniform_array_reset_prp/nonuniform_array_reset.prp" \
  || fail "Slang did not preserve the nonuniform reset value in Pyrope"
"$LHD" compile "$W/nonuniform_array_reset_prp/nonuniform_array_reset.prp" \
  --top nonuniform_array_reset --emit-dir "lg:$W/nonuniform_array_reset_lg" \
  --workdir "$W/w_nonuniform_array_reset_prp" >"$W/nonuniform_array_reset_prp.log" 2>&1 \
  || fail "Pyrope recompile of nonuniform_array_reset failed: $(cat "$W/nonuniform_array_reset_prp.log")"
"$LHD" compile "lg:$W/nonuniform_array_reset_lg" --top nonuniform_array_reset \
  --emit "verilog:$W/nonuniform_array_reset.v" \
  --workdir "$W/w_nonuniform_array_reset_lg" >"$W/nonuniform_array_reset_lg.log" 2>&1 \
  || fail "LGraph reload of nonuniform_array_reset failed: $(cat "$W/nonuniform_array_reset_lg.log")"
grep -Fq "wire [55:0] __lhdmem_h707472_e_data_rst = 56'he182840608080;" "$W/nonuniform_array_reset.v" \
  || fail "Pyrope whole-array init was dropped or changed in generated Verilog"
grep -Fq 'always @(posedge clock or posedge reset)' "$W/nonuniform_array_reset.v" \
  || fail "Pyrope whole-array async reset became synchronous in generated Verilog"
# cgen must keep this as a packed two-dimensional reg with one whole-array
# assignment. Its former unpacked, per-lane spelling reread as independent
# memory write ports and lost both the bulk-update identity and async reset
# (the shape generated for XiangShan ROB's enqPtrVec_data).
grep -Fq 'reg [7:0][6:0] __lhdmem_h707472_e_data;' "$W/nonuniform_array_reset.v" \
  || fail "whole-array cgen did not preserve a packed two-dimensional reg"
grep -Fq '__lhdmem_h707472_e_data <= __lhdmem_h707472_e_data_rst;' "$W/nonuniform_array_reset.v" \
  || fail "whole-array cgen expanded the reset into per-lane writes"
"$LHD" compile "$W/nonuniform_array_reset.v" --top nonuniform_array_reset \
  --emit "verilog:$W/nonuniform_array_reset_reread.v" \
  --workdir "$W/w_nonuniform_array_reset_reread" >"$W/nonuniform_array_reset_reread.log" 2>&1 \
  || fail "cgen packed-array async reset did not reread: $(cat "$W/nonuniform_array_reset_reread.log")"
if grep -Fq 'async-reset-as-sync' "$W/nonuniform_array_reset_reread.log"; then
  fail "cgen packed-array reset was demoted to synchronous on reread"
fi
grep -Fq 'always @(posedge clock or posedge reset)' "$W/nonuniform_array_reset_reread.v" \
  || fail "cgen packed-array async reset edge was lost on reread: $(grep -n 'always @' "$W/nonuniform_array_reset_reread.v")"
"$LHD" lec --impl "pyrope:$W/nonuniform_array_reset_prp/nonuniform_array_reset.prp" \
  --ref "verilog:$W/nonuniform_array_reset.sv" --top nonuniform_array_reset \
  --set formal.strict=true --workdir "$W/lec_nonuniform_array_reset" \
  >"$W/nonuniform_array_reset_lec.log" 2>&1 \
  || fail "nonuniform array reset failed LEC: $(cat "$W/nonuniform_array_reset_lec.log")"
echo "PASS: nonuniform whole-array reset value and async edge survive Pyrope"

# ── (7) sibling fields in one continuous struct pattern are dependency-ordered ──
# This is the reduced XiangShan ROB shape.  The packed assignment is a set of
# simultaneous net equations: `valid` reads the independently-computed `bits`
# field.  Bundle lowering used to emit fields in declaration order, resolving
# `vxsat.bits` before its later leaf driver and silently wiring nil.
cat >"$W/struct_sibling_dep.sv" <<'EOF'
module struct_sibling_dep(
  input  logic       commit,
  input  logic [7:0] lanes,
  output logic       valid_o,
  output logic       bits_o
);
  wire struct packed { logic valid; logic bits; } vxsat;
  assign vxsat = '{valid: commit & vxsat.bits, bits: |lanes};
  assign valid_o = vxsat.valid;
  assign bits_o  = vxsat.bits;
endmodule
EOF
"$LHD" compile "$W/struct_sibling_dep.sv" --top struct_sibling_dep \
  --emit-dir "pyrope:$W/struct_sibling_dep_prp" \
  --emit "verilog:$W/struct_sibling_dep.v" \
  --workdir "$W/w_struct_sibling_dep" >"$W/struct_sibling_dep.log" 2>&1 \
  || fail "compile of struct_sibling_dep failed: $(cat "$W/struct_sibling_dep.log")"
grep -q '"code":"unresolved-ref"' "$W/struct_sibling_dep.log" \
  && fail "sibling field read resolved before its driver: $(cat "$W/struct_sibling_dep.log")"
"$LHD" lec --impl "pyrope:$W/struct_sibling_dep_prp/struct_sibling_dep.prp" \
  --ref "verilog:$W/struct_sibling_dep.sv" --top struct_sibling_dep \
  --set formal.strict=true --workdir "$W/lec_struct_sibling_dep" \
  >"$W/struct_sibling_dep_lec.log" 2>&1 \
  || fail "sibling-dependent struct pattern failed LEC: $(cat "$W/struct_sibling_dep_lec.log")"
echo "PASS: sibling-dependent struct pattern emits its leaf drivers in dependency order"

# ── (8) zero-output submodules round-trip as sink-call statements ────────────
# A sink instance has no value result.  Emitting `mut u = Sink(...)` caused the
# Pyrope reader to synthesize `u = %call_result`; tolg intentionally creates no
# result pin for Sink, so that copy resolved `%call_result` to nil.  Preserve the
# instance and its name with a standalone call instead.
cat >"$W/sink_roundtrip.sv" <<'EOF'
module sink_observer(input logic [3:0] data);
endmodule
module sink_roundtrip(input logic [3:0] data, output logic parity);
  sink_observer u_observer(.data(data));
  assign parity = ^data;
endmodule
EOF
"$LHD" compile "$W/sink_roundtrip.sv" --top sink_roundtrip \
  --emit-dir "pyrope:$W/sink_roundtrip_prp" \
  --workdir "$W/w_sink_roundtrip" >"$W/sink_roundtrip.log" 2>&1 \
  || fail "compile of sink_roundtrip failed: $(cat "$W/sink_roundtrip.log")"
grep -Fq 'sink_observer::[name=u_observer](' "$W/sink_roundtrip_prp/sink_roundtrip.prp" \
  || fail "zero-output instance was not emitted with its hierarchy name"
grep -Fq 'u_observer = sink_observer' "$W/sink_roundtrip_prp/sink_roundtrip.prp" \
  && fail "zero-output instance was incorrectly emitted as a value assignment"
"$LHD" compile "$W/sink_roundtrip_prp/sink_roundtrip.prp" --top sink_roundtrip \
  --emit "verilog:$W/sink_roundtrip.v" --workdir "$W/w_sink_roundtrip_prp" \
  >"$W/sink_roundtrip_prp.log" 2>&1 \
  || fail "Pyrope reread of zero-output sink failed: $(cat "$W/sink_roundtrip_prp.log")"
grep -q '"code":"unresolved-ref"' "$W/sink_roundtrip_prp.log" \
  && fail "zero-output sink reread synthesized an unresolved call-result copy: $(cat "$W/sink_roundtrip_prp.log")"
# The default compile recipe may remove an empty sink instance as dead after it
# has been lowered (the real DPI wrapper is non-empty outside SYNTHESIS), so the
# source-level call spelling above pins the hierarchy name; here only require
# that the imported module survived and the reread stayed diagnostic-free.
grep -Fq 'module sink_observer' "$W/sink_roundtrip.v" \
  || fail "zero-output sink module was lost on round trip"
echo "PASS: zero-output module call round-trips as a named sink instance"

echo "ALL PASS"

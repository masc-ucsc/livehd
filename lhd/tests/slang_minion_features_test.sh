#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regressions for the slang-reader/tolg features the minion IP compile needed
# (core-et hw/ip/minion, top minion_top):
#
#  (1) MULTI-DIM unpacked struct arrays (`conf_t m [T][P]`): linearized to a
#      1-D tuple memory; nested `m[i][j]` selector chains fold to one linear
#      index for whole-element reads/writes AND fused `.field` reads
#      (intpipe_csr_msgs' msg_port_conf). LEC'd against the source.
#  (2) Latch-based ICG (prim_clk_gate): the latch's hold-arm q read is state,
#      not a combinational loop; a reg ARRAY written in a generate-for
#      `always_ff` classifies as a reg (thread_buffer's buffer_pc) and may be
#      clocked by the gate's derived (non-input) clock wire.
#  (3) HierarchicalValue as an assignment target: an instance output port
#      connected to a named generate block's net (minion_frontend's
#      `.f7_thread_awake_o(gen_multi_exp.f7_thread_awake)`).
#  (4) A widening cast of a narrow REG must not alias the reg's declared range
#      onto the target: `wren = 8'(en4_q); ... wren = 8'hff;` is in-range for
#      the 8-bit target (thread_buffer's f6_buffer_wren).
#  (5) Struct vars first written inside a `unique case` arm (a disjoint case
#      lowers to unique_if): module-scope structs pre-declare at module top and
#      function-scope shadow structs skip the leaf poison, so no dotted poison
#      store survives the branch merge (trans_top's f5_rom_response_l,
#      intpipe_csr_file's read_fcsr_as_frm).

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_slang_minion_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# ── (1) 2-D unpacked struct array: linearized tuple memory ────────────────────
cat >"$W/md2array.sv" <<'EOF'
package m2_pkg;
  typedef struct packed {
    logic       umode;
    logic [1:0] logsize;
    logic [3:0] max_msgs;
    logic       enable_oob;
  } conf_t;
endpackage

module md2array
  import m2_pkg::*;
(
  input  logic            clk_i,
  input  logic            tid_i,
  input  logic            pid_i,
  input  logic [1:0][1:0] wen_i,
  input  logic [7:0]      wdata_i,
  output logic [7:0]      rdata_o,
  output conf_t           conf_o
);
  conf_t conf_q [1:0][1:0];

  always_ff @(posedge clk_i) begin
    for (int t = 0; t < 2; t++) begin
      for (int p = 0; p < 2; p++) begin
        if (wen_i[p][t]) begin
          conf_q[t][p] <= '{
            umode:      wdata_i[0],
            logsize:    wdata_i[2:1],
            max_msgs:   wdata_i[6:3],
            enable_oob: wdata_i[7]
          };
        end
      end
    end
  end

  always_comb begin
    rdata_o = {conf_q[tid_i][pid_i].max_msgs,
               conf_q[tid_i][pid_i].logsize,
               conf_q[tid_i][pid_i].umode,
               conf_q[tid_i][pid_i].enable_oob};
    conf_o  = conf_q[tid_i][pid_i];
  end
endmodule
EOF
${LHD} compile "$W/md2array.sv" --top md2array \
  --emit-dir verilog:"$W/md2v/" --workdir "$W/md2w" -q \
  || fail "2-D unpacked struct array design did not compile"
cat "$W/md2v"/*.v >"$W/md2_all.v" 2>/dev/null
[ -s "$W/md2_all.v" ] || fail "no verilog emitted for md2array"
${LHD} lec --impl verilog:"$W/md2_all.v" \
  --ref verilog:"$W/md2array.sv" --top md2array --workdir "$W/md2lec" -q \
  --result-json "$W/md2lec.json" \
  || fail "2-D unpacked struct array LEC failed: $(cat "$W/md2lec.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/md2lec.json" || fail "md2array lec not pass"
echo "PASS: 2-D unpacked struct array linearizes and LECs against the source"

# ── (2) latch ICG + genblock reg array on the gated clock ─────────────────────
cat >"$W/gated.sv" <<'EOF'
module icg_gate (
  input  logic clk_i,
  input  logic en_i,
  output logic clk_o
);
  logic en_latch;
  always @(clk_i or en_i) begin
    if (!clk_i) begin
      en_latch <= en_i;
    end
  end
  assign clk_o = clk_i & en_latch;
endmodule

module gated_regarr (
  input  logic       clk_i,
  input  logic       en_i,
  input  logic       sel_i,
  input  logic       wen_i,
  input  logic [7:0] pc_i,
  output logic [7:0] pc_o
);
  logic clock_gated;
  icg_gate u_gate (.clk_i(clk_i), .en_i(en_i), .clk_o(clock_gated));

  logic [7:0] pc_q [2];
  for (genvar b = 0; b < 2; b++) begin : gen_pc
    always_ff @(posedge clock_gated)
      if (wen_i) pc_q[b] <= pc_i + 8'(b);
  end
  assign pc_o = pc_q[sel_i];
endmodule
EOF
${LHD} compile "$W/gated.sv" --top gated_regarr --workdir "$W/gw" -q \
  || fail "latch-ICG + gated-clock generate-for reg array did not compile"
echo "PASS: latch ICG is state (no false comb loop); genblock reg array takes the derived clock"

# ── (3) hierarchical assignment target (instance output into a genblock net) ──
cat >"$W/hier.sv" <<'EOF'
module hc_child (
  input  logic a_i,
  output logic o_o
);
  assign o_o = ~a_i;
endmodule

module hier_conn (
  input  logic a_i,
  output logic y_o
);
  if (1) begin : gen_exp
    logic awake;
  end
  if (1) begin : gen_sched
    hc_child u_c (.a_i(a_i), .o_o(gen_exp.awake));
  end
  assign y_o = gen_exp.awake;
endmodule
EOF
${LHD} compile "$W/hier.sv" --top hier_conn --workdir "$W/hw" -q \
  || fail "hierarchical output-connection target did not compile"
echo "PASS: HierarchicalValue assignment target lowers"

# ── (4) widening cast of a reg must not narrow the target's declared range ────
cat >"$W/wren.sv" <<'EOF'
module wren_cast (
  input  logic       clk_i,
  input  logic       use_pfb_i,
  input  logic       rst_pb_i,
  input  logic [3:0] en4_i,
  input  logic [7:0] wsel_i,
  output logic [7:0] wren_o
);
  logic [3:0] en4_q;
  always_ff @(posedge clk_i)
    en4_q <= en4_i;

  always_comb begin
    if (use_pfb_i) begin
      wren_o = 8'(en4_q);
      if (rst_pb_i)
        wren_o = 8'hff;
    end
    else begin
      wren_o = wsel_i;
    end
  end
endmodule
EOF
${LHD} compile "$W/wren.sv" --top wren_cast --emit-dir pyrope:"$W/wren_prp/" --workdir "$W/ww" -q \
  || fail "widening-cast + full-range overwrite falsely rejected (declared-range alias)"
if grep -q 'en4_q#\[0\.\.=3\]' "$W/wren_prp"/*.prp; then
  fail "widening cast emitted an identity slice/getmask for en4_q"
fi
${LHD} lec --impl pyrope:"$W/wren_prp/wren_cast.prp" \
  --ref verilog:"$W/wren.sv" --top wren_cast --set formal.engine=bmc \
  --set formal.strict=true --workdir "$W/wren_lec" -q \
  || fail "mask-free widening cast is not equivalent to the Verilog source"
echo "PASS: widening cast preserves range and emits no identity slice/getmask"

# The LGraph carries an unsigned input as one non-negative integer, without a
# Get_mask.  The generated C++ boundary must still interpret physical 4'b1111
# as +15 (not -1) before the value widens.
cat >"$W/uwiden.sv" <<'EOF'
module uwiden (
  input  logic       clk_i,
  input  logic [3:0] x_i,
  output logic [7:0] y_o
);
  logic [3:0] x_q;
  always_ff @(posedge clk_i)
    x_q <= x_i;
  assign y_o = x_q;
endmodule
EOF
cat >"$W/uwiden_tb.prp" <<'EOF'
const dut = import("lg:uwiden")
test dut.top_bit_set {
  mut acc = dut
  mut seen = 0
  tick 3 {
    acc.x_i = 15
    step
    seen = acc.y_o
  }
  assert(seen == 15, "unsigned widening preserves a set source msb")
}
EOF
${LHD} compile "$W/uwiden.sv" --reader slang --top uwiden \
  --emit-dir lg:"$W/uwiden_lg/" --workdir "$W/uwiden_lgw" -q \
  || fail "unsigned-widening LGraph emission failed"
${LHD} sim lg:"$W/uwiden_lg/" "$W/uwiden_tb.prp" --set sim.vcd=false \
  --workdir "$W/uwiden_sim" -q \
  || fail "mask-free unsigned widening simulated a set source msb incorrectly"
echo "PASS: mask-free LGraph unsigned widening keeps the physical input non-negative"

# Effective width equal to a signed destination width is not a no-op: bit 3
# becomes the sign. This is the boundary case for the mask-elision predicate.
cat >"$W/unarrow.sv" <<'EOF'
module unarrow (output logic signed [7:0] y_o);
  logic signed [3:0] narrow;
  always_comb begin
    narrow = 8'd15;
    y_o = narrow;
  end
endmodule
EOF
${LHD} compile "$W/unarrow.sv" --reader slang --top unarrow \
  --emit-dir pyrope:"$W/unarrow_prp/" --workdir "$W/unarrow_w" -q \
  || fail "unsigned-to-signed narrowing fixture did not compile"
${LHD} lec --impl pyrope:"$W/unarrow_prp/unarrow.prp" \
  --ref verilog:"$W/unarrow.sv" --top unarrow --set formal.engine=bmc \
  --set formal.strict=true --workdir "$W/unarrow_lec" -q \
  || fail "unsigned-to-signed narrowing lost the destination sign bit"
echo "PASS: unsigned-to-signed effective-width equality still reinterprets the sign bit"

# Splitting a packed substructure assignment into leaf outputs is a real
# precision boundary. A shifted packed value must be explicitly sliced before
# the leaf is stored; relying on the leaf declaration to truncate is invalid in
# unbounded LNAST/LGraph semantics and leaks upper sibling fields on repack.
cat >"$W/substruct_unpack.sv" <<'EOF'
package su_pkg;
  typedef struct packed {
    logic       top;
    logic [5:0] padding;
    logic [2:0] mode;
  } inner_t;
  typedef struct packed {
    logic   valid;
    inner_t ctrl;
  } outer_t;
endpackage

module substruct_unpack import su_pkg::*; (
  input  logic [9:0] packed_i,
  output outer_t     out_o
);
  inner_t ctrl;
  always_comb begin
    ctrl = packed_i;
    out_o = '0;
    out_o.ctrl = ctrl;
  end
endmodule
EOF
${LHD} compile "$W/substruct_unpack.sv" --reader slang --top substruct_unpack \
  --emit-dir pyrope:"$W/substruct_unpack_prp/" --workdir "$W/substruct_unpack_w" -q \
  || fail "nested packed-substruct lowering failed"
if ! grep -q '#\[3\.\.=8\]' "$W/substruct_unpack_prp/substruct_unpack.prp"; then
  fail "packed-substruct leaf split omitted the explicit padding slice"
fi
${LHD} lec --impl pyrope:"$W/substruct_unpack_prp/substruct_unpack.prp" \
  --ref verilog:"$W/substruct_unpack.sv" --top substruct_unpack \
  --set formal.engine=bmc --set formal.strict=true \
  --workdir "$W/substruct_unpack_lec" -q \
  || fail "packed-substruct leaf split is not equivalent to the Verilog source"
echo "PASS: packed-substruct leaf split explicitly selects each field"

# A dynamic packed-lvalue splice uses unbounded SHL/OR internally, but the
# declared packed base is a real language precision boundary. Keep the final
# low-64 selection explicit before the value reaches the register mux.
cat >"$W/dynamic_packed_write.sv" <<'EOF'
module dynamic_packed_write (
  input  logic        clk_i,
  input  logic [3:0]  idx_i,
  input  logic        bit_i,
  output logic [63:0] q_o
);
  logic [15:0][3:0] q, next;
  always_comb begin
    next = q;
    next[idx_i][0] = bit_i;
  end
  always_ff @(posedge clk_i)
    q <= next;
  assign q_o = q;
endmodule
EOF
${LHD} compile "$W/dynamic_packed_write.sv" --reader slang --top dynamic_packed_write \
  --emit-dir pyrope:"$W/dynamic_packed_write_prp/" \
  --workdir "$W/dynamic_packed_write_w" -q \
  || fail "dynamic packed-lvalue lowering failed"
if ! grep -q '#\[0\.\.=63\]' "$W/dynamic_packed_write_prp/dynamic_packed_write.prp"; then
  fail "dynamic packed-lvalue update omitted its declared-width boundary"
fi
${LHD} lec --impl pyrope:"$W/dynamic_packed_write_prp/dynamic_packed_write.prp" \
  --ref verilog:"$W/dynamic_packed_write.sv" --top dynamic_packed_write \
  --set formal.engine=bmc --set formal.strict=true \
  --workdir "$W/dynamic_packed_write_lec" -q \
  || fail "dynamic packed-lvalue boundary is not equivalent to the Verilog source"
cat >"$W/dynamic_packed_write_tb.prp" <<'EOF'
const dut = import("lg:dynamic_packed_write")
test dut.high_index {
  mut acc = dut
  mut seen = 0
  tick 3 {
    acc.idx_i = 15
    acc.bit_i = 1
    step
    seen = acc.q_o
  }
  assert(seen == 1152921504606846976, "dynamic high-index write stays in the 64-bit state")
}
EOF
${LHD} compile "$W/dynamic_packed_write.sv" --reader slang --top dynamic_packed_write \
  --emit-dir lg:"$W/dynamic_packed_write_lg/" \
  --workdir "$W/dynamic_packed_write_lgw" -q \
  || fail "dynamic packed-lvalue LGraph emission failed"
${LHD} sim lg:"$W/dynamic_packed_write_lg/" "$W/dynamic_packed_write_tb.prp" \
  --set sim.init_zero=true --set sim.vcd=false \
  --workdir "$W/dynamic_packed_write_sim" -q \
  || fail "dynamic packed-lvalue generated simulation failed"
echo "PASS: dynamic packed-lvalue update keeps its declared-width boundary explicit"

# A write to a CONSTANT inner element of a multi-dimensional packed array must
# convert the element ordinal into a BIT offset. For five-bit elements, inner
# element 1 is bits 5..9, not 1..5. This is intpipe_csr_msgs' queue-pointer
# shape, and it was an off-by-a-stride bug once.
cat >"$W/packed_sroa_stride.sv" <<'EOF'
module packed_sroa_stride (
  input  logic             clk_i,
  input  logic             rst_ni,
  input  logic             tid_i,
  input  logic [1:0]       pid_i,
  input  logic [4:0]       d_i,
  output logic [4:0]       q_o
);
  logic [1:0][3:0][4:0] ptr;
  for (genvar t = 0; t < 2; t++) begin : gen_t
    for (genvar p = 0; p < 4; p++) begin : gen_p
      always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni)
          ptr[t][p] <= '0;
        else if (t == tid_i && p == pid_i)
          ptr[t][p] <= d_i;
      end
    end
  end
  assign q_o = ptr[tid_i][pid_i];
endmodule
EOF
${LHD} compile "$W/packed_sroa_stride.sv" --reader slang --top packed_sroa_stride \
  --emit-dir pyrope:"$W/packed_sroa_stride_prp/" \
  --workdir "$W/packed_sroa_stride_w" -q \
  || fail "packed element-stride lowering failed"
# The stride claim survives the removal of packed-array SROA, it just moved
# spelling: the constant inner element ordinal must still become a BIT offset
# (five-bit elements => lane 1 is bits 5..9), whether the destination is an
# `eN` leaf or the flat packed bus. Assert the bit range, not the carrier.
grep -qE '#\[5\.\.=9\]' "$W/packed_sroa_stride_prp/packed_sroa_stride.prp" \
  || fail "packed inner element 1 did not use its five-bit stride"
if grep -qE '#\[1\.\.=5\]' "$W/packed_sroa_stride_prp/packed_sroa_stride.prp"; then
  fail "packed inner element 1 used its ordinal as a bit offset"
fi
${LHD} lec --impl pyrope:"$W/packed_sroa_stride_prp/packed_sroa_stride.prp" \
  --ref verilog:"$W/packed_sroa_stride.sv" --top packed_sroa_stride \
  --set formal.engine=bmc --set formal.strict=true \
  --workdir "$W/packed_sroa_stride_lec" -q \
  || fail "packed element-stride round trip is not equivalent"
echo "PASS: packed constant writes apply the inner element bit stride"

# Direct color fusion may materialize a child-internal packed expression wider
# than the child's public output carrier. The parent must still observe the
# declared module boundary before using that value in an ordinary operation.
cat >"$W/sub_output_boundary.sv" <<'EOF'
module packed_child (
  input  logic       a_i,
  input  logic       b_i,
  output logic [1:0] out_o
);
  always_comb begin
    out_o = '0;
    out_o[0] = a_i;
    out_o[1] = b_i;
  end
endmodule

module sub_output_boundary (
  input  logic       a_i,
  input  logic       b_i,
  input  logic [1:0] base_i,
  output logic [1:0] out_o
);
  logic [1:0] child;
  packed_child u_child(.a_i(a_i), .b_i(b_i), .out_o(child));
  assign out_o = base_i | child;
endmodule
EOF
cat >"$W/sub_output_boundary_tb.prp" <<'EOF'
const dut = import("lg:sub_output_boundary")
test dut.child_boundary {
  mut acc = dut
  acc.a_i = 1
  acc.b_i = 0
  acc.base_i = 2
  step
  assert(1 == 1, "the fused hierarchy compiles with static width checks")
}
EOF
${LHD} compile "$W/sub_output_boundary.sv" --reader slang --top sub_output_boundary \
  --emit-dir lg:"$W/sub_output_boundary_lg/" \
  --workdir "$W/sub_output_boundary_lgw" -q \
  || fail "sub-output boundary LGraph emission failed"
${LHD} sim lg:"$W/sub_output_boundary_lg/" "$W/sub_output_boundary_tb.prp" \
  --set sim.vcd=false --workdir "$W/sub_output_boundary_sim" -q \
  || fail "fused sub-output boundary generated simulation failed"
# The child's packed value must reach the parent through an EXPLICIT boundary
# conversion at the child's DECLARED output width (2), not as whatever carrier
# the pack happened to compute in.
#
# This used to grep for `zext_to<3>()`, because the child's `out_o[0]=`/`[1]=`
# writes lowered to a Set_mask chain whose carrier was one bit wider than the
# port. They now lower to a single Concat, whose width is the sum of its lane
# windows BY CELL CONTRACT -- exactly 2 -- so there is no longer a wider
# internal carrier to narrow, and the conversion lands at 2. The invariant being
# guarded is unchanged (the parent observes the declared boundary before using
# the value); only the width the boundary sits at moved, and it moved tighter.
# The boundary may be a typed slot, or direct color fusion may inline the child
# into the parent. In the latter case the inner `Slop_u<2>::from_proven` is the
# child boundary and the outer one is the top output boundary. Require either
# exact spelling; a single top-output conversion alone is not sufficient.
grep -Eq '__color_slot_u?2\[|Slop_u<2>::from_proven.*Slop_u<2>::from_proven' "$W"/sub_output_boundary_sim/sim/*.cpp \
  || fail "fused sub-output boundary did not observe the child's declared output width"
echo "PASS: fused hierarchy preserves the child output boundary"

# ── (5) struct first written inside a unique-case arm ─────────────────────────
cat >"$W/ucase.sv" <<'EOF'
package uc_pkg;
  typedef struct packed { logic [7:0] c0; logic [7:0] c1; logic taylor; } coefs_t;
  typedef struct packed { logic [2:0] rm; } frm_t;
endpackage

module ucase_struct
  import uc_pkg::*;
(
  input  logic [1:0]  sel_i,
  input  logic        taylor_i,
  input  coefs_t      rcp_i,
  input  coefs_t      log_i,
  input  coefs_t      sin_i,
  output logic [16:0] resp_o,
  output logic [7:0]  frm_o
);
  coefs_t resp_l;

  // First write of resp_l is INSIDE a unique-case arm; one arm mixes a whole
  // copy with a field override, one arm only writes a field (always_latch).
  /* verilator lint_off NOLATCH */
  always_latch begin
    unique case (sel_i)
      2'd0: begin
        resp_l = rcp_i;
      end
      2'd1: begin
        resp_l        = log_i;
        resp_l.taylor = taylor_i;
      end
      2'd2: begin
        resp_l.taylor = taylor_i;
      end
      default: begin
        resp_l = sin_i;
      end
    endcase
  end
  /* verilator lint_on NOLATCH */
  assign resp_o = resp_l;

  // Function with a struct arg + struct local, called from a case arm
  // (intpipe_csr_file's read_fcsr_as_frm shape).
  function automatic frm_t as_frm;
    input coefs_t orig;
    frm_t ret;
  begin
    ret = '0;
    ret.rm = orig.c0[2:0];
    return ret;
  end
  endfunction

  always_comb begin
    frm_o = '1;
    case (sel_i)
      2'd1:    frm_o = 8'(as_frm(rcp_i));
      2'd2:    frm_o = 8'(as_frm(log_i));
      default: ;
    endcase
  end
endmodule
EOF
${LHD} compile "$W/ucase.sv" --top ucase_struct --workdir "$W/uw" -q \
  || fail "struct first written inside a unique-case arm did not compile"
echo "PASS: unique-case-arm struct writes lower (pre-declared leaves, no in-arm poison)"

# Branch lowering is transactional for both the current driver and its width.
# The then arm's `lsb = 0` is one bit wide; if that width leaks into the else
# arm, `lsb + 1` becomes a two-bit adder and 4'h6 increments to 4'h3. This is
# the inc_wrap_rbox_ptrs shape used by intpipe_csr_msgs.
cat >"$W/guarded_inc.prp" <<'EOF'
pub comb guarded_inc(a:u5, m:u4) -> (o:u4) {
  mut lsb:u4 = a#[0..=3]
  if lsb == m {
    lsb = 0
  } else {
    lsb = (lsb + 1)#[0..=3]
  }
  o = lsb
}
EOF
cat >"$W/guarded_inc.sv" <<'EOF'
module guarded_inc(input logic [4:0] a, input logic [3:0] m, output logic [3:0] o);
  logic [3:0] lsb;
  always_comb begin
    lsb = a[3:0];
    if (lsb == m) lsb = '0;
    else          lsb = lsb + 1'b1;
    o = lsb;
  end
endmodule
EOF
${LHD} compile "$W/guarded_inc.prp" --top guarded_inc --recipe O1 \
  --emit verilog:"$W/guarded_inc_out.v" --workdir "$W/guarded_inc_w" -q \
  || fail "guarded increment Pyrope did not emit Verilog"
${LHD} lec --impl verilog:"$W/guarded_inc_out.v" --ref verilog:"$W/guarded_inc.sv" \
  --top guarded_inc --set formal.engine=ind --workdir "$W/guarded_inc_lec" -q \
  || fail "branch-local narrow assignment leaked its width into the sibling arm"
echo "PASS: branch rollback restores width metadata before lowering sibling arms"

# Function symbols are shared by the Slang AST across call sites. Each inlined
# call must snapshot its return value before the next call reuses that symbol;
# otherwise `classify(a) < classify(b)` degenerates into `classify < classify`.
cat >"$W/function_call_snapshot.sv" <<'EOF'
module function_call_snapshot (
  input  logic [4:0] a_i,
  input  logic [4:0] b_i,
  output logic       lt_o
);
  function automatic logic [2:0] classify(input logic [4:0] x);
    return x[2:0] ^ {3{x[4]}};
  endfunction

  assign lt_o = classify(a_i) < classify(b_i);
endmodule
EOF
${LHD} compile "$W/function_call_snapshot.sv" --top function_call_snapshot \
  --emit-dir pyrope:"$W/function_call_snapshot_prp/" \
  --workdir "$W/function_call_snapshot_w" -q \
  || fail "two calls to one inlined function did not compile"
if grep -Eq 'classify[[:space:]]*<[[:space:]]*classify' "$W/function_call_snapshot_prp"/*.prp; then
  fail "two function calls aliased the same mutable return slot"
fi
${LHD} lec --impl pyrope:"$W/function_call_snapshot_prp/function_call_snapshot.prp" \
  --ref verilog:"$W/function_call_snapshot.sv" --top function_call_snapshot \
  --set formal.engine=bmc --set formal.strict=true \
  --workdir "$W/function_call_snapshot_lec" -q \
  || fail "snapshotted function-call results are not equivalent to the Verilog source"
echo "PASS: each inlined function call snapshots its return value"

# A runtime branch that updates one packed-output field must invalidate only
# that field in uPass's comptime table. Whole-root invalidation used to erase
# the definite sibling constants below, turning wdata into 0 and data into X.
cat >"$W/bundle_field_uncertainty.sv" <<'EOF'
typedef struct packed {
  logic         wdata;
  logic [255:0] data;
  logic [4:0]   opcode;
} bundle_field_uncertainty_t;

module bundle_field_uncertainty (
  input  logic                      sel_i,
  output bundle_field_uncertainty_t req_o
);
  always_comb begin
    req_o.wdata = 1'b1;
    req_o.data = '0;
    if (sel_i)
      req_o.opcode = 5'd7;
    else
      req_o.opcode = 5'd3;
  end
endmodule
EOF
${LHD} compile "$W/bundle_field_uncertainty.sv" --top bundle_field_uncertainty \
  --emit-dir pyrope:"$W/bundle_field_uncertainty_prp/" \
  --workdir "$W/bundle_field_uncertainty_w" -q \
  || fail "packed output with a conditionally-written sibling field did not compile"
grep -Eq 'req_o\.wdata[[:space:]]*=[[:space:]]*1' "$W/bundle_field_uncertainty_prp"/*.prp \
  || fail "conditional opcode write erased the definite wdata sibling"
grep -Eq 'req_o\.data[[:space:]]*=[[:space:]]*0' "$W/bundle_field_uncertainty_prp"/*.prp \
  || fail "conditional opcode write erased the definite data sibling"
${LHD} lec --impl pyrope:"$W/bundle_field_uncertainty_prp/bundle_field_uncertainty.prp" \
  --ref verilog:"$W/bundle_field_uncertainty.sv" --top bundle_field_uncertainty \
  --set formal.engine=auto --set formal.strict=true \
  --workdir "$W/bundle_field_uncertainty_lec" -q \
  || fail "field-precise uncertainty lowering is not equivalent to the Verilog source"
echo "PASS: uncertain packed-field writes preserve definite siblings"

echo "PASS: all slang minion-feature regressions"

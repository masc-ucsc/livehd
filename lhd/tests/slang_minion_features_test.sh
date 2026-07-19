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
${LHD} lec --set formal.solver=lgyosys --impl verilog:"$W/md2_all.v" \
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
${LHD} compile "$W/wren.sv" --top wren_cast --workdir "$W/ww" -q \
  || fail "widening-cast + full-range overwrite falsely rejected (declared-range alias)"
echo "PASS: widening cast of a narrow reg does not narrow the target's declared range"

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

echo "PASS: all slang minion-feature regressions"

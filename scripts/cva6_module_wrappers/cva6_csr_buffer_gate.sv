// One-line instantiation top for CVA6's `csr_buffer` (holds the CSR address
// between issue and commit).
//
// `fu_data_t` is declared locally rather than imported from
// `cva6_alu_export_pkg`: the module runner appends exactly ONE wrapper file to the
// filelist, so a wrapper that depends on a second local package cannot compile.
// Same self-contained style as `cva6_tlb_gate.sv`.
module cva6_csr_buffer_gate
  import ariane_pkg::*;
#(
    parameter config_pkg::cva6_cfg_t CVA6Cfg = build_config_pkg::build_config(
        cva6_config_pkg::cva6_cfg
    )
) (
    input  logic                    clk_i,
    input  logic                    rst_ni,
    input  logic                    flush_i,
    // fu_data_t flattened to plain logic ports.
    input  logic [             7:0] operation_i,
    input  logic [CVA6Cfg.XLEN-1:0] operand_a_i,
    input  logic [CVA6Cfg.XLEN-1:0] operand_b_i,
    input  logic [CVA6Cfg.XLEN-1:0] imm_i,
    input  logic [             3:0] trans_id_i,
    output logic                    csr_ready_o,
    input  logic                    csr_valid_i,
    output logic [CVA6Cfg.XLEN-1:0] csr_result_o,
    input  logic                    csr_commit_i,
    output logic [            11:0] csr_addr_o
);

  // Mirrors the `fu_data_t` shape CVA6 binds in cva6.sv.
  typedef struct packed {
    fu_t                              fu;
    fu_op                             operation;
    logic [CVA6Cfg.XLEN-1:0]          operand_a;
    logic [CVA6Cfg.XLEN-1:0]          operand_b;
    logic [CVA6Cfg.XLEN-1:0]          imm;
    logic [CVA6Cfg.TRANS_ID_BITS-1:0] trans_id;
  } export_fu_data_t;

  export_fu_data_t fu_data;

  always_comb begin
    fu_data           = '0;
    fu_data.operation = fu_op'(operation_i);
    fu_data.operand_a = operand_a_i;
    fu_data.operand_b = operand_b_i;
    fu_data.imm       = imm_i;
    fu_data.trans_id  = trans_id_i[CVA6Cfg.TRANS_ID_BITS-1:0];
  end

  csr_buffer #(
      .CVA6Cfg  (CVA6Cfg),
      .fu_data_t(export_fu_data_t)
  ) dut (
      .clk_i       (clk_i),
      .rst_ni      (rst_ni),
      .flush_i     (flush_i),
      .fu_data_i   (fu_data),
      .csr_ready_o (csr_ready_o),
      .csr_valid_i (csr_valid_i),
      .csr_result_o(csr_result_o),
      .csr_commit_i(csr_commit_i),
      .csr_addr_o  (csr_addr_o)
  );

endmodule

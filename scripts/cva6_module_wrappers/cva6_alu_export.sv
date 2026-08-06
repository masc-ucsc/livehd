module cva6_alu_export
  import ariane_pkg::*;
(
    input  logic        clk_i,
    input  logic        rst_ni,
    input  logic [7:0]  operation_i,
    input  logic [63:0] operand_a_i,
    input  logic [63:0] operand_b_i,
    input  logic [63:0] imm_i,
    input  logic [3:0]  trans_id_i,
    output logic [63:0] result_o,
    output logic        branch_res_o
);

  import cva6_alu_export_pkg::*;

  export_fu_data_t fu_data;

  always_comb begin
    fu_data           = '0;
    fu_data.operation = fu_op'(operation_i);
    fu_data.operand_a = operand_a_i;
    fu_data.operand_b = operand_b_i;
    fu_data.imm       = imm_i;
    fu_data.trans_id  = trans_id_i[Cfg.TRANS_ID_BITS-1:0];
  end

  alu #(
      .CVA6Cfg  (Cfg),
      .HasBranch(1'b1),
      .fu_data_t(export_fu_data_t)
  ) dut (
      .clk_i           (clk_i),
      .rst_ni          (rst_ni),
      .fu_data_i       (fu_data),
      .fu_data_cpop_i  (fu_data),
      .result_o        (result_o),
      .alu_branch_res_o(branch_res_o)
  );

endmodule

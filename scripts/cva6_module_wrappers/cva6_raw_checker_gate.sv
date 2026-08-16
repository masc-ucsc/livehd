// One-line instantiation top for CVA6's `raw_checker` (scoreboard RAW-hazard
// search).  `CVA6Cfg` is the only parameter.
//
// `rd_i` is a PACKED 2-D array (`[NR_SB_ENTRIES-1:0][REG_ADDR_SIZE-1:0]`), so it
// is just a flat vector to the front end and needs no unpacking here.
module cva6_raw_checker_gate
  import ariane_pkg::*;
#(
    parameter config_pkg::cva6_cfg_t CVA6Cfg = build_config_pkg::build_config(
        cva6_config_pkg::cva6_cfg
    )
) (
    input  logic                                                         clk_i,
    input  logic                                                         rst_ni,
    input  logic [                REG_ADDR_SIZE-1:0]                     rs_i,
    input  logic                                                         rs_fpr_i,
    input  logic [CVA6Cfg.NR_SB_ENTRIES-1:0][REG_ADDR_SIZE-1:0]          rd_i,
    input  logic [CVA6Cfg.NR_SB_ENTRIES-1:0]                             rd_fpr_i,
    input  logic [CVA6Cfg.NR_SB_ENTRIES-1:0]                             still_issued_i,
    input  logic [CVA6Cfg.TRANS_ID_BITS-1:0]                             issue_pointer_i,
    output logic [CVA6Cfg.TRANS_ID_BITS-1:0]                             idx_o,
    output logic                                                         valid_o
);

  raw_checker #(
      .CVA6Cfg(CVA6Cfg)
  ) dut (
      .clk_i          (clk_i),
      .rst_ni         (rst_ni),
      .rs_i           (rs_i),
      .rs_fpr_i       (rs_fpr_i),
      .rd_i           (rd_i),
      .rd_fpr_i       (rd_fpr_i),
      .still_issued_i (still_issued_i),
      .issue_pointer_i(issue_pointer_i),
      .idx_o          (idx_o),
      .valid_o        (valid_o)
  );

endmodule

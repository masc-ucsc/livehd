// One-line instantiation top for CVA6's `instr_realign` (fetch-buffer realigner
// for mixed 16/32-bit instructions).  `CVA6Cfg` is its only parameter.
//
// Sequential: ~81 flops (`unaligned_instr_q`, `unaligned_q`, `unaligned_address_q`),
// so this exercises `_next`/`_step`, not just `_comb`.
module cva6_instr_realign_gate
  import ariane_pkg::*;
#(
    parameter config_pkg::cva6_cfg_t CVA6Cfg = build_config_pkg::build_config(
        cva6_config_pkg::cva6_cfg
    )
) (
    input  logic                                                    clk_i,
    input  logic                                                    rst_ni,
    input  logic                                                    flush_i,
    input  logic                                                    valid_i,
    output logic                                                    serving_unaligned_o,
    input  logic [       CVA6Cfg.VLEN-1:0]                          address_i,
    input  logic [CVA6Cfg.FETCH_WIDTH-1:0]                          data_i,
    output logic [CVA6Cfg.INSTR_PER_FETCH-1:0]                      valid_o,
    output logic [CVA6Cfg.INSTR_PER_FETCH-1:0][CVA6Cfg.VLEN-1:0]    addr_o,
    output logic [CVA6Cfg.INSTR_PER_FETCH-1:0][            31:0]    instr_o
);

  instr_realign #(
      .CVA6Cfg(CVA6Cfg)
  ) dut (
      .clk_i              (clk_i),
      .rst_ni             (rst_ni),
      .flush_i            (flush_i),
      .valid_i            (valid_i),
      .serving_unaligned_o(serving_unaligned_o),
      .address_i          (address_i),
      .data_i             (data_i),
      .valid_o            (valid_o),
      .addr_o             (addr_o),
      .instr_o            (instr_o)
  );

endmodule

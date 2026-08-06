package cva6_alu_export_pkg;
  import ariane_pkg::*;

  localparam config_pkg::cva6_cfg_t Cfg = build_config_pkg::build_config(cva6_config_pkg::cva6_cfg);

  typedef struct packed {
    fu_t                              fu;
    fu_op                             operation;
    logic [Cfg.XLEN-1:0]              operand_a;
    logic [Cfg.XLEN-1:0]              operand_b;
    logic [Cfg.XLEN-1:0]              imm;
    logic [Cfg.TRANS_ID_BITS-1:0]     trans_id;
  } export_fu_data_t;
endpackage

// One-line instantiation top for CVA6's `instr_scan` (branch/jump predecoder).
//
// Its only parameter is `CVA6Cfg`; every port is already plain `logic`, so this
// wrapper does nothing but bind the config so the module elaborates standalone.
// Without it, `CVA6Cfg` defaults to `config_pkg::cva6_cfg_empty` and the
// `CVA6Cfg.VLEN`-wide ports collapse to 1 bit (see
// scripts/CVA6_SV2V_FILELIST_REFERENCE.md section 4).
module cva6_instr_scan_gate
  import ariane_pkg::*;
#(
    parameter config_pkg::cva6_cfg_t CVA6Cfg = build_config_pkg::build_config(
        cva6_config_pkg::cva6_cfg
    )
) (
    input  logic [                31:0] instr_i,
    output logic                        rvi_return_o,
    output logic                        rvi_call_o,
    output logic                        rvi_branch_o,
    output logic                        rvi_jalr_o,
    output logic                        rvi_jump_o,
    output logic [CVA6Cfg.VLEN-1:0]     rvi_imm_o,
    output logic                        rvc_branch_o,
    output logic                        rvc_jump_o,
    output logic                        rvc_jr_o,
    output logic                        rvc_return_o,
    output logic                        rvc_jalr_o,
    output logic                        rvc_call_o,
    output logic [CVA6Cfg.VLEN-1:0]     rvc_imm_o
);

  instr_scan #(
      .CVA6Cfg(CVA6Cfg)
  ) dut (
      .instr_i     (instr_i),
      .rvi_return_o(rvi_return_o),
      .rvi_call_o  (rvi_call_o),
      .rvi_branch_o(rvi_branch_o),
      .rvi_jalr_o  (rvi_jalr_o),
      .rvi_jump_o  (rvi_jump_o),
      .rvi_imm_o   (rvi_imm_o),
      .rvc_branch_o(rvc_branch_o),
      .rvc_jump_o  (rvc_jump_o),
      .rvc_jr_o    (rvc_jr_o),
      .rvc_return_o(rvc_return_o),
      .rvc_jalr_o  (rvc_jalr_o),
      .rvc_call_o  (rvc_call_o),
      .rvc_imm_o   (rvc_imm_o)
  );

endmodule

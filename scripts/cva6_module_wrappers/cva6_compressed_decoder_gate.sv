// One-line instantiation top for CVA6's `compressed_decoder` (RVC -> RV32
// expansion).  `CVA6Cfg` is the only parameter, every port is plain `logic`, and
// it instantiates nothing -- the simplest wrapper in the sweep.
//
// 948 lines of pure decode with no state: the widest combinational fan-out block
// available, so it is the scale test for the decode-shaped (many narrow nodes)
// end of the spectrum, as opposed to the ALU's wide datapath.
module cva6_compressed_decoder_gate
  import ariane_pkg::*;
#(
    parameter config_pkg::cva6_cfg_t CVA6Cfg = build_config_pkg::build_config(
        cva6_config_pkg::cva6_cfg
    )
) (
    input  logic [31:0] instr_i,
    output logic [31:0] instr_o,
    output logic        illegal_instr_o,
    output logic        is_macro_instr_o,
    output logic        is_compressed_o,
    output logic        is_zcmt_instr_o
);

  compressed_decoder #(
      .CVA6Cfg(CVA6Cfg)
  ) dut (
      .instr_i         (instr_i),
      .instr_o         (instr_o),
      .illegal_instr_o (illegal_instr_o),
      .is_macro_instr_o(is_macro_instr_o),
      .is_compressed_o (is_compressed_o),
      .is_zcmt_instr_o (is_zcmt_instr_o)
  );

endmodule

// One-line instantiation top for CVA6's `ras` (return-address stack).
//
// `ras_t` is a `localparam type` inside frontend.sv (frontend.sv:88), not a
// package type, so it is re-declared here.  DEPTH comes from the config
// (RASDepth = 2 for cv64a6_imafdc_sv39_hpdcache_wb).
//
// Note the stack is `ras_t [DEPTH-1:0] stack_q` -- a PACKED array, so it lowers
// to flops, not to a memory node.  That is what keeps this module inside the
// bridge's reach today.
module cva6_ras_gate
  import ariane_pkg::*;
#(
    parameter config_pkg::cva6_cfg_t CVA6Cfg = build_config_pkg::build_config(
        cva6_config_pkg::cva6_cfg
    )
) (
    input  logic                    clk_i,
    input  logic                    rst_ni,
    input  logic                    flush_bp_i,
    input  logic                    push_i,
    input  logic                    pop_i,
    input  logic [CVA6Cfg.VLEN-1:0] data_i,
    output logic                    data_valid_o,
    output logic [CVA6Cfg.VLEN-1:0] data_ra_o
);

  // Mirrors frontend.sv:88.
  typedef struct packed {
    logic                    valid;
    logic [CVA6Cfg.VLEN-1:0] ra;
  } ras_t;

  ras_t data_out;

  assign data_valid_o = data_out.valid;
  assign data_ra_o    = data_out.ra;

  ras #(
      .CVA6Cfg(CVA6Cfg),
      .ras_t  (ras_t),
      .DEPTH  (CVA6Cfg.RASDepth)
  ) dut (
      .clk_i     (clk_i),
      .rst_ni    (rst_ni),
      .flush_bp_i(flush_bp_i),
      .push_i    (push_i),
      .pop_i     (pop_i),
      .data_i    (data_i),
      .data_o    (data_out)
  );

endmodule

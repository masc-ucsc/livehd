module StageReg_6(
  input   clock,
  input   reset,
  input   io_in_wb_ctrl_toreg,
  input   io_in_wb_ctrl_regwrite,
  input   io_flush,
  input   io_valid,
  output  io_data_wb_ctrl_toreg,
  output  io_data_wb_ctrl_regwrite
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
`endif // RANDOMIZE_REG_INIT
  reg  reg_wb_ctrl_toreg;
  reg  reg_wb_ctrl_regwrite;
  assign io_data_wb_ctrl_toreg = reg_wb_ctrl_toreg;
  assign io_data_wb_ctrl_regwrite = reg_wb_ctrl_regwrite;
  always @(posedge clock) begin
    if (reset) begin
      reg_wb_ctrl_toreg <= 1'h0;
    end else if (io_flush) begin
      reg_wb_ctrl_toreg <= 1'h0;
    end else if (io_valid) begin
      reg_wb_ctrl_toreg <= io_in_wb_ctrl_toreg;
    end
    if (reset) begin
      reg_wb_ctrl_regwrite <= 1'h0;
    end else if (io_flush) begin
      reg_wb_ctrl_regwrite <= 1'h0;
    end else if (io_valid) begin
      reg_wb_ctrl_regwrite <= io_in_wb_ctrl_regwrite;
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  reg_wb_ctrl_toreg = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  reg_wb_ctrl_regwrite = _RAND_1[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

module StageReg_2(
  input        clock,
  input        reset,
  input        io_in_ex_ctrl_itype,
  input        io_in_ex_ctrl_aluop,
  input        io_in_ex_ctrl_resultselect,
  input        io_in_ex_ctrl_xsrc,
  input        io_in_ex_ctrl_ysrc,
  input        io_in_ex_ctrl_plus4,
  input        io_in_ex_ctrl_branch,
  input        io_in_ex_ctrl_jal,
  input        io_in_ex_ctrl_jalr,
  input        io_in_ex_ctrl_prediction,
  input  [1:0] io_in_mem_ctrl_memop,
  input        io_in_wb_ctrl_toreg,
  input        io_in_wb_ctrl_regwrite,
  input        io_flush,
  input        io_valid,
  output       io_data_ex_ctrl_itype,
  output       io_data_ex_ctrl_aluop,
  output       io_data_ex_ctrl_resultselect,
  output       io_data_ex_ctrl_xsrc,
  output       io_data_ex_ctrl_ysrc,
  output       io_data_ex_ctrl_plus4,
  output       io_data_ex_ctrl_branch,
  output       io_data_ex_ctrl_jal,
  output       io_data_ex_ctrl_jalr,
  output       io_data_ex_ctrl_prediction,
  output [1:0] io_data_mem_ctrl_memop,
  output       io_data_wb_ctrl_toreg,
  output       io_data_wb_ctrl_regwrite
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_7;
  reg [31:0] _RAND_8;
  reg [31:0] _RAND_9;
  reg [31:0] _RAND_10;
  reg [31:0] _RAND_11;
  reg [31:0] _RAND_12;
`endif // RANDOMIZE_REG_INIT
  reg  reg_ex_ctrl_itype;
  reg  reg_ex_ctrl_aluop;
  reg  reg_ex_ctrl_resultselect;
  reg  reg_ex_ctrl_xsrc;
  reg  reg_ex_ctrl_ysrc;
  reg  reg_ex_ctrl_plus4;
  reg  reg_ex_ctrl_branch;
  reg  reg_ex_ctrl_jal;
  reg  reg_ex_ctrl_jalr;
  reg  reg_ex_ctrl_prediction;
  reg [1:0] reg_mem_ctrl_memop;
  reg  reg_wb_ctrl_toreg;
  reg  reg_wb_ctrl_regwrite;
  assign io_data_ex_ctrl_itype = reg_ex_ctrl_itype;
  assign io_data_ex_ctrl_aluop = reg_ex_ctrl_aluop;
  assign io_data_ex_ctrl_resultselect = reg_ex_ctrl_resultselect;
  assign io_data_ex_ctrl_xsrc = reg_ex_ctrl_xsrc;
  assign io_data_ex_ctrl_ysrc = reg_ex_ctrl_ysrc;
  assign io_data_ex_ctrl_plus4 = reg_ex_ctrl_plus4;
  assign io_data_ex_ctrl_branch = reg_ex_ctrl_branch;
  assign io_data_ex_ctrl_jal = reg_ex_ctrl_jal;
  assign io_data_ex_ctrl_jalr = reg_ex_ctrl_jalr;
  assign io_data_ex_ctrl_prediction = reg_ex_ctrl_prediction;
  assign io_data_mem_ctrl_memop = reg_mem_ctrl_memop;
  assign io_data_wb_ctrl_toreg = reg_wb_ctrl_toreg;
  assign io_data_wb_ctrl_regwrite = reg_wb_ctrl_regwrite;
  always @(posedge clock) begin
    if (reset) begin
      reg_ex_ctrl_itype <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_itype <= 1'h0;
    end else if (io_valid) begin
      reg_ex_ctrl_itype <= io_in_ex_ctrl_itype;
    end
    if (reset) begin
      reg_ex_ctrl_aluop <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_aluop <= 1'h0;
    end else if (io_valid) begin
      reg_ex_ctrl_aluop <= io_in_ex_ctrl_aluop;
    end
    if (reset) begin
      reg_ex_ctrl_resultselect <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_resultselect <= 1'h0;
    end else if (io_valid) begin
      reg_ex_ctrl_resultselect <= io_in_ex_ctrl_resultselect;
    end
    if (reset) begin
      reg_ex_ctrl_xsrc <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_xsrc <= 1'h0;
    end else if (io_valid) begin
      reg_ex_ctrl_xsrc <= io_in_ex_ctrl_xsrc;
    end
    if (reset) begin
      reg_ex_ctrl_ysrc <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_ysrc <= 1'h0;
    end else if (io_valid) begin
      reg_ex_ctrl_ysrc <= io_in_ex_ctrl_ysrc;
    end
    if (reset) begin
      reg_ex_ctrl_plus4 <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_plus4 <= 1'h0;
    end else if (io_valid) begin
      reg_ex_ctrl_plus4 <= io_in_ex_ctrl_plus4;
    end
    if (reset) begin
      reg_ex_ctrl_branch <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_branch <= 1'h0;
    end else if (io_valid) begin
      reg_ex_ctrl_branch <= io_in_ex_ctrl_branch;
    end
    if (reset) begin
      reg_ex_ctrl_jal <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_jal <= 1'h0;
    end else if (io_valid) begin
      reg_ex_ctrl_jal <= io_in_ex_ctrl_jal;
    end
    if (reset) begin
      reg_ex_ctrl_jalr <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_jalr <= 1'h0;
    end else if (io_valid) begin
      reg_ex_ctrl_jalr <= io_in_ex_ctrl_jalr;
    end
    if (reset) begin
      reg_ex_ctrl_prediction <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_prediction <= 1'h0;
    end else if (io_valid) begin
      reg_ex_ctrl_prediction <= io_in_ex_ctrl_prediction;
    end
    if (reset) begin
      reg_mem_ctrl_memop <= 2'h0;
    end else if (io_flush) begin
      reg_mem_ctrl_memop <= 2'h0;
    end else if (io_valid) begin
      reg_mem_ctrl_memop <= io_in_mem_ctrl_memop;
    end
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
  reg_ex_ctrl_itype = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  reg_ex_ctrl_aluop = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  reg_ex_ctrl_resultselect = _RAND_2[0:0];
  _RAND_3 = {1{`RANDOM}};
  reg_ex_ctrl_xsrc = _RAND_3[0:0];
  _RAND_4 = {1{`RANDOM}};
  reg_ex_ctrl_ysrc = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  reg_ex_ctrl_plus4 = _RAND_5[0:0];
  _RAND_6 = {1{`RANDOM}};
  reg_ex_ctrl_branch = _RAND_6[0:0];
  _RAND_7 = {1{`RANDOM}};
  reg_ex_ctrl_jal = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  reg_ex_ctrl_jalr = _RAND_8[0:0];
  _RAND_9 = {1{`RANDOM}};
  reg_ex_ctrl_prediction = _RAND_9[0:0];
  _RAND_10 = {1{`RANDOM}};
  reg_mem_ctrl_memop = _RAND_10[1:0];
  _RAND_11 = {1{`RANDOM}};
  reg_wb_ctrl_toreg = _RAND_11[0:0];
  _RAND_12 = {1{`RANDOM}};
  reg_wb_ctrl_regwrite = _RAND_12[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

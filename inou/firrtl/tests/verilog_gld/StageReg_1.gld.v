module StageReg_1(
  input         clock,
  input         reset,
  input  [31:0] io_in_pc,
  input  [31:0] io_in_instruction,
  input  [31:0] io_in_sextImm,
  input  [31:0] io_in_readdata1,
  input  [31:0] io_in_readdata2,
  input         io_flush,
  input         io_valid,
  output [31:0] io_data_pc,
  output [31:0] io_data_instruction,
  output [31:0] io_data_sextImm,
  output [31:0] io_data_readdata1,
  output [31:0] io_data_readdata2
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
`endif // RANDOMIZE_REG_INIT
  reg [31:0] reg_pc;
  reg [31:0] reg_instruction;
  reg [31:0] reg_sextImm;
  reg [31:0] reg_readdata1;
  reg [31:0] reg_readdata2;
  assign io_data_pc = reg_pc;
  assign io_data_instruction = reg_instruction;
  assign io_data_sextImm = reg_sextImm;
  assign io_data_readdata1 = reg_readdata1;
  assign io_data_readdata2 = reg_readdata2;
  always @(posedge clock) begin
    if (reset) begin
      reg_pc <= 32'h0;
    end else if (io_flush) begin
      reg_pc <= 32'h0;
    end else if (io_valid) begin
      reg_pc <= io_in_pc;
    end
    if (reset) begin
      reg_instruction <= 32'h0;
    end else if (io_flush) begin
      reg_instruction <= 32'h0;
    end else if (io_valid) begin
      reg_instruction <= io_in_instruction;
    end
    if (reset) begin
      reg_sextImm <= 32'h0;
    end else if (io_flush) begin
      reg_sextImm <= 32'h0;
    end else if (io_valid) begin
      reg_sextImm <= io_in_sextImm;
    end
    if (reset) begin
      reg_readdata1 <= 32'h0;
    end else if (io_flush) begin
      reg_readdata1 <= 32'h0;
    end else if (io_valid) begin
      reg_readdata1 <= io_in_readdata1;
    end
    if (reset) begin
      reg_readdata2 <= 32'h0;
    end else if (io_flush) begin
      reg_readdata2 <= 32'h0;
    end else if (io_valid) begin
      reg_readdata2 <= io_in_readdata2;
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
  reg_pc = _RAND_0[31:0];
  _RAND_1 = {1{`RANDOM}};
  reg_instruction = _RAND_1[31:0];
  _RAND_2 = {1{`RANDOM}};
  reg_sextImm = _RAND_2[31:0];
  _RAND_3 = {1{`RANDOM}};
  reg_readdata1 = _RAND_3[31:0];
  _RAND_4 = {1{`RANDOM}};
  reg_readdata2 = _RAND_4[31:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

module StageReg_3(
  input         clock,
  input         reset,
  input  [31:0] io_in_ex_result,
  input  [31:0] io_in_mem_writedata,
  input  [31:0] io_in_instruction,
  input  [31:0] io_in_next_pc,
  input         io_in_taken,
  input         io_flush,
  input         io_valid,
  output [31:0] io_data_ex_result,
  output [31:0] io_data_mem_writedata,
  output [31:0] io_data_instruction,
  output [31:0] io_data_next_pc,
  output        io_data_taken
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
`endif // RANDOMIZE_REG_INIT
  reg [31:0] reg_ex_result;
  reg [31:0] reg_mem_writedata;
  reg [31:0] reg_instruction;
  reg [31:0] reg_next_pc;
  reg  reg_taken;
  assign io_data_ex_result = reg_ex_result;
  assign io_data_mem_writedata = reg_mem_writedata;
  assign io_data_instruction = reg_instruction;
  assign io_data_next_pc = reg_next_pc;
  assign io_data_taken = reg_taken;
  always @(posedge clock) begin
    if (reset) begin
      reg_ex_result <= 32'h0;
    end else if (io_flush) begin
      reg_ex_result <= 32'h0;
    end else if (io_valid) begin
      reg_ex_result <= io_in_ex_result;
    end
    if (reset) begin
      reg_mem_writedata <= 32'h0;
    end else if (io_flush) begin
      reg_mem_writedata <= 32'h0;
    end else if (io_valid) begin
      reg_mem_writedata <= io_in_mem_writedata;
    end
    if (reset) begin
      reg_instruction <= 32'h0;
    end else if (io_flush) begin
      reg_instruction <= 32'h0;
    end else if (io_valid) begin
      reg_instruction <= io_in_instruction;
    end
    if (reset) begin
      reg_next_pc <= 32'h0;
    end else if (io_flush) begin
      reg_next_pc <= 32'h0;
    end else if (io_valid) begin
      reg_next_pc <= io_in_next_pc;
    end
    if (reset) begin
      reg_taken <= 1'h0;
    end else if (io_flush) begin
      reg_taken <= 1'h0;
    end else if (io_valid) begin
      reg_taken <= io_in_taken;
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
  reg_ex_result = _RAND_0[31:0];
  _RAND_1 = {1{`RANDOM}};
  reg_mem_writedata = _RAND_1[31:0];
  _RAND_2 = {1{`RANDOM}};
  reg_instruction = _RAND_2[31:0];
  _RAND_3 = {1{`RANDOM}};
  reg_next_pc = _RAND_3[31:0];
  _RAND_4 = {1{`RANDOM}};
  reg_taken = _RAND_4[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

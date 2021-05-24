module MaskedOnePortReadWriteSmem(
  input         clock,
  input         reset,
  input         io_enable,
  input         io_write,
  input         io_mask_0,
  input         io_mask_1,
  input  [9:0]  io_addr,
  input  [31:0] io_dataIn_0,
  input  [31:0] io_dataIn_1,
  output [31:0] io_dataOut_0,
  output [31:0] io_dataOut_1
);
`ifdef RANDOMIZE_MEM_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_3;
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
`endif // RANDOMIZE_REG_INIT
  reg [31:0] mem_0 [0:1023]; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  wire [31:0] mem_0_rdwrPort_r_data; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  wire [9:0] mem_0_rdwrPort_r_addr; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  wire [31:0] mem_0_rdwrPort_w_data; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  wire [9:0] mem_0_rdwrPort_w_addr; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  wire  mem_0_rdwrPort_w_mask; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  wire  mem_0_rdwrPort_w_en; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  reg  mem_0_rdwrPort_r_en_pipe_0;
  reg [9:0] mem_0_rdwrPort_r_addr_pipe_0;
  reg [31:0] mem_1 [0:1023]; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  wire [31:0] mem_1_rdwrPort_r_data; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  wire [9:0] mem_1_rdwrPort_r_addr; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  wire [31:0] mem_1_rdwrPort_w_data; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  wire [9:0] mem_1_rdwrPort_w_addr; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  wire  mem_1_rdwrPort_w_mask; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  wire  mem_1_rdwrPort_w_en; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  reg  mem_1_rdwrPort_r_en_pipe_0;
  reg [9:0] mem_1_rdwrPort_r_addr_pipe_0;
  wire  _GEN_4 = io_mask_1 | io_mask_0; // @[MaskedOnePortReadWriteSmem.scala 22:24 MaskedOnePortReadWriteSmem.scala 23:21]
  wire  _GEN_7 = io_write & _GEN_4; // @[MaskedOnePortReadWriteSmem.scala 18:21 MaskedOnePortReadWriteSmem.scala 14:24]
  wire  _GEN_18 = io_enable & _GEN_7; // @[MaskedOnePortReadWriteSmem.scala 16:19 MaskedOnePortReadWriteSmem.scala 14:24]
  assign mem_0_rdwrPort_r_addr = mem_0_rdwrPort_r_addr_pipe_0;
  assign mem_0_rdwrPort_r_data = mem_0[mem_0_rdwrPort_r_addr]; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  assign mem_0_rdwrPort_w_data = io_dataIn_0;
  assign mem_0_rdwrPort_w_addr = io_addr;
  assign mem_0_rdwrPort_w_mask = io_write & io_mask_0;
  assign mem_0_rdwrPort_w_en = io_enable & _GEN_18;
  assign mem_1_rdwrPort_r_addr = mem_1_rdwrPort_r_addr_pipe_0;
  assign mem_1_rdwrPort_r_data = mem_1[mem_1_rdwrPort_r_addr]; // @[MaskedOnePortReadWriteSmem.scala 14:24]
  assign mem_1_rdwrPort_w_data = io_dataIn_1;
  assign mem_1_rdwrPort_w_addr = io_addr;
  assign mem_1_rdwrPort_w_mask = io_write & io_mask_1;
  assign mem_1_rdwrPort_w_en = io_enable & _GEN_18;
  assign io_dataOut_0 = mem_0_rdwrPort_r_data; // @[MaskedOnePortReadWriteSmem.scala 18:21 MaskedOnePortReadWriteSmem.scala 25:30]
  assign io_dataOut_1 = mem_1_rdwrPort_r_data; // @[MaskedOnePortReadWriteSmem.scala 18:21 MaskedOnePortReadWriteSmem.scala 25:30]
  always @(posedge clock) begin
    if(mem_0_rdwrPort_w_en & mem_0_rdwrPort_w_mask) begin
      mem_0[mem_0_rdwrPort_w_addr] <= mem_0_rdwrPort_w_data; // @[MaskedOnePortReadWriteSmem.scala 14:24]
    end
    mem_0_rdwrPort_r_en_pipe_0 <= io_enable & ~_GEN_18;
    if (io_enable & ~_GEN_18) begin
      mem_0_rdwrPort_r_addr_pipe_0 <= io_addr;
    end
    if(mem_1_rdwrPort_w_en & mem_1_rdwrPort_w_mask) begin
      mem_1[mem_1_rdwrPort_w_addr] <= mem_1_rdwrPort_w_data; // @[MaskedOnePortReadWriteSmem.scala 14:24]
    end
    mem_1_rdwrPort_r_en_pipe_0 <= io_enable & ~_GEN_18;
    if (io_enable & ~_GEN_18) begin
      mem_1_rdwrPort_r_addr_pipe_0 <= io_addr;
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
`ifdef RANDOMIZE_MEM_INIT
  _RAND_0 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    mem_0[initvar] = _RAND_0[31:0];
  _RAND_3 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    mem_1[initvar] = _RAND_3[31:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  mem_0_rdwrPort_r_en_pipe_0 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  mem_0_rdwrPort_r_addr_pipe_0 = _RAND_2[9:0];
  _RAND_4 = {1{`RANDOM}};
  mem_1_rdwrPort_r_en_pipe_0 = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  mem_1_rdwrPort_r_addr_pipe_0 = _RAND_5[9:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

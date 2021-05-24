module OnePortReadWriteSmem(
  input         clock,
  input         reset,
  input         io_enable,
  input         io_write,
  input  [9:0]  io_addr,
  input  [31:0] io_dataIn,
  output [31:0] io_dataOut
);
`ifdef RANDOMIZE_MEM_INIT
  reg [31:0] _RAND_0;
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
`endif // RANDOMIZE_REG_INIT
  reg [31:0] mem [0:1023]; // @[OnePortReadWriteSmem.scala 14:24]
  wire [31:0] mem_rdwrPort_r_data; // @[OnePortReadWriteSmem.scala 14:24]
  wire [9:0] mem_rdwrPort_r_addr; // @[OnePortReadWriteSmem.scala 14:24]
  wire [31:0] mem_rdwrPort_w_data; // @[OnePortReadWriteSmem.scala 14:24]
  wire [9:0] mem_rdwrPort_w_addr; // @[OnePortReadWriteSmem.scala 14:24]
  wire  mem_rdwrPort_w_mask; // @[OnePortReadWriteSmem.scala 14:24]
  wire  mem_rdwrPort_w_en; // @[OnePortReadWriteSmem.scala 14:24]
  reg  mem_rdwrPort_r_en_pipe_0;
  reg [9:0] mem_rdwrPort_r_addr_pipe_0;
  wire  _GEN_8 = io_enable & io_write; // @[OnePortReadWriteSmem.scala 16:19 OnePortReadWriteSmem.scala 14:24]
  assign mem_rdwrPort_r_addr = mem_rdwrPort_r_addr_pipe_0;
  assign mem_rdwrPort_r_data = mem[mem_rdwrPort_r_addr]; // @[OnePortReadWriteSmem.scala 14:24]
  assign mem_rdwrPort_w_data = io_dataIn;
  assign mem_rdwrPort_w_addr = io_addr;
  assign mem_rdwrPort_w_mask = io_write;
  assign mem_rdwrPort_w_en = io_enable & _GEN_8;
  assign io_dataOut = mem_rdwrPort_r_data; // @[OnePortReadWriteSmem.scala 18:21 OnePortReadWriteSmem.scala 19:34]
  always @(posedge clock) begin
    if(mem_rdwrPort_w_en & mem_rdwrPort_w_mask) begin
      mem[mem_rdwrPort_w_addr] <= mem_rdwrPort_w_data; // @[OnePortReadWriteSmem.scala 14:24]
    end
    mem_rdwrPort_r_en_pipe_0 <= io_enable & ~_GEN_8;
    if (io_enable & ~_GEN_8) begin
      mem_rdwrPort_r_addr_pipe_0 <= io_addr;
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
    mem[initvar] = _RAND_0[31:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  mem_rdwrPort_r_en_pipe_0 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  mem_rdwrPort_r_addr_pipe_0 = _RAND_2[9:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

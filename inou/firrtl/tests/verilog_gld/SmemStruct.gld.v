module SmemStruct(
  input        clock,
  input        reset,
  input        io_enable,
  input        io_write,
  input  [9:0] io_addr,
  input  [1:0] io_dataIn_xx_d1,
  input  [1:0] io_dataIn_xx_d2,
  input  [3:0] io_dataIn_yy,
  output [1:0] io_dataOut_xx_d1,
  output [1:0] io_dataOut_xx_d2,
  output [3:0] io_dataOut_yy
);
`ifdef RANDOMIZE_MEM_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_6;
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_7;
  reg [31:0] _RAND_8;
`endif // RANDOMIZE_REG_INIT
  reg [1:0] my_mem_xx_d1 [0:1023]; // @[SmemStruct.scala 29:27]
  wire [1:0] my_mem_xx_d1_io_dataOut_MPORT_data; // @[SmemStruct.scala 29:27]
  wire [9:0] my_mem_xx_d1_io_dataOut_MPORT_addr; // @[SmemStruct.scala 29:27]
  wire [1:0] my_mem_xx_d1_MPORT_data; // @[SmemStruct.scala 29:27]
  wire [9:0] my_mem_xx_d1_MPORT_addr; // @[SmemStruct.scala 29:27]
  wire  my_mem_xx_d1_MPORT_mask; // @[SmemStruct.scala 29:27]
  wire  my_mem_xx_d1_MPORT_en; // @[SmemStruct.scala 29:27]
  reg  my_mem_xx_d1_io_dataOut_MPORT_en_pipe_0;
  reg [9:0] my_mem_xx_d1_io_dataOut_MPORT_addr_pipe_0;
  reg [1:0] my_mem_xx_d2 [0:1023]; // @[SmemStruct.scala 29:27]
  wire [1:0] my_mem_xx_d2_io_dataOut_MPORT_data; // @[SmemStruct.scala 29:27]
  wire [9:0] my_mem_xx_d2_io_dataOut_MPORT_addr; // @[SmemStruct.scala 29:27]
  wire [1:0] my_mem_xx_d2_MPORT_data; // @[SmemStruct.scala 29:27]
  wire [9:0] my_mem_xx_d2_MPORT_addr; // @[SmemStruct.scala 29:27]
  wire  my_mem_xx_d2_MPORT_mask; // @[SmemStruct.scala 29:27]
  wire  my_mem_xx_d2_MPORT_en; // @[SmemStruct.scala 29:27]
  reg  my_mem_xx_d2_io_dataOut_MPORT_en_pipe_0;
  reg [9:0] my_mem_xx_d2_io_dataOut_MPORT_addr_pipe_0;
  reg [3:0] my_mem_yy [0:1023]; // @[SmemStruct.scala 29:27]
  wire [3:0] my_mem_yy_io_dataOut_MPORT_data; // @[SmemStruct.scala 29:27]
  wire [9:0] my_mem_yy_io_dataOut_MPORT_addr; // @[SmemStruct.scala 29:27]
  wire [3:0] my_mem_yy_MPORT_data; // @[SmemStruct.scala 29:27]
  wire [9:0] my_mem_yy_MPORT_addr; // @[SmemStruct.scala 29:27]
  wire  my_mem_yy_MPORT_mask; // @[SmemStruct.scala 29:27]
  wire  my_mem_yy_MPORT_en; // @[SmemStruct.scala 29:27]
  reg  my_mem_yy_io_dataOut_MPORT_en_pipe_0;
  reg [9:0] my_mem_yy_io_dataOut_MPORT_addr_pipe_0;
  wire  _T = io_addr > 10'h20; // @[SmemStruct.scala 36:17]
  assign my_mem_xx_d1_io_dataOut_MPORT_addr = my_mem_xx_d1_io_dataOut_MPORT_addr_pipe_0;
  assign my_mem_xx_d1_io_dataOut_MPORT_data = my_mem_xx_d1[my_mem_xx_d1_io_dataOut_MPORT_addr]; // @[SmemStruct.scala 29:27]
  assign my_mem_xx_d1_MPORT_data = io_dataIn_xx_d1;
  assign my_mem_xx_d1_MPORT_addr = io_addr;
  assign my_mem_xx_d1_MPORT_mask = 1'h1;
  assign my_mem_xx_d1_MPORT_en = 1'h1;
  assign my_mem_xx_d2_io_dataOut_MPORT_addr = my_mem_xx_d2_io_dataOut_MPORT_addr_pipe_0;
  assign my_mem_xx_d2_io_dataOut_MPORT_data = my_mem_xx_d2[my_mem_xx_d2_io_dataOut_MPORT_addr]; // @[SmemStruct.scala 29:27]
  assign my_mem_xx_d2_MPORT_data = io_dataIn_xx_d2;
  assign my_mem_xx_d2_MPORT_addr = io_addr;
  assign my_mem_xx_d2_MPORT_mask = 1'h1;
  assign my_mem_xx_d2_MPORT_en = 1'h1;
  assign my_mem_yy_io_dataOut_MPORT_addr = my_mem_yy_io_dataOut_MPORT_addr_pipe_0;
  assign my_mem_yy_io_dataOut_MPORT_data = my_mem_yy[my_mem_yy_io_dataOut_MPORT_addr]; // @[SmemStruct.scala 29:27]
  assign my_mem_yy_MPORT_data = io_dataIn_yy;
  assign my_mem_yy_MPORT_addr = io_addr;
  assign my_mem_yy_MPORT_mask = 1'h1;
  assign my_mem_yy_MPORT_en = 1'h1;
  assign io_dataOut_xx_d1 = my_mem_xx_d1_io_dataOut_MPORT_data; // @[SmemStruct.scala 41:14]
  assign io_dataOut_xx_d2 = my_mem_xx_d2_io_dataOut_MPORT_data; // @[SmemStruct.scala 41:14]
  assign io_dataOut_yy = my_mem_yy_io_dataOut_MPORT_data; // @[SmemStruct.scala 41:14]
  always @(posedge clock) begin
    if(my_mem_xx_d1_MPORT_en & my_mem_xx_d1_MPORT_mask) begin
      my_mem_xx_d1[my_mem_xx_d1_MPORT_addr] <= my_mem_xx_d1_MPORT_data; // @[SmemStruct.scala 29:27]
    end
    if (_T) begin
      my_mem_xx_d1_io_dataOut_MPORT_en_pipe_0 <= io_enable;
    end else begin
      my_mem_xx_d1_io_dataOut_MPORT_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_xx_d1_io_dataOut_MPORT_addr_pipe_0 <= io_addr;
    end
    if(my_mem_xx_d2_MPORT_en & my_mem_xx_d2_MPORT_mask) begin
      my_mem_xx_d2[my_mem_xx_d2_MPORT_addr] <= my_mem_xx_d2_MPORT_data; // @[SmemStruct.scala 29:27]
    end
    if (_T) begin
      my_mem_xx_d2_io_dataOut_MPORT_en_pipe_0 <= io_enable;
    end else begin
      my_mem_xx_d2_io_dataOut_MPORT_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_xx_d2_io_dataOut_MPORT_addr_pipe_0 <= io_addr;
    end
    if(my_mem_yy_MPORT_en & my_mem_yy_MPORT_mask) begin
      my_mem_yy[my_mem_yy_MPORT_addr] <= my_mem_yy_MPORT_data; // @[SmemStruct.scala 29:27]
    end
    if (_T) begin
      my_mem_yy_io_dataOut_MPORT_en_pipe_0 <= io_enable;
    end else begin
      my_mem_yy_io_dataOut_MPORT_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_yy_io_dataOut_MPORT_addr_pipe_0 <= io_addr;
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
    my_mem_xx_d1[initvar] = _RAND_0[1:0];
  _RAND_3 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_xx_d2[initvar] = _RAND_3[1:0];
  _RAND_6 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_yy[initvar] = _RAND_6[3:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  my_mem_xx_d1_io_dataOut_MPORT_en_pipe_0 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  my_mem_xx_d1_io_dataOut_MPORT_addr_pipe_0 = _RAND_2[9:0];
  _RAND_4 = {1{`RANDOM}};
  my_mem_xx_d2_io_dataOut_MPORT_en_pipe_0 = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  my_mem_xx_d2_io_dataOut_MPORT_addr_pipe_0 = _RAND_5[9:0];
  _RAND_7 = {1{`RANDOM}};
  my_mem_yy_io_dataOut_MPORT_en_pipe_0 = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  my_mem_yy_io_dataOut_MPORT_addr_pipe_0 = _RAND_8[9:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

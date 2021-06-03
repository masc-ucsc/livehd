module MaskedSmemStruct(
  input        clock,
  input        reset,
  input        io_enable,
  input        io_write,
  input  [9:0] io_addr,
  input        io_mask_0,
  input        io_mask_1,
  input        io_mask_2,
  input        io_mask_3,
  input  [7:0] io_dataIn_0_x_data,
  input  [7:0] io_dataIn_0_y_data,
  input  [7:0] io_dataIn_1_x_data,
  input  [7:0] io_dataIn_1_y_data,
  input  [7:0] io_dataIn_2_x_data,
  input  [7:0] io_dataIn_2_y_data,
  input  [7:0] io_dataIn_3_x_data,
  input  [7:0] io_dataIn_3_y_data,
  output [7:0] io_dataOut_0_x_data,
  output [7:0] io_dataOut_0_y_data,
  output [7:0] io_dataOut_1_x_data,
  output [7:0] io_dataOut_1_y_data,
  output [7:0] io_dataOut_2_x_data,
  output [7:0] io_dataOut_2_y_data,
  output [7:0] io_dataOut_3_x_data,
  output [7:0] io_dataOut_3_y_data
);
`ifdef RANDOMIZE_MEM_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_9;
  reg [31:0] _RAND_12;
  reg [31:0] _RAND_15;
  reg [31:0] _RAND_18;
  reg [31:0] _RAND_21;
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_7;
  reg [31:0] _RAND_8;
  reg [31:0] _RAND_10;
  reg [31:0] _RAND_11;
  reg [31:0] _RAND_13;
  reg [31:0] _RAND_14;
  reg [31:0] _RAND_16;
  reg [31:0] _RAND_17;
  reg [31:0] _RAND_19;
  reg [31:0] _RAND_20;
  reg [31:0] _RAND_22;
  reg [31:0] _RAND_23;
`endif // RANDOMIZE_REG_INIT
  reg [7:0] my_mem_0_x_data [0:1023]; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_0_x_data_MPORT_1_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_0_x_data_MPORT_1_addr; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_0_x_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_0_x_data_MPORT_addr; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_0_x_data_MPORT_mask; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_0_x_data_MPORT_en; // @[MaskedSmemStruct.scala 24:27]
  reg  my_mem_0_x_data_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_0_x_data_MPORT_1_addr_pipe_0;
  reg [7:0] my_mem_0_y_data [0:1023]; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_0_y_data_MPORT_1_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_0_y_data_MPORT_1_addr; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_0_y_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_0_y_data_MPORT_addr; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_0_y_data_MPORT_mask; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_0_y_data_MPORT_en; // @[MaskedSmemStruct.scala 24:27]
  reg  my_mem_0_y_data_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_0_y_data_MPORT_1_addr_pipe_0;
  reg [7:0] my_mem_1_x_data [0:1023]; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_1_x_data_MPORT_1_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_1_x_data_MPORT_1_addr; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_1_x_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_1_x_data_MPORT_addr; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_1_x_data_MPORT_mask; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_1_x_data_MPORT_en; // @[MaskedSmemStruct.scala 24:27]
  reg  my_mem_1_x_data_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_1_x_data_MPORT_1_addr_pipe_0;
  reg [7:0] my_mem_1_y_data [0:1023]; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_1_y_data_MPORT_1_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_1_y_data_MPORT_1_addr; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_1_y_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_1_y_data_MPORT_addr; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_1_y_data_MPORT_mask; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_1_y_data_MPORT_en; // @[MaskedSmemStruct.scala 24:27]
  reg  my_mem_1_y_data_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_1_y_data_MPORT_1_addr_pipe_0;
  reg [7:0] my_mem_2_x_data [0:1023]; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_2_x_data_MPORT_1_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_2_x_data_MPORT_1_addr; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_2_x_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_2_x_data_MPORT_addr; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_2_x_data_MPORT_mask; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_2_x_data_MPORT_en; // @[MaskedSmemStruct.scala 24:27]
  reg  my_mem_2_x_data_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_2_x_data_MPORT_1_addr_pipe_0;
  reg [7:0] my_mem_2_y_data [0:1023]; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_2_y_data_MPORT_1_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_2_y_data_MPORT_1_addr; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_2_y_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_2_y_data_MPORT_addr; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_2_y_data_MPORT_mask; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_2_y_data_MPORT_en; // @[MaskedSmemStruct.scala 24:27]
  reg  my_mem_2_y_data_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_2_y_data_MPORT_1_addr_pipe_0;
  reg [7:0] my_mem_3_x_data [0:1023]; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_3_x_data_MPORT_1_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_3_x_data_MPORT_1_addr; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_3_x_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_3_x_data_MPORT_addr; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_3_x_data_MPORT_mask; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_3_x_data_MPORT_en; // @[MaskedSmemStruct.scala 24:27]
  reg  my_mem_3_x_data_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_3_x_data_MPORT_1_addr_pipe_0;
  reg [7:0] my_mem_3_y_data [0:1023]; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_3_y_data_MPORT_1_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_3_y_data_MPORT_1_addr; // @[MaskedSmemStruct.scala 24:27]
  wire [7:0] my_mem_3_y_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
  wire [9:0] my_mem_3_y_data_MPORT_addr; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_3_y_data_MPORT_mask; // @[MaskedSmemStruct.scala 24:27]
  wire  my_mem_3_y_data_MPORT_en; // @[MaskedSmemStruct.scala 24:27]
  reg  my_mem_3_y_data_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_3_y_data_MPORT_1_addr_pipe_0;
  wire  _T = io_addr > 10'h20; // @[MaskedSmemStruct.scala 30:17]
  assign my_mem_0_x_data_MPORT_1_addr = my_mem_0_x_data_MPORT_1_addr_pipe_0;
  assign my_mem_0_x_data_MPORT_1_data = my_mem_0_x_data[my_mem_0_x_data_MPORT_1_addr]; // @[MaskedSmemStruct.scala 24:27]
  assign my_mem_0_x_data_MPORT_data = io_dataIn_0_x_data;
  assign my_mem_0_x_data_MPORT_addr = io_addr;
  assign my_mem_0_x_data_MPORT_mask = io_mask_0;
  assign my_mem_0_x_data_MPORT_en = 1'h1;
  assign my_mem_0_y_data_MPORT_1_addr = my_mem_0_y_data_MPORT_1_addr_pipe_0;
  assign my_mem_0_y_data_MPORT_1_data = my_mem_0_y_data[my_mem_0_y_data_MPORT_1_addr]; // @[MaskedSmemStruct.scala 24:27]
  assign my_mem_0_y_data_MPORT_data = io_dataIn_0_y_data;
  assign my_mem_0_y_data_MPORT_addr = io_addr;
  assign my_mem_0_y_data_MPORT_mask = io_mask_0;
  assign my_mem_0_y_data_MPORT_en = 1'h1;
  assign my_mem_1_x_data_MPORT_1_addr = my_mem_1_x_data_MPORT_1_addr_pipe_0;
  assign my_mem_1_x_data_MPORT_1_data = my_mem_1_x_data[my_mem_1_x_data_MPORT_1_addr]; // @[MaskedSmemStruct.scala 24:27]
  assign my_mem_1_x_data_MPORT_data = io_dataIn_1_x_data;
  assign my_mem_1_x_data_MPORT_addr = io_addr;
  assign my_mem_1_x_data_MPORT_mask = io_mask_1;
  assign my_mem_1_x_data_MPORT_en = 1'h1;
  assign my_mem_1_y_data_MPORT_1_addr = my_mem_1_y_data_MPORT_1_addr_pipe_0;
  assign my_mem_1_y_data_MPORT_1_data = my_mem_1_y_data[my_mem_1_y_data_MPORT_1_addr]; // @[MaskedSmemStruct.scala 24:27]
  assign my_mem_1_y_data_MPORT_data = io_dataIn_1_y_data;
  assign my_mem_1_y_data_MPORT_addr = io_addr;
  assign my_mem_1_y_data_MPORT_mask = io_mask_1;
  assign my_mem_1_y_data_MPORT_en = 1'h1;
  assign my_mem_2_x_data_MPORT_1_addr = my_mem_2_x_data_MPORT_1_addr_pipe_0;
  assign my_mem_2_x_data_MPORT_1_data = my_mem_2_x_data[my_mem_2_x_data_MPORT_1_addr]; // @[MaskedSmemStruct.scala 24:27]
  assign my_mem_2_x_data_MPORT_data = io_dataIn_2_x_data;
  assign my_mem_2_x_data_MPORT_addr = io_addr;
  assign my_mem_2_x_data_MPORT_mask = io_mask_2;
  assign my_mem_2_x_data_MPORT_en = 1'h1;
  assign my_mem_2_y_data_MPORT_1_addr = my_mem_2_y_data_MPORT_1_addr_pipe_0;
  assign my_mem_2_y_data_MPORT_1_data = my_mem_2_y_data[my_mem_2_y_data_MPORT_1_addr]; // @[MaskedSmemStruct.scala 24:27]
  assign my_mem_2_y_data_MPORT_data = io_dataIn_2_y_data;
  assign my_mem_2_y_data_MPORT_addr = io_addr;
  assign my_mem_2_y_data_MPORT_mask = io_mask_2;
  assign my_mem_2_y_data_MPORT_en = 1'h1;
  assign my_mem_3_x_data_MPORT_1_addr = my_mem_3_x_data_MPORT_1_addr_pipe_0;
  assign my_mem_3_x_data_MPORT_1_data = my_mem_3_x_data[my_mem_3_x_data_MPORT_1_addr]; // @[MaskedSmemStruct.scala 24:27]
  assign my_mem_3_x_data_MPORT_data = io_dataIn_3_x_data;
  assign my_mem_3_x_data_MPORT_addr = io_addr;
  assign my_mem_3_x_data_MPORT_mask = io_mask_3;
  assign my_mem_3_x_data_MPORT_en = 1'h1;
  assign my_mem_3_y_data_MPORT_1_addr = my_mem_3_y_data_MPORT_1_addr_pipe_0;
  assign my_mem_3_y_data_MPORT_1_data = my_mem_3_y_data[my_mem_3_y_data_MPORT_1_addr]; // @[MaskedSmemStruct.scala 24:27]
  assign my_mem_3_y_data_MPORT_data = io_dataIn_3_y_data;
  assign my_mem_3_y_data_MPORT_addr = io_addr;
  assign my_mem_3_y_data_MPORT_mask = io_mask_3;
  assign my_mem_3_y_data_MPORT_en = 1'h1;
  assign io_dataOut_0_x_data = my_mem_0_x_data_MPORT_1_data; // @[MaskedSmemStruct.scala 35:14]
  assign io_dataOut_0_y_data = my_mem_0_y_data_MPORT_1_data; // @[MaskedSmemStruct.scala 35:14]
  assign io_dataOut_1_x_data = my_mem_1_x_data_MPORT_1_data; // @[MaskedSmemStruct.scala 35:14]
  assign io_dataOut_1_y_data = my_mem_1_y_data_MPORT_1_data; // @[MaskedSmemStruct.scala 35:14]
  assign io_dataOut_2_x_data = my_mem_2_x_data_MPORT_1_data; // @[MaskedSmemStruct.scala 35:14]
  assign io_dataOut_2_y_data = my_mem_2_y_data_MPORT_1_data; // @[MaskedSmemStruct.scala 35:14]
  assign io_dataOut_3_x_data = my_mem_3_x_data_MPORT_1_data; // @[MaskedSmemStruct.scala 35:14]
  assign io_dataOut_3_y_data = my_mem_3_y_data_MPORT_1_data; // @[MaskedSmemStruct.scala 35:14]
  always @(posedge clock) begin
    if(my_mem_0_x_data_MPORT_en & my_mem_0_x_data_MPORT_mask) begin
      my_mem_0_x_data[my_mem_0_x_data_MPORT_addr] <= my_mem_0_x_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
    end
    if (_T) begin
      my_mem_0_x_data_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_0_x_data_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_0_x_data_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_0_y_data_MPORT_en & my_mem_0_y_data_MPORT_mask) begin
      my_mem_0_y_data[my_mem_0_y_data_MPORT_addr] <= my_mem_0_y_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
    end
    if (_T) begin
      my_mem_0_y_data_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_0_y_data_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_0_y_data_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_1_x_data_MPORT_en & my_mem_1_x_data_MPORT_mask) begin
      my_mem_1_x_data[my_mem_1_x_data_MPORT_addr] <= my_mem_1_x_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
    end
    if (_T) begin
      my_mem_1_x_data_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_1_x_data_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_1_x_data_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_1_y_data_MPORT_en & my_mem_1_y_data_MPORT_mask) begin
      my_mem_1_y_data[my_mem_1_y_data_MPORT_addr] <= my_mem_1_y_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
    end
    if (_T) begin
      my_mem_1_y_data_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_1_y_data_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_1_y_data_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_2_x_data_MPORT_en & my_mem_2_x_data_MPORT_mask) begin
      my_mem_2_x_data[my_mem_2_x_data_MPORT_addr] <= my_mem_2_x_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
    end
    if (_T) begin
      my_mem_2_x_data_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_2_x_data_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_2_x_data_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_2_y_data_MPORT_en & my_mem_2_y_data_MPORT_mask) begin
      my_mem_2_y_data[my_mem_2_y_data_MPORT_addr] <= my_mem_2_y_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
    end
    if (_T) begin
      my_mem_2_y_data_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_2_y_data_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_2_y_data_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_3_x_data_MPORT_en & my_mem_3_x_data_MPORT_mask) begin
      my_mem_3_x_data[my_mem_3_x_data_MPORT_addr] <= my_mem_3_x_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
    end
    if (_T) begin
      my_mem_3_x_data_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_3_x_data_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_3_x_data_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_3_y_data_MPORT_en & my_mem_3_y_data_MPORT_mask) begin
      my_mem_3_y_data[my_mem_3_y_data_MPORT_addr] <= my_mem_3_y_data_MPORT_data; // @[MaskedSmemStruct.scala 24:27]
    end
    if (_T) begin
      my_mem_3_y_data_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_3_y_data_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_3_y_data_MPORT_1_addr_pipe_0 <= io_addr;
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
    my_mem_0_x_data[initvar] = _RAND_0[7:0];
  _RAND_3 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_0_y_data[initvar] = _RAND_3[7:0];
  _RAND_6 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_1_x_data[initvar] = _RAND_6[7:0];
  _RAND_9 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_1_y_data[initvar] = _RAND_9[7:0];
  _RAND_12 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_2_x_data[initvar] = _RAND_12[7:0];
  _RAND_15 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_2_y_data[initvar] = _RAND_15[7:0];
  _RAND_18 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_3_x_data[initvar] = _RAND_18[7:0];
  _RAND_21 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_3_y_data[initvar] = _RAND_21[7:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  my_mem_0_x_data_MPORT_1_en_pipe_0 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  my_mem_0_x_data_MPORT_1_addr_pipe_0 = _RAND_2[9:0];
  _RAND_4 = {1{`RANDOM}};
  my_mem_0_y_data_MPORT_1_en_pipe_0 = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  my_mem_0_y_data_MPORT_1_addr_pipe_0 = _RAND_5[9:0];
  _RAND_7 = {1{`RANDOM}};
  my_mem_1_x_data_MPORT_1_en_pipe_0 = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  my_mem_1_x_data_MPORT_1_addr_pipe_0 = _RAND_8[9:0];
  _RAND_10 = {1{`RANDOM}};
  my_mem_1_y_data_MPORT_1_en_pipe_0 = _RAND_10[0:0];
  _RAND_11 = {1{`RANDOM}};
  my_mem_1_y_data_MPORT_1_addr_pipe_0 = _RAND_11[9:0];
  _RAND_13 = {1{`RANDOM}};
  my_mem_2_x_data_MPORT_1_en_pipe_0 = _RAND_13[0:0];
  _RAND_14 = {1{`RANDOM}};
  my_mem_2_x_data_MPORT_1_addr_pipe_0 = _RAND_14[9:0];
  _RAND_16 = {1{`RANDOM}};
  my_mem_2_y_data_MPORT_1_en_pipe_0 = _RAND_16[0:0];
  _RAND_17 = {1{`RANDOM}};
  my_mem_2_y_data_MPORT_1_addr_pipe_0 = _RAND_17[9:0];
  _RAND_19 = {1{`RANDOM}};
  my_mem_3_x_data_MPORT_1_en_pipe_0 = _RAND_19[0:0];
  _RAND_20 = {1{`RANDOM}};
  my_mem_3_x_data_MPORT_1_addr_pipe_0 = _RAND_20[9:0];
  _RAND_22 = {1{`RANDOM}};
  my_mem_3_y_data_MPORT_1_en_pipe_0 = _RAND_22[0:0];
  _RAND_23 = {1{`RANDOM}};
  my_mem_3_y_data_MPORT_1_addr_pipe_0 = _RAND_23[9:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

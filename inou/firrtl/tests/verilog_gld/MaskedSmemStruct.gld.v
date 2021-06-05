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
  input  [1:0] io_dataIn_0_xx_d1,
  input  [1:0] io_dataIn_0_xx_d2,
  input  [3:0] io_dataIn_0_yy,
  input  [1:0] io_dataIn_1_xx_d1,
  input  [1:0] io_dataIn_1_xx_d2,
  input  [3:0] io_dataIn_1_yy,
  input  [1:0] io_dataIn_2_xx_d1,
  input  [1:0] io_dataIn_2_xx_d2,
  input  [3:0] io_dataIn_2_yy,
  input  [1:0] io_dataIn_3_xx_d1,
  input  [1:0] io_dataIn_3_xx_d2,
  input  [3:0] io_dataIn_3_yy,
  output [1:0] io_dataOut_0_xx_d1,
  output [1:0] io_dataOut_0_xx_d2,
  output [3:0] io_dataOut_0_yy,
  output [1:0] io_dataOut_1_xx_d1,
  output [1:0] io_dataOut_1_xx_d2,
  output [3:0] io_dataOut_1_yy,
  output [1:0] io_dataOut_2_xx_d1,
  output [1:0] io_dataOut_2_xx_d2,
  output [3:0] io_dataOut_2_yy,
  output [1:0] io_dataOut_3_xx_d1,
  output [1:0] io_dataOut_3_xx_d2,
  output [3:0] io_dataOut_3_yy
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
  reg [31:0] _RAND_24;
  reg [31:0] _RAND_27;
  reg [31:0] _RAND_30;
  reg [31:0] _RAND_33;
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
  reg [31:0] _RAND_25;
  reg [31:0] _RAND_26;
  reg [31:0] _RAND_28;
  reg [31:0] _RAND_29;
  reg [31:0] _RAND_31;
  reg [31:0] _RAND_32;
  reg [31:0] _RAND_34;
  reg [31:0] _RAND_35;
`endif // RANDOMIZE_REG_INIT
  reg [1:0] my_mem_0_xx_d1 [0:1023]; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_0_xx_d1_MPORT_1_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_0_xx_d1_MPORT_1_addr; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_0_xx_d1_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_0_xx_d1_MPORT_addr; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_0_xx_d1_MPORT_mask; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_0_xx_d1_MPORT_en; // @[MaskedSmemStruct.scala 30:27]
  reg  my_mem_0_xx_d1_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_0_xx_d1_MPORT_1_addr_pipe_0;
  reg [1:0] my_mem_0_xx_d2 [0:1023]; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_0_xx_d2_MPORT_1_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_0_xx_d2_MPORT_1_addr; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_0_xx_d2_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_0_xx_d2_MPORT_addr; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_0_xx_d2_MPORT_mask; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_0_xx_d2_MPORT_en; // @[MaskedSmemStruct.scala 30:27]
  reg  my_mem_0_xx_d2_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_0_xx_d2_MPORT_1_addr_pipe_0;
  reg [3:0] my_mem_0_yy [0:1023]; // @[MaskedSmemStruct.scala 30:27]
  wire [3:0] my_mem_0_yy_MPORT_1_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_0_yy_MPORT_1_addr; // @[MaskedSmemStruct.scala 30:27]
  wire [3:0] my_mem_0_yy_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_0_yy_MPORT_addr; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_0_yy_MPORT_mask; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_0_yy_MPORT_en; // @[MaskedSmemStruct.scala 30:27]
  reg  my_mem_0_yy_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_0_yy_MPORT_1_addr_pipe_0;
  reg [1:0] my_mem_1_xx_d1 [0:1023]; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_1_xx_d1_MPORT_1_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_1_xx_d1_MPORT_1_addr; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_1_xx_d1_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_1_xx_d1_MPORT_addr; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_1_xx_d1_MPORT_mask; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_1_xx_d1_MPORT_en; // @[MaskedSmemStruct.scala 30:27]
  reg  my_mem_1_xx_d1_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_1_xx_d1_MPORT_1_addr_pipe_0;
  reg [1:0] my_mem_1_xx_d2 [0:1023]; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_1_xx_d2_MPORT_1_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_1_xx_d2_MPORT_1_addr; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_1_xx_d2_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_1_xx_d2_MPORT_addr; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_1_xx_d2_MPORT_mask; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_1_xx_d2_MPORT_en; // @[MaskedSmemStruct.scala 30:27]
  reg  my_mem_1_xx_d2_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_1_xx_d2_MPORT_1_addr_pipe_0;
  reg [3:0] my_mem_1_yy [0:1023]; // @[MaskedSmemStruct.scala 30:27]
  wire [3:0] my_mem_1_yy_MPORT_1_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_1_yy_MPORT_1_addr; // @[MaskedSmemStruct.scala 30:27]
  wire [3:0] my_mem_1_yy_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_1_yy_MPORT_addr; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_1_yy_MPORT_mask; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_1_yy_MPORT_en; // @[MaskedSmemStruct.scala 30:27]
  reg  my_mem_1_yy_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_1_yy_MPORT_1_addr_pipe_0;
  reg [1:0] my_mem_2_xx_d1 [0:1023]; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_2_xx_d1_MPORT_1_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_2_xx_d1_MPORT_1_addr; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_2_xx_d1_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_2_xx_d1_MPORT_addr; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_2_xx_d1_MPORT_mask; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_2_xx_d1_MPORT_en; // @[MaskedSmemStruct.scala 30:27]
  reg  my_mem_2_xx_d1_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_2_xx_d1_MPORT_1_addr_pipe_0;
  reg [1:0] my_mem_2_xx_d2 [0:1023]; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_2_xx_d2_MPORT_1_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_2_xx_d2_MPORT_1_addr; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_2_xx_d2_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_2_xx_d2_MPORT_addr; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_2_xx_d2_MPORT_mask; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_2_xx_d2_MPORT_en; // @[MaskedSmemStruct.scala 30:27]
  reg  my_mem_2_xx_d2_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_2_xx_d2_MPORT_1_addr_pipe_0;
  reg [3:0] my_mem_2_yy [0:1023]; // @[MaskedSmemStruct.scala 30:27]
  wire [3:0] my_mem_2_yy_MPORT_1_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_2_yy_MPORT_1_addr; // @[MaskedSmemStruct.scala 30:27]
  wire [3:0] my_mem_2_yy_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_2_yy_MPORT_addr; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_2_yy_MPORT_mask; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_2_yy_MPORT_en; // @[MaskedSmemStruct.scala 30:27]
  reg  my_mem_2_yy_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_2_yy_MPORT_1_addr_pipe_0;
  reg [1:0] my_mem_3_xx_d1 [0:1023]; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_3_xx_d1_MPORT_1_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_3_xx_d1_MPORT_1_addr; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_3_xx_d1_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_3_xx_d1_MPORT_addr; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_3_xx_d1_MPORT_mask; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_3_xx_d1_MPORT_en; // @[MaskedSmemStruct.scala 30:27]
  reg  my_mem_3_xx_d1_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_3_xx_d1_MPORT_1_addr_pipe_0;
  reg [1:0] my_mem_3_xx_d2 [0:1023]; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_3_xx_d2_MPORT_1_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_3_xx_d2_MPORT_1_addr; // @[MaskedSmemStruct.scala 30:27]
  wire [1:0] my_mem_3_xx_d2_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_3_xx_d2_MPORT_addr; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_3_xx_d2_MPORT_mask; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_3_xx_d2_MPORT_en; // @[MaskedSmemStruct.scala 30:27]
  reg  my_mem_3_xx_d2_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_3_xx_d2_MPORT_1_addr_pipe_0;
  reg [3:0] my_mem_3_yy [0:1023]; // @[MaskedSmemStruct.scala 30:27]
  wire [3:0] my_mem_3_yy_MPORT_1_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_3_yy_MPORT_1_addr; // @[MaskedSmemStruct.scala 30:27]
  wire [3:0] my_mem_3_yy_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
  wire [9:0] my_mem_3_yy_MPORT_addr; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_3_yy_MPORT_mask; // @[MaskedSmemStruct.scala 30:27]
  wire  my_mem_3_yy_MPORT_en; // @[MaskedSmemStruct.scala 30:27]
  reg  my_mem_3_yy_MPORT_1_en_pipe_0;
  reg [9:0] my_mem_3_yy_MPORT_1_addr_pipe_0;
  wire  _T = io_addr > 10'h20; // @[MaskedSmemStruct.scala 36:17]
  assign my_mem_0_xx_d1_MPORT_1_addr = my_mem_0_xx_d1_MPORT_1_addr_pipe_0;
  assign my_mem_0_xx_d1_MPORT_1_data = my_mem_0_xx_d1[my_mem_0_xx_d1_MPORT_1_addr]; // @[MaskedSmemStruct.scala 30:27]
  assign my_mem_0_xx_d1_MPORT_data = io_dataIn_0_xx_d1;
  assign my_mem_0_xx_d1_MPORT_addr = io_addr;
  assign my_mem_0_xx_d1_MPORT_mask = io_mask_0;
  assign my_mem_0_xx_d1_MPORT_en = 1'h1;
  assign my_mem_0_xx_d2_MPORT_1_addr = my_mem_0_xx_d2_MPORT_1_addr_pipe_0;
  assign my_mem_0_xx_d2_MPORT_1_data = my_mem_0_xx_d2[my_mem_0_xx_d2_MPORT_1_addr]; // @[MaskedSmemStruct.scala 30:27]
  assign my_mem_0_xx_d2_MPORT_data = io_dataIn_0_xx_d2;
  assign my_mem_0_xx_d2_MPORT_addr = io_addr;
  assign my_mem_0_xx_d2_MPORT_mask = io_mask_0;
  assign my_mem_0_xx_d2_MPORT_en = 1'h1;
  assign my_mem_0_yy_MPORT_1_addr = my_mem_0_yy_MPORT_1_addr_pipe_0;
  assign my_mem_0_yy_MPORT_1_data = my_mem_0_yy[my_mem_0_yy_MPORT_1_addr]; // @[MaskedSmemStruct.scala 30:27]
  assign my_mem_0_yy_MPORT_data = io_dataIn_0_yy;
  assign my_mem_0_yy_MPORT_addr = io_addr;
  assign my_mem_0_yy_MPORT_mask = io_mask_0;
  assign my_mem_0_yy_MPORT_en = 1'h1;
  assign my_mem_1_xx_d1_MPORT_1_addr = my_mem_1_xx_d1_MPORT_1_addr_pipe_0;
  assign my_mem_1_xx_d1_MPORT_1_data = my_mem_1_xx_d1[my_mem_1_xx_d1_MPORT_1_addr]; // @[MaskedSmemStruct.scala 30:27]
  assign my_mem_1_xx_d1_MPORT_data = io_dataIn_1_xx_d1;
  assign my_mem_1_xx_d1_MPORT_addr = io_addr;
  assign my_mem_1_xx_d1_MPORT_mask = io_mask_1;
  assign my_mem_1_xx_d1_MPORT_en = 1'h1;
  assign my_mem_1_xx_d2_MPORT_1_addr = my_mem_1_xx_d2_MPORT_1_addr_pipe_0;
  assign my_mem_1_xx_d2_MPORT_1_data = my_mem_1_xx_d2[my_mem_1_xx_d2_MPORT_1_addr]; // @[MaskedSmemStruct.scala 30:27]
  assign my_mem_1_xx_d2_MPORT_data = io_dataIn_1_xx_d2;
  assign my_mem_1_xx_d2_MPORT_addr = io_addr;
  assign my_mem_1_xx_d2_MPORT_mask = io_mask_1;
  assign my_mem_1_xx_d2_MPORT_en = 1'h1;
  assign my_mem_1_yy_MPORT_1_addr = my_mem_1_yy_MPORT_1_addr_pipe_0;
  assign my_mem_1_yy_MPORT_1_data = my_mem_1_yy[my_mem_1_yy_MPORT_1_addr]; // @[MaskedSmemStruct.scala 30:27]
  assign my_mem_1_yy_MPORT_data = io_dataIn_1_yy;
  assign my_mem_1_yy_MPORT_addr = io_addr;
  assign my_mem_1_yy_MPORT_mask = io_mask_1;
  assign my_mem_1_yy_MPORT_en = 1'h1;
  assign my_mem_2_xx_d1_MPORT_1_addr = my_mem_2_xx_d1_MPORT_1_addr_pipe_0;
  assign my_mem_2_xx_d1_MPORT_1_data = my_mem_2_xx_d1[my_mem_2_xx_d1_MPORT_1_addr]; // @[MaskedSmemStruct.scala 30:27]
  assign my_mem_2_xx_d1_MPORT_data = io_dataIn_2_xx_d1;
  assign my_mem_2_xx_d1_MPORT_addr = io_addr;
  assign my_mem_2_xx_d1_MPORT_mask = io_mask_2;
  assign my_mem_2_xx_d1_MPORT_en = 1'h1;
  assign my_mem_2_xx_d2_MPORT_1_addr = my_mem_2_xx_d2_MPORT_1_addr_pipe_0;
  assign my_mem_2_xx_d2_MPORT_1_data = my_mem_2_xx_d2[my_mem_2_xx_d2_MPORT_1_addr]; // @[MaskedSmemStruct.scala 30:27]
  assign my_mem_2_xx_d2_MPORT_data = io_dataIn_2_xx_d2;
  assign my_mem_2_xx_d2_MPORT_addr = io_addr;
  assign my_mem_2_xx_d2_MPORT_mask = io_mask_2;
  assign my_mem_2_xx_d2_MPORT_en = 1'h1;
  assign my_mem_2_yy_MPORT_1_addr = my_mem_2_yy_MPORT_1_addr_pipe_0;
  assign my_mem_2_yy_MPORT_1_data = my_mem_2_yy[my_mem_2_yy_MPORT_1_addr]; // @[MaskedSmemStruct.scala 30:27]
  assign my_mem_2_yy_MPORT_data = io_dataIn_2_yy;
  assign my_mem_2_yy_MPORT_addr = io_addr;
  assign my_mem_2_yy_MPORT_mask = io_mask_2;
  assign my_mem_2_yy_MPORT_en = 1'h1;
  assign my_mem_3_xx_d1_MPORT_1_addr = my_mem_3_xx_d1_MPORT_1_addr_pipe_0;
  assign my_mem_3_xx_d1_MPORT_1_data = my_mem_3_xx_d1[my_mem_3_xx_d1_MPORT_1_addr]; // @[MaskedSmemStruct.scala 30:27]
  assign my_mem_3_xx_d1_MPORT_data = io_dataIn_3_xx_d1;
  assign my_mem_3_xx_d1_MPORT_addr = io_addr;
  assign my_mem_3_xx_d1_MPORT_mask = io_mask_3;
  assign my_mem_3_xx_d1_MPORT_en = 1'h1;
  assign my_mem_3_xx_d2_MPORT_1_addr = my_mem_3_xx_d2_MPORT_1_addr_pipe_0;
  assign my_mem_3_xx_d2_MPORT_1_data = my_mem_3_xx_d2[my_mem_3_xx_d2_MPORT_1_addr]; // @[MaskedSmemStruct.scala 30:27]
  assign my_mem_3_xx_d2_MPORT_data = io_dataIn_3_xx_d2;
  assign my_mem_3_xx_d2_MPORT_addr = io_addr;
  assign my_mem_3_xx_d2_MPORT_mask = io_mask_3;
  assign my_mem_3_xx_d2_MPORT_en = 1'h1;
  assign my_mem_3_yy_MPORT_1_addr = my_mem_3_yy_MPORT_1_addr_pipe_0;
  assign my_mem_3_yy_MPORT_1_data = my_mem_3_yy[my_mem_3_yy_MPORT_1_addr]; // @[MaskedSmemStruct.scala 30:27]
  assign my_mem_3_yy_MPORT_data = io_dataIn_3_yy;
  assign my_mem_3_yy_MPORT_addr = io_addr;
  assign my_mem_3_yy_MPORT_mask = io_mask_3;
  assign my_mem_3_yy_MPORT_en = 1'h1;
  assign io_dataOut_0_xx_d1 = my_mem_0_xx_d1_MPORT_1_data; // @[MaskedSmemStruct.scala 41:14]
  assign io_dataOut_0_xx_d2 = my_mem_0_xx_d2_MPORT_1_data; // @[MaskedSmemStruct.scala 41:14]
  assign io_dataOut_0_yy = my_mem_0_yy_MPORT_1_data; // @[MaskedSmemStruct.scala 41:14]
  assign io_dataOut_1_xx_d1 = my_mem_1_xx_d1_MPORT_1_data; // @[MaskedSmemStruct.scala 41:14]
  assign io_dataOut_1_xx_d2 = my_mem_1_xx_d2_MPORT_1_data; // @[MaskedSmemStruct.scala 41:14]
  assign io_dataOut_1_yy = my_mem_1_yy_MPORT_1_data; // @[MaskedSmemStruct.scala 41:14]
  assign io_dataOut_2_xx_d1 = my_mem_2_xx_d1_MPORT_1_data; // @[MaskedSmemStruct.scala 41:14]
  assign io_dataOut_2_xx_d2 = my_mem_2_xx_d2_MPORT_1_data; // @[MaskedSmemStruct.scala 41:14]
  assign io_dataOut_2_yy = my_mem_2_yy_MPORT_1_data; // @[MaskedSmemStruct.scala 41:14]
  assign io_dataOut_3_xx_d1 = my_mem_3_xx_d1_MPORT_1_data; // @[MaskedSmemStruct.scala 41:14]
  assign io_dataOut_3_xx_d2 = my_mem_3_xx_d2_MPORT_1_data; // @[MaskedSmemStruct.scala 41:14]
  assign io_dataOut_3_yy = my_mem_3_yy_MPORT_1_data; // @[MaskedSmemStruct.scala 41:14]
  always @(posedge clock) begin
    if(my_mem_0_xx_d1_MPORT_en & my_mem_0_xx_d1_MPORT_mask) begin
      my_mem_0_xx_d1[my_mem_0_xx_d1_MPORT_addr] <= my_mem_0_xx_d1_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
    end
    if (_T) begin
      my_mem_0_xx_d1_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_0_xx_d1_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_0_xx_d1_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_0_xx_d2_MPORT_en & my_mem_0_xx_d2_MPORT_mask) begin
      my_mem_0_xx_d2[my_mem_0_xx_d2_MPORT_addr] <= my_mem_0_xx_d2_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
    end
    if (_T) begin
      my_mem_0_xx_d2_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_0_xx_d2_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_0_xx_d2_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_0_yy_MPORT_en & my_mem_0_yy_MPORT_mask) begin
      my_mem_0_yy[my_mem_0_yy_MPORT_addr] <= my_mem_0_yy_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
    end
    if (_T) begin
      my_mem_0_yy_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_0_yy_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_0_yy_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_1_xx_d1_MPORT_en & my_mem_1_xx_d1_MPORT_mask) begin
      my_mem_1_xx_d1[my_mem_1_xx_d1_MPORT_addr] <= my_mem_1_xx_d1_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
    end
    if (_T) begin
      my_mem_1_xx_d1_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_1_xx_d1_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_1_xx_d1_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_1_xx_d2_MPORT_en & my_mem_1_xx_d2_MPORT_mask) begin
      my_mem_1_xx_d2[my_mem_1_xx_d2_MPORT_addr] <= my_mem_1_xx_d2_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
    end
    if (_T) begin
      my_mem_1_xx_d2_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_1_xx_d2_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_1_xx_d2_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_1_yy_MPORT_en & my_mem_1_yy_MPORT_mask) begin
      my_mem_1_yy[my_mem_1_yy_MPORT_addr] <= my_mem_1_yy_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
    end
    if (_T) begin
      my_mem_1_yy_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_1_yy_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_1_yy_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_2_xx_d1_MPORT_en & my_mem_2_xx_d1_MPORT_mask) begin
      my_mem_2_xx_d1[my_mem_2_xx_d1_MPORT_addr] <= my_mem_2_xx_d1_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
    end
    if (_T) begin
      my_mem_2_xx_d1_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_2_xx_d1_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_2_xx_d1_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_2_xx_d2_MPORT_en & my_mem_2_xx_d2_MPORT_mask) begin
      my_mem_2_xx_d2[my_mem_2_xx_d2_MPORT_addr] <= my_mem_2_xx_d2_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
    end
    if (_T) begin
      my_mem_2_xx_d2_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_2_xx_d2_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_2_xx_d2_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_2_yy_MPORT_en & my_mem_2_yy_MPORT_mask) begin
      my_mem_2_yy[my_mem_2_yy_MPORT_addr] <= my_mem_2_yy_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
    end
    if (_T) begin
      my_mem_2_yy_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_2_yy_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_2_yy_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_3_xx_d1_MPORT_en & my_mem_3_xx_d1_MPORT_mask) begin
      my_mem_3_xx_d1[my_mem_3_xx_d1_MPORT_addr] <= my_mem_3_xx_d1_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
    end
    if (_T) begin
      my_mem_3_xx_d1_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_3_xx_d1_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_3_xx_d1_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_3_xx_d2_MPORT_en & my_mem_3_xx_d2_MPORT_mask) begin
      my_mem_3_xx_d2[my_mem_3_xx_d2_MPORT_addr] <= my_mem_3_xx_d2_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
    end
    if (_T) begin
      my_mem_3_xx_d2_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_3_xx_d2_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_3_xx_d2_MPORT_1_addr_pipe_0 <= io_addr;
    end
    if(my_mem_3_yy_MPORT_en & my_mem_3_yy_MPORT_mask) begin
      my_mem_3_yy[my_mem_3_yy_MPORT_addr] <= my_mem_3_yy_MPORT_data; // @[MaskedSmemStruct.scala 30:27]
    end
    if (_T) begin
      my_mem_3_yy_MPORT_1_en_pipe_0 <= io_enable;
    end else begin
      my_mem_3_yy_MPORT_1_en_pipe_0 <= 1'h1;
    end
    if (_T ? io_enable : 1'h1) begin
      my_mem_3_yy_MPORT_1_addr_pipe_0 <= io_addr;
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
    my_mem_0_xx_d1[initvar] = _RAND_0[1:0];
  _RAND_3 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_0_xx_d2[initvar] = _RAND_3[1:0];
  _RAND_6 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_0_yy[initvar] = _RAND_6[3:0];
  _RAND_9 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_1_xx_d1[initvar] = _RAND_9[1:0];
  _RAND_12 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_1_xx_d2[initvar] = _RAND_12[1:0];
  _RAND_15 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_1_yy[initvar] = _RAND_15[3:0];
  _RAND_18 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_2_xx_d1[initvar] = _RAND_18[1:0];
  _RAND_21 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_2_xx_d2[initvar] = _RAND_21[1:0];
  _RAND_24 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_2_yy[initvar] = _RAND_24[3:0];
  _RAND_27 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_3_xx_d1[initvar] = _RAND_27[1:0];
  _RAND_30 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_3_xx_d2[initvar] = _RAND_30[1:0];
  _RAND_33 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    my_mem_3_yy[initvar] = _RAND_33[3:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  my_mem_0_xx_d1_MPORT_1_en_pipe_0 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  my_mem_0_xx_d1_MPORT_1_addr_pipe_0 = _RAND_2[9:0];
  _RAND_4 = {1{`RANDOM}};
  my_mem_0_xx_d2_MPORT_1_en_pipe_0 = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  my_mem_0_xx_d2_MPORT_1_addr_pipe_0 = _RAND_5[9:0];
  _RAND_7 = {1{`RANDOM}};
  my_mem_0_yy_MPORT_1_en_pipe_0 = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  my_mem_0_yy_MPORT_1_addr_pipe_0 = _RAND_8[9:0];
  _RAND_10 = {1{`RANDOM}};
  my_mem_1_xx_d1_MPORT_1_en_pipe_0 = _RAND_10[0:0];
  _RAND_11 = {1{`RANDOM}};
  my_mem_1_xx_d1_MPORT_1_addr_pipe_0 = _RAND_11[9:0];
  _RAND_13 = {1{`RANDOM}};
  my_mem_1_xx_d2_MPORT_1_en_pipe_0 = _RAND_13[0:0];
  _RAND_14 = {1{`RANDOM}};
  my_mem_1_xx_d2_MPORT_1_addr_pipe_0 = _RAND_14[9:0];
  _RAND_16 = {1{`RANDOM}};
  my_mem_1_yy_MPORT_1_en_pipe_0 = _RAND_16[0:0];
  _RAND_17 = {1{`RANDOM}};
  my_mem_1_yy_MPORT_1_addr_pipe_0 = _RAND_17[9:0];
  _RAND_19 = {1{`RANDOM}};
  my_mem_2_xx_d1_MPORT_1_en_pipe_0 = _RAND_19[0:0];
  _RAND_20 = {1{`RANDOM}};
  my_mem_2_xx_d1_MPORT_1_addr_pipe_0 = _RAND_20[9:0];
  _RAND_22 = {1{`RANDOM}};
  my_mem_2_xx_d2_MPORT_1_en_pipe_0 = _RAND_22[0:0];
  _RAND_23 = {1{`RANDOM}};
  my_mem_2_xx_d2_MPORT_1_addr_pipe_0 = _RAND_23[9:0];
  _RAND_25 = {1{`RANDOM}};
  my_mem_2_yy_MPORT_1_en_pipe_0 = _RAND_25[0:0];
  _RAND_26 = {1{`RANDOM}};
  my_mem_2_yy_MPORT_1_addr_pipe_0 = _RAND_26[9:0];
  _RAND_28 = {1{`RANDOM}};
  my_mem_3_xx_d1_MPORT_1_en_pipe_0 = _RAND_28[0:0];
  _RAND_29 = {1{`RANDOM}};
  my_mem_3_xx_d1_MPORT_1_addr_pipe_0 = _RAND_29[9:0];
  _RAND_31 = {1{`RANDOM}};
  my_mem_3_xx_d2_MPORT_1_en_pipe_0 = _RAND_31[0:0];
  _RAND_32 = {1{`RANDOM}};
  my_mem_3_xx_d2_MPORT_1_addr_pipe_0 = _RAND_32[9:0];
  _RAND_34 = {1{`RANDOM}};
  my_mem_3_yy_MPORT_1_en_pipe_0 = _RAND_34[0:0];
  _RAND_35 = {1{`RANDOM}};
  my_mem_3_yy_MPORT_1_addr_pipe_0 = _RAND_35[9:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

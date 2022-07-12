module MaxPeriodFibonacciLFSR_2(
  input   clock,
  input   reset,
  input   io_increment,
  output  io_out_0,
  output  io_out_1,
  output  io_out_2,
  output  io_out_3,
  output  io_out_4,
  output  io_out_5,
  output  io_out_6,
  output  io_out_7,
  output  io_out_8,
  output  io_out_9,
  output  io_out_10,
  output  io_out_11,
  output  io_out_12,
  output  io_out_13,
  output  io_out_14,
  output  io_out_15
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
  reg [31:0] _RAND_13;
  reg [31:0] _RAND_14;
  reg [31:0] _RAND_15;
`endif // RANDOMIZE_REG_INIT
  reg  state_0; // @[PRNG.scala 47:50]
  reg  state_1; // @[PRNG.scala 47:50]
  reg  state_2; // @[PRNG.scala 47:50]
  reg  state_3; // @[PRNG.scala 47:50]
  reg  state_4; // @[PRNG.scala 47:50]
  reg  state_5; // @[PRNG.scala 47:50]
  reg  state_6; // @[PRNG.scala 47:50]
  reg  state_7; // @[PRNG.scala 47:50]
  reg  state_8; // @[PRNG.scala 47:50]
  reg  state_9; // @[PRNG.scala 47:50]
  reg  state_10; // @[PRNG.scala 47:50]
  reg  state_11; // @[PRNG.scala 47:50]
  reg  state_12; // @[PRNG.scala 47:50]
  reg  state_13; // @[PRNG.scala 47:50]
  reg  state_14; // @[PRNG.scala 47:50]
  reg  state_15; // @[PRNG.scala 47:50]
  wire  _T_3 = state_15 ^ state_13 ^ state_12 ^ state_10; // @[LFSR.scala 15:41]
  wire  _GEN_15 = io_increment ? state_14 : state_15; // @[PRNG.scala 61:23 62:11 47:50]
  assign io_out_0 = state_0; // @[PRNG.scala 69:10]
  assign io_out_1 = state_1; // @[PRNG.scala 69:10]
  assign io_out_2 = state_2; // @[PRNG.scala 69:10]
  assign io_out_3 = state_3; // @[PRNG.scala 69:10]
  assign io_out_4 = state_4; // @[PRNG.scala 69:10]
  assign io_out_5 = state_5; // @[PRNG.scala 69:10]
  assign io_out_6 = state_6; // @[PRNG.scala 69:10]
  assign io_out_7 = state_7; // @[PRNG.scala 69:10]
  assign io_out_8 = state_8; // @[PRNG.scala 69:10]
  assign io_out_9 = state_9; // @[PRNG.scala 69:10]
  assign io_out_10 = state_10; // @[PRNG.scala 69:10]
  assign io_out_11 = state_11; // @[PRNG.scala 69:10]
  assign io_out_12 = state_12; // @[PRNG.scala 69:10]
  assign io_out_13 = state_13; // @[PRNG.scala 69:10]
  assign io_out_14 = state_14; // @[PRNG.scala 69:10]
  assign io_out_15 = state_15; // @[PRNG.scala 69:10]
  always @(posedge clock) begin
    if (reset) begin // @[PRNG.scala 47:50]
      state_0 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_0 <= _T_3; // @[PRNG.scala 62:11]
    end
    if (reset) begin // @[PRNG.scala 47:50]
      state_1 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_1 <= state_0; // @[PRNG.scala 62:11]
    end
    if (reset) begin // @[PRNG.scala 47:50]
      state_2 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_2 <= state_1; // @[PRNG.scala 62:11]
    end
    if (reset) begin // @[PRNG.scala 47:50]
      state_3 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_3 <= state_2; // @[PRNG.scala 62:11]
    end
    if (reset) begin // @[PRNG.scala 47:50]
      state_4 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_4 <= state_3; // @[PRNG.scala 62:11]
    end
    if (reset) begin // @[PRNG.scala 47:50]
      state_5 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_5 <= state_4; // @[PRNG.scala 62:11]
    end
    if (reset) begin // @[PRNG.scala 47:50]
      state_6 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_6 <= state_5; // @[PRNG.scala 62:11]
    end
    if (reset) begin // @[PRNG.scala 47:50]
      state_7 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_7 <= state_6; // @[PRNG.scala 62:11]
    end
    if (reset) begin // @[PRNG.scala 47:50]
      state_8 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_8 <= state_7; // @[PRNG.scala 62:11]
    end
    if (reset) begin // @[PRNG.scala 47:50]
      state_9 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_9 <= state_8; // @[PRNG.scala 62:11]
    end
    if (reset) begin // @[PRNG.scala 47:50]
      state_10 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_10 <= state_9; // @[PRNG.scala 62:11]
    end
    if (reset) begin // @[PRNG.scala 47:50]
      state_11 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_11 <= state_10; // @[PRNG.scala 62:11]
    end
    if (reset) begin // @[PRNG.scala 47:50]
      state_12 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_12 <= state_11; // @[PRNG.scala 62:11]
    end
    if (reset) begin // @[PRNG.scala 47:50]
      state_13 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_13 <= state_12; // @[PRNG.scala 62:11]
    end
    if (reset) begin // @[PRNG.scala 47:50]
      state_14 <= 1'h0; // @[PRNG.scala 47:50]
    end else if (io_increment) begin // @[PRNG.scala 61:23]
      state_14 <= state_13; // @[PRNG.scala 62:11]
    end
    state_15 <= reset | _GEN_15; // @[PRNG.scala 47:{50,50}]
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
  state_0 = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  state_1 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  state_2 = _RAND_2[0:0];
  _RAND_3 = {1{`RANDOM}};
  state_3 = _RAND_3[0:0];
  _RAND_4 = {1{`RANDOM}};
  state_4 = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  state_5 = _RAND_5[0:0];
  _RAND_6 = {1{`RANDOM}};
  state_6 = _RAND_6[0:0];
  _RAND_7 = {1{`RANDOM}};
  state_7 = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  state_8 = _RAND_8[0:0];
  _RAND_9 = {1{`RANDOM}};
  state_9 = _RAND_9[0:0];
  _RAND_10 = {1{`RANDOM}};
  state_10 = _RAND_10[0:0];
  _RAND_11 = {1{`RANDOM}};
  state_11 = _RAND_11[0:0];
  _RAND_12 = {1{`RANDOM}};
  state_12 = _RAND_12[0:0];
  _RAND_13 = {1{`RANDOM}};
  state_13 = _RAND_13[0:0];
  _RAND_14 = {1{`RANDOM}};
  state_14 = _RAND_14[0:0];
  _RAND_15 = {1{`RANDOM}};
  state_15 = _RAND_15[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module ICache(
  input         clock,
  input         reset,
  input         auto_master_out_a_ready,
  output        auto_master_out_a_valid,
  output [2:0]  auto_master_out_a_bits_opcode,
  output [2:0]  auto_master_out_a_bits_param,
  output [3:0]  auto_master_out_a_bits_size,
  output        auto_master_out_a_bits_source,
  output [31:0] auto_master_out_a_bits_address,
  output [7:0]  auto_master_out_a_bits_mask,
  output [63:0] auto_master_out_a_bits_data,
  output        auto_master_out_a_bits_corrupt,
  output        auto_master_out_d_ready,
  input         auto_master_out_d_valid,
  input  [2:0]  auto_master_out_d_bits_opcode,
  input  [1:0]  auto_master_out_d_bits_param,
  input  [3:0]  auto_master_out_d_bits_size,
  input         auto_master_out_d_bits_source,
  input  [2:0]  auto_master_out_d_bits_sink,
  input         auto_master_out_d_bits_denied,
  input  [63:0] auto_master_out_d_bits_data,
  input         auto_master_out_d_bits_corrupt,
  input         io_hartid,
  output        io_req_ready,
  input         io_req_valid,
  input  [38:0] io_req_bits_addr,
  input  [31:0] io_s1_paddr,
  input         io_s1_kill,
  input         io_s2_kill,
  input         io_s2_prefetch,
  output        io_resp_valid,
  output [63:0] io_resp_bits_data,
  output        io_resp_bits_replay,
  output        io_resp_bits_ae,
  input         io_invalidate,
  output        io_perf_acquire
);
`ifdef RANDOMIZE_MEM_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_9;
  reg [63:0] _RAND_12;
  reg [63:0] _RAND_15;
  reg [63:0] _RAND_18;
  reg [63:0] _RAND_21;
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
  reg [31:0] _RAND_24;
  reg [255:0] _RAND_25;
  reg [31:0] _RAND_26;
  reg [31:0] _RAND_27;
  reg [31:0] _RAND_28;
  reg [31:0] _RAND_29;
  reg [31:0] _RAND_30;
  reg [31:0] _RAND_31;
  reg [31:0] _RAND_32;
  reg [63:0] _RAND_33;
  reg [63:0] _RAND_34;
  reg [63:0] _RAND_35;
  reg [63:0] _RAND_36;
  reg [31:0] _RAND_37;
  reg [31:0] _RAND_38;
  reg [31:0] _RAND_39;
  reg [31:0] _RAND_40;
`endif // RANDOMIZE_REG_INIT
  wire  MaxPeriodFibonacciLFSR_clock; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_reset; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_increment; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_0; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_1; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_2; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_3; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_4; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_5; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_6; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_7; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_8; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_9; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_10; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_11; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_12; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_13; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_14; // @[PRNG.scala 82:22]
  wire  MaxPeriodFibonacciLFSR_io_out_15; // @[PRNG.scala 82:22]
  reg [19:0] tag_array_0 [0:63]; // @[icache.scala 179:30]
  wire  tag_array_0_tag_rdata_en; // @[icache.scala 179:30]
  wire [5:0] tag_array_0_tag_rdata_addr; // @[icache.scala 179:30]
  wire [19:0] tag_array_0_tag_rdata_data; // @[icache.scala 179:30]
  wire [19:0] tag_array_0__T_89_data; // @[icache.scala 179:30]
  wire [5:0] tag_array_0__T_89_addr; // @[icache.scala 179:30]
  wire  tag_array_0__T_89_mask; // @[icache.scala 179:30]
  wire  tag_array_0__T_89_en; // @[icache.scala 179:30]
  reg  tag_array_0_tag_rdata_en_pipe_0;
  reg [5:0] tag_array_0_tag_rdata_addr_pipe_0;
  reg [19:0] tag_array_1 [0:63]; // @[icache.scala 179:30]
  wire  tag_array_1_tag_rdata_en; // @[icache.scala 179:30]
  wire [5:0] tag_array_1_tag_rdata_addr; // @[icache.scala 179:30]
  wire [19:0] tag_array_1_tag_rdata_data; // @[icache.scala 179:30]
  wire [19:0] tag_array_1__T_89_data; // @[icache.scala 179:30]
  wire [5:0] tag_array_1__T_89_addr; // @[icache.scala 179:30]
  wire  tag_array_1__T_89_mask; // @[icache.scala 179:30]
  wire  tag_array_1__T_89_en; // @[icache.scala 179:30]
  reg  tag_array_1_tag_rdata_en_pipe_0;
  reg [5:0] tag_array_1_tag_rdata_addr_pipe_0;
  reg [19:0] tag_array_2 [0:63]; // @[icache.scala 179:30]
  wire  tag_array_2_tag_rdata_en; // @[icache.scala 179:30]
  wire [5:0] tag_array_2_tag_rdata_addr; // @[icache.scala 179:30]
  wire [19:0] tag_array_2_tag_rdata_data; // @[icache.scala 179:30]
  wire [19:0] tag_array_2__T_89_data; // @[icache.scala 179:30]
  wire [5:0] tag_array_2__T_89_addr; // @[icache.scala 179:30]
  wire  tag_array_2__T_89_mask; // @[icache.scala 179:30]
  wire  tag_array_2__T_89_en; // @[icache.scala 179:30]
  reg  tag_array_2_tag_rdata_en_pipe_0;
  reg [5:0] tag_array_2_tag_rdata_addr_pipe_0;
  reg [19:0] tag_array_3 [0:63]; // @[icache.scala 179:30]
  wire  tag_array_3_tag_rdata_en; // @[icache.scala 179:30]
  wire [5:0] tag_array_3_tag_rdata_addr; // @[icache.scala 179:30]
  wire [19:0] tag_array_3_tag_rdata_data; // @[icache.scala 179:30]
  wire [19:0] tag_array_3__T_89_data; // @[icache.scala 179:30]
  wire [5:0] tag_array_3__T_89_addr; // @[icache.scala 179:30]
  wire  tag_array_3__T_89_mask; // @[icache.scala 179:30]
  wire  tag_array_3__T_89_en; // @[icache.scala 179:30]
  reg  tag_array_3_tag_rdata_en_pipe_0;
  reg [5:0] tag_array_3_tag_rdata_addr_pipe_0;
  reg [63:0] dataArrayWay_0 [0:511]; // @[DescribedSRAM.scala 23:26]
  wire  dataArrayWay_0__T_139_en; // @[DescribedSRAM.scala 23:26]
  wire [8:0] dataArrayWay_0__T_139_addr; // @[DescribedSRAM.scala 23:26]
  wire [63:0] dataArrayWay_0__T_139_data; // @[DescribedSRAM.scala 23:26]
  wire [63:0] dataArrayWay_0__T_133_data; // @[DescribedSRAM.scala 23:26]
  wire [8:0] dataArrayWay_0__T_133_addr; // @[DescribedSRAM.scala 23:26]
  wire  dataArrayWay_0__T_133_mask; // @[DescribedSRAM.scala 23:26]
  wire  dataArrayWay_0__T_133_en; // @[DescribedSRAM.scala 23:26]
  reg  dataArrayWay_0__T_139_en_pipe_0;
  reg [8:0] dataArrayWay_0__T_139_addr_pipe_0;
  reg [63:0] dataArrayWay_1 [0:511]; // @[DescribedSRAM.scala 23:26]
  wire  dataArrayWay_1__T_153_en; // @[DescribedSRAM.scala 23:26]
  wire [8:0] dataArrayWay_1__T_153_addr; // @[DescribedSRAM.scala 23:26]
  wire [63:0] dataArrayWay_1__T_153_data; // @[DescribedSRAM.scala 23:26]
  wire [63:0] dataArrayWay_1__T_147_data; // @[DescribedSRAM.scala 23:26]
  wire [8:0] dataArrayWay_1__T_147_addr; // @[DescribedSRAM.scala 23:26]
  wire  dataArrayWay_1__T_147_mask; // @[DescribedSRAM.scala 23:26]
  wire  dataArrayWay_1__T_147_en; // @[DescribedSRAM.scala 23:26]
  reg  dataArrayWay_1__T_153_en_pipe_0;
  reg [8:0] dataArrayWay_1__T_153_addr_pipe_0;
  reg [63:0] dataArrayWay_2 [0:511]; // @[DescribedSRAM.scala 23:26]
  wire  dataArrayWay_2__T_167_en; // @[DescribedSRAM.scala 23:26]
  wire [8:0] dataArrayWay_2__T_167_addr; // @[DescribedSRAM.scala 23:26]
  wire [63:0] dataArrayWay_2__T_167_data; // @[DescribedSRAM.scala 23:26]
  wire [63:0] dataArrayWay_2__T_161_data; // @[DescribedSRAM.scala 23:26]
  wire [8:0] dataArrayWay_2__T_161_addr; // @[DescribedSRAM.scala 23:26]
  wire  dataArrayWay_2__T_161_mask; // @[DescribedSRAM.scala 23:26]
  wire  dataArrayWay_2__T_161_en; // @[DescribedSRAM.scala 23:26]
  reg  dataArrayWay_2__T_167_en_pipe_0;
  reg [8:0] dataArrayWay_2__T_167_addr_pipe_0;
  reg [63:0] dataArrayWay_3 [0:511]; // @[DescribedSRAM.scala 23:26]
  wire  dataArrayWay_3__T_181_en; // @[DescribedSRAM.scala 23:26]
  wire [8:0] dataArrayWay_3__T_181_addr; // @[DescribedSRAM.scala 23:26]
  wire [63:0] dataArrayWay_3__T_181_data; // @[DescribedSRAM.scala 23:26]
  wire [63:0] dataArrayWay_3__T_175_data; // @[DescribedSRAM.scala 23:26]
  wire [8:0] dataArrayWay_3__T_175_addr; // @[DescribedSRAM.scala 23:26]
  wire  dataArrayWay_3__T_175_mask; // @[DescribedSRAM.scala 23:26]
  wire  dataArrayWay_3__T_175_en; // @[DescribedSRAM.scala 23:26]
  reg  dataArrayWay_3__T_181_en_pipe_0;
  reg [8:0] dataArrayWay_3__T_181_addr_pipe_0;
  wire  s0_valid = io_req_ready & io_req_valid; // @[Decoupled.scala 40:37]
  reg  s1_valid; // @[icache.scala 154:25]
  reg [255:0] vb_array; // @[icache.scala 185:25]
  wire [5:0] s1_idx = io_s1_paddr[11:6]; // @[icache.scala 199:29]
  wire [6:0] _T_99 = {1'h0,s1_idx}; // @[Cat.scala 29:58]
  wire [255:0] _T_100 = vb_array >> _T_99; // @[icache.scala 201:25]
  wire  s1_vb = _T_100[0]; // @[icache.scala 201:25]
  wire [19:0] s1_tag = io_s1_paddr[31:12]; // @[icache.scala 200:29]
  wire  s1_tag_hit_0 = s1_vb & tag_array_0_tag_rdata_data == s1_tag; // @[icache.scala 203:28]
  wire [6:0] _T_103 = {1'h1,s1_idx}; // @[Cat.scala 29:58]
  wire [255:0] _T_104 = vb_array >> _T_103; // @[icache.scala 201:25]
  wire  s1_vb_1 = _T_104[0]; // @[icache.scala 201:25]
  wire  s1_tag_hit_1 = s1_vb_1 & tag_array_1_tag_rdata_data == s1_tag; // @[icache.scala 203:28]
  wire [7:0] _T_107 = {2'h2,s1_idx}; // @[Cat.scala 29:58]
  wire [255:0] _T_108 = vb_array >> _T_107; // @[icache.scala 201:25]
  wire  s1_vb_2 = _T_108[0]; // @[icache.scala 201:25]
  wire  s1_tag_hit_2 = s1_vb_2 & tag_array_2_tag_rdata_data == s1_tag; // @[icache.scala 203:28]
  wire [7:0] _T_111 = {2'h3,s1_idx}; // @[Cat.scala 29:58]
  wire [255:0] _T_112 = vb_array >> _T_111; // @[icache.scala 201:25]
  wire  s1_vb_3 = _T_112[0]; // @[icache.scala 201:25]
  wire  s1_tag_hit_3 = s1_vb_3 & tag_array_3_tag_rdata_data == s1_tag; // @[icache.scala 203:28]
  reg  s2_valid; // @[icache.scala 157:25]
  reg  s2_hit; // @[icache.scala 158:23]
  reg  invalidated; // @[icache.scala 161:24]
  reg  refill_valid; // @[icache.scala 162:29]
  reg  _T_6; // @[icache.scala 164:48]
  wire  s2_miss = s2_valid & ~s2_hit & ~_T_6; // @[icache.scala 164:37]
  wire  _T_199 = ~refill_valid; // @[icache.scala 350:32]
  wire  tl_out_a_valid = s2_miss & ~refill_valid & ~io_s2_kill; // @[icache.scala 350:46]
  wire  refill_fire = auto_master_out_a_ready & tl_out_a_valid; // @[Decoupled.scala 40:37]
  wire  _T_10 = s1_valid & ~(refill_valid | s2_miss); // @[icache.scala 165:54]
  reg [31:0] refill_paddr; // @[Reg.scala 15:16]
  wire [5:0] refill_idx = refill_paddr[11:6]; // @[icache.scala 167:32]
  wire  refill_one_beat = auto_master_out_d_valid & auto_master_out_d_bits_opcode[0]; // @[icache.scala 168:41]
  wire [26:0] _T_16 = 27'hfff << auto_master_out_d_bits_size; // @[package.scala 189:77]
  wire [11:0] _T_18 = ~_T_16[11:0]; // @[package.scala 189:46]
  wire [8:0] _T_21 = auto_master_out_d_bits_opcode[0] ? _T_18[11:3] : 9'h0; // @[Edges.scala 222:14]
  reg [8:0] _T_22; // @[Edges.scala 230:27]
  wire [8:0] _T_24 = _T_22 - 9'h1; // @[Edges.scala 231:28]
  wire  _T_25 = _T_22 == 9'h0; // @[Edges.scala 232:25]
  wire  _T_28 = _T_22 == 9'h1 | _T_21 == 9'h0; // @[Edges.scala 233:37]
  wire  d_done = _T_28 & auto_master_out_d_valid; // @[Edges.scala 234:22]
  wire [8:0] _T_29 = ~_T_24; // @[Edges.scala 235:27]
  wire [8:0] refill_cnt = _T_21 & _T_29; // @[Edges.scala 235:25]
  wire  refill_done = refill_one_beat & d_done; // @[icache.scala 173:37]
  wire [7:0] _T_37 = {MaxPeriodFibonacciLFSR_io_out_7,MaxPeriodFibonacciLFSR_io_out_6,MaxPeriodFibonacciLFSR_io_out_5,
    MaxPeriodFibonacciLFSR_io_out_4,MaxPeriodFibonacciLFSR_io_out_3,MaxPeriodFibonacciLFSR_io_out_2,
    MaxPeriodFibonacciLFSR_io_out_1,MaxPeriodFibonacciLFSR_io_out_0}; // @[PRNG.scala 86:17]
  wire [15:0] _T_45 = {MaxPeriodFibonacciLFSR_io_out_15,MaxPeriodFibonacciLFSR_io_out_14,
    MaxPeriodFibonacciLFSR_io_out_13,MaxPeriodFibonacciLFSR_io_out_12,MaxPeriodFibonacciLFSR_io_out_11,
    MaxPeriodFibonacciLFSR_io_out_10,MaxPeriodFibonacciLFSR_io_out_9,MaxPeriodFibonacciLFSR_io_out_8,_T_37}; // @[PRNG.scala 86:17]
  wire [7:0] _T_69 = {_T_45[8],_T_45[9],_T_45[10],_T_45[11],_T_45[12],_T_45[13],_T_45[14],_T_45[15]}; // @[LFSR.scala 43:8]
  wire [15:0] _T_77 = {_T_45[0],_T_45[1],_T_45[2],_T_45[3],_T_45[4],_T_45[5],_T_45[6],_T_45[7],_T_69}; // @[LFSR.scala 43:8]
  wire [1:0] repl_way = _T_77[1:0]; // @[icache.scala 177:56]
  wire  _T_79 = ~refill_done; // @[icache.scala 180:71]
  wire  _T_85 = repl_way == 2'h0; // @[icache.scala 182:100]
  wire  _T_86 = repl_way == 2'h1; // @[icache.scala 182:100]
  wire  _T_87 = repl_way == 2'h2; // @[icache.scala 182:100]
  wire  _T_88 = repl_way == 2'h3; // @[icache.scala 182:100]
  wire [7:0] _T_90 = {repl_way,refill_idx}; // @[Cat.scala 29:58]
  wire  _T_91 = ~invalidated; // @[icache.scala 187:75]
  wire [255:0] _T_93 = 256'h1 << _T_90; // @[icache.scala 187:32]
  wire [255:0] _T_94 = vb_array | _T_93; // @[icache.scala 187:32]
  wire [255:0] _T_95 = ~vb_array; // @[icache.scala 187:32]
  wire [255:0] _T_96 = _T_95 | _T_93; // @[icache.scala 187:32]
  wire [255:0] _T_97 = ~_T_96; // @[icache.scala 187:32]
  wire  _GEN_27 = io_invalidate | invalidated; // @[icache.scala 190:24 192:17 161:24]
  wire [1:0] _T_115 = s1_tag_hit_0 + s1_tag_hit_1; // @[Bitwise.scala 47:55]
  wire [1:0] _T_117 = s1_tag_hit_2 + s1_tag_hit_3; // @[Bitwise.scala 47:55]
  wire [2:0] _T_119 = _T_115 + _T_117; // @[Bitwise.scala 47:55]
  wire  _T_128 = refill_one_beat & _T_91; // @[icache.scala 247:34]
  wire  wen = refill_one_beat & _T_91 & _T_85; // @[icache.scala 247:51]
  wire [8:0] _T_130 = {refill_idx, 3'h0}; // @[icache.scala 249:54]
  wire [8:0] _T_131 = _T_130 | refill_cnt; // @[icache.scala 249:81]
  wire  _T_134 = ~wen; // @[icache.scala 257:55]
  reg [63:0] s2_dout_0; // @[icache.scala 257:30]
  wire  wen_1 = refill_one_beat & _T_91 & _T_86; // @[icache.scala 247:51]
  wire  _T_148 = ~wen_1; // @[icache.scala 257:55]
  reg [63:0] s2_dout_1; // @[icache.scala 257:30]
  wire  wen_2 = refill_one_beat & _T_91 & _T_87; // @[icache.scala 247:51]
  wire  _T_162 = ~wen_2; // @[icache.scala 257:55]
  reg [63:0] s2_dout_2; // @[icache.scala 257:30]
  wire  wen_3 = refill_one_beat & _T_91 & _T_88; // @[icache.scala 247:51]
  wire  _T_176 = ~wen_3; // @[icache.scala 257:55]
  reg [63:0] s2_dout_3; // @[icache.scala 257:30]
  reg  s2_tag_hit_0; // @[icache.scala 328:27]
  reg  s2_tag_hit_1; // @[icache.scala 328:27]
  reg  s2_tag_hit_2; // @[icache.scala 328:27]
  reg  s2_tag_hit_3; // @[icache.scala 328:27]
  wire [63:0] _T_191 = s2_tag_hit_0 ? s2_dout_0 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_192 = s2_tag_hit_1 ? s2_dout_1 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_193 = s2_tag_hit_2 ? s2_dout_2 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_194 = s2_tag_hit_3 ? s2_dout_3 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_195 = _T_191 | _T_192; // @[Mux.scala 27:72]
  wire [63:0] _T_196 = _T_195 | _T_193; // @[Mux.scala 27:72]
  wire  _GEN_65 = refill_fire | refill_valid; // @[icache.scala 362:22 162:29 362:37]
  MaxPeriodFibonacciLFSR_2 MaxPeriodFibonacciLFSR ( // @[PRNG.scala 82:22]
    .clock(MaxPeriodFibonacciLFSR_clock),
    .reset(MaxPeriodFibonacciLFSR_reset),
    .io_increment(MaxPeriodFibonacciLFSR_io_increment),
    .io_out_0(MaxPeriodFibonacciLFSR_io_out_0),
    .io_out_1(MaxPeriodFibonacciLFSR_io_out_1),
    .io_out_2(MaxPeriodFibonacciLFSR_io_out_2),
    .io_out_3(MaxPeriodFibonacciLFSR_io_out_3),
    .io_out_4(MaxPeriodFibonacciLFSR_io_out_4),
    .io_out_5(MaxPeriodFibonacciLFSR_io_out_5),
    .io_out_6(MaxPeriodFibonacciLFSR_io_out_6),
    .io_out_7(MaxPeriodFibonacciLFSR_io_out_7),
    .io_out_8(MaxPeriodFibonacciLFSR_io_out_8),
    .io_out_9(MaxPeriodFibonacciLFSR_io_out_9),
    .io_out_10(MaxPeriodFibonacciLFSR_io_out_10),
    .io_out_11(MaxPeriodFibonacciLFSR_io_out_11),
    .io_out_12(MaxPeriodFibonacciLFSR_io_out_12),
    .io_out_13(MaxPeriodFibonacciLFSR_io_out_13),
    .io_out_14(MaxPeriodFibonacciLFSR_io_out_14),
    .io_out_15(MaxPeriodFibonacciLFSR_io_out_15)
  );
  assign tag_array_0_tag_rdata_en = tag_array_0_tag_rdata_en_pipe_0;
  assign tag_array_0_tag_rdata_addr = tag_array_0_tag_rdata_addr_pipe_0;
  assign tag_array_0_tag_rdata_data = tag_array_0[tag_array_0_tag_rdata_addr]; // @[icache.scala 179:30]
  assign tag_array_0__T_89_data = refill_paddr[31:12];
  assign tag_array_0__T_89_addr = refill_paddr[11:6];
  assign tag_array_0__T_89_mask = repl_way == 2'h0;
  assign tag_array_0__T_89_en = refill_one_beat & d_done;
  assign tag_array_1_tag_rdata_en = tag_array_1_tag_rdata_en_pipe_0;
  assign tag_array_1_tag_rdata_addr = tag_array_1_tag_rdata_addr_pipe_0;
  assign tag_array_1_tag_rdata_data = tag_array_1[tag_array_1_tag_rdata_addr]; // @[icache.scala 179:30]
  assign tag_array_1__T_89_data = refill_paddr[31:12];
  assign tag_array_1__T_89_addr = refill_paddr[11:6];
  assign tag_array_1__T_89_mask = repl_way == 2'h1;
  assign tag_array_1__T_89_en = refill_one_beat & d_done;
  assign tag_array_2_tag_rdata_en = tag_array_2_tag_rdata_en_pipe_0;
  assign tag_array_2_tag_rdata_addr = tag_array_2_tag_rdata_addr_pipe_0;
  assign tag_array_2_tag_rdata_data = tag_array_2[tag_array_2_tag_rdata_addr]; // @[icache.scala 179:30]
  assign tag_array_2__T_89_data = refill_paddr[31:12];
  assign tag_array_2__T_89_addr = refill_paddr[11:6];
  assign tag_array_2__T_89_mask = repl_way == 2'h2;
  assign tag_array_2__T_89_en = refill_one_beat & d_done;
  assign tag_array_3_tag_rdata_en = tag_array_3_tag_rdata_en_pipe_0;
  assign tag_array_3_tag_rdata_addr = tag_array_3_tag_rdata_addr_pipe_0;
  assign tag_array_3_tag_rdata_data = tag_array_3[tag_array_3_tag_rdata_addr]; // @[icache.scala 179:30]
  assign tag_array_3__T_89_data = refill_paddr[31:12];
  assign tag_array_3__T_89_addr = refill_paddr[11:6];
  assign tag_array_3__T_89_mask = repl_way == 2'h3;
  assign tag_array_3__T_89_en = refill_one_beat & d_done;
  assign dataArrayWay_0__T_139_en = dataArrayWay_0__T_139_en_pipe_0;
  assign dataArrayWay_0__T_139_addr = dataArrayWay_0__T_139_addr_pipe_0;
  assign dataArrayWay_0__T_139_data = dataArrayWay_0[dataArrayWay_0__T_139_addr]; // @[DescribedSRAM.scala 23:26]
  assign dataArrayWay_0__T_133_data = auto_master_out_d_bits_data;
  assign dataArrayWay_0__T_133_addr = refill_one_beat ? _T_131 : io_req_bits_addr[11:3];
  assign dataArrayWay_0__T_133_mask = 1'h1;
  assign dataArrayWay_0__T_133_en = _T_128 & _T_85;
  assign dataArrayWay_1__T_153_en = dataArrayWay_1__T_153_en_pipe_0;
  assign dataArrayWay_1__T_153_addr = dataArrayWay_1__T_153_addr_pipe_0;
  assign dataArrayWay_1__T_153_data = dataArrayWay_1[dataArrayWay_1__T_153_addr]; // @[DescribedSRAM.scala 23:26]
  assign dataArrayWay_1__T_147_data = auto_master_out_d_bits_data;
  assign dataArrayWay_1__T_147_addr = refill_one_beat ? _T_131 : io_req_bits_addr[11:3];
  assign dataArrayWay_1__T_147_mask = 1'h1;
  assign dataArrayWay_1__T_147_en = _T_128 & _T_86;
  assign dataArrayWay_2__T_167_en = dataArrayWay_2__T_167_en_pipe_0;
  assign dataArrayWay_2__T_167_addr = dataArrayWay_2__T_167_addr_pipe_0;
  assign dataArrayWay_2__T_167_data = dataArrayWay_2[dataArrayWay_2__T_167_addr]; // @[DescribedSRAM.scala 23:26]
  assign dataArrayWay_2__T_161_data = auto_master_out_d_bits_data;
  assign dataArrayWay_2__T_161_addr = refill_one_beat ? _T_131 : io_req_bits_addr[11:3];
  assign dataArrayWay_2__T_161_mask = 1'h1;
  assign dataArrayWay_2__T_161_en = _T_128 & _T_87;
  assign dataArrayWay_3__T_181_en = dataArrayWay_3__T_181_en_pipe_0;
  assign dataArrayWay_3__T_181_addr = dataArrayWay_3__T_181_addr_pipe_0;
  assign dataArrayWay_3__T_181_data = dataArrayWay_3[dataArrayWay_3__T_181_addr]; // @[DescribedSRAM.scala 23:26]
  assign dataArrayWay_3__T_175_data = auto_master_out_d_bits_data;
  assign dataArrayWay_3__T_175_addr = refill_one_beat ? _T_131 : io_req_bits_addr[11:3];
  assign dataArrayWay_3__T_175_mask = 1'h1;
  assign dataArrayWay_3__T_175_en = _T_128 & _T_88;
  assign auto_master_out_a_valid = s2_miss & ~refill_valid & ~io_s2_kill; // @[icache.scala 350:46]
  assign auto_master_out_a_bits_opcode = 3'h4; // @[Edges.scala 430:17 431:15]
  assign auto_master_out_a_bits_param = 3'h0; // @[Edges.scala 430:17 432:15]
  assign auto_master_out_a_bits_size = 4'h6; // @[Edges.scala 430:17 433:15]
  assign auto_master_out_a_bits_source = 1'h0; // @[Edges.scala 430:17 434:15]
  assign auto_master_out_a_bits_address = {refill_paddr[31:6], 6'h0}; // @[icache.scala 353:48]
  assign auto_master_out_a_bits_mask = 8'hff; // @[Cat.scala 29:58]
  assign auto_master_out_a_bits_data = 64'h0; // @[Edges.scala 430:17 437:15]
  assign auto_master_out_a_bits_corrupt = 1'h0; // @[Edges.scala 430:17 438:15]
  assign auto_master_out_d_ready = 1'h1; // @[Nodes.scala 388:84 icache.scala 174:18]
  assign io_req_ready = ~refill_one_beat; // @[icache.scala 170:19]
  assign io_resp_valid = s2_valid & s2_hit; // @[icache.scala 348:29]
  assign io_resp_bits_data = _T_196 | _T_194; // @[Mux.scala 27:72]
  assign io_resp_bits_replay = 1'h0;
  assign io_resp_bits_ae = 1'h0;
  assign io_perf_acquire = auto_master_out_a_ready & tl_out_a_valid; // @[Decoupled.scala 40:37]
  assign MaxPeriodFibonacciLFSR_clock = clock;
  assign MaxPeriodFibonacciLFSR_reset = reset;
  assign MaxPeriodFibonacciLFSR_io_increment = auto_master_out_a_ready & tl_out_a_valid; // @[Decoupled.scala 40:37]
  always @(posedge clock) begin
    if (tag_array_0__T_89_en & tag_array_0__T_89_mask) begin
      tag_array_0[tag_array_0__T_89_addr] <= tag_array_0__T_89_data; // @[icache.scala 179:30]
    end
    tag_array_0_tag_rdata_en_pipe_0 <= _T_79 & s0_valid;
    if (_T_79 & s0_valid) begin
      tag_array_0_tag_rdata_addr_pipe_0 <= io_req_bits_addr[11:6];
    end
    if (tag_array_1__T_89_en & tag_array_1__T_89_mask) begin
      tag_array_1[tag_array_1__T_89_addr] <= tag_array_1__T_89_data; // @[icache.scala 179:30]
    end
    tag_array_1_tag_rdata_en_pipe_0 <= _T_79 & s0_valid;
    if (_T_79 & s0_valid) begin
      tag_array_1_tag_rdata_addr_pipe_0 <= io_req_bits_addr[11:6];
    end
    if (tag_array_2__T_89_en & tag_array_2__T_89_mask) begin
      tag_array_2[tag_array_2__T_89_addr] <= tag_array_2__T_89_data; // @[icache.scala 179:30]
    end
    tag_array_2_tag_rdata_en_pipe_0 <= _T_79 & s0_valid;
    if (_T_79 & s0_valid) begin
      tag_array_2_tag_rdata_addr_pipe_0 <= io_req_bits_addr[11:6];
    end
    if (tag_array_3__T_89_en & tag_array_3__T_89_mask) begin
      tag_array_3[tag_array_3__T_89_addr] <= tag_array_3__T_89_data; // @[icache.scala 179:30]
    end
    tag_array_3_tag_rdata_en_pipe_0 <= _T_79 & s0_valid;
    if (_T_79 & s0_valid) begin
      tag_array_3_tag_rdata_addr_pipe_0 <= io_req_bits_addr[11:6];
    end
    if (dataArrayWay_0__T_133_en & dataArrayWay_0__T_133_mask) begin
      dataArrayWay_0[dataArrayWay_0__T_133_addr] <= dataArrayWay_0__T_133_data; // @[DescribedSRAM.scala 23:26]
    end
    dataArrayWay_0__T_139_en_pipe_0 <= _T_134 & s0_valid;
    if (_T_134 & s0_valid) begin
      if (refill_one_beat) begin
        dataArrayWay_0__T_139_addr_pipe_0 <= _T_131;
      end else begin
        dataArrayWay_0__T_139_addr_pipe_0 <= io_req_bits_addr[11:3];
      end
    end
    if (dataArrayWay_1__T_147_en & dataArrayWay_1__T_147_mask) begin
      dataArrayWay_1[dataArrayWay_1__T_147_addr] <= dataArrayWay_1__T_147_data; // @[DescribedSRAM.scala 23:26]
    end
    dataArrayWay_1__T_153_en_pipe_0 <= _T_148 & s0_valid;
    if (_T_148 & s0_valid) begin
      if (refill_one_beat) begin
        dataArrayWay_1__T_153_addr_pipe_0 <= _T_131;
      end else begin
        dataArrayWay_1__T_153_addr_pipe_0 <= io_req_bits_addr[11:3];
      end
    end
    if (dataArrayWay_2__T_161_en & dataArrayWay_2__T_161_mask) begin
      dataArrayWay_2[dataArrayWay_2__T_161_addr] <= dataArrayWay_2__T_161_data; // @[DescribedSRAM.scala 23:26]
    end
    dataArrayWay_2__T_167_en_pipe_0 <= _T_162 & s0_valid;
    if (_T_162 & s0_valid) begin
      if (refill_one_beat) begin
        dataArrayWay_2__T_167_addr_pipe_0 <= _T_131;
      end else begin
        dataArrayWay_2__T_167_addr_pipe_0 <= io_req_bits_addr[11:3];
      end
    end
    if (dataArrayWay_3__T_175_en & dataArrayWay_3__T_175_mask) begin
      dataArrayWay_3[dataArrayWay_3__T_175_addr] <= dataArrayWay_3__T_175_data; // @[DescribedSRAM.scala 23:26]
    end
    dataArrayWay_3__T_181_en_pipe_0 <= _T_176 & s0_valid;
    if (_T_176 & s0_valid) begin
      if (refill_one_beat) begin
        dataArrayWay_3__T_181_addr_pipe_0 <= _T_131;
      end else begin
        dataArrayWay_3__T_181_addr_pipe_0 <= io_req_bits_addr[11:3];
      end
    end
    s1_valid <= io_req_ready & io_req_valid; // @[Decoupled.scala 40:37]
    if (reset) begin // @[icache.scala 185:25]
      vb_array <= 256'h0; // @[icache.scala 185:25]
    end else if (io_invalidate) begin // @[icache.scala 190:24]
      vb_array <= 256'h0; // @[icache.scala 191:14]
    end else if (refill_one_beat) begin // @[icache.scala 186:26]
      if (refill_done & ~invalidated) begin // @[icache.scala 187:32]
        vb_array <= _T_94;
      end else begin
        vb_array <= _T_97;
      end
    end
    s2_valid <= s1_valid & ~io_s1_kill; // @[icache.scala 157:35]
    s2_hit <= s1_tag_hit_0 | s1_tag_hit_1 | s1_tag_hit_2 | s1_tag_hit_3; // @[icache.scala 156:35]
    if (_T_199) begin // @[icache.scala 361:24]
      invalidated <= 1'h0; // @[icache.scala 361:38]
    end else begin
      invalidated <= _GEN_27;
    end
    if (reset) begin // @[icache.scala 162:29]
      refill_valid <= 1'h0; // @[icache.scala 162:29]
    end else if (refill_done) begin // @[icache.scala 363:22]
      refill_valid <= 1'h0; // @[icache.scala 363:37]
    end else begin
      refill_valid <= _GEN_65;
    end
    _T_6 <= refill_valid; // @[icache.scala 164:48]
    if (_T_10) begin // @[Reg.scala 16:19]
      refill_paddr <= io_s1_paddr; // @[Reg.scala 16:23]
    end
    if (reset) begin // @[Edges.scala 230:27]
      _T_22 <= 9'h0; // @[Edges.scala 230:27]
    end else if (auto_master_out_d_valid) begin // @[Edges.scala 236:17]
      if (_T_25) begin // @[Edges.scala 237:21]
        if (auto_master_out_d_bits_opcode[0]) begin // @[Edges.scala 222:14]
          _T_22 <= _T_18[11:3];
        end else begin
          _T_22 <= 9'h0;
        end
      end else begin
        _T_22 <= _T_24;
      end
    end
    s2_dout_0 <= dataArrayWay_0__T_139_data; // @[icache.scala 257:30]
    s2_dout_1 <= dataArrayWay_1__T_153_data; // @[icache.scala 257:30]
    s2_dout_2 <= dataArrayWay_2__T_167_data; // @[icache.scala 257:30]
    s2_dout_3 <= dataArrayWay_3__T_181_data; // @[icache.scala 257:30]
    s2_tag_hit_0 <= s1_vb & tag_array_0_tag_rdata_data == s1_tag; // @[icache.scala 203:28]
    s2_tag_hit_1 <= s1_vb_1 & tag_array_1_tag_rdata_data == s1_tag; // @[icache.scala 203:28]
    s2_tag_hit_2 <= s1_vb_2 & tag_array_2_tag_rdata_data == s1_tag; // @[icache.scala 203:28]
    s2_tag_hit_3 <= s1_vb_3 & tag_array_3_tag_rdata_data == s1_tag; // @[icache.scala 203:28]
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (~(_T_119 <= 3'h1 | ~s1_valid | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed\n    at icache.scala:205 assert(PopCount(s1_tag_hit) <= 1.U || !s1_valid)\n"); // @[icache.scala 205:9]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (~(_T_119 <= 3'h1 | ~s1_valid | reset)) begin
          $fatal; // @[icache.scala 205:9]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
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
  for (initvar = 0; initvar < 64; initvar = initvar+1)
    tag_array_0[initvar] = _RAND_0[19:0];
  _RAND_3 = {1{`RANDOM}};
  for (initvar = 0; initvar < 64; initvar = initvar+1)
    tag_array_1[initvar] = _RAND_3[19:0];
  _RAND_6 = {1{`RANDOM}};
  for (initvar = 0; initvar < 64; initvar = initvar+1)
    tag_array_2[initvar] = _RAND_6[19:0];
  _RAND_9 = {1{`RANDOM}};
  for (initvar = 0; initvar < 64; initvar = initvar+1)
    tag_array_3[initvar] = _RAND_9[19:0];
  _RAND_12 = {2{`RANDOM}};
  for (initvar = 0; initvar < 512; initvar = initvar+1)
    dataArrayWay_0[initvar] = _RAND_12[63:0];
  _RAND_15 = {2{`RANDOM}};
  for (initvar = 0; initvar < 512; initvar = initvar+1)
    dataArrayWay_1[initvar] = _RAND_15[63:0];
  _RAND_18 = {2{`RANDOM}};
  for (initvar = 0; initvar < 512; initvar = initvar+1)
    dataArrayWay_2[initvar] = _RAND_18[63:0];
  _RAND_21 = {2{`RANDOM}};
  for (initvar = 0; initvar < 512; initvar = initvar+1)
    dataArrayWay_3[initvar] = _RAND_21[63:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  tag_array_0_tag_rdata_en_pipe_0 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  tag_array_0_tag_rdata_addr_pipe_0 = _RAND_2[5:0];
  _RAND_4 = {1{`RANDOM}};
  tag_array_1_tag_rdata_en_pipe_0 = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  tag_array_1_tag_rdata_addr_pipe_0 = _RAND_5[5:0];
  _RAND_7 = {1{`RANDOM}};
  tag_array_2_tag_rdata_en_pipe_0 = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  tag_array_2_tag_rdata_addr_pipe_0 = _RAND_8[5:0];
  _RAND_10 = {1{`RANDOM}};
  tag_array_3_tag_rdata_en_pipe_0 = _RAND_10[0:0];
  _RAND_11 = {1{`RANDOM}};
  tag_array_3_tag_rdata_addr_pipe_0 = _RAND_11[5:0];
  _RAND_13 = {1{`RANDOM}};
  dataArrayWay_0__T_139_en_pipe_0 = _RAND_13[0:0];
  _RAND_14 = {1{`RANDOM}};
  dataArrayWay_0__T_139_addr_pipe_0 = _RAND_14[8:0];
  _RAND_16 = {1{`RANDOM}};
  dataArrayWay_1__T_153_en_pipe_0 = _RAND_16[0:0];
  _RAND_17 = {1{`RANDOM}};
  dataArrayWay_1__T_153_addr_pipe_0 = _RAND_17[8:0];
  _RAND_19 = {1{`RANDOM}};
  dataArrayWay_2__T_167_en_pipe_0 = _RAND_19[0:0];
  _RAND_20 = {1{`RANDOM}};
  dataArrayWay_2__T_167_addr_pipe_0 = _RAND_20[8:0];
  _RAND_22 = {1{`RANDOM}};
  dataArrayWay_3__T_181_en_pipe_0 = _RAND_22[0:0];
  _RAND_23 = {1{`RANDOM}};
  dataArrayWay_3__T_181_addr_pipe_0 = _RAND_23[8:0];
  _RAND_24 = {1{`RANDOM}};
  s1_valid = _RAND_24[0:0];
  _RAND_25 = {8{`RANDOM}};
  vb_array = _RAND_25[255:0];
  _RAND_26 = {1{`RANDOM}};
  s2_valid = _RAND_26[0:0];
  _RAND_27 = {1{`RANDOM}};
  s2_hit = _RAND_27[0:0];
  _RAND_28 = {1{`RANDOM}};
  invalidated = _RAND_28[0:0];
  _RAND_29 = {1{`RANDOM}};
  refill_valid = _RAND_29[0:0];
  _RAND_30 = {1{`RANDOM}};
  _T_6 = _RAND_30[0:0];
  _RAND_31 = {1{`RANDOM}};
  refill_paddr = _RAND_31[31:0];
  _RAND_32 = {1{`RANDOM}};
  _T_22 = _RAND_32[8:0];
  _RAND_33 = {2{`RANDOM}};
  s2_dout_0 = _RAND_33[63:0];
  _RAND_34 = {2{`RANDOM}};
  s2_dout_1 = _RAND_34[63:0];
  _RAND_35 = {2{`RANDOM}};
  s2_dout_2 = _RAND_35[63:0];
  _RAND_36 = {2{`RANDOM}};
  s2_dout_3 = _RAND_36[63:0];
  _RAND_37 = {1{`RANDOM}};
  s2_tag_hit_0 = _RAND_37[0:0];
  _RAND_38 = {1{`RANDOM}};
  s2_tag_hit_1 = _RAND_38[0:0];
  _RAND_39 = {1{`RANDOM}};
  s2_tag_hit_2 = _RAND_39[0:0];
  _RAND_40 = {1{`RANDOM}};
  s2_tag_hit_3 = _RAND_40[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

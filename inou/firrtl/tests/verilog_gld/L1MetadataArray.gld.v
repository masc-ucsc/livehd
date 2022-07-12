module L1MetadataArray(
  input         clock,
  input         reset,
  output        io_read_ready,
  input         io_read_valid,
  input  [5:0]  io_read_bits_idx,
  input  [3:0]  io_read_bits_way_en,
  input  [19:0] io_read_bits_tag,
  output        io_write_ready,
  input         io_write_valid,
  input  [5:0]  io_write_bits_idx,
  input  [3:0]  io_write_bits_way_en,
  input  [19:0] io_write_bits_tag,
  input  [1:0]  io_write_bits_data_coh_state,
  input  [19:0] io_write_bits_data_tag,
  output [1:0]  io_resp_0_coh_state,
  output [19:0] io_resp_0_tag,
  output [1:0]  io_resp_1_coh_state,
  output [19:0] io_resp_1_tag,
  output [1:0]  io_resp_2_coh_state,
  output [19:0] io_resp_2_tag,
  output [1:0]  io_resp_3_coh_state,
  output [19:0] io_resp_3_tag
);
`ifdef RANDOMIZE_MEM_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_9;
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
  reg [31:0] _RAND_12;
`endif // RANDOMIZE_REG_INIT
  reg [21:0] tag_array_0 [0:63]; // @[HellaCache.scala 307:25]
  wire  tag_array_0__T_17_en; // @[HellaCache.scala 307:25]
  wire [5:0] tag_array_0__T_17_addr; // @[HellaCache.scala 307:25]
  wire [21:0] tag_array_0__T_17_data; // @[HellaCache.scala 307:25]
  wire [21:0] tag_array_0__T_12_data; // @[HellaCache.scala 307:25]
  wire [5:0] tag_array_0__T_12_addr; // @[HellaCache.scala 307:25]
  wire  tag_array_0__T_12_mask; // @[HellaCache.scala 307:25]
  wire  tag_array_0__T_12_en; // @[HellaCache.scala 307:25]
  reg  tag_array_0__T_17_en_pipe_0;
  reg [5:0] tag_array_0__T_17_addr_pipe_0;
  reg [21:0] tag_array_1 [0:63]; // @[HellaCache.scala 307:25]
  wire  tag_array_1__T_17_en; // @[HellaCache.scala 307:25]
  wire [5:0] tag_array_1__T_17_addr; // @[HellaCache.scala 307:25]
  wire [21:0] tag_array_1__T_17_data; // @[HellaCache.scala 307:25]
  wire [21:0] tag_array_1__T_12_data; // @[HellaCache.scala 307:25]
  wire [5:0] tag_array_1__T_12_addr; // @[HellaCache.scala 307:25]
  wire  tag_array_1__T_12_mask; // @[HellaCache.scala 307:25]
  wire  tag_array_1__T_12_en; // @[HellaCache.scala 307:25]
  reg  tag_array_1__T_17_en_pipe_0;
  reg [5:0] tag_array_1__T_17_addr_pipe_0;
  reg [21:0] tag_array_2 [0:63]; // @[HellaCache.scala 307:25]
  wire  tag_array_2__T_17_en; // @[HellaCache.scala 307:25]
  wire [5:0] tag_array_2__T_17_addr; // @[HellaCache.scala 307:25]
  wire [21:0] tag_array_2__T_17_data; // @[HellaCache.scala 307:25]
  wire [21:0] tag_array_2__T_12_data; // @[HellaCache.scala 307:25]
  wire [5:0] tag_array_2__T_12_addr; // @[HellaCache.scala 307:25]
  wire  tag_array_2__T_12_mask; // @[HellaCache.scala 307:25]
  wire  tag_array_2__T_12_en; // @[HellaCache.scala 307:25]
  reg  tag_array_2__T_17_en_pipe_0;
  reg [5:0] tag_array_2__T_17_addr_pipe_0;
  reg [21:0] tag_array_3 [0:63]; // @[HellaCache.scala 307:25]
  wire  tag_array_3__T_17_en; // @[HellaCache.scala 307:25]
  wire [5:0] tag_array_3__T_17_addr; // @[HellaCache.scala 307:25]
  wire [21:0] tag_array_3__T_17_data; // @[HellaCache.scala 307:25]
  wire [21:0] tag_array_3__T_12_data; // @[HellaCache.scala 307:25]
  wire [5:0] tag_array_3__T_12_addr; // @[HellaCache.scala 307:25]
  wire  tag_array_3__T_12_mask; // @[HellaCache.scala 307:25]
  wire  tag_array_3__T_12_en; // @[HellaCache.scala 307:25]
  reg  tag_array_3__T_17_en_pipe_0;
  reg [5:0] tag_array_3__T_17_addr_pipe_0;
  reg [6:0] rst_cnt; // @[HellaCache.scala 298:20]
  wire  rst = rst_cnt < 7'h40; // @[HellaCache.scala 299:21]
  wire [6:0] waddr = rst ? rst_cnt : {{1'd0}, io_write_bits_idx}; // @[HellaCache.scala 300:18]
  wire [1:0] _T_1_coh_state = rst ? 2'h0 : io_write_bits_data_coh_state; // @[HellaCache.scala 301:18]
  wire [19:0] _T_1_tag = rst ? 20'h0 : io_write_bits_data_tag; // @[HellaCache.scala 301:18]
  wire [3:0] _T_4 = rst ? $signed(-4'sh1) : $signed(io_write_bits_way_en); // @[HellaCache.scala 302:18]
  wire [6:0] _T_9 = rst_cnt + 7'h1; // @[HellaCache.scala 304:34]
  wire  wen = rst | io_write_valid; // @[HellaCache.scala 308:17]
  wire [21:0] _T_19 = tag_array_0__T_17_data;
  wire [21:0] _T_23 = tag_array_1__T_17_data;
  wire [21:0] _T_27 = tag_array_2__T_17_data;
  wire [21:0] _T_31 = tag_array_3__T_17_data;
  assign tag_array_0__T_17_en = tag_array_0__T_17_en_pipe_0;
  assign tag_array_0__T_17_addr = tag_array_0__T_17_addr_pipe_0;
  assign tag_array_0__T_17_data = tag_array_0[tag_array_0__T_17_addr]; // @[HellaCache.scala 307:25]
  assign tag_array_0__T_12_data = {_T_1_coh_state,_T_1_tag};
  assign tag_array_0__T_12_addr = waddr[5:0];
  assign tag_array_0__T_12_mask = _T_4[0];
  assign tag_array_0__T_12_en = rst | io_write_valid;
  assign tag_array_1__T_17_en = tag_array_1__T_17_en_pipe_0;
  assign tag_array_1__T_17_addr = tag_array_1__T_17_addr_pipe_0;
  assign tag_array_1__T_17_data = tag_array_1[tag_array_1__T_17_addr]; // @[HellaCache.scala 307:25]
  assign tag_array_1__T_12_data = {_T_1_coh_state,_T_1_tag};
  assign tag_array_1__T_12_addr = waddr[5:0];
  assign tag_array_1__T_12_mask = _T_4[1];
  assign tag_array_1__T_12_en = rst | io_write_valid;
  assign tag_array_2__T_17_en = tag_array_2__T_17_en_pipe_0;
  assign tag_array_2__T_17_addr = tag_array_2__T_17_addr_pipe_0;
  assign tag_array_2__T_17_data = tag_array_2[tag_array_2__T_17_addr]; // @[HellaCache.scala 307:25]
  assign tag_array_2__T_12_data = {_T_1_coh_state,_T_1_tag};
  assign tag_array_2__T_12_addr = waddr[5:0];
  assign tag_array_2__T_12_mask = _T_4[2];
  assign tag_array_2__T_12_en = rst | io_write_valid;
  assign tag_array_3__T_17_en = tag_array_3__T_17_en_pipe_0;
  assign tag_array_3__T_17_addr = tag_array_3__T_17_addr_pipe_0;
  assign tag_array_3__T_17_data = tag_array_3[tag_array_3__T_17_addr]; // @[HellaCache.scala 307:25]
  assign tag_array_3__T_12_data = {_T_1_coh_state,_T_1_tag};
  assign tag_array_3__T_12_addr = waddr[5:0];
  assign tag_array_3__T_12_mask = _T_4[3];
  assign tag_array_3__T_12_en = rst | io_write_valid;
  assign io_read_ready = ~wen; // @[HellaCache.scala 314:20]
  assign io_write_ready = ~rst; // @[HellaCache.scala 315:21]
  assign io_resp_0_coh_state = _T_19[21:20]; // @[HellaCache.scala 312:82]
  assign io_resp_0_tag = _T_19[19:0]; // @[HellaCache.scala 312:82]
  assign io_resp_1_coh_state = _T_23[21:20]; // @[HellaCache.scala 312:82]
  assign io_resp_1_tag = _T_23[19:0]; // @[HellaCache.scala 312:82]
  assign io_resp_2_coh_state = _T_27[21:20]; // @[HellaCache.scala 312:82]
  assign io_resp_2_tag = _T_27[19:0]; // @[HellaCache.scala 312:82]
  assign io_resp_3_coh_state = _T_31[21:20]; // @[HellaCache.scala 312:82]
  assign io_resp_3_tag = _T_31[19:0]; // @[HellaCache.scala 312:82]
  always @(posedge clock) begin
    if (tag_array_0__T_12_en & tag_array_0__T_12_mask) begin
      tag_array_0[tag_array_0__T_12_addr] <= tag_array_0__T_12_data; // @[HellaCache.scala 307:25]
    end
    tag_array_0__T_17_en_pipe_0 <= io_read_ready & io_read_valid;
    if (io_read_ready & io_read_valid) begin
      tag_array_0__T_17_addr_pipe_0 <= io_read_bits_idx;
    end
    if (tag_array_1__T_12_en & tag_array_1__T_12_mask) begin
      tag_array_1[tag_array_1__T_12_addr] <= tag_array_1__T_12_data; // @[HellaCache.scala 307:25]
    end
    tag_array_1__T_17_en_pipe_0 <= io_read_ready & io_read_valid;
    if (io_read_ready & io_read_valid) begin
      tag_array_1__T_17_addr_pipe_0 <= io_read_bits_idx;
    end
    if (tag_array_2__T_12_en & tag_array_2__T_12_mask) begin
      tag_array_2[tag_array_2__T_12_addr] <= tag_array_2__T_12_data; // @[HellaCache.scala 307:25]
    end
    tag_array_2__T_17_en_pipe_0 <= io_read_ready & io_read_valid;
    if (io_read_ready & io_read_valid) begin
      tag_array_2__T_17_addr_pipe_0 <= io_read_bits_idx;
    end
    if (tag_array_3__T_12_en & tag_array_3__T_12_mask) begin
      tag_array_3[tag_array_3__T_12_addr] <= tag_array_3__T_12_data; // @[HellaCache.scala 307:25]
    end
    tag_array_3__T_17_en_pipe_0 <= io_read_ready & io_read_valid;
    if (io_read_ready & io_read_valid) begin
      tag_array_3__T_17_addr_pipe_0 <= io_read_bits_idx;
    end
    if (reset) begin // @[HellaCache.scala 298:20]
      rst_cnt <= 7'h0; // @[HellaCache.scala 298:20]
    end else if (rst) begin // @[HellaCache.scala 304:14]
      rst_cnt <= _T_9; // @[HellaCache.scala 304:24]
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
  for (initvar = 0; initvar < 64; initvar = initvar+1)
    tag_array_0[initvar] = _RAND_0[21:0];
  _RAND_3 = {1{`RANDOM}};
  for (initvar = 0; initvar < 64; initvar = initvar+1)
    tag_array_1[initvar] = _RAND_3[21:0];
  _RAND_6 = {1{`RANDOM}};
  for (initvar = 0; initvar < 64; initvar = initvar+1)
    tag_array_2[initvar] = _RAND_6[21:0];
  _RAND_9 = {1{`RANDOM}};
  for (initvar = 0; initvar < 64; initvar = initvar+1)
    tag_array_3[initvar] = _RAND_9[21:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  tag_array_0__T_17_en_pipe_0 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  tag_array_0__T_17_addr_pipe_0 = _RAND_2[5:0];
  _RAND_4 = {1{`RANDOM}};
  tag_array_1__T_17_en_pipe_0 = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  tag_array_1__T_17_addr_pipe_0 = _RAND_5[5:0];
  _RAND_7 = {1{`RANDOM}};
  tag_array_2__T_17_en_pipe_0 = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  tag_array_2__T_17_addr_pipe_0 = _RAND_8[5:0];
  _RAND_10 = {1{`RANDOM}};
  tag_array_3__T_17_en_pipe_0 = _RAND_10[0:0];
  _RAND_11 = {1{`RANDOM}};
  tag_array_3__T_17_addr_pipe_0 = _RAND_11[5:0];
  _RAND_12 = {1{`RANDOM}};
  rst_cnt = _RAND_12[6:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

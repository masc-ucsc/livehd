module ListBuffer(
  input         clock,
  input         reset,
  output        io_push_ready,
  input         io_push_valid,
  input  [5:0]  io_push_bits_index,
  input  [63:0] io_push_bits_data_data,
  input  [7:0]  io_push_bits_data_mask,
  input         io_push_bits_data_corrupt,
  output [39:0] io_valid,
  input         io_pop_valid,
  input  [5:0]  io_pop_bits,
  output [63:0] io_data_data,
  output [7:0]  io_data_mask,
  output        io_data_corrupt
);
`ifdef RANDOMIZE_GARBAGE_ASSIGN
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_6;
  reg [63:0] _RAND_8;
  reg [31:0] _RAND_10;
  reg [31:0] _RAND_12;
`endif // RANDOMIZE_GARBAGE_ASSIGN
`ifdef RANDOMIZE_MEM_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_5;
  reg [63:0] _RAND_7;
  reg [31:0] _RAND_9;
  reg [31:0] _RAND_11;
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  reg [63:0] _RAND_13;
  reg [63:0] _RAND_14;
`endif // RANDOMIZE_REG_INIT
  reg [5:0] head [0:39]; // @[ListBuffer.scala 46:18]
  wire [5:0] head_pop_head_data; // @[ListBuffer.scala 46:18]
  wire [5:0] head_pop_head_addr; // @[ListBuffer.scala 46:18]
  wire [5:0] head__T_57_data; // @[ListBuffer.scala 46:18]
  wire [5:0] head__T_57_addr; // @[ListBuffer.scala 46:18]
  wire  head__T_57_mask; // @[ListBuffer.scala 46:18]
  wire  head__T_57_en; // @[ListBuffer.scala 46:18]
  wire [5:0] head__T_82_data; // @[ListBuffer.scala 46:18]
  wire [5:0] head__T_82_addr; // @[ListBuffer.scala 46:18]
  wire  head__T_82_mask; // @[ListBuffer.scala 46:18]
  wire  head__T_82_en; // @[ListBuffer.scala 46:18]
  reg [5:0] tail [0:39]; // @[ListBuffer.scala 47:18]
  wire [5:0] tail_push_tail_data; // @[ListBuffer.scala 47:18]
  wire [5:0] tail_push_tail_addr; // @[ListBuffer.scala 47:18]
  wire [5:0] tail__T_71_data; // @[ListBuffer.scala 47:18]
  wire [5:0] tail__T_71_addr; // @[ListBuffer.scala 47:18]
  wire [5:0] tail__T_58_data; // @[ListBuffer.scala 47:18]
  wire [5:0] tail__T_58_addr; // @[ListBuffer.scala 47:18]
  wire  tail__T_58_mask; // @[ListBuffer.scala 47:18]
  wire  tail__T_58_en; // @[ListBuffer.scala 47:18]
  reg [5:0] next [0:39]; // @[ListBuffer.scala 49:18]
  wire [5:0] next__T_80_data; // @[ListBuffer.scala 49:18]
  wire [5:0] next__T_80_addr; // @[ListBuffer.scala 49:18]
  wire [5:0] next__T_56_data; // @[ListBuffer.scala 49:18]
  wire [5:0] next__T_56_addr; // @[ListBuffer.scala 49:18]
  wire  next__T_56_mask; // @[ListBuffer.scala 49:18]
  wire  next__T_56_en; // @[ListBuffer.scala 49:18]
  reg [63:0] data_data [0:39]; // @[ListBuffer.scala 50:18]
  wire [63:0] data_data__T_60_data; // @[ListBuffer.scala 50:18]
  wire [5:0] data_data__T_60_addr; // @[ListBuffer.scala 50:18]
  wire [63:0] data_data__T_55_data; // @[ListBuffer.scala 50:18]
  wire [5:0] data_data__T_55_addr; // @[ListBuffer.scala 50:18]
  wire  data_data__T_55_mask; // @[ListBuffer.scala 50:18]
  wire  data_data__T_55_en; // @[ListBuffer.scala 50:18]
  reg [7:0] data_mask [0:39]; // @[ListBuffer.scala 50:18]
  wire [7:0] data_mask__T_60_data; // @[ListBuffer.scala 50:18]
  wire [5:0] data_mask__T_60_addr; // @[ListBuffer.scala 50:18]
  wire [7:0] data_mask__T_55_data; // @[ListBuffer.scala 50:18]
  wire [5:0] data_mask__T_55_addr; // @[ListBuffer.scala 50:18]
  wire  data_mask__T_55_mask; // @[ListBuffer.scala 50:18]
  wire  data_mask__T_55_en; // @[ListBuffer.scala 50:18]
  reg  data_corrupt [0:39]; // @[ListBuffer.scala 50:18]
  wire  data_corrupt__T_60_data; // @[ListBuffer.scala 50:18]
  wire [5:0] data_corrupt__T_60_addr; // @[ListBuffer.scala 50:18]
  wire  data_corrupt__T_55_data; // @[ListBuffer.scala 50:18]
  wire [5:0] data_corrupt__T_55_addr; // @[ListBuffer.scala 50:18]
  wire  data_corrupt__T_55_mask; // @[ListBuffer.scala 50:18]
  wire  data_corrupt__T_55_en; // @[ListBuffer.scala 50:18]
  reg [39:0] valid; // @[ListBuffer.scala 45:22]
  reg [39:0] used; // @[ListBuffer.scala 48:22]
  wire [39:0] _T = ~used; // @[ListBuffer.scala 52:25]
  wire [40:0] _T_1 = {_T, 1'h0}; // @[package.scala 199:48]
  wire [39:0] _T_3 = _T | _T_1[39:0]; // @[package.scala 199:43]
  wire [41:0] _T_4 = {_T_3, 2'h0}; // @[package.scala 199:48]
  wire [39:0] _T_6 = _T_3 | _T_4[39:0]; // @[package.scala 199:43]
  wire [43:0] _T_7 = {_T_6, 4'h0}; // @[package.scala 199:48]
  wire [39:0] _T_9 = _T_6 | _T_7[39:0]; // @[package.scala 199:43]
  wire [47:0] _T_10 = {_T_9, 8'h0}; // @[package.scala 199:48]
  wire [39:0] _T_12 = _T_9 | _T_10[39:0]; // @[package.scala 199:43]
  wire [55:0] _T_13 = {_T_12, 16'h0}; // @[package.scala 199:48]
  wire [39:0] _T_15 = _T_12 | _T_13[39:0]; // @[package.scala 199:43]
  wire [71:0] _T_16 = {_T_15, 32'h0}; // @[package.scala 199:48]
  wire [39:0] _T_18 = _T_15 | _T_16[39:0]; // @[package.scala 199:43]
  wire [40:0] _T_20 = {_T_18, 1'h0}; // @[ListBuffer.scala 52:32]
  wire [40:0] _T_21 = ~_T_20; // @[ListBuffer.scala 52:16]
  wire [40:0] _GEN_41 = {{1'd0}, _T}; // @[ListBuffer.scala 52:38]
  wire [40:0] freeOH = _T_21 & _GEN_41; // @[ListBuffer.scala 52:38]
  wire  _T_25 = |freeOH[40:32]; // @[OneHot.scala 32:14]
  wire [31:0] _GEN_42 = {{23'd0}, freeOH[40:32]}; // @[OneHot.scala 32:28]
  wire [31:0] _T_26 = _GEN_42 | freeOH[31:0]; // @[OneHot.scala 32:28]
  wire  _T_29 = |_T_26[31:16]; // @[OneHot.scala 32:14]
  wire [15:0] _T_30 = _T_26[31:16] | _T_26[15:0]; // @[OneHot.scala 32:28]
  wire  _T_33 = |_T_30[15:8]; // @[OneHot.scala 32:14]
  wire [7:0] _T_34 = _T_30[15:8] | _T_30[7:0]; // @[OneHot.scala 32:28]
  wire  _T_37 = |_T_34[7:4]; // @[OneHot.scala 32:14]
  wire [3:0] _T_38 = _T_34[7:4] | _T_34[3:0]; // @[OneHot.scala 32:28]
  wire  _T_41 = |_T_38[3:2]; // @[OneHot.scala 32:14]
  wire [1:0] _T_42 = _T_38[3:2] | _T_38[1:0]; // @[OneHot.scala 32:28]
  wire [4:0] _T_47 = {_T_29,_T_33,_T_37,_T_41,_T_42[1]}; // @[Cat.scala 29:58]
  wire [5:0] freeIdx = {_T_25,_T_29,_T_33,_T_37,_T_41,_T_42[1]}; // @[Cat.scala 29:58]
  wire [39:0] _T_48 = valid >> io_push_bits_index; // @[ListBuffer.scala 61:25]
  wire  push_valid = _T_48[0]; // @[ListBuffer.scala 61:25]
  wire  _T_51 = io_push_ready & io_push_valid; // @[Decoupled.scala 40:37]
  wire [63:0] _T_53 = 64'h1 << io_push_bits_index; // @[OneHot.scala 65:12]
  wire  _GEN_7 = push_valid ? 1'h0 : 1'h1; // @[ListBuffer.scala 68:23 ListBuffer.scala 46:18]
  wire [39:0] valid_set = _T_51 ? _T_53[39:0] : 40'h0; // @[ListBuffer.scala 64:25 ListBuffer.scala 65:15]
  wire [40:0] _GEN_11 = _T_51 ? freeOH : 41'h0; // @[ListBuffer.scala 64:25 ListBuffer.scala 66:14]
  wire  _GEN_21 = _T_51 & push_valid; // @[ListBuffer.scala 64:25 ListBuffer.scala 49:18]
  wire [39:0] _T_62 = io_valid >> io_pop_bits; // @[ListBuffer.scala 84:39]
  wire [5:0] _T_68 = head_pop_head_data; // @[OneHot.scala 64:49]
  wire [63:0] _T_69 = 64'h1 << _T_68; // @[OneHot.scala 65:12]
  wire [63:0] _T_74 = 64'h1 << io_pop_bits; // @[OneHot.scala 65:12]
  wire [39:0] _GEN_30 = head_pop_head_data == tail__T_71_data ? _T_74[39:0] : 40'h0; // @[ListBuffer.scala 88:48 ListBuffer.scala 89:17]
  wire  _T_79 = _GEN_21 & tail_push_tail_data == head_pop_head_data; // @[ListBuffer.scala 91:62]
  wire [39:0] used_clr = io_pop_valid ? _T_69[39:0] : 40'h0; // @[ListBuffer.scala 86:24 ListBuffer.scala 87:14]
  wire [39:0] valid_clr = io_pop_valid ? _GEN_30 : 40'h0; // @[ListBuffer.scala 86:24]
  wire [39:0] _T_86 = ~used_clr; // @[ListBuffer.scala 96:23]
  wire [39:0] _T_87 = used & _T_86; // @[ListBuffer.scala 96:21]
  wire [39:0] used_set = _GEN_11[39:0];
  wire [39:0] _T_88 = _T_87 | used_set; // @[ListBuffer.scala 96:35]
  wire [39:0] _T_89 = ~valid_clr; // @[ListBuffer.scala 97:23]
  wire [39:0] _T_90 = valid & _T_89; // @[ListBuffer.scala 97:21]
  wire [39:0] _T_91 = _T_90 | valid_set; // @[ListBuffer.scala 97:35]
  assign head_pop_head_addr = io_pop_bits;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign head_pop_head_data = head[head_pop_head_addr]; // @[ListBuffer.scala 46:18]
  `else
  assign head_pop_head_data = head_pop_head_addr >= 6'h28 ? _RAND_1[5:0] : head[head_pop_head_addr]; // @[ListBuffer.scala 46:18]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign head__T_57_data = {_T_25,_T_47};
  assign head__T_57_addr = io_push_bits_index;
  assign head__T_57_mask = 1'h1;
  assign head__T_57_en = _T_51 & _GEN_7;
  assign head__T_82_data = _T_79 ? freeIdx : next__T_80_data;
  assign head__T_82_addr = io_pop_bits;
  assign head__T_82_mask = 1'h1;
  assign head__T_82_en = io_pop_valid;
  assign tail_push_tail_addr = io_push_bits_index;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign tail_push_tail_data = tail[tail_push_tail_addr]; // @[ListBuffer.scala 47:18]
  `else
  assign tail_push_tail_data = tail_push_tail_addr >= 6'h28 ? _RAND_3[5:0] : tail[tail_push_tail_addr]; // @[ListBuffer.scala 47:18]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign tail__T_71_addr = io_pop_bits;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign tail__T_71_data = tail[tail__T_71_addr]; // @[ListBuffer.scala 47:18]
  `else
  assign tail__T_71_data = tail__T_71_addr >= 6'h28 ? _RAND_4[5:0] : tail[tail__T_71_addr]; // @[ListBuffer.scala 47:18]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign tail__T_58_data = {_T_25,_T_47};
  assign tail__T_58_addr = io_push_bits_index;
  assign tail__T_58_mask = 1'h1;
  assign tail__T_58_en = io_push_ready & io_push_valid;
  assign next__T_80_addr = head_pop_head_data;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign next__T_80_data = next[next__T_80_addr]; // @[ListBuffer.scala 49:18]
  `else
  assign next__T_80_data = next__T_80_addr >= 6'h28 ? _RAND_6[5:0] : next[next__T_80_addr]; // @[ListBuffer.scala 49:18]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign next__T_56_data = {_T_25,_T_47};
  assign next__T_56_addr = tail_push_tail_data;
  assign next__T_56_mask = 1'h1;
  assign next__T_56_en = _T_51 & push_valid;
  assign data_data__T_60_addr = head_pop_head_data;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign data_data__T_60_data = data_data[data_data__T_60_addr]; // @[ListBuffer.scala 50:18]
  `else
  assign data_data__T_60_data = data_data__T_60_addr >= 6'h28 ? _RAND_8[63:0] : data_data[data_data__T_60_addr]; // @[ListBuffer.scala 50:18]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign data_data__T_55_data = io_push_bits_data_data;
  assign data_data__T_55_addr = {_T_25,_T_47};
  assign data_data__T_55_mask = 1'h1;
  assign data_data__T_55_en = io_push_ready & io_push_valid;
  assign data_mask__T_60_addr = head_pop_head_data;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign data_mask__T_60_data = data_mask[data_mask__T_60_addr]; // @[ListBuffer.scala 50:18]
  `else
  assign data_mask__T_60_data = data_mask__T_60_addr >= 6'h28 ? _RAND_10[7:0] : data_mask[data_mask__T_60_addr]; // @[ListBuffer.scala 50:18]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign data_mask__T_55_data = io_push_bits_data_mask;
  assign data_mask__T_55_addr = {_T_25,_T_47};
  assign data_mask__T_55_mask = 1'h1;
  assign data_mask__T_55_en = io_push_ready & io_push_valid;
  assign data_corrupt__T_60_addr = head_pop_head_data;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign data_corrupt__T_60_data = data_corrupt[data_corrupt__T_60_addr]; // @[ListBuffer.scala 50:18]
  `else
  assign data_corrupt__T_60_data = data_corrupt__T_60_addr >= 6'h28 ? _RAND_12[0:0] :
    data_corrupt[data_corrupt__T_60_addr]; // @[ListBuffer.scala 50:18]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign data_corrupt__T_55_data = io_push_bits_data_corrupt;
  assign data_corrupt__T_55_addr = {_T_25,_T_47};
  assign data_corrupt__T_55_mask = 1'h1;
  assign data_corrupt__T_55_en = io_push_ready & io_push_valid;
  assign io_push_ready = ~(&used); // @[ListBuffer.scala 63:20]
  assign io_valid = valid; // @[ListBuffer.scala 81:12]
  assign io_data_data = data_data__T_60_data; // @[ListBuffer.scala 80:11]
  assign io_data_mask = data_mask__T_60_data; // @[ListBuffer.scala 80:11]
  assign io_data_corrupt = data_corrupt__T_60_data; // @[ListBuffer.scala 80:11]
  always @(posedge clock) begin
    if(head__T_57_en & head__T_57_mask) begin
      head[head__T_57_addr] <= head__T_57_data; // @[ListBuffer.scala 46:18]
    end
    if(head__T_82_en & head__T_82_mask) begin
      head[head__T_82_addr] <= head__T_82_data; // @[ListBuffer.scala 46:18]
    end
    if(tail__T_58_en & tail__T_58_mask) begin
      tail[tail__T_58_addr] <= tail__T_58_data; // @[ListBuffer.scala 47:18]
    end
    if(next__T_56_en & next__T_56_mask) begin
      next[next__T_56_addr] <= next__T_56_data; // @[ListBuffer.scala 49:18]
    end
    if(data_data__T_55_en & data_data__T_55_mask) begin
      data_data[data_data__T_55_addr] <= data_data__T_55_data; // @[ListBuffer.scala 50:18]
    end
    if(data_mask__T_55_en & data_mask__T_55_mask) begin
      data_mask[data_mask__T_55_addr] <= data_mask__T_55_data; // @[ListBuffer.scala 50:18]
    end
    if(data_corrupt__T_55_en & data_corrupt__T_55_mask) begin
      data_corrupt[data_corrupt__T_55_addr] <= data_corrupt__T_55_data; // @[ListBuffer.scala 50:18]
    end
    if (reset) begin // @[ListBuffer.scala 45:22]
      valid <= 40'h0; // @[ListBuffer.scala 45:22]
    end else begin
      valid <= _T_91;
    end
    if (reset) begin // @[ListBuffer.scala 48:22]
      used <= 40'h0; // @[ListBuffer.scala 48:22]
    end else begin
      used <= _T_88;
    end
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (~(~io_pop_valid | _T_62[0] | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed\n    at ListBuffer.scala:84 assert (!io.pop.fire() || (io.valid)(io.pop.bits))\n"); // @[ListBuffer.scala 84:10]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (~(~io_pop_valid | _T_62[0] | reset)) begin
          $fatal; // @[ListBuffer.scala 84:10]
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
`ifdef RANDOMIZE_GARBAGE_ASSIGN
  _RAND_1 = {1{`RANDOM}};
  _RAND_3 = {1{`RANDOM}};
  _RAND_4 = {1{`RANDOM}};
  _RAND_6 = {1{`RANDOM}};
  _RAND_8 = {2{`RANDOM}};
  _RAND_10 = {1{`RANDOM}};
  _RAND_12 = {1{`RANDOM}};
`endif // RANDOMIZE_GARBAGE_ASSIGN
`ifdef RANDOMIZE_MEM_INIT
  _RAND_0 = {1{`RANDOM}};
  for (initvar = 0; initvar < 40; initvar = initvar+1)
    head[initvar] = _RAND_0[5:0];
  _RAND_2 = {1{`RANDOM}};
  for (initvar = 0; initvar < 40; initvar = initvar+1)
    tail[initvar] = _RAND_2[5:0];
  _RAND_5 = {1{`RANDOM}};
  for (initvar = 0; initvar < 40; initvar = initvar+1)
    next[initvar] = _RAND_5[5:0];
  _RAND_7 = {2{`RANDOM}};
  for (initvar = 0; initvar < 40; initvar = initvar+1)
    data_data[initvar] = _RAND_7[63:0];
  _RAND_9 = {1{`RANDOM}};
  for (initvar = 0; initvar < 40; initvar = initvar+1)
    data_mask[initvar] = _RAND_9[7:0];
  _RAND_11 = {1{`RANDOM}};
  for (initvar = 0; initvar < 40; initvar = initvar+1)
    data_corrupt[initvar] = _RAND_11[0:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_13 = {2{`RANDOM}};
  valid = _RAND_13[39:0];
  _RAND_14 = {2{`RANDOM}};
  used = _RAND_14[39:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

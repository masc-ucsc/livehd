module ICache(
  input         clock,
  input         reset,
  input         io_req_valid,
  input  [38:0] io_req_bits_addr,
  input  [31:0] io_s1_paddr,
  input         io_s1_kill,
  input         io_s2_kill,
  input         io_resp_ready,
  output        io_resp_valid,
  output [15:0] io_resp_bits_data,
  output [63:0] io_resp_bits_datablock,
  input         io_invalidate,
  input         io_mem_0_a_ready,
  output        io_mem_0_a_valid,
  output [2:0]  io_mem_0_a_bits_opcode,
  output [2:0]  io_mem_0_a_bits_param,
  output [3:0]  io_mem_0_a_bits_size,
  output        io_mem_0_a_bits_source,
  output [31:0] io_mem_0_a_bits_address,
  output [7:0]  io_mem_0_a_bits_mask,
  output [63:0] io_mem_0_a_bits_data,
  output        io_mem_0_b_ready,
  input         io_mem_0_b_valid,
  input  [2:0]  io_mem_0_b_bits_opcode,
  input  [1:0]  io_mem_0_b_bits_param,
  input  [3:0]  io_mem_0_b_bits_size,
  input         io_mem_0_b_bits_source,
  input  [31:0] io_mem_0_b_bits_address,
  input  [7:0]  io_mem_0_b_bits_mask,
  input  [63:0] io_mem_0_b_bits_data,
  input         io_mem_0_c_ready,
  output        io_mem_0_c_valid,
  output [2:0]  io_mem_0_c_bits_opcode,
  output [2:0]  io_mem_0_c_bits_param,
  output [3:0]  io_mem_0_c_bits_size,
  output        io_mem_0_c_bits_source,
  output [31:0] io_mem_0_c_bits_address,
  output [63:0] io_mem_0_c_bits_data,
  output        io_mem_0_c_bits_error,
  output        io_mem_0_d_ready,
  input         io_mem_0_d_valid,
  input  [2:0]  io_mem_0_d_bits_opcode,
  input  [1:0]  io_mem_0_d_bits_param,
  input  [3:0]  io_mem_0_d_bits_size,
  input         io_mem_0_d_bits_source,
  input  [3:0]  io_mem_0_d_bits_sink,
  input  [2:0]  io_mem_0_d_bits_addr_lo,
  input  [63:0] io_mem_0_d_bits_data,
  input         io_mem_0_d_bits_error,
  input         io_mem_0_e_ready,
  output        io_mem_0_e_valid,
  output [3:0]  io_mem_0_e_bits_sink
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
  reg [31:0] _RAND_25;
  reg [31:0] _RAND_26;
  reg [31:0] _RAND_27;
  reg [255:0] _RAND_28;
  reg [31:0] _RAND_29;
  reg [31:0] _RAND_30;
  reg [31:0] _RAND_31;
  reg [31:0] _RAND_32;
  reg [31:0] _RAND_33;
  reg [31:0] _RAND_34;
  reg [31:0] _RAND_35;
  reg [63:0] _RAND_36;
  reg [63:0] _RAND_37;
  reg [63:0] _RAND_38;
  reg [63:0] _RAND_39;
  reg [31:0] _RAND_40;
  reg [31:0] _RAND_41;
  reg [31:0] _RAND_42;
  reg [31:0] _RAND_43;
  reg [31:0] _RAND_44;
  reg [63:0] _RAND_45;
  reg [63:0] _RAND_46;
  reg [63:0] _RAND_47;
  reg [63:0] _RAND_48;
`endif // RANDOMIZE_REG_INIT
  reg [19:0] tag_array_0 [0:63]; // @[ICache.scala 97:25]
  wire [19:0] tag_array_0_tag_rdata_data; // @[ICache.scala 97:25]
  wire [5:0] tag_array_0_tag_rdata_addr; // @[ICache.scala 97:25]
  wire [19:0] tag_array_0__T_328_data; // @[ICache.scala 97:25]
  wire [5:0] tag_array_0__T_328_addr; // @[ICache.scala 97:25]
  wire  tag_array_0__T_328_mask; // @[ICache.scala 97:25]
  wire  tag_array_0__T_328_en; // @[ICache.scala 97:25]
  reg  tag_array_0_tag_rdata_en_pipe_0;
  reg [5:0] tag_array_0_tag_rdata_addr_pipe_0;
  reg [19:0] tag_array_1 [0:63]; // @[ICache.scala 97:25]
  wire [19:0] tag_array_1_tag_rdata_data; // @[ICache.scala 97:25]
  wire [5:0] tag_array_1_tag_rdata_addr; // @[ICache.scala 97:25]
  wire [19:0] tag_array_1__T_328_data; // @[ICache.scala 97:25]
  wire [5:0] tag_array_1__T_328_addr; // @[ICache.scala 97:25]
  wire  tag_array_1__T_328_mask; // @[ICache.scala 97:25]
  wire  tag_array_1__T_328_en; // @[ICache.scala 97:25]
  reg  tag_array_1_tag_rdata_en_pipe_0;
  reg [5:0] tag_array_1_tag_rdata_addr_pipe_0;
  reg [19:0] tag_array_2 [0:63]; // @[ICache.scala 97:25]
  wire [19:0] tag_array_2_tag_rdata_data; // @[ICache.scala 97:25]
  wire [5:0] tag_array_2_tag_rdata_addr; // @[ICache.scala 97:25]
  wire [19:0] tag_array_2__T_328_data; // @[ICache.scala 97:25]
  wire [5:0] tag_array_2__T_328_addr; // @[ICache.scala 97:25]
  wire  tag_array_2__T_328_mask; // @[ICache.scala 97:25]
  wire  tag_array_2__T_328_en; // @[ICache.scala 97:25]
  reg  tag_array_2_tag_rdata_en_pipe_0;
  reg [5:0] tag_array_2_tag_rdata_addr_pipe_0;
  reg [19:0] tag_array_3 [0:63]; // @[ICache.scala 97:25]
  wire [19:0] tag_array_3_tag_rdata_data; // @[ICache.scala 97:25]
  wire [5:0] tag_array_3_tag_rdata_addr; // @[ICache.scala 97:25]
  wire [19:0] tag_array_3__T_328_data; // @[ICache.scala 97:25]
  wire [5:0] tag_array_3__T_328_addr; // @[ICache.scala 97:25]
  wire  tag_array_3__T_328_mask; // @[ICache.scala 97:25]
  wire  tag_array_3__T_328_en; // @[ICache.scala 97:25]
  reg  tag_array_3_tag_rdata_en_pipe_0;
  reg [5:0] tag_array_3_tag_rdata_addr_pipe_0;
  reg [63:0] _T_550 [0:511]; // @[ICache.scala 132:28]
  wire [63:0] _T_550__T_566_data; // @[ICache.scala 132:28]
  wire [8:0] _T_550__T_566_addr; // @[ICache.scala 132:28]
  wire [63:0] _T_550__T_556_data; // @[ICache.scala 132:28]
  wire [8:0] _T_550__T_556_addr; // @[ICache.scala 132:28]
  wire  _T_550__T_556_mask; // @[ICache.scala 132:28]
  wire  _T_550__T_556_en; // @[ICache.scala 132:28]
  reg  _T_550__T_566_en_pipe_0;
  reg [8:0] _T_550__T_566_addr_pipe_0;
  reg [63:0] _T_572 [0:511]; // @[ICache.scala 132:28]
  wire [63:0] _T_572__T_588_data; // @[ICache.scala 132:28]
  wire [8:0] _T_572__T_588_addr; // @[ICache.scala 132:28]
  wire [63:0] _T_572__T_578_data; // @[ICache.scala 132:28]
  wire [8:0] _T_572__T_578_addr; // @[ICache.scala 132:28]
  wire  _T_572__T_578_mask; // @[ICache.scala 132:28]
  wire  _T_572__T_578_en; // @[ICache.scala 132:28]
  reg  _T_572__T_588_en_pipe_0;
  reg [8:0] _T_572__T_588_addr_pipe_0;
  reg [63:0] _T_594 [0:511]; // @[ICache.scala 132:28]
  wire [63:0] _T_594__T_610_data; // @[ICache.scala 132:28]
  wire [8:0] _T_594__T_610_addr; // @[ICache.scala 132:28]
  wire [63:0] _T_594__T_600_data; // @[ICache.scala 132:28]
  wire [8:0] _T_594__T_600_addr; // @[ICache.scala 132:28]
  wire  _T_594__T_600_mask; // @[ICache.scala 132:28]
  wire  _T_594__T_600_en; // @[ICache.scala 132:28]
  reg  _T_594__T_610_en_pipe_0;
  reg [8:0] _T_594__T_610_addr_pipe_0;
  reg [63:0] _T_616 [0:511]; // @[ICache.scala 132:28]
  wire [63:0] _T_616__T_632_data; // @[ICache.scala 132:28]
  wire [8:0] _T_616__T_632_addr; // @[ICache.scala 132:28]
  wire [63:0] _T_616__T_622_data; // @[ICache.scala 132:28]
  wire [8:0] _T_616__T_622_addr; // @[ICache.scala 132:28]
  wire  _T_616__T_622_mask; // @[ICache.scala 132:28]
  wire  _T_616__T_622_en; // @[ICache.scala 132:28]
  reg  _T_616__T_632_en_pipe_0;
  reg [8:0] _T_616__T_632_addr_pipe_0;
  reg [1:0] state;
  reg  invalidated;
  wire  stall = ~io_resp_ready; // @[ICache.scala 69:15]
  reg [31:0] refill_addr;
  reg  s1_valid;
  wire  _T_221 = ~io_s1_kill; // @[ICache.scala 75:31]
  wire  _T_222 = s1_valid & _T_221; // @[ICache.scala 75:28]
  wire  _T_223 = state == 2'h0; // @[ICache.scala 75:52]
  wire  out_valid = _T_222 & _T_223; // @[ICache.scala 75:43]
  wire [5:0] s1_idx = io_s1_paddr[11:6]; // @[ICache.scala 76:27]
  wire [19:0] s1_tag = io_s1_paddr[31:12]; // @[ICache.scala 77:27]
  wire  _T_436 = ~io_invalidate; // @[ICache.scala 122:17]
  reg [255:0] vb_array;
  wire [6:0] _T_439 = {1'h0,s1_idx}; // @[Cat.scala 30:58]
  wire [255:0] _T_440 = vb_array >> _T_439; // @[ICache.scala 122:43]
  wire  _T_443 = _T_436 & _T_440[0]; // @[ICache.scala 122:32]
  reg  s1_dout_valid;
  wire [19:0] _T_450 = tag_array_0_tag_rdata_data; // @[ICache.scala 125:32]
  wire  _T_451 = _T_450 == s1_tag; // @[ICache.scala 125:46]
  reg  _T_453;
  wire  s1_tag_match_0 = s1_dout_valid ? _T_451 : _T_453; // @[Package.scala 27:42]
  wire  s1_tag_hit_0 = _T_443 & s1_tag_match_0; // @[ICache.scala 126:28]
  wire [6:0] _T_465 = {1'h1,s1_idx}; // @[Cat.scala 30:58]
  wire [255:0] _T_466 = vb_array >> _T_465; // @[ICache.scala 122:43]
  wire  _T_469 = _T_436 & _T_466[0]; // @[ICache.scala 122:32]
  wire [19:0] _T_476 = tag_array_1_tag_rdata_data; // @[ICache.scala 125:32]
  wire  _T_477 = _T_476 == s1_tag; // @[ICache.scala 125:46]
  reg  _T_479;
  wire  s1_tag_match_1 = s1_dout_valid ? _T_477 : _T_479; // @[Package.scala 27:42]
  wire  s1_tag_hit_1 = _T_469 & s1_tag_match_1; // @[ICache.scala 126:28]
  wire  _T_539 = s1_tag_hit_0 | s1_tag_hit_1; // @[ICache.scala 129:44]
  wire [7:0] _T_491 = {2'h2,s1_idx}; // @[Cat.scala 30:58]
  wire [255:0] _T_492 = vb_array >> _T_491; // @[ICache.scala 122:43]
  wire  _T_495 = _T_436 & _T_492[0]; // @[ICache.scala 122:32]
  wire [19:0] _T_502 = tag_array_2_tag_rdata_data; // @[ICache.scala 125:32]
  wire  _T_503 = _T_502 == s1_tag; // @[ICache.scala 125:46]
  reg  _T_505;
  wire  s1_tag_match_2 = s1_dout_valid ? _T_503 : _T_505; // @[Package.scala 27:42]
  wire  s1_tag_hit_2 = _T_495 & s1_tag_match_2; // @[ICache.scala 126:28]
  wire  _T_540 = _T_539 | s1_tag_hit_2; // @[ICache.scala 129:44]
  wire [7:0] _T_517 = {2'h3,s1_idx}; // @[Cat.scala 30:58]
  wire [255:0] _T_518 = vb_array >> _T_517; // @[ICache.scala 122:43]
  wire  _T_521 = _T_436 & _T_518[0]; // @[ICache.scala 122:32]
  wire [19:0] _T_528 = tag_array_3_tag_rdata_data; // @[ICache.scala 125:32]
  wire  _T_529 = _T_528 == s1_tag; // @[ICache.scala 125:46]
  reg  _T_531;
  wire  s1_tag_match_3 = s1_dout_valid ? _T_529 : _T_531; // @[Package.scala 27:42]
  wire  s1_tag_hit_3 = _T_521 & s1_tag_match_3; // @[ICache.scala 126:28]
  wire  s1_any_tag_hit = _T_540 | s1_tag_hit_3; // @[ICache.scala 129:44]
  wire  s1_hit = out_valid & s1_any_tag_hit; // @[ICache.scala 78:26]
  wire  _T_225 = ~s1_any_tag_hit; // @[ICache.scala 79:30]
  wire  s1_miss = out_valid & _T_225; // @[ICache.scala 79:27]
  wire  _T_227 = io_req_valid & _T_223; // @[ICache.scala 81:31]
  wire  _T_228 = out_valid & stall; // @[ICache.scala 81:67]
  wire  _T_230 = ~_T_228; // @[ICache.scala 81:55]
  wire  s0_valid = _T_227 & _T_230; // @[ICache.scala 81:52]
  wire  _T_232 = s0_valid | _T_228; // @[ICache.scala 84:24]
  wire  _T_234 = s1_miss & _T_223; // @[ICache.scala 86:17]
  wire [5:0] refill_idx = refill_addr[11:6]; // @[ICache.scala 90:31]
  wire  _T_235 = io_mem_0_d_ready & io_mem_0_d_valid; // @[Decoupled.scala 30:37]
  wire [22:0] _T_238 = 23'hff << io_mem_0_d_bits_size; // @[package.scala 19:71]
  wire [7:0] _T_240 = ~_T_238[7:0]; // @[package.scala 19:40]
  wire [4:0] _T_244 = io_mem_0_d_bits_opcode[0] ? _T_240[7:3] : 5'h0; // @[Edges.scala 199:14]
  reg [4:0] _T_246;
  wire [4:0] _T_250 = _T_246 - 5'h1; // @[Edges.scala 208:28]
  wire  _T_252 = _T_246 == 5'h0; // @[Edges.scala 209:25]
  wire  _T_254 = _T_246 == 5'h1; // @[Edges.scala 210:25]
  wire  _T_256 = _T_244 == 5'h0; // @[Edges.scala 210:47]
  wire  _T_257 = _T_254 | _T_256; // @[Edges.scala 210:37]
  wire  refill_done = _T_257 & _T_235; // @[Edges.scala 211:22]
  wire [4:0] _T_258 = ~_T_250; // @[Edges.scala 212:27]
  wire [4:0] refill_cnt = _T_244 & _T_258; // @[Edges.scala 212:25]
  reg [15:0] _T_262;
  wire  _T_265 = _T_262[0] ^ _T_262[2]; // @[LFSR.scala 23:43]
  wire  _T_267 = _T_265 ^ _T_262[3]; // @[LFSR.scala 23:51]
  wire  _T_269 = _T_267 ^ _T_262[5]; // @[LFSR.scala 23:59]
  wire [15:0] _T_271 = {_T_269,_T_262[15:1]}; // @[Cat.scala 30:58]
  wire [1:0] repl_way = _T_262[1:0]; // @[ICache.scala 95:56]
  wire  _T_284 = ~refill_done; // @[ICache.scala 98:70]
  wire  _T_312 = repl_way == 2'h0; // @[ICache.scala 101:84]
  wire  _T_314 = repl_way == 2'h1; // @[ICache.scala 101:84]
  wire  _T_316 = repl_way == 2'h2; // @[ICache.scala 101:84]
  wire  _T_318 = repl_way == 2'h3; // @[ICache.scala 101:84]
  wire  _T_342 = ~invalidated; // @[ICache.scala 105:24]
  wire  _T_343 = refill_done & _T_342; // @[ICache.scala 105:21]
  wire [7:0] _T_344 = {repl_way,refill_idx}; // @[Cat.scala 30:58]
  wire [255:0] _T_347 = 256'h1 << _T_344; // @[ICache.scala 106:32]
  wire [255:0] _T_348 = vb_array | _T_347; // @[ICache.scala 106:32]
  wire  _GEN_28 = io_invalidate | invalidated; // @[ICache.scala 108:24]
  wire  _T_553 = io_mem_0_d_valid & _T_312; // @[ICache.scala 133:30]
  wire [8:0] _T_554 = {refill_idx, 3'h0}; // @[ICache.scala 136:36]
  wire [8:0] _GEN_104 = {{4'd0}, refill_cnt}; // @[ICache.scala 136:63]
  wire  _T_559 = ~_T_553; // @[ICache.scala 139:45]
  reg [63:0] _T_568;
  wire  _T_575 = io_mem_0_d_valid & _T_314; // @[ICache.scala 133:30]
  wire  _T_581 = ~_T_575; // @[ICache.scala 139:45]
  reg [63:0] _T_590;
  wire  _T_597 = io_mem_0_d_valid & _T_316; // @[ICache.scala 133:30]
  wire  _T_603 = ~_T_597; // @[ICache.scala 139:45]
  reg [63:0] _T_612;
  wire  _T_619 = io_mem_0_d_valid & _T_318; // @[ICache.scala 133:30]
  wire  _T_625 = ~_T_619; // @[ICache.scala 139:45]
  reg [63:0] _T_634;
  wire  _T_638 = ~stall; // @[ICache.scala 148:51]
  reg  _T_639;
  reg  _T_654_0;
  reg  _T_654_1;
  reg  _T_654_2;
  reg  _T_654_3;
  reg [63:0] _T_685_0;
  reg [63:0] _T_685_1;
  reg [63:0] _T_685_2;
  reg [63:0] _T_685_3;
  wire [63:0] _T_703 = _T_654_0 ? _T_685_0 : 64'h0; // @[Mux.scala 19:72]
  wire [63:0] _T_705 = _T_654_1 ? _T_685_1 : 64'h0; // @[Mux.scala 19:72]
  wire [63:0] _T_707 = _T_654_2 ? _T_685_2 : 64'h0; // @[Mux.scala 19:72]
  wire [63:0] _T_709 = _T_654_3 ? _T_685_3 : 64'h0; // @[Mux.scala 19:72]
  wire [63:0] _T_711 = _T_703 | _T_705; // @[Mux.scala 19:72]
  wire [63:0] _T_712 = _T_711 | _T_707; // @[Mux.scala 19:72]
  wire  _T_716 = state == 2'h1; // @[ICache.scala 154:27]
  wire  _T_718 = ~io_s2_kill; // @[ICache.scala 154:44]
  wire  _T_848 = 2'h0 == state; // @[Conditional.scala 29:28]
  wire [1:0] _GEN_90 = s1_miss ? 2'h1 : state; // @[ICache.scala 165:22]
  wire [1:0] _GEN_91 = _T_848 ? _GEN_90 : state; // @[Conditional.scala 29:59]
  wire  _T_850 = 2'h1 == state; // @[Conditional.scala 29:28]
  wire  _T_851 = 2'h2 == state; // @[Conditional.scala 29:28]
  wire  _T_852 = 2'h3 == state; // @[Conditional.scala 29:28]
  assign tag_array_0_tag_rdata_addr = tag_array_0_tag_rdata_addr_pipe_0;
  assign tag_array_0_tag_rdata_data = tag_array_0[tag_array_0_tag_rdata_addr]; // @[ICache.scala 97:25]
  assign tag_array_0__T_328_data = refill_addr[31:12];
  assign tag_array_0__T_328_addr = refill_addr[11:6];
  assign tag_array_0__T_328_mask = repl_way == 2'h0;
  assign tag_array_0__T_328_en = _T_257 & _T_235;
  assign tag_array_1_tag_rdata_addr = tag_array_1_tag_rdata_addr_pipe_0;
  assign tag_array_1_tag_rdata_data = tag_array_1[tag_array_1_tag_rdata_addr]; // @[ICache.scala 97:25]
  assign tag_array_1__T_328_data = refill_addr[31:12];
  assign tag_array_1__T_328_addr = refill_addr[11:6];
  assign tag_array_1__T_328_mask = repl_way == 2'h1;
  assign tag_array_1__T_328_en = _T_257 & _T_235;
  assign tag_array_2_tag_rdata_addr = tag_array_2_tag_rdata_addr_pipe_0;
  assign tag_array_2_tag_rdata_data = tag_array_2[tag_array_2_tag_rdata_addr]; // @[ICache.scala 97:25]
  assign tag_array_2__T_328_data = refill_addr[31:12];
  assign tag_array_2__T_328_addr = refill_addr[11:6];
  assign tag_array_2__T_328_mask = repl_way == 2'h2;
  assign tag_array_2__T_328_en = _T_257 & _T_235;
  assign tag_array_3_tag_rdata_addr = tag_array_3_tag_rdata_addr_pipe_0;
  assign tag_array_3_tag_rdata_data = tag_array_3[tag_array_3_tag_rdata_addr]; // @[ICache.scala 97:25]
  assign tag_array_3__T_328_data = refill_addr[31:12];
  assign tag_array_3__T_328_addr = refill_addr[11:6];
  assign tag_array_3__T_328_mask = repl_way == 2'h3;
  assign tag_array_3__T_328_en = _T_257 & _T_235;
  assign _T_550__T_566_addr = _T_550__T_566_addr_pipe_0;
  assign _T_550__T_566_data = _T_550[_T_550__T_566_addr]; // @[ICache.scala 132:28]
  assign _T_550__T_556_data = io_mem_0_d_bits_data;
  assign _T_550__T_556_addr = _T_554 | _GEN_104;
  assign _T_550__T_556_mask = 1'h1;
  assign _T_550__T_556_en = io_mem_0_d_valid & _T_312;
  assign _T_572__T_588_addr = _T_572__T_588_addr_pipe_0;
  assign _T_572__T_588_data = _T_572[_T_572__T_588_addr]; // @[ICache.scala 132:28]
  assign _T_572__T_578_data = io_mem_0_d_bits_data;
  assign _T_572__T_578_addr = _T_554 | _GEN_104;
  assign _T_572__T_578_mask = 1'h1;
  assign _T_572__T_578_en = io_mem_0_d_valid & _T_314;
  assign _T_594__T_610_addr = _T_594__T_610_addr_pipe_0;
  assign _T_594__T_610_data = _T_594[_T_594__T_610_addr]; // @[ICache.scala 132:28]
  assign _T_594__T_600_data = io_mem_0_d_bits_data;
  assign _T_594__T_600_addr = _T_554 | _GEN_104;
  assign _T_594__T_600_mask = 1'h1;
  assign _T_594__T_600_en = io_mem_0_d_valid & _T_316;
  assign _T_616__T_632_addr = _T_616__T_632_addr_pipe_0;
  assign _T_616__T_632_data = _T_616[_T_616__T_632_addr]; // @[ICache.scala 132:28]
  assign _T_616__T_622_data = io_mem_0_d_bits_data;
  assign _T_616__T_622_addr = _T_554 | _GEN_104;
  assign _T_616__T_622_mask = 1'h1;
  assign _T_616__T_622_en = io_mem_0_d_valid & _T_318;
  assign io_resp_valid = _T_639; // @[ICache.scala 152:21]
  assign io_resp_bits_data = 16'h0;
  assign io_resp_bits_datablock = _T_712 | _T_709; // @[ICache.scala 151:30]
  assign io_mem_0_a_valid = _T_716 & _T_718; // @[ICache.scala 154:18]
  assign io_mem_0_a_bits_opcode = 3'h4; // @[ICache.scala 155:17]
  assign io_mem_0_a_bits_param = 3'h0; // @[ICache.scala 155:17]
  assign io_mem_0_a_bits_size = 4'h6; // @[ICache.scala 155:17]
  assign io_mem_0_a_bits_source = 1'h0; // @[ICache.scala 155:17]
  assign io_mem_0_a_bits_address = {refill_addr[31:6], 6'h0}; // @[ICache.scala 155:17]
  assign io_mem_0_a_bits_mask = 8'hff; // @[ICache.scala 155:17]
  assign io_mem_0_a_bits_data = 64'h0; // @[ICache.scala 155:17]
  assign io_mem_0_b_ready = 1'h0;
  assign io_mem_0_c_valid = 1'h0; // @[ICache.scala 159:18]
  assign io_mem_0_c_bits_opcode = 3'h0;
  assign io_mem_0_c_bits_param = 3'h0;
  assign io_mem_0_c_bits_size = 4'h0;
  assign io_mem_0_c_bits_source = 1'h0;
  assign io_mem_0_c_bits_address = 32'h0;
  assign io_mem_0_c_bits_data = 64'h0;
  assign io_mem_0_c_bits_error = 1'h0;
  assign io_mem_0_d_ready = 1'h1; // @[ICache.scala 92:18]
  assign io_mem_0_e_valid = 1'h0; // @[ICache.scala 160:18]
  assign io_mem_0_e_bits_sink = 4'h0;
  always @(posedge clock) begin
    if(tag_array_0__T_328_en & tag_array_0__T_328_mask) begin
      tag_array_0[tag_array_0__T_328_addr] <= tag_array_0__T_328_data; // @[ICache.scala 97:25]
    end
    tag_array_0_tag_rdata_en_pipe_0 <= _T_284 & s0_valid;
    if (_T_284 & s0_valid) begin
      tag_array_0_tag_rdata_addr_pipe_0 <= io_req_bits_addr[11:6];
    end
    if(tag_array_1__T_328_en & tag_array_1__T_328_mask) begin
      tag_array_1[tag_array_1__T_328_addr] <= tag_array_1__T_328_data; // @[ICache.scala 97:25]
    end
    tag_array_1_tag_rdata_en_pipe_0 <= _T_284 & s0_valid;
    if (_T_284 & s0_valid) begin
      tag_array_1_tag_rdata_addr_pipe_0 <= io_req_bits_addr[11:6];
    end
    if(tag_array_2__T_328_en & tag_array_2__T_328_mask) begin
      tag_array_2[tag_array_2__T_328_addr] <= tag_array_2__T_328_data; // @[ICache.scala 97:25]
    end
    tag_array_2_tag_rdata_en_pipe_0 <= _T_284 & s0_valid;
    if (_T_284 & s0_valid) begin
      tag_array_2_tag_rdata_addr_pipe_0 <= io_req_bits_addr[11:6];
    end
    if(tag_array_3__T_328_en & tag_array_3__T_328_mask) begin
      tag_array_3[tag_array_3__T_328_addr] <= tag_array_3__T_328_data; // @[ICache.scala 97:25]
    end
    tag_array_3_tag_rdata_en_pipe_0 <= _T_284 & s0_valid;
    if (_T_284 & s0_valid) begin
      tag_array_3_tag_rdata_addr_pipe_0 <= io_req_bits_addr[11:6];
    end
    if(_T_550__T_556_en & _T_550__T_556_mask) begin
      _T_550[_T_550__T_556_addr] <= _T_550__T_556_data; // @[ICache.scala 132:28]
    end
    _T_550__T_566_en_pipe_0 <= _T_559 & s0_valid;
    if (_T_559 & s0_valid) begin
      _T_550__T_566_addr_pipe_0 <= io_req_bits_addr[11:3];
    end
    if(_T_572__T_578_en & _T_572__T_578_mask) begin
      _T_572[_T_572__T_578_addr] <= _T_572__T_578_data; // @[ICache.scala 132:28]
    end
    _T_572__T_588_en_pipe_0 <= _T_581 & s0_valid;
    if (_T_581 & s0_valid) begin
      _T_572__T_588_addr_pipe_0 <= io_req_bits_addr[11:3];
    end
    if(_T_594__T_600_en & _T_594__T_600_mask) begin
      _T_594[_T_594__T_600_addr] <= _T_594__T_600_data; // @[ICache.scala 132:28]
    end
    _T_594__T_610_en_pipe_0 <= _T_603 & s0_valid;
    if (_T_603 & s0_valid) begin
      _T_594__T_610_addr_pipe_0 <= io_req_bits_addr[11:3];
    end
    if(_T_616__T_622_en & _T_616__T_622_mask) begin
      _T_616[_T_616__T_622_addr] <= _T_616__T_622_data; // @[ICache.scala 132:28]
    end
    _T_616__T_632_en_pipe_0 <= _T_625 & s0_valid;
    if (_T_625 & s0_valid) begin
      _T_616__T_632_addr_pipe_0 <= io_req_bits_addr[11:3];
    end
    if (reset) begin
      state <= 2'h0;
    end else if (_T_852) begin
      if (refill_done) begin
        state <= 2'h0;
      end else if (_T_851) begin
        if (io_mem_0_d_valid) begin
          state <= 2'h3;
        end else if (_T_850) begin
          if (io_s2_kill) begin
            state <= 2'h0;
          end else if (io_mem_0_a_ready) begin
            state <= 2'h2;
          end else if (_T_848) begin
            if (s1_miss) begin
              state <= 2'h1;
            end
          end
        end else if (_T_848) begin
          if (s1_miss) begin
            state <= 2'h1;
          end
        end
      end else if (_T_850) begin
        if (io_s2_kill) begin
          state <= 2'h0;
        end else if (io_mem_0_a_ready) begin
          state <= 2'h2;
        end else if (_T_848) begin
          if (s1_miss) begin
            state <= 2'h1;
          end
        end
      end else if (_T_848) begin
        if (s1_miss) begin
          state <= 2'h1;
        end
      end
    end else if (_T_851) begin
      if (io_mem_0_d_valid) begin
        state <= 2'h3;
      end else if (_T_850) begin
        if (io_s2_kill) begin
          state <= 2'h0;
        end else if (io_mem_0_a_ready) begin
          state <= 2'h2;
        end else begin
          state <= _GEN_91;
        end
      end else begin
        state <= _GEN_91;
      end
    end else if (_T_850) begin
      if (io_s2_kill) begin
        state <= 2'h0;
      end else if (io_mem_0_a_ready) begin
        state <= 2'h2;
      end else begin
        state <= _GEN_91;
      end
    end else begin
      state <= _GEN_91;
    end
    if (_T_848) begin
      invalidated <= 1'h0;
    end else begin
      invalidated <= _GEN_28;
    end
    if (_T_234) begin
      refill_addr <= io_s1_paddr;
    end
    if (reset) begin
      s1_valid <= 1'h0;
    end else begin
      s1_valid <= _T_232;
    end
    if (reset) begin
      vb_array <= 256'h0;
    end else if (io_invalidate) begin
      vb_array <= 256'h0;
    end else if (_T_343) begin
      vb_array <= _T_348;
    end
    s1_dout_valid <= _T_227 & _T_230;
    if (s1_dout_valid) begin
      _T_453 <= _T_451;
    end
    if (s1_dout_valid) begin
      _T_479 <= _T_477;
    end
    if (s1_dout_valid) begin
      _T_505 <= _T_503;
    end
    if (s1_dout_valid) begin
      _T_531 <= _T_529;
    end
    if (reset) begin
      _T_246 <= 5'h0;
    end else if (_T_235) begin
      if (_T_252) begin
        if (io_mem_0_d_bits_opcode[0]) begin
          _T_246 <= _T_240[7:3];
        end else begin
          _T_246 <= 5'h0;
        end
      end else begin
        _T_246 <= _T_250;
      end
    end
    if (reset) begin
      _T_262 <= 16'h1;
    end else if (s1_miss) begin
      _T_262 <= _T_271;
    end
    if (s1_dout_valid) begin
      _T_568 <= _T_550__T_566_data;
    end
    if (s1_dout_valid) begin
      _T_590 <= _T_572__T_588_data;
    end
    if (s1_dout_valid) begin
      _T_612 <= _T_594__T_610_data;
    end
    if (s1_dout_valid) begin
      _T_634 <= _T_616__T_632_data;
    end
    if (reset) begin
      _T_639 <= 1'h0;
    end else if (_T_638) begin
      _T_639 <= s1_hit;
    end
    if (_T_638) begin
      _T_654_0 <= s1_tag_hit_0;
    end
    if (_T_638) begin
      _T_654_1 <= s1_tag_hit_1;
    end
    if (_T_638) begin
      _T_654_2 <= s1_tag_hit_2;
    end
    if (_T_638) begin
      _T_654_3 <= s1_tag_hit_3;
    end
    if (_T_638) begin
      if (s1_dout_valid) begin
        _T_685_0 <= _T_550__T_566_data;
      end else begin
        _T_685_0 <= _T_568;
      end
    end
    if (_T_638) begin
      if (s1_dout_valid) begin
        _T_685_1 <= _T_572__T_588_data;
      end else begin
        _T_685_1 <= _T_590;
      end
    end
    if (_T_638) begin
      if (s1_dout_valid) begin
        _T_685_2 <= _T_594__T_610_data;
      end else begin
        _T_685_2 <= _T_612;
      end
    end
    if (_T_638) begin
      if (s1_dout_valid) begin
        _T_685_3 <= _T_616__T_632_data;
      end else begin
        _T_685_3 <= _T_634;
      end
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
    _T_550[initvar] = _RAND_12[63:0];
  _RAND_15 = {2{`RANDOM}};
  for (initvar = 0; initvar < 512; initvar = initvar+1)
    _T_572[initvar] = _RAND_15[63:0];
  _RAND_18 = {2{`RANDOM}};
  for (initvar = 0; initvar < 512; initvar = initvar+1)
    _T_594[initvar] = _RAND_18[63:0];
  _RAND_21 = {2{`RANDOM}};
  for (initvar = 0; initvar < 512; initvar = initvar+1)
    _T_616[initvar] = _RAND_21[63:0];
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
  _T_550__T_566_en_pipe_0 = _RAND_13[0:0];
  _RAND_14 = {1{`RANDOM}};
  _T_550__T_566_addr_pipe_0 = _RAND_14[8:0];
  _RAND_16 = {1{`RANDOM}};
  _T_572__T_588_en_pipe_0 = _RAND_16[0:0];
  _RAND_17 = {1{`RANDOM}};
  _T_572__T_588_addr_pipe_0 = _RAND_17[8:0];
  _RAND_19 = {1{`RANDOM}};
  _T_594__T_610_en_pipe_0 = _RAND_19[0:0];
  _RAND_20 = {1{`RANDOM}};
  _T_594__T_610_addr_pipe_0 = _RAND_20[8:0];
  _RAND_22 = {1{`RANDOM}};
  _T_616__T_632_en_pipe_0 = _RAND_22[0:0];
  _RAND_23 = {1{`RANDOM}};
  _T_616__T_632_addr_pipe_0 = _RAND_23[8:0];
  _RAND_24 = {1{`RANDOM}};
  state = _RAND_24[1:0];
  _RAND_25 = {1{`RANDOM}};
  invalidated = _RAND_25[0:0];
  _RAND_26 = {1{`RANDOM}};
  refill_addr = _RAND_26[31:0];
  _RAND_27 = {1{`RANDOM}};
  s1_valid = _RAND_27[0:0];
  _RAND_28 = {8{`RANDOM}};
  vb_array = _RAND_28[255:0];
  _RAND_29 = {1{`RANDOM}};
  s1_dout_valid = _RAND_29[0:0];
  _RAND_30 = {1{`RANDOM}};
  _T_453 = _RAND_30[0:0];
  _RAND_31 = {1{`RANDOM}};
  _T_479 = _RAND_31[0:0];
  _RAND_32 = {1{`RANDOM}};
  _T_505 = _RAND_32[0:0];
  _RAND_33 = {1{`RANDOM}};
  _T_531 = _RAND_33[0:0];
  _RAND_34 = {1{`RANDOM}};
  _T_246 = _RAND_34[4:0];
  _RAND_35 = {1{`RANDOM}};
  _T_262 = _RAND_35[15:0];
  _RAND_36 = {2{`RANDOM}};
  _T_568 = _RAND_36[63:0];
  _RAND_37 = {2{`RANDOM}};
  _T_590 = _RAND_37[63:0];
  _RAND_38 = {2{`RANDOM}};
  _T_612 = _RAND_38[63:0];
  _RAND_39 = {2{`RANDOM}};
  _T_634 = _RAND_39[63:0];
  _RAND_40 = {1{`RANDOM}};
  _T_639 = _RAND_40[0:0];
  _RAND_41 = {1{`RANDOM}};
  _T_654_0 = _RAND_41[0:0];
  _RAND_42 = {1{`RANDOM}};
  _T_654_1 = _RAND_42[0:0];
  _RAND_43 = {1{`RANDOM}};
  _T_654_2 = _RAND_43[0:0];
  _RAND_44 = {1{`RANDOM}};
  _T_654_3 = _RAND_44[0:0];
  _RAND_45 = {2{`RANDOM}};
  _T_685_0 = _RAND_45[63:0];
  _RAND_46 = {2{`RANDOM}};
  _T_685_1 = _RAND_46[63:0];
  _RAND_47 = {2{`RANDOM}};
  _T_685_2 = _RAND_47[63:0];
  _RAND_48 = {2{`RANDOM}};
  _T_685_3 = _RAND_48[63:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

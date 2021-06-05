module BankedStore(
  input         clock,
  input         reset,
  output        io_sinkC_adr_ready,
  input         io_sinkC_adr_valid,
  input         io_sinkC_adr_bits_noop,
  input  [2:0]  io_sinkC_adr_bits_way,
  input  [9:0]  io_sinkC_adr_bits_set,
  input  [2:0]  io_sinkC_adr_bits_beat,
  input         io_sinkC_adr_bits_mask,
  input  [63:0] io_sinkC_dat_data,
  output        io_sinkD_adr_ready,
  input         io_sinkD_adr_valid,
  input         io_sinkD_adr_bits_noop,
  input  [2:0]  io_sinkD_adr_bits_way,
  input  [9:0]  io_sinkD_adr_bits_set,
  input  [2:0]  io_sinkD_adr_bits_beat,
  input         io_sinkD_adr_bits_mask,
  input  [63:0] io_sinkD_dat_data,
  output        io_sourceC_adr_ready,
  input         io_sourceC_adr_valid,
  input         io_sourceC_adr_bits_noop,
  input  [2:0]  io_sourceC_adr_bits_way,
  input  [9:0]  io_sourceC_adr_bits_set,
  input  [2:0]  io_sourceC_adr_bits_beat,
  input         io_sourceC_adr_bits_mask,
  output [63:0] io_sourceC_dat_data,
  output        io_sourceD_radr_ready,
  input         io_sourceD_radr_valid,
  input         io_sourceD_radr_bits_noop,
  input  [2:0]  io_sourceD_radr_bits_way,
  input  [9:0]  io_sourceD_radr_bits_set,
  input  [2:0]  io_sourceD_radr_bits_beat,
  input         io_sourceD_radr_bits_mask,
  output [63:0] io_sourceD_rdat_data,
  output        io_sourceD_wadr_ready,
  input         io_sourceD_wadr_valid,
  input         io_sourceD_wadr_bits_noop,
  input  [2:0]  io_sourceD_wadr_bits_way,
  input  [9:0]  io_sourceD_wadr_bits_set,
  input  [2:0]  io_sourceD_wadr_bits_beat,
  input         io_sourceD_wadr_bits_mask,
  input  [63:0] io_sourceD_wdat_data
);
`ifdef RANDOMIZE_MEM_INIT
  reg [63:0] _RAND_0;
  reg [63:0] _RAND_3;
  reg [63:0] _RAND_6;
  reg [63:0] _RAND_9;
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
  reg [63:0] _RAND_13;
  reg [31:0] _RAND_14;
  reg [63:0] _RAND_15;
  reg [31:0] _RAND_16;
  reg [63:0] _RAND_17;
  reg [31:0] _RAND_18;
  reg [63:0] _RAND_19;
  reg [31:0] _RAND_20;
  reg [31:0] _RAND_21;
  reg [31:0] _RAND_22;
  reg [31:0] _RAND_23;
`endif // RANDOMIZE_REG_INIT
  reg [63:0] cc_banks_0 [0:16383]; // @[DescribedSRAM.scala 23:26]
  wire [63:0] cc_banks_0__T_291_data; // @[DescribedSRAM.scala 23:26]
  wire [13:0] cc_banks_0__T_291_addr; // @[DescribedSRAM.scala 23:26]
  wire [63:0] cc_banks_0__T_285_data; // @[DescribedSRAM.scala 23:26]
  wire [13:0] cc_banks_0__T_285_addr; // @[DescribedSRAM.scala 23:26]
  wire  cc_banks_0__T_285_mask; // @[DescribedSRAM.scala 23:26]
  wire  cc_banks_0__T_285_en; // @[DescribedSRAM.scala 23:26]
  reg  cc_banks_0__T_291_en_pipe_0;
  reg [13:0] cc_banks_0__T_291_addr_pipe_0;
  reg [63:0] cc_banks_1 [0:16383]; // @[DescribedSRAM.scala 23:26]
  wire [63:0] cc_banks_1__T_329_data; // @[DescribedSRAM.scala 23:26]
  wire [13:0] cc_banks_1__T_329_addr; // @[DescribedSRAM.scala 23:26]
  wire [63:0] cc_banks_1__T_323_data; // @[DescribedSRAM.scala 23:26]
  wire [13:0] cc_banks_1__T_323_addr; // @[DescribedSRAM.scala 23:26]
  wire  cc_banks_1__T_323_mask; // @[DescribedSRAM.scala 23:26]
  wire  cc_banks_1__T_323_en; // @[DescribedSRAM.scala 23:26]
  reg  cc_banks_1__T_329_en_pipe_0;
  reg [13:0] cc_banks_1__T_329_addr_pipe_0;
  reg [63:0] cc_banks_2 [0:16383]; // @[DescribedSRAM.scala 23:26]
  wire [63:0] cc_banks_2__T_367_data; // @[DescribedSRAM.scala 23:26]
  wire [13:0] cc_banks_2__T_367_addr; // @[DescribedSRAM.scala 23:26]
  wire [63:0] cc_banks_2__T_361_data; // @[DescribedSRAM.scala 23:26]
  wire [13:0] cc_banks_2__T_361_addr; // @[DescribedSRAM.scala 23:26]
  wire  cc_banks_2__T_361_mask; // @[DescribedSRAM.scala 23:26]
  wire  cc_banks_2__T_361_en; // @[DescribedSRAM.scala 23:26]
  reg  cc_banks_2__T_367_en_pipe_0;
  reg [13:0] cc_banks_2__T_367_addr_pipe_0;
  reg [63:0] cc_banks_3 [0:16383]; // @[DescribedSRAM.scala 23:26]
  wire [63:0] cc_banks_3__T_405_data; // @[DescribedSRAM.scala 23:26]
  wire [13:0] cc_banks_3__T_405_addr; // @[DescribedSRAM.scala 23:26]
  wire [63:0] cc_banks_3__T_399_data; // @[DescribedSRAM.scala 23:26]
  wire [13:0] cc_banks_3__T_399_addr; // @[DescribedSRAM.scala 23:26]
  wire  cc_banks_3__T_399_mask; // @[DescribedSRAM.scala 23:26]
  wire  cc_banks_3__T_399_en; // @[DescribedSRAM.scala 23:26]
  reg  cc_banks_3__T_405_en_pipe_0;
  reg [13:0] cc_banks_3__T_405_addr_pipe_0;
  wire [15:0] _T_2 = {io_sinkC_adr_bits_way,io_sinkC_adr_bits_set,io_sinkC_adr_bits_beat}; // @[Cat.scala 29:58]
  wire [3:0] _T_5 = 4'h1 << _T_2[1:0]; // @[OneHot.scala 65:12]
  wire [3:0] _T_27 = 4'hf >> _T_2[1:0]; // @[BankedStore.scala 131:21]
  wire [13:0] reqs_0_index = _T_2[15:2]; // @[BankedStore.scala 134:23]
  wire [3:0] _T_36 = {_T_5[3],_T_5[2],_T_5[1],_T_5[0]}; // @[Cat.scala 29:58]
  wire [3:0] _T_38 = io_sinkC_adr_bits_mask ? 4'hf : 4'h0; // @[Bitwise.scala 72:12]
  wire [3:0] _T_39 = _T_36 & _T_38; // @[BankedStore.scala 135:65]
  wire [3:0] reqs_0_bankSel = io_sinkC_adr_valid ? _T_39 : 4'h0; // @[BankedStore.scala 135:24]
  wire [3:0] reqs_0_bankEn = io_sinkC_adr_bits_noop ? 4'h0 : reqs_0_bankSel; // @[BankedStore.scala 136:24]
  wire [15:0] _T_53 = {io_sinkD_adr_bits_way,io_sinkD_adr_bits_set,io_sinkD_adr_bits_beat}; // @[Cat.scala 29:58]
  wire [3:0] _T_56 = 4'h1 << _T_53[1:0]; // @[OneHot.scala 65:12]
  wire [15:0] _T_103 = {io_sourceC_adr_bits_way,io_sourceC_adr_bits_set,io_sourceC_adr_bits_beat}; // @[Cat.scala 29:58]
  wire [3:0] _T_106 = 4'h1 << _T_103[1:0]; // @[OneHot.scala 65:12]
  wire [3:0] _T_137 = {_T_106[3],_T_106[2],_T_106[1],_T_106[0]}; // @[Cat.scala 29:58]
  wire [3:0] _T_139 = io_sourceC_adr_bits_mask ? 4'hf : 4'h0; // @[Bitwise.scala 72:12]
  wire [3:0] _T_140 = _T_137 & _T_139; // @[BankedStore.scala 135:65]
  wire [3:0] reqs_1_bankSel = io_sourceC_adr_valid ? _T_140 : 4'h0; // @[BankedStore.scala 135:24]
  wire [3:0] reqs_2_bankSum = reqs_1_bankSel | reqs_0_bankSel; // @[BankedStore.scala 160:17]
  wire  _T_61 = ~(|(reqs_2_bankSum[0] & io_sinkD_adr_bits_mask)); // @[BankedStore.scala 130:58]
  wire  _T_65 = ~(|(reqs_2_bankSum[1] & io_sinkD_adr_bits_mask)); // @[BankedStore.scala 130:58]
  wire  _T_69 = ~(|(reqs_2_bankSum[2] & io_sinkD_adr_bits_mask)); // @[BankedStore.scala 130:58]
  wire  _T_73 = ~(|(reqs_2_bankSum[3] & io_sinkD_adr_bits_mask)); // @[BankedStore.scala 130:58]
  wire [3:0] _T_76 = {_T_73,_T_69,_T_65,_T_61}; // @[Cat.scala 29:58]
  wire [3:0] _T_78 = _T_76 >> _T_53[1:0]; // @[BankedStore.scala 131:21]
  wire [13:0] reqs_2_index = _T_53[15:2]; // @[BankedStore.scala 134:23]
  wire [3:0] _T_87 = {_T_56[3],_T_56[2],_T_56[1],_T_56[0]}; // @[Cat.scala 29:58]
  wire [3:0] _T_89 = io_sinkD_adr_bits_mask ? 4'hf : 4'h0; // @[Bitwise.scala 72:12]
  wire [3:0] _T_90 = _T_87 & _T_89; // @[BankedStore.scala 135:65]
  wire [3:0] reqs_2_bankSel = io_sinkD_adr_valid ? _T_90 : 4'h0; // @[BankedStore.scala 135:24]
  wire [3:0] _T_98 = {_T_76[3],_T_76[2],_T_76[1],_T_76[0]}; // @[Cat.scala 29:58]
  wire [3:0] _T_99 = reqs_2_bankSel & _T_98; // @[BankedStore.scala 136:59]
  wire [3:0] reqs_2_bankEn = io_sinkD_adr_bits_noop ? 4'h0 : _T_99; // @[BankedStore.scala 136:24]
  wire  _T_111 = ~(|(reqs_0_bankSel[0] & io_sourceC_adr_bits_mask)); // @[BankedStore.scala 130:58]
  wire  _T_115 = ~(|(reqs_0_bankSel[1] & io_sourceC_adr_bits_mask)); // @[BankedStore.scala 130:58]
  wire  _T_119 = ~(|(reqs_0_bankSel[2] & io_sourceC_adr_bits_mask)); // @[BankedStore.scala 130:58]
  wire  _T_123 = ~(|(reqs_0_bankSel[3] & io_sourceC_adr_bits_mask)); // @[BankedStore.scala 130:58]
  wire [3:0] _T_126 = {_T_123,_T_119,_T_115,_T_111}; // @[Cat.scala 29:58]
  wire [3:0] _T_128 = _T_126 >> _T_103[1:0]; // @[BankedStore.scala 131:21]
  wire [13:0] reqs_1_index = _T_103[15:2]; // @[BankedStore.scala 134:23]
  wire [3:0] _T_148 = {_T_126[3],_T_126[2],_T_126[1],_T_126[0]}; // @[Cat.scala 29:58]
  wire [3:0] _T_149 = reqs_1_bankSel & _T_148; // @[BankedStore.scala 136:59]
  wire [3:0] reqs_1_bankEn = io_sourceC_adr_bits_noop ? 4'h0 : _T_149; // @[BankedStore.scala 136:24]
  wire [15:0] _T_153 = {io_sourceD_radr_bits_way,io_sourceD_radr_bits_set,io_sourceD_radr_bits_beat}; // @[Cat.scala 29:58]
  wire [3:0] _T_156 = 4'h1 << _T_153[1:0]; // @[OneHot.scala 65:12]
  wire [15:0] _T_204 = {io_sourceD_wadr_bits_way,io_sourceD_wadr_bits_set,io_sourceD_wadr_bits_beat}; // @[Cat.scala 29:58]
  wire [3:0] _T_207 = 4'h1 << _T_204[1:0]; // @[OneHot.scala 65:12]
  wire [3:0] _T_238 = {_T_207[3],_T_207[2],_T_207[1],_T_207[0]}; // @[Cat.scala 29:58]
  wire [3:0] _T_240 = io_sourceD_wadr_bits_mask ? 4'hf : 4'h0; // @[Bitwise.scala 72:12]
  wire [3:0] _T_241 = _T_238 & _T_240; // @[BankedStore.scala 135:65]
  wire [3:0] reqs_3_bankSel = io_sourceD_wadr_valid ? _T_241 : 4'h0; // @[BankedStore.scala 135:24]
  wire [3:0] reqs_3_bankSum = reqs_2_bankSel | reqs_2_bankSum; // @[BankedStore.scala 160:17]
  wire [3:0] reqs_4_bankSum = reqs_3_bankSel | reqs_3_bankSum; // @[BankedStore.scala 160:17]
  wire  _T_161 = ~(|(reqs_4_bankSum[0] & io_sourceD_radr_bits_mask)); // @[BankedStore.scala 130:58]
  wire  _T_165 = ~(|(reqs_4_bankSum[1] & io_sourceD_radr_bits_mask)); // @[BankedStore.scala 130:58]
  wire  _T_169 = ~(|(reqs_4_bankSum[2] & io_sourceD_radr_bits_mask)); // @[BankedStore.scala 130:58]
  wire  _T_173 = ~(|(reqs_4_bankSum[3] & io_sourceD_radr_bits_mask)); // @[BankedStore.scala 130:58]
  wire [3:0] _T_176 = {_T_173,_T_169,_T_165,_T_161}; // @[Cat.scala 29:58]
  wire [3:0] _T_178 = _T_176 >> _T_153[1:0]; // @[BankedStore.scala 131:21]
  wire [13:0] reqs_4_index = _T_153[15:2]; // @[BankedStore.scala 134:23]
  wire [3:0] _T_187 = {_T_156[3],_T_156[2],_T_156[1],_T_156[0]}; // @[Cat.scala 29:58]
  wire [3:0] _T_189 = io_sourceD_radr_bits_mask ? 4'hf : 4'h0; // @[Bitwise.scala 72:12]
  wire [3:0] _T_190 = _T_187 & _T_189; // @[BankedStore.scala 135:65]
  wire [3:0] reqs_4_bankSel = io_sourceD_radr_valid ? _T_190 : 4'h0; // @[BankedStore.scala 135:24]
  wire [3:0] _T_198 = {_T_176[3],_T_176[2],_T_176[1],_T_176[0]}; // @[Cat.scala 29:58]
  wire [3:0] _T_199 = reqs_4_bankSel & _T_198; // @[BankedStore.scala 136:59]
  wire [3:0] reqs_4_bankEn = io_sourceD_radr_bits_noop ? 4'h0 : _T_199; // @[BankedStore.scala 136:24]
  wire  _T_212 = ~(|(reqs_3_bankSum[0] & io_sourceD_wadr_bits_mask)); // @[BankedStore.scala 130:58]
  wire  _T_216 = ~(|(reqs_3_bankSum[1] & io_sourceD_wadr_bits_mask)); // @[BankedStore.scala 130:58]
  wire  _T_220 = ~(|(reqs_3_bankSum[2] & io_sourceD_wadr_bits_mask)); // @[BankedStore.scala 130:58]
  wire  _T_224 = ~(|(reqs_3_bankSum[3] & io_sourceD_wadr_bits_mask)); // @[BankedStore.scala 130:58]
  wire [3:0] _T_227 = {_T_224,_T_220,_T_216,_T_212}; // @[Cat.scala 29:58]
  wire [3:0] _T_229 = _T_227 >> _T_204[1:0]; // @[BankedStore.scala 131:21]
  wire [13:0] reqs_3_index = _T_204[15:2]; // @[BankedStore.scala 134:23]
  wire [3:0] _T_249 = {_T_227[3],_T_227[2],_T_227[1],_T_227[0]}; // @[Cat.scala 29:58]
  wire [3:0] _T_250 = reqs_3_bankSel & _T_249; // @[BankedStore.scala 136:59]
  wire [3:0] reqs_3_bankEn = io_sourceD_wadr_bits_noop ? 4'h0 : _T_250; // @[BankedStore.scala 136:24]
  wire  _T_266 = reqs_0_bankEn[0] | reqs_1_bankEn[0] | reqs_2_bankEn[0] | reqs_3_bankEn[0] | reqs_4_bankEn[0]; // @[BankedStore.scala 164:45]
  wire  _T_274 = reqs_1_bankSel[0] ? 1'h0 : reqs_2_bankSel[0] | reqs_3_bankSel[0]; // @[Mux.scala 47:69]
  wire  _T_275 = reqs_0_bankSel[0] | _T_274; // @[Mux.scala 47:69]
  wire [13:0] _T_276 = reqs_3_bankSel[0] ? reqs_3_index : reqs_4_index; // @[Mux.scala 47:69]
  wire [13:0] _T_277 = reqs_2_bankSel[0] ? reqs_2_index : _T_276; // @[Mux.scala 47:69]
  wire [13:0] _T_278 = reqs_1_bankSel[0] ? reqs_1_index : _T_277; // @[Mux.scala 47:69]
  wire [63:0] _T_280 = reqs_3_bankSel[0] ? io_sourceD_wdat_data : 64'h0; // @[Mux.scala 47:69]
  wire [63:0] _T_281 = reqs_2_bankSel[0] ? io_sinkD_dat_data : _T_280; // @[Mux.scala 47:69]
  wire [63:0] _T_282 = reqs_1_bankSel[0] ? 64'h0 : _T_281; // @[Mux.scala 47:69]
  wire  _T_286 = ~_T_275; // @[BankedStore.scala 171:27]
  reg  _T_294; // @[BankedStore.scala 171:47]
  reg [63:0] regout_0; // @[Reg.scala 15:16]
  wire  _T_304 = reqs_0_bankEn[1] | reqs_1_bankEn[1] | reqs_2_bankEn[1] | reqs_3_bankEn[1] | reqs_4_bankEn[1]; // @[BankedStore.scala 164:45]
  wire  _T_312 = reqs_1_bankSel[1] ? 1'h0 : reqs_2_bankSel[1] | reqs_3_bankSel[1]; // @[Mux.scala 47:69]
  wire  _T_313 = reqs_0_bankSel[1] | _T_312; // @[Mux.scala 47:69]
  wire [13:0] _T_314 = reqs_3_bankSel[1] ? reqs_3_index : reqs_4_index; // @[Mux.scala 47:69]
  wire [13:0] _T_315 = reqs_2_bankSel[1] ? reqs_2_index : _T_314; // @[Mux.scala 47:69]
  wire [13:0] _T_316 = reqs_1_bankSel[1] ? reqs_1_index : _T_315; // @[Mux.scala 47:69]
  wire [63:0] _T_318 = reqs_3_bankSel[1] ? io_sourceD_wdat_data : 64'h0; // @[Mux.scala 47:69]
  wire [63:0] _T_319 = reqs_2_bankSel[1] ? io_sinkD_dat_data : _T_318; // @[Mux.scala 47:69]
  wire [63:0] _T_320 = reqs_1_bankSel[1] ? 64'h0 : _T_319; // @[Mux.scala 47:69]
  wire  _T_324 = ~_T_313; // @[BankedStore.scala 171:27]
  reg  _T_332; // @[BankedStore.scala 171:47]
  reg [63:0] regout_1; // @[Reg.scala 15:16]
  wire  _T_342 = reqs_0_bankEn[2] | reqs_1_bankEn[2] | reqs_2_bankEn[2] | reqs_3_bankEn[2] | reqs_4_bankEn[2]; // @[BankedStore.scala 164:45]
  wire  _T_350 = reqs_1_bankSel[2] ? 1'h0 : reqs_2_bankSel[2] | reqs_3_bankSel[2]; // @[Mux.scala 47:69]
  wire  _T_351 = reqs_0_bankSel[2] | _T_350; // @[Mux.scala 47:69]
  wire [13:0] _T_352 = reqs_3_bankSel[2] ? reqs_3_index : reqs_4_index; // @[Mux.scala 47:69]
  wire [13:0] _T_353 = reqs_2_bankSel[2] ? reqs_2_index : _T_352; // @[Mux.scala 47:69]
  wire [13:0] _T_354 = reqs_1_bankSel[2] ? reqs_1_index : _T_353; // @[Mux.scala 47:69]
  wire [63:0] _T_356 = reqs_3_bankSel[2] ? io_sourceD_wdat_data : 64'h0; // @[Mux.scala 47:69]
  wire [63:0] _T_357 = reqs_2_bankSel[2] ? io_sinkD_dat_data : _T_356; // @[Mux.scala 47:69]
  wire [63:0] _T_358 = reqs_1_bankSel[2] ? 64'h0 : _T_357; // @[Mux.scala 47:69]
  wire  _T_362 = ~_T_351; // @[BankedStore.scala 171:27]
  reg  _T_370; // @[BankedStore.scala 171:47]
  reg [63:0] regout_2; // @[Reg.scala 15:16]
  wire  _T_380 = reqs_0_bankEn[3] | reqs_1_bankEn[3] | reqs_2_bankEn[3] | reqs_3_bankEn[3] | reqs_4_bankEn[3]; // @[BankedStore.scala 164:45]
  wire  _T_388 = reqs_1_bankSel[3] ? 1'h0 : reqs_2_bankSel[3] | reqs_3_bankSel[3]; // @[Mux.scala 47:69]
  wire  _T_389 = reqs_0_bankSel[3] | _T_388; // @[Mux.scala 47:69]
  wire [13:0] _T_390 = reqs_3_bankSel[3] ? reqs_3_index : reqs_4_index; // @[Mux.scala 47:69]
  wire [13:0] _T_391 = reqs_2_bankSel[3] ? reqs_2_index : _T_390; // @[Mux.scala 47:69]
  wire [13:0] _T_392 = reqs_1_bankSel[3] ? reqs_1_index : _T_391; // @[Mux.scala 47:69]
  wire [63:0] _T_394 = reqs_3_bankSel[3] ? io_sourceD_wdat_data : 64'h0; // @[Mux.scala 47:69]
  wire [63:0] _T_395 = reqs_2_bankSel[3] ? io_sinkD_dat_data : _T_394; // @[Mux.scala 47:69]
  wire [63:0] _T_396 = reqs_1_bankSel[3] ? 64'h0 : _T_395; // @[Mux.scala 47:69]
  wire  _T_400 = ~_T_389; // @[BankedStore.scala 171:27]
  reg  _T_408; // @[BankedStore.scala 171:47]
  reg [63:0] regout_3; // @[Reg.scala 15:16]
  reg [3:0] _T_410; // @[BankedStore.scala 174:39]
  reg [3:0] regsel_sourceC; // @[BankedStore.scala 174:31]
  reg [3:0] _T_411; // @[BankedStore.scala 175:39]
  reg [3:0] regsel_sourceD; // @[BankedStore.scala 175:31]
  wire [63:0] _T_413 = regsel_sourceC[0] ? regout_0 : 64'h0; // @[BankedStore.scala 178:23]
  wire [63:0] _T_415 = regsel_sourceC[1] ? regout_1 : 64'h0; // @[BankedStore.scala 178:23]
  wire [63:0] _T_417 = regsel_sourceC[2] ? regout_2 : 64'h0; // @[BankedStore.scala 178:23]
  wire [63:0] _T_419 = regsel_sourceC[3] ? regout_3 : 64'h0; // @[BankedStore.scala 178:23]
  wire [63:0] _T_420 = _T_413 | _T_415; // @[BankedStore.scala 179:85]
  wire [63:0] _T_421 = _T_420 | _T_417; // @[BankedStore.scala 179:85]
  wire [63:0] _T_423 = regsel_sourceD[0] ? regout_0 : 64'h0; // @[BankedStore.scala 185:23]
  wire [63:0] _T_425 = regsel_sourceD[1] ? regout_1 : 64'h0; // @[BankedStore.scala 185:23]
  wire [63:0] _T_427 = regsel_sourceD[2] ? regout_2 : 64'h0; // @[BankedStore.scala 185:23]
  wire [63:0] _T_429 = regsel_sourceD[3] ? regout_3 : 64'h0; // @[BankedStore.scala 185:23]
  wire [63:0] _T_430 = _T_423 | _T_425; // @[BankedStore.scala 186:85]
  wire [63:0] _T_431 = _T_430 | _T_427; // @[BankedStore.scala 186:85]
  assign cc_banks_0__T_291_addr = cc_banks_0__T_291_addr_pipe_0;
  assign cc_banks_0__T_291_data = cc_banks_0[cc_banks_0__T_291_addr]; // @[DescribedSRAM.scala 23:26]
  assign cc_banks_0__T_285_data = reqs_0_bankSel[0] ? io_sinkC_dat_data : _T_282;
  assign cc_banks_0__T_285_addr = reqs_0_bankSel[0] ? reqs_0_index : _T_278;
  assign cc_banks_0__T_285_mask = 1'h1;
  assign cc_banks_0__T_285_en = _T_275 & _T_266;
  assign cc_banks_1__T_329_addr = cc_banks_1__T_329_addr_pipe_0;
  assign cc_banks_1__T_329_data = cc_banks_1[cc_banks_1__T_329_addr]; // @[DescribedSRAM.scala 23:26]
  assign cc_banks_1__T_323_data = reqs_0_bankSel[1] ? io_sinkC_dat_data : _T_320;
  assign cc_banks_1__T_323_addr = reqs_0_bankSel[1] ? reqs_0_index : _T_316;
  assign cc_banks_1__T_323_mask = 1'h1;
  assign cc_banks_1__T_323_en = _T_313 & _T_304;
  assign cc_banks_2__T_367_addr = cc_banks_2__T_367_addr_pipe_0;
  assign cc_banks_2__T_367_data = cc_banks_2[cc_banks_2__T_367_addr]; // @[DescribedSRAM.scala 23:26]
  assign cc_banks_2__T_361_data = reqs_0_bankSel[2] ? io_sinkC_dat_data : _T_358;
  assign cc_banks_2__T_361_addr = reqs_0_bankSel[2] ? reqs_0_index : _T_354;
  assign cc_banks_2__T_361_mask = 1'h1;
  assign cc_banks_2__T_361_en = _T_351 & _T_342;
  assign cc_banks_3__T_405_addr = cc_banks_3__T_405_addr_pipe_0;
  assign cc_banks_3__T_405_data = cc_banks_3[cc_banks_3__T_405_addr]; // @[DescribedSRAM.scala 23:26]
  assign cc_banks_3__T_399_data = reqs_0_bankSel[3] ? io_sinkC_dat_data : _T_396;
  assign cc_banks_3__T_399_addr = reqs_0_bankSel[3] ? reqs_0_index : _T_392;
  assign cc_banks_3__T_399_mask = 1'h1;
  assign cc_banks_3__T_399_en = _T_389 & _T_380;
  assign io_sinkC_adr_ready = _T_27[0]; // @[BankedStore.scala 131:21]
  assign io_sinkD_adr_ready = _T_78[0]; // @[BankedStore.scala 131:21]
  assign io_sourceC_adr_ready = _T_128[0]; // @[BankedStore.scala 131:21]
  assign io_sourceC_dat_data = _T_421 | _T_419; // @[BankedStore.scala 179:85]
  assign io_sourceD_radr_ready = _T_178[0]; // @[BankedStore.scala 131:21]
  assign io_sourceD_rdat_data = _T_431 | _T_429; // @[BankedStore.scala 186:85]
  assign io_sourceD_wadr_ready = _T_229[0]; // @[BankedStore.scala 131:21]
  always @(posedge clock) begin
    if(cc_banks_0__T_285_en & cc_banks_0__T_285_mask) begin
      cc_banks_0[cc_banks_0__T_285_addr] <= cc_banks_0__T_285_data; // @[DescribedSRAM.scala 23:26]
    end
    cc_banks_0__T_291_en_pipe_0 <= _T_286 & _T_266;
    if (_T_286 & _T_266) begin
      if (reqs_0_bankSel[0]) begin
        cc_banks_0__T_291_addr_pipe_0 <= reqs_0_index;
      end else if (reqs_1_bankSel[0]) begin // @[Mux.scala 47:69]
        cc_banks_0__T_291_addr_pipe_0 <= reqs_1_index;
      end else if (reqs_2_bankSel[0]) begin // @[Mux.scala 47:69]
        cc_banks_0__T_291_addr_pipe_0 <= reqs_2_index;
      end else begin
        cc_banks_0__T_291_addr_pipe_0 <= _T_276;
      end
    end
    if(cc_banks_1__T_323_en & cc_banks_1__T_323_mask) begin
      cc_banks_1[cc_banks_1__T_323_addr] <= cc_banks_1__T_323_data; // @[DescribedSRAM.scala 23:26]
    end
    cc_banks_1__T_329_en_pipe_0 <= _T_324 & _T_304;
    if (_T_324 & _T_304) begin
      if (reqs_0_bankSel[1]) begin
        cc_banks_1__T_329_addr_pipe_0 <= reqs_0_index;
      end else if (reqs_1_bankSel[1]) begin // @[Mux.scala 47:69]
        cc_banks_1__T_329_addr_pipe_0 <= reqs_1_index;
      end else if (reqs_2_bankSel[1]) begin // @[Mux.scala 47:69]
        cc_banks_1__T_329_addr_pipe_0 <= reqs_2_index;
      end else begin
        cc_banks_1__T_329_addr_pipe_0 <= _T_314;
      end
    end
    if(cc_banks_2__T_361_en & cc_banks_2__T_361_mask) begin
      cc_banks_2[cc_banks_2__T_361_addr] <= cc_banks_2__T_361_data; // @[DescribedSRAM.scala 23:26]
    end
    cc_banks_2__T_367_en_pipe_0 <= _T_362 & _T_342;
    if (_T_362 & _T_342) begin
      if (reqs_0_bankSel[2]) begin
        cc_banks_2__T_367_addr_pipe_0 <= reqs_0_index;
      end else if (reqs_1_bankSel[2]) begin // @[Mux.scala 47:69]
        cc_banks_2__T_367_addr_pipe_0 <= reqs_1_index;
      end else if (reqs_2_bankSel[2]) begin // @[Mux.scala 47:69]
        cc_banks_2__T_367_addr_pipe_0 <= reqs_2_index;
      end else begin
        cc_banks_2__T_367_addr_pipe_0 <= _T_352;
      end
    end
    if(cc_banks_3__T_399_en & cc_banks_3__T_399_mask) begin
      cc_banks_3[cc_banks_3__T_399_addr] <= cc_banks_3__T_399_data; // @[DescribedSRAM.scala 23:26]
    end
    cc_banks_3__T_405_en_pipe_0 <= _T_400 & _T_380;
    if (_T_400 & _T_380) begin
      if (reqs_0_bankSel[3]) begin
        cc_banks_3__T_405_addr_pipe_0 <= reqs_0_index;
      end else if (reqs_1_bankSel[3]) begin // @[Mux.scala 47:69]
        cc_banks_3__T_405_addr_pipe_0 <= reqs_1_index;
      end else if (reqs_2_bankSel[3]) begin // @[Mux.scala 47:69]
        cc_banks_3__T_405_addr_pipe_0 <= reqs_2_index;
      end else begin
        cc_banks_3__T_405_addr_pipe_0 <= _T_390;
      end
    end
    _T_294 <= ~_T_275 & _T_266; // @[BankedStore.scala 171:53]
    if (_T_294) begin // @[Reg.scala 16:19]
      regout_0 <= cc_banks_0__T_291_data; // @[Reg.scala 16:23]
    end
    _T_332 <= ~_T_313 & _T_304; // @[BankedStore.scala 171:53]
    if (_T_332) begin // @[Reg.scala 16:19]
      regout_1 <= cc_banks_1__T_329_data; // @[Reg.scala 16:23]
    end
    _T_370 <= ~_T_351 & _T_342; // @[BankedStore.scala 171:53]
    if (_T_370) begin // @[Reg.scala 16:19]
      regout_2 <= cc_banks_2__T_367_data; // @[Reg.scala 16:23]
    end
    _T_408 <= ~_T_389 & _T_380; // @[BankedStore.scala 171:53]
    if (_T_408) begin // @[Reg.scala 16:19]
      regout_3 <= cc_banks_3__T_405_data; // @[Reg.scala 16:23]
    end
    if (io_sourceC_adr_bits_noop) begin // @[BankedStore.scala 136:24]
      _T_410 <= 4'h0;
    end else begin
      _T_410 <= _T_149;
    end
    regsel_sourceC <= _T_410; // @[BankedStore.scala 174:31]
    if (io_sourceD_radr_bits_noop) begin // @[BankedStore.scala 136:24]
      _T_411 <= 4'h0;
    end else begin
      _T_411 <= _T_199;
    end
    regsel_sourceD <= _T_411; // @[BankedStore.scala 175:31]
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
  _RAND_0 = {2{`RANDOM}};
  for (initvar = 0; initvar < 16384; initvar = initvar+1)
    cc_banks_0[initvar] = _RAND_0[63:0];
  _RAND_3 = {2{`RANDOM}};
  for (initvar = 0; initvar < 16384; initvar = initvar+1)
    cc_banks_1[initvar] = _RAND_3[63:0];
  _RAND_6 = {2{`RANDOM}};
  for (initvar = 0; initvar < 16384; initvar = initvar+1)
    cc_banks_2[initvar] = _RAND_6[63:0];
  _RAND_9 = {2{`RANDOM}};
  for (initvar = 0; initvar < 16384; initvar = initvar+1)
    cc_banks_3[initvar] = _RAND_9[63:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  cc_banks_0__T_291_en_pipe_0 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  cc_banks_0__T_291_addr_pipe_0 = _RAND_2[13:0];
  _RAND_4 = {1{`RANDOM}};
  cc_banks_1__T_329_en_pipe_0 = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  cc_banks_1__T_329_addr_pipe_0 = _RAND_5[13:0];
  _RAND_7 = {1{`RANDOM}};
  cc_banks_2__T_367_en_pipe_0 = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  cc_banks_2__T_367_addr_pipe_0 = _RAND_8[13:0];
  _RAND_10 = {1{`RANDOM}};
  cc_banks_3__T_405_en_pipe_0 = _RAND_10[0:0];
  _RAND_11 = {1{`RANDOM}};
  cc_banks_3__T_405_addr_pipe_0 = _RAND_11[13:0];
  _RAND_12 = {1{`RANDOM}};
  _T_294 = _RAND_12[0:0];
  _RAND_13 = {2{`RANDOM}};
  regout_0 = _RAND_13[63:0];
  _RAND_14 = {1{`RANDOM}};
  _T_332 = _RAND_14[0:0];
  _RAND_15 = {2{`RANDOM}};
  regout_1 = _RAND_15[63:0];
  _RAND_16 = {1{`RANDOM}};
  _T_370 = _RAND_16[0:0];
  _RAND_17 = {2{`RANDOM}};
  regout_2 = _RAND_17[63:0];
  _RAND_18 = {1{`RANDOM}};
  _T_408 = _RAND_18[0:0];
  _RAND_19 = {2{`RANDOM}};
  regout_3 = _RAND_19[63:0];
  _RAND_20 = {1{`RANDOM}};
  _T_410 = _RAND_20[3:0];
  _RAND_21 = {1{`RANDOM}};
  regsel_sourceC = _RAND_21[3:0];
  _RAND_22 = {1{`RANDOM}};
  _T_411 = _RAND_22[3:0];
  _RAND_23 = {1{`RANDOM}};
  regsel_sourceD = _RAND_23[3:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

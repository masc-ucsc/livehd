module TLMonitor(
  input         clock,
  input         reset,
  input         io_in_a_ready,
  input         io_in_a_valid,
  input  [2:0]  io_in_a_bits_opcode,
  input  [2:0]  io_in_a_bits_param,
  input  [3:0]  io_in_a_bits_size,
  input         io_in_a_bits_source,
  input  [31:0] io_in_a_bits_address,
  input  [7:0]  io_in_a_bits_mask,
  input  [63:0] io_in_a_bits_data,
  input         io_in_a_bits_corrupt,
  input         io_in_d_ready,
  input         io_in_d_valid,
  input  [2:0]  io_in_d_bits_opcode,
  input  [1:0]  io_in_d_bits_param,
  input  [3:0]  io_in_d_bits_size,
  input         io_in_d_bits_source,
  input  [2:0]  io_in_d_bits_sink,
  input         io_in_d_bits_denied,
  input  [63:0] io_in_d_bits_data,
  input         io_in_d_bits_corrupt
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
  reg [31:0] _RAND_16;
  reg [31:0] _RAND_17;
  reg [31:0] _RAND_18;
`endif // RANDOMIZE_REG_INIT
  wire [31:0] plusarg_reader_out; // @[PlusArg.scala 44:11]
  wire  _T_4 = ~io_in_a_bits_source; // @[Parameters.scala 47:9]
  wire [26:0] _T_7 = 27'hfff << io_in_a_bits_size; // @[package.scala 189:77]
  wire [11:0] _T_9 = ~_T_7[11:0]; // @[package.scala 189:46]
  wire [31:0] _GEN_56 = {{20'd0}, _T_9}; // @[Edges.scala 22:16]
  wire [31:0] _T_10 = io_in_a_bits_address & _GEN_56; // @[Edges.scala 22:16]
  wire  _T_11 = _T_10 == 32'h0; // @[Edges.scala 22:24]
  wire [3:0] _T_14 = 4'h1 << io_in_a_bits_size[1:0]; // @[OneHot.scala 65:12]
  wire [2:0] _T_16 = _T_14[2:0] | 3'h1; // @[Misc.scala 201:81]
  wire  _T_17 = io_in_a_bits_size >= 4'h3; // @[Misc.scala 205:21]
  wire  _T_20 = ~io_in_a_bits_address[2]; // @[Misc.scala 210:20]
  wire  _T_29 = ~io_in_a_bits_address[1]; // @[Misc.scala 210:20]
  wire  _T_30 = _T_20 & _T_29; // @[Misc.scala 213:27]
  wire  _T_33 = _T_20 & io_in_a_bits_address[1]; // @[Misc.scala 213:27]
  wire  _T_36 = io_in_a_bits_address[2] & _T_29; // @[Misc.scala 213:27]
  wire  _T_39 = io_in_a_bits_address[2] & io_in_a_bits_address[1]; // @[Misc.scala 213:27]
  wire  _T_44 = ~io_in_a_bits_address[0]; // @[Misc.scala 210:20]
  wire  _T_45 = _T_20 & _T_29 & _T_44; // @[Misc.scala 213:27]
  wire  _T_47 = _T_17 | _T_16[2] & _T_20 | _T_16[1] & _T_30 | _T_16[0] & _T_45; // @[Misc.scala 214:29]
  wire  _T_48 = _T_20 & _T_29 & io_in_a_bits_address[0]; // @[Misc.scala 213:27]
  wire  _T_50 = _T_17 | _T_16[2] & _T_20 | _T_16[1] & _T_30 | _T_16[0] & _T_48; // @[Misc.scala 214:29]
  wire  _T_51 = _T_20 & io_in_a_bits_address[1] & _T_44; // @[Misc.scala 213:27]
  wire  _T_53 = _T_17 | _T_16[2] & _T_20 | _T_16[1] & _T_33 | _T_16[0] & _T_51; // @[Misc.scala 214:29]
  wire  _T_54 = _T_20 & io_in_a_bits_address[1] & io_in_a_bits_address[0]; // @[Misc.scala 213:27]
  wire  _T_56 = _T_17 | _T_16[2] & _T_20 | _T_16[1] & _T_33 | _T_16[0] & _T_54; // @[Misc.scala 214:29]
  wire  _T_57 = io_in_a_bits_address[2] & _T_29 & _T_44; // @[Misc.scala 213:27]
  wire  _T_59 = _T_17 | _T_16[2] & io_in_a_bits_address[2] | _T_16[1] & _T_36 | _T_16[0] & _T_57; // @[Misc.scala 214:29]
  wire  _T_60 = io_in_a_bits_address[2] & _T_29 & io_in_a_bits_address[0]; // @[Misc.scala 213:27]
  wire  _T_62 = _T_17 | _T_16[2] & io_in_a_bits_address[2] | _T_16[1] & _T_36 | _T_16[0] & _T_60; // @[Misc.scala 214:29]
  wire  _T_63 = io_in_a_bits_address[2] & io_in_a_bits_address[1] & _T_44; // @[Misc.scala 213:27]
  wire  _T_65 = _T_17 | _T_16[2] & io_in_a_bits_address[2] | _T_16[1] & _T_39 | _T_16[0] & _T_63; // @[Misc.scala 214:29]
  wire  _T_66 = io_in_a_bits_address[2] & io_in_a_bits_address[1] & io_in_a_bits_address[0]; // @[Misc.scala 213:27]
  wire  _T_68 = _T_17 | _T_16[2] & io_in_a_bits_address[2] | _T_16[1] & _T_39 | _T_16[0] & _T_66; // @[Misc.scala 214:29]
  wire [7:0] _T_75 = {_T_68,_T_65,_T_62,_T_59,_T_56,_T_53,_T_50,_T_47}; // @[Cat.scala 29:58]
  wire [32:0] _T_79 = {1'b0,$signed(io_in_a_bits_address)}; // @[Parameters.scala 137:49]
  wire  _T_87 = io_in_a_bits_opcode == 3'h6; // @[Monitor.scala 79:25]
  wire [31:0] _T_89 = io_in_a_bits_address ^ 32'h3000; // @[Parameters.scala 137:31]
  wire [32:0] _T_90 = {1'b0,$signed(_T_89)}; // @[Parameters.scala 137:49]
  wire [32:0] _T_92 = $signed(_T_90) & -33'sh1000; // @[Parameters.scala 137:52]
  wire  _T_93 = $signed(_T_92) == 33'sh0; // @[Parameters.scala 137:67]
  wire [31:0] _T_94 = io_in_a_bits_address ^ 32'h2010000; // @[Parameters.scala 137:31]
  wire [32:0] _T_95 = {1'b0,$signed(_T_94)}; // @[Parameters.scala 137:49]
  wire [32:0] _T_97 = $signed(_T_95) & -33'sh1000; // @[Parameters.scala 137:52]
  wire  _T_98 = $signed(_T_97) == 33'sh0; // @[Parameters.scala 137:67]
  wire [31:0] _T_99 = io_in_a_bits_address ^ 32'h54000000; // @[Parameters.scala 137:31]
  wire [32:0] _T_100 = {1'b0,$signed(_T_99)}; // @[Parameters.scala 137:49]
  wire [32:0] _T_102 = $signed(_T_100) & -33'sh1000; // @[Parameters.scala 137:52]
  wire  _T_103 = $signed(_T_102) == 33'sh0; // @[Parameters.scala 137:67]
  wire [31:0] _T_104 = io_in_a_bits_address ^ 32'hc000000; // @[Parameters.scala 137:31]
  wire [32:0] _T_105 = {1'b0,$signed(_T_104)}; // @[Parameters.scala 137:49]
  wire [32:0] _T_107 = $signed(_T_105) & -33'sh4000000; // @[Parameters.scala 137:52]
  wire  _T_108 = $signed(_T_107) == 33'sh0; // @[Parameters.scala 137:67]
  wire [31:0] _T_109 = io_in_a_bits_address ^ 32'h2000000; // @[Parameters.scala 137:31]
  wire [32:0] _T_110 = {1'b0,$signed(_T_109)}; // @[Parameters.scala 137:49]
  wire [32:0] _T_112 = $signed(_T_110) & -33'sh10000; // @[Parameters.scala 137:52]
  wire  _T_113 = $signed(_T_112) == 33'sh0; // @[Parameters.scala 137:67]
  wire [32:0] _T_117 = $signed(_T_79) & -33'sh1000; // @[Parameters.scala 137:52]
  wire  _T_118 = $signed(_T_117) == 33'sh0; // @[Parameters.scala 137:67]
  wire [31:0] _T_119 = io_in_a_bits_address ^ 32'h10000; // @[Parameters.scala 137:31]
  wire [32:0] _T_120 = {1'b0,$signed(_T_119)}; // @[Parameters.scala 137:49]
  wire [32:0] _T_122 = $signed(_T_120) & -33'sh10000; // @[Parameters.scala 137:52]
  wire  _T_123 = $signed(_T_122) == 33'sh0; // @[Parameters.scala 137:67]
  wire  _T_131 = 4'h6 == io_in_a_bits_size; // @[Parameters.scala 92:48]
  wire [31:0] _T_133 = io_in_a_bits_address ^ 32'h80000000; // @[Parameters.scala 137:31]
  wire [32:0] _T_134 = {1'b0,$signed(_T_133)}; // @[Parameters.scala 137:49]
  wire [32:0] _T_136 = $signed(_T_134) & -33'sh10000000; // @[Parameters.scala 137:52]
  wire  _T_137 = $signed(_T_136) == 33'sh0; // @[Parameters.scala 137:67]
  wire  _T_138 = _T_131 & _T_137; // @[Parameters.scala 551:56]
  wire  _T_157 = io_in_a_bits_param <= 3'h2; // @[Bundles.scala 110:27]
  wire [7:0] _T_161 = ~io_in_a_bits_mask; // @[Monitor.scala 86:18]
  wire  _T_162 = _T_161 == 8'h0; // @[Monitor.scala 86:31]
  wire  _T_166 = ~io_in_a_bits_corrupt; // @[Monitor.scala 87:18]
  wire  _T_170 = io_in_a_bits_opcode == 3'h7; // @[Monitor.scala 90:25]
  wire  _T_244 = io_in_a_bits_param != 3'h0; // @[Monitor.scala 97:31]
  wire  _T_257 = io_in_a_bits_opcode == 3'h4; // @[Monitor.scala 102:25]
  wire  _T_259 = io_in_a_bits_size <= 4'hc; // @[Parameters.scala 93:42]
  wire  _T_267 = _T_259 & _T_93; // @[Parameters.scala 551:56]
  wire  _T_269 = io_in_a_bits_size <= 4'h6; // @[Parameters.scala 93:42]
  wire  _T_312 = _T_98 | _T_103 | _T_108 | _T_113 | _T_118 | _T_123 | _T_137; // @[Parameters.scala 552:42]
  wire  _T_313 = _T_269 & _T_312; // @[Parameters.scala 551:56]
  wire  _T_315 = _T_267 | _T_313; // @[Parameters.scala 553:30]
  wire  _T_325 = io_in_a_bits_param == 3'h0; // @[Monitor.scala 106:31]
  wire  _T_329 = io_in_a_bits_mask == _T_75; // @[Monitor.scala 107:30]
  wire  _T_337 = io_in_a_bits_opcode == 3'h0; // @[Monitor.scala 111:25]
  wire  _T_386 = _T_98 | _T_103 | _T_108 | _T_113 | _T_118 | _T_137; // @[Parameters.scala 552:42]
  wire  _T_387 = _T_269 & _T_386; // @[Parameters.scala 551:56]
  wire  _T_396 = _T_267 | _T_387; // @[Parameters.scala 553:30]
  wire  _T_415 = io_in_a_bits_opcode == 3'h1; // @[Monitor.scala 119:25]
  wire [7:0] _T_489 = ~_T_75; // @[Monitor.scala 124:33]
  wire [7:0] _T_490 = io_in_a_bits_mask & _T_489; // @[Monitor.scala 124:31]
  wire  _T_491 = _T_490 == 8'h0; // @[Monitor.scala 124:40]
  wire  _T_495 = io_in_a_bits_opcode == 3'h2; // @[Monitor.scala 127:25]
  wire  _T_497 = io_in_a_bits_size <= 4'h3; // @[Parameters.scala 93:42]
  wire  _T_540 = _T_93 | _T_98 | _T_103 | _T_108 | _T_113 | _T_118 | _T_137; // @[Parameters.scala 552:42]
  wire  _T_541 = _T_497 & _T_540; // @[Parameters.scala 551:56]
  wire  _T_560 = io_in_a_bits_param <= 3'h4; // @[Bundles.scala 140:33]
  wire  _T_568 = io_in_a_bits_opcode == 3'h3; // @[Monitor.scala 135:25]
  wire  _T_633 = io_in_a_bits_param <= 3'h3; // @[Bundles.scala 147:30]
  wire  _T_641 = io_in_a_bits_opcode == 3'h5; // @[Monitor.scala 143:25]
  wire  _T_698 = _T_269 & _T_137; // @[Parameters.scala 551:56]
  wire  _T_701 = _T_267 | _T_698; // @[Parameters.scala 553:30]
  wire  _T_711 = io_in_a_bits_param <= 3'h1; // @[Bundles.scala 160:28]
  wire  _T_723 = io_in_d_bits_opcode <= 3'h6; // @[Bundles.scala 44:24]
  wire  _T_727 = ~io_in_d_bits_source; // @[Parameters.scala 47:9]
  wire  _T_730 = io_in_d_bits_opcode == 3'h6; // @[Monitor.scala 307:25]
  wire  _T_734 = io_in_d_bits_size >= 4'h3; // @[Monitor.scala 309:27]
  wire  _T_738 = io_in_d_bits_param == 2'h0; // @[Monitor.scala 310:28]
  wire  _T_742 = ~io_in_d_bits_corrupt; // @[Monitor.scala 311:15]
  wire  _T_746 = ~io_in_d_bits_denied; // @[Monitor.scala 312:15]
  wire  _T_750 = io_in_d_bits_opcode == 3'h4; // @[Monitor.scala 315:25]
  wire  _T_761 = io_in_d_bits_param <= 2'h2; // @[Bundles.scala 104:26]
  wire  _T_765 = io_in_d_bits_param != 2'h2; // @[Monitor.scala 320:28]
  wire  _T_778 = io_in_d_bits_opcode == 3'h5; // @[Monitor.scala 325:25]
  wire  _T_798 = _T_746 | io_in_d_bits_corrupt; // @[Monitor.scala 331:30]
  wire  _T_807 = io_in_d_bits_opcode == 3'h0; // @[Monitor.scala 335:25]
  wire  _T_824 = io_in_d_bits_opcode == 3'h1; // @[Monitor.scala 343:25]
  wire  _T_842 = io_in_d_bits_opcode == 3'h2; // @[Monitor.scala 351:25]
  wire  _T_874 = io_in_a_ready & io_in_a_valid; // @[Decoupled.scala 40:37]
  wire  _T_881 = ~io_in_a_bits_opcode[2]; // @[Edges.scala 93:28]
  reg [8:0] _T_883; // @[Edges.scala 230:27]
  wire [8:0] _T_885 = _T_883 - 9'h1; // @[Edges.scala 231:28]
  wire  _T_886 = _T_883 == 9'h0; // @[Edges.scala 232:25]
  reg [2:0] _T_894; // @[Monitor.scala 381:22]
  reg [2:0] _T_895; // @[Monitor.scala 382:22]
  reg [3:0] _T_896; // @[Monitor.scala 383:22]
  reg  _T_897; // @[Monitor.scala 384:22]
  reg [31:0] _T_898; // @[Monitor.scala 385:22]
  wire  _T_900 = io_in_a_valid & ~_T_886; // @[Monitor.scala 386:19]
  wire  _T_901 = io_in_a_bits_opcode == _T_894; // @[Monitor.scala 387:32]
  wire  _T_905 = io_in_a_bits_param == _T_895; // @[Monitor.scala 388:32]
  wire  _T_909 = io_in_a_bits_size == _T_896; // @[Monitor.scala 389:32]
  wire  _T_913 = io_in_a_bits_source == _T_897; // @[Monitor.scala 390:32]
  wire  _T_917 = io_in_a_bits_address == _T_898; // @[Monitor.scala 391:32]
  wire  _T_923 = io_in_d_ready & io_in_d_valid; // @[Decoupled.scala 40:37]
  wire [26:0] _T_925 = 27'hfff << io_in_d_bits_size; // @[package.scala 189:77]
  wire [11:0] _T_927 = ~_T_925[11:0]; // @[package.scala 189:46]
  reg [8:0] _T_931; // @[Edges.scala 230:27]
  wire [8:0] _T_933 = _T_931 - 9'h1; // @[Edges.scala 231:28]
  wire  _T_934 = _T_931 == 9'h0; // @[Edges.scala 232:25]
  reg [2:0] _T_942; // @[Monitor.scala 532:22]
  reg [1:0] _T_943; // @[Monitor.scala 533:22]
  reg [3:0] _T_944; // @[Monitor.scala 534:22]
  reg  _T_945; // @[Monitor.scala 535:22]
  reg [2:0] _T_946; // @[Monitor.scala 536:22]
  reg  _T_947; // @[Monitor.scala 537:22]
  wire  _T_949 = io_in_d_valid & ~_T_934; // @[Monitor.scala 538:19]
  wire  _T_950 = io_in_d_bits_opcode == _T_942; // @[Monitor.scala 539:29]
  wire  _T_954 = io_in_d_bits_param == _T_943; // @[Monitor.scala 540:29]
  wire  _T_958 = io_in_d_bits_size == _T_944; // @[Monitor.scala 541:29]
  wire  _T_962 = io_in_d_bits_source == _T_945; // @[Monitor.scala 542:29]
  wire  _T_966 = io_in_d_bits_sink == _T_946; // @[Monitor.scala 543:29]
  wire  _T_970 = io_in_d_bits_denied == _T_947; // @[Monitor.scala 544:29]
  reg  inflight; // @[Monitor.scala 608:27]
  reg [3:0] inflight_opcodes; // @[Monitor.scala 610:35]
  reg [7:0] inflight_sizes; // @[Monitor.scala 612:33]
  reg [8:0] _T_985; // @[Edges.scala 230:27]
  wire [8:0] _T_987 = _T_985 - 9'h1; // @[Edges.scala 231:28]
  wire  a_first = _T_985 == 9'h0; // @[Edges.scala 232:25]
  reg [8:0] _T_1003; // @[Edges.scala 230:27]
  wire [8:0] _T_1005 = _T_1003 - 9'h1; // @[Edges.scala 231:28]
  wire  d_first = _T_1003 == 9'h0; // @[Edges.scala 232:25]
  wire [2:0] _GEN_57 = {io_in_d_bits_source, 2'h0}; // @[Monitor.scala 629:69]
  wire [3:0] _T_1013 = {{1'd0}, _GEN_57}; // @[Monitor.scala 629:69]
  wire [3:0] _T_1014 = inflight_opcodes >> _T_1013; // @[Monitor.scala 629:44]
  wire [15:0] _T_1018 = 16'h10 - 16'h1; // @[Monitor.scala 606:57]
  wire [15:0] _GEN_58 = {{12'd0}, _T_1014}; // @[Monitor.scala 629:97]
  wire [15:0] _T_1019 = _GEN_58 & _T_1018; // @[Monitor.scala 629:97]
  wire [15:0] _T_1020 = {{1'd0}, _T_1019[15:1]}; // @[Monitor.scala 629:152]
  wire [3:0] _T_1021 = {io_in_d_bits_source, 3'h0}; // @[Monitor.scala 633:65]
  wire [7:0] _T_1022 = inflight_sizes >> _T_1021; // @[Monitor.scala 633:40]
  wire [15:0] _T_1026 = 16'h100 - 16'h1; // @[Monitor.scala 606:57]
  wire [15:0] _GEN_60 = {{8'd0}, _T_1022}; // @[Monitor.scala 633:91]
  wire [15:0] _T_1027 = _GEN_60 & _T_1026; // @[Monitor.scala 633:91]
  wire [15:0] _T_1028 = {{1'd0}, _T_1027[15:1]}; // @[Monitor.scala 633:144]
  wire  _T_1032 = _T_874 & a_first; // @[Monitor.scala 643:27]
  wire [1:0] _T_1034 = 2'h1 << io_in_a_bits_source; // @[OneHot.scala 58:35]
  wire [3:0] _T_1035 = {io_in_a_bits_opcode, 1'h0}; // @[Monitor.scala 645:53]
  wire [3:0] _T_1036 = _T_1035 | 4'h1; // @[Monitor.scala 645:61]
  wire [4:0] _T_1037 = {io_in_a_bits_size, 1'h0}; // @[Monitor.scala 646:49]
  wire [4:0] _T_1038 = _T_1037 | 5'h1; // @[Monitor.scala 646:57]
  wire [2:0] _GEN_62 = {io_in_a_bits_source, 2'h0}; // @[Monitor.scala 647:72]
  wire [3:0] _T_1039 = {{1'd0}, _GEN_62}; // @[Monitor.scala 647:72]
  wire [3:0] a_opcodes_set_interm = _T_874 & a_first ? _T_1036 : 4'h0; // @[Monitor.scala 643:72 Monitor.scala 645:28]
  wire [18:0] _GEN_63 = {{15'd0}, a_opcodes_set_interm}; // @[Monitor.scala 647:47]
  wire [18:0] _T_1040 = _GEN_63 << _T_1039; // @[Monitor.scala 647:47]
  wire [3:0] _T_1041 = {io_in_a_bits_source, 3'h0}; // @[Monitor.scala 648:68]
  wire [4:0] a_sizes_set_interm = _T_874 & a_first ? _T_1038 : 5'h0; // @[Monitor.scala 643:72 Monitor.scala 646:26]
  wire [19:0] _GEN_64 = {{15'd0}, a_sizes_set_interm}; // @[Monitor.scala 648:43]
  wire [19:0] _T_1042 = _GEN_64 << _T_1041; // @[Monitor.scala 648:43]
  wire  _T_1045 = ~(inflight >> io_in_a_bits_source); // @[Monitor.scala 649:17]
  wire [1:0] _GEN_15 = _T_874 & a_first ? _T_1034 : 2'h0; // @[Monitor.scala 643:72 Monitor.scala 644:13]
  wire [18:0] _GEN_18 = _T_874 & a_first ? _T_1040 : 19'h0; // @[Monitor.scala 643:72 Monitor.scala 647:21]
  wire [19:0] _GEN_19 = _T_874 & a_first ? _T_1042 : 20'h0; // @[Monitor.scala 643:72 Monitor.scala 648:19]
  wire  _T_1053 = ~_T_730; // @[Monitor.scala 660:75]
  wire [1:0] _T_1055 = 2'h1 << io_in_d_bits_source; // @[OneHot.scala 58:35]
  wire [30:0] _GEN_66 = {{15'd0}, _T_1018}; // @[Monitor.scala 662:76]
  wire [30:0] _T_1061 = _GEN_66 << _T_1013; // @[Monitor.scala 662:76]
  wire [30:0] _GEN_67 = {{15'd0}, _T_1026}; // @[Monitor.scala 663:72]
  wire [30:0] _T_1067 = _GEN_67 << _T_1021; // @[Monitor.scala 663:72]
  wire [1:0] _GEN_20 = _T_923 & d_first & ~_T_730 ? _T_1055 : 2'h0; // @[Monitor.scala 660:91 Monitor.scala 661:13]
  wire [30:0] _GEN_21 = _T_923 & d_first & ~_T_730 ? _T_1061 : 31'h0; // @[Monitor.scala 660:91 Monitor.scala 662:21]
  wire [30:0] _GEN_22 = _T_923 & d_first & ~_T_730 ? _T_1067 : 31'h0; // @[Monitor.scala 660:91 Monitor.scala 663:19]
  wire  _T_1068 = io_in_d_valid & d_first; // @[Monitor.scala 665:26]
  wire  _T_1071 = io_in_d_valid & d_first & _T_1053; // @[Monitor.scala 665:71]
  wire  _T_1074 = io_in_a_bits_source == io_in_d_bits_source; // @[Monitor.scala 666:93]
  wire  _T_1076 = io_in_a_bits_size == io_in_d_bits_size; // @[Monitor.scala 666:142]
  wire  _T_1079 = inflight >> io_in_d_bits_source | io_in_a_valid & io_in_a_bits_source == io_in_d_bits_source &
    io_in_a_bits_size == io_in_d_bits_size & a_first; // @[Monitor.scala 666:49]
  wire [3:0] a_opcode_lookup = _T_1020[3:0];
  wire [2:0] _GEN_25 = 3'h2 == a_opcode_lookup[2:0] ? 3'h1 : 3'h0; // @[Monitor.scala 667:37 Monitor.scala 667:37]
  wire [2:0] _GEN_26 = 3'h3 == a_opcode_lookup[2:0] ? 3'h1 : _GEN_25; // @[Monitor.scala 667:37 Monitor.scala 667:37]
  wire [2:0] _GEN_27 = 3'h4 == a_opcode_lookup[2:0] ? 3'h1 : _GEN_26; // @[Monitor.scala 667:37 Monitor.scala 667:37]
  wire [2:0] _GEN_28 = 3'h5 == a_opcode_lookup[2:0] ? 3'h2 : _GEN_27; // @[Monitor.scala 667:37 Monitor.scala 667:37]
  wire [2:0] _GEN_29 = 3'h6 == a_opcode_lookup[2:0] ? 3'h4 : _GEN_28; // @[Monitor.scala 667:37 Monitor.scala 667:37]
  wire [2:0] _GEN_30 = 3'h7 == a_opcode_lookup[2:0] ? 3'h4 : _GEN_29; // @[Monitor.scala 667:37 Monitor.scala 667:37]
  wire [2:0] _GEN_37 = 3'h6 == a_opcode_lookup[2:0] ? 3'h5 : _GEN_28; // @[Monitor.scala 667:96 Monitor.scala 667:96]
  wire [2:0] _GEN_38 = 3'h7 == a_opcode_lookup[2:0] ? 3'h4 : _GEN_37; // @[Monitor.scala 667:96 Monitor.scala 667:96]
  wire  _T_1087 = io_in_d_bits_opcode == _GEN_30 | io_in_d_bits_opcode == _GEN_38; // @[Monitor.scala 667:71]
  wire [2:0] _GEN_41 = 3'h2 == io_in_a_bits_opcode ? 3'h1 : 3'h0; // @[Monitor.scala 668:60 Monitor.scala 668:60]
  wire [2:0] _GEN_42 = 3'h3 == io_in_a_bits_opcode ? 3'h1 : _GEN_41; // @[Monitor.scala 668:60 Monitor.scala 668:60]
  wire [2:0] _GEN_43 = 3'h4 == io_in_a_bits_opcode ? 3'h1 : _GEN_42; // @[Monitor.scala 668:60 Monitor.scala 668:60]
  wire [2:0] _GEN_44 = 3'h5 == io_in_a_bits_opcode ? 3'h2 : _GEN_43; // @[Monitor.scala 668:60 Monitor.scala 668:60]
  wire [2:0] _GEN_45 = 3'h6 == io_in_a_bits_opcode ? 3'h4 : _GEN_44; // @[Monitor.scala 668:60 Monitor.scala 668:60]
  wire [2:0] _GEN_46 = 3'h7 == io_in_a_bits_opcode ? 3'h4 : _GEN_45; // @[Monitor.scala 668:60 Monitor.scala 668:60]
  wire [2:0] _GEN_53 = 3'h6 == io_in_a_bits_opcode ? 3'h5 : _GEN_44; // @[Monitor.scala 668:124 Monitor.scala 668:124]
  wire [2:0] _GEN_54 = 3'h7 == io_in_a_bits_opcode ? 3'h4 : _GEN_53; // @[Monitor.scala 668:124 Monitor.scala 668:124]
  wire  _T_1092 = _T_1087 | io_in_a_valid & (io_in_d_bits_opcode == _GEN_46 | io_in_d_bits_opcode == _GEN_54); // @[Monitor.scala 668:15]
  wire [7:0] a_size_lookup = _T_1028[7:0];
  wire [7:0] _GEN_68 = {{4'd0}, io_in_d_bits_size}; // @[Monitor.scala 670:34]
  wire  _T_1099 = _GEN_68 == a_size_lookup | io_in_a_valid & _T_1076; // @[Monitor.scala 670:53]
  wire  _T_1109 = _T_1068 & a_first & io_in_a_valid & _T_1074 & _T_1053; // @[Monitor.scala 672:116]
  wire  _T_1111 = ~io_in_d_ready | io_in_a_ready; // @[Monitor.scala 673:32]
  wire  a_set = _GEN_15[0];
  wire  d_clr = _GEN_20[0];
  wire  _T_1118 = a_set != d_clr | ~(|a_set); // @[Monitor.scala 677:30]
  wire [3:0] a_opcodes_set = _GEN_18[3:0];
  wire [3:0] _T_1125 = inflight_opcodes | a_opcodes_set; // @[Monitor.scala 681:43]
  wire [3:0] d_opcodes_clr = _GEN_21[3:0];
  wire [3:0] _T_1126 = ~d_opcodes_clr; // @[Monitor.scala 681:62]
  wire [3:0] _T_1127 = _T_1125 & _T_1126; // @[Monitor.scala 681:60]
  wire [7:0] a_sizes_set = _GEN_19[7:0];
  wire [7:0] _T_1128 = inflight_sizes | a_sizes_set; // @[Monitor.scala 682:39]
  wire [7:0] d_sizes_clr = _GEN_22[7:0];
  wire [7:0] _T_1129 = ~d_sizes_clr; // @[Monitor.scala 682:56]
  wire [7:0] _T_1130 = _T_1128 & _T_1129; // @[Monitor.scala 682:54]
  reg [31:0] _T_1131; // @[Monitor.scala 684:27]
  wire  _T_1137 = ~(|inflight) | plusarg_reader_out == 32'h0 | _T_1131 < plusarg_reader_out; // @[Monitor.scala 687:47]
  wire [31:0] _T_1142 = _T_1131 + 32'h1; // @[Monitor.scala 689:26]
  plusarg_reader #(.FORMAT("tilelink_timeout=%d"), .DEFAULT(0), .WIDTH(32)) plusarg_reader ( // @[PlusArg.scala 44:11]
    .out(plusarg_reader_out)
  );
  always @(posedge clock) begin
    if (reset) begin // @[Edges.scala 230:27]
      _T_883 <= 9'h0; // @[Edges.scala 230:27]
    end else if (_T_874) begin // @[Edges.scala 236:17]
      if (_T_886) begin // @[Edges.scala 237:21]
        if (_T_881) begin // @[Edges.scala 222:14]
          _T_883 <= _T_9[11:3];
        end else begin
          _T_883 <= 9'h0;
        end
      end else begin
        _T_883 <= _T_885;
      end
    end
    if (_T_874 & _T_886) begin // @[Monitor.scala 393:32]
      _T_894 <= io_in_a_bits_opcode; // @[Monitor.scala 394:15]
    end
    if (_T_874 & _T_886) begin // @[Monitor.scala 393:32]
      _T_895 <= io_in_a_bits_param; // @[Monitor.scala 395:15]
    end
    if (_T_874 & _T_886) begin // @[Monitor.scala 393:32]
      _T_896 <= io_in_a_bits_size; // @[Monitor.scala 396:15]
    end
    if (_T_874 & _T_886) begin // @[Monitor.scala 393:32]
      _T_897 <= io_in_a_bits_source; // @[Monitor.scala 397:15]
    end
    if (_T_874 & _T_886) begin // @[Monitor.scala 393:32]
      _T_898 <= io_in_a_bits_address; // @[Monitor.scala 398:15]
    end
    if (reset) begin // @[Edges.scala 230:27]
      _T_931 <= 9'h0; // @[Edges.scala 230:27]
    end else if (_T_923) begin // @[Edges.scala 236:17]
      if (_T_934) begin // @[Edges.scala 237:21]
        if (io_in_d_bits_opcode[0]) begin // @[Edges.scala 222:14]
          _T_931 <= _T_927[11:3];
        end else begin
          _T_931 <= 9'h0;
        end
      end else begin
        _T_931 <= _T_933;
      end
    end
    if (_T_923 & _T_934) begin // @[Monitor.scala 546:32]
      _T_942 <= io_in_d_bits_opcode; // @[Monitor.scala 547:15]
    end
    if (_T_923 & _T_934) begin // @[Monitor.scala 546:32]
      _T_943 <= io_in_d_bits_param; // @[Monitor.scala 548:15]
    end
    if (_T_923 & _T_934) begin // @[Monitor.scala 546:32]
      _T_944 <= io_in_d_bits_size; // @[Monitor.scala 549:15]
    end
    if (_T_923 & _T_934) begin // @[Monitor.scala 546:32]
      _T_945 <= io_in_d_bits_source; // @[Monitor.scala 550:15]
    end
    if (_T_923 & _T_934) begin // @[Monitor.scala 546:32]
      _T_946 <= io_in_d_bits_sink; // @[Monitor.scala 551:15]
    end
    if (_T_923 & _T_934) begin // @[Monitor.scala 546:32]
      _T_947 <= io_in_d_bits_denied; // @[Monitor.scala 552:15]
    end
    if (reset) begin // @[Monitor.scala 608:27]
      inflight <= 1'h0; // @[Monitor.scala 608:27]
    end else begin
      inflight <= (inflight | a_set) & ~d_clr; // @[Monitor.scala 680:14]
    end
    if (reset) begin // @[Monitor.scala 610:35]
      inflight_opcodes <= 4'h0; // @[Monitor.scala 610:35]
    end else begin
      inflight_opcodes <= _T_1127; // @[Monitor.scala 681:22]
    end
    if (reset) begin // @[Monitor.scala 612:33]
      inflight_sizes <= 8'h0; // @[Monitor.scala 612:33]
    end else begin
      inflight_sizes <= _T_1130; // @[Monitor.scala 682:20]
    end
    if (reset) begin // @[Edges.scala 230:27]
      _T_985 <= 9'h0; // @[Edges.scala 230:27]
    end else if (_T_874) begin // @[Edges.scala 236:17]
      if (a_first) begin // @[Edges.scala 237:21]
        if (_T_881) begin // @[Edges.scala 222:14]
          _T_985 <= _T_9[11:3];
        end else begin
          _T_985 <= 9'h0;
        end
      end else begin
        _T_985 <= _T_987;
      end
    end
    if (reset) begin // @[Edges.scala 230:27]
      _T_1003 <= 9'h0; // @[Edges.scala 230:27]
    end else if (_T_923) begin // @[Edges.scala 236:17]
      if (d_first) begin // @[Edges.scala 237:21]
        if (io_in_d_bits_opcode[0]) begin // @[Edges.scala 222:14]
          _T_1003 <= _T_927[11:3];
        end else begin
          _T_1003 <= 9'h0;
        end
      end else begin
        _T_1003 <= _T_1005;
      end
    end
    if (reset) begin // @[Monitor.scala 684:27]
      _T_1131 <= 32'h0; // @[Monitor.scala 684:27]
    end else if (_T_874 | _T_923) begin // @[Monitor.scala 690:47]
      _T_1131 <= 32'h0; // @[Monitor.scala 690:58]
    end else begin
      _T_1131 <= _T_1142; // @[Monitor.scala 689:14]
    end
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~(_T_138 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel carries AcquireBlock type unsupported by manager (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~(_T_138 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~reset) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel carries AcquireBlock from a client which does not support Probe (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~reset) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~(_T_4 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel AcquireBlock carries invalid source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~(_T_4 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~(_T_17 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel AcquireBlock smaller than a beat (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~(_T_17 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~(_T_11 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel AcquireBlock address not aligned to size (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~(_T_11 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~(_T_157 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel AcquireBlock carries invalid grow param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~(_T_157 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~(_T_162 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel AcquireBlock contains invalid mask (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~(_T_162 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~(_T_166 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel AcquireBlock is corrupt (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_87 & ~(_T_166 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_138 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel carries AcquirePerm type unsupported by manager (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_138 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~reset) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel carries AcquirePerm from a client which does not support Probe (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~reset) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_4 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel AcquirePerm carries invalid source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_4 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_17 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel AcquirePerm smaller than a beat (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_17 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_11 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel AcquirePerm address not aligned to size (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_11 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_157 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel AcquirePerm carries invalid grow param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_157 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_244 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel AcquirePerm requests NtoB (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_244 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_162 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel AcquirePerm contains invalid mask (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_162 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_166 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel AcquirePerm is corrupt (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_170 & ~(_T_166 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_257 & ~(_T_315 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel carries Get type unsupported by manager (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_257 & ~(_T_315 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_257 & ~(_T_4 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Get carries invalid source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_257 & ~(_T_4 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_257 & ~(_T_11 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Get address not aligned to size (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_257 & ~(_T_11 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_257 & ~(_T_325 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Get carries invalid param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_257 & ~(_T_325 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_257 & ~(_T_329 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Get contains invalid mask (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_257 & ~(_T_329 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_257 & ~(_T_166 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Get is corrupt (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_257 & ~(_T_166 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_337 & ~(_T_396 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel carries PutFull type unsupported by manager (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_337 & ~(_T_396 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_337 & ~(_T_4 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel PutFull carries invalid source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_337 & ~(_T_4 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_337 & ~(_T_11 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel PutFull address not aligned to size (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_337 & ~(_T_11 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_337 & ~(_T_325 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel PutFull carries invalid param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_337 & ~(_T_325 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_337 & ~(_T_329 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel PutFull contains invalid mask (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_337 & ~(_T_329 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_415 & ~(_T_396 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel carries PutPartial type unsupported by manager (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_415 & ~(_T_396 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_415 & ~(_T_4 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel PutPartial carries invalid source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_415 & ~(_T_4 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_415 & ~(_T_11 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel PutPartial address not aligned to size (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_415 & ~(_T_11 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_415 & ~(_T_325 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel PutPartial carries invalid param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_415 & ~(_T_325 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_415 & ~(_T_491 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel PutPartial contains invalid mask (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_415 & ~(_T_491 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_495 & ~(_T_541 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel carries Arithmetic type unsupported by manager (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_495 & ~(_T_541 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_495 & ~(_T_4 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Arithmetic carries invalid source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_495 & ~(_T_4 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_495 & ~(_T_11 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Arithmetic address not aligned to size (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_495 & ~(_T_11 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_495 & ~(_T_560 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Arithmetic carries invalid opcode param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_495 & ~(_T_560 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_495 & ~(_T_329 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Arithmetic contains invalid mask (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_495 & ~(_T_329 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_568 & ~(_T_541 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel carries Logical type unsupported by manager (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_568 & ~(_T_541 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_568 & ~(_T_4 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Logical carries invalid source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_568 & ~(_T_4 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_568 & ~(_T_11 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Logical address not aligned to size (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_568 & ~(_T_11 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_568 & ~(_T_633 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Logical carries invalid opcode param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_568 & ~(_T_633 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_568 & ~(_T_329 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Logical contains invalid mask (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_568 & ~(_T_329 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_641 & ~(_T_701 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel carries Hint type unsupported by manager (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_641 & ~(_T_701 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_641 & ~(_T_4 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Hint carries invalid source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_641 & ~(_T_4 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_641 & ~(_T_11 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Hint address not aligned to size (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_641 & ~(_T_11 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_641 & ~(_T_711 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Hint carries invalid opcode param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_641 & ~(_T_711 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_641 & ~(_T_329 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Hint contains invalid mask (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_641 & ~(_T_329 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_a_valid & _T_641 & ~(_T_166 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel Hint is corrupt (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_a_valid & _T_641 & ~(_T_166 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & ~(_T_723 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel has invalid opcode (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & ~(_T_723 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_730 & ~(_T_727 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel ReleaseAck carries invalid source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_730 & ~(_T_727 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_730 & ~(_T_734 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel ReleaseAck smaller than a beat (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_730 & ~(_T_734 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_730 & ~(_T_738 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel ReleaseeAck carries invalid param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_730 & ~(_T_738 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_730 & ~(_T_742 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel ReleaseAck is corrupt (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_730 & ~(_T_742 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_730 & ~(_T_746 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel ReleaseAck is denied (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_730 & ~(_T_746 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_750 & ~(_T_727 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel Grant carries invalid source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_750 & ~(_T_727 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_750 & ~(_T_734 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel Grant smaller than a beat (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_750 & ~(_T_734 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_750 & ~(_T_761 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel Grant carries invalid cap param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_750 & ~(_T_761 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_750 & ~(_T_765 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel Grant carries toN param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_750 & ~(_T_765 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_750 & ~(_T_742 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel Grant is corrupt (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_750 & ~(_T_742 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_778 & ~(_T_727 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel GrantData carries invalid source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_778 & ~(_T_727 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_778 & ~(_T_734 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel GrantData smaller than a beat (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_778 & ~(_T_734 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_778 & ~(_T_761 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel GrantData carries invalid cap param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_778 & ~(_T_761 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_778 & ~(_T_765 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel GrantData carries toN param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_778 & ~(_T_765 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_778 & ~(_T_798 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel GrantData is denied but not corrupt (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_778 & ~(_T_798 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_807 & ~(_T_727 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel AccessAck carries invalid source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_807 & ~(_T_727 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_807 & ~(_T_738 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel AccessAck carries invalid param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_807 & ~(_T_738 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_807 & ~(_T_742 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel AccessAck is corrupt (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_807 & ~(_T_742 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_824 & ~(_T_727 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel AccessAckData carries invalid source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_824 & ~(_T_727 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_824 & ~(_T_738 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel AccessAckData carries invalid param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_824 & ~(_T_738 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_824 & ~(_T_798 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel AccessAckData is denied but not corrupt (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_824 & ~(_T_798 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_842 & ~(_T_727 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel HintAck carries invalid source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_842 & ~(_T_727 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_842 & ~(_T_738 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel HintAck carries invalid param (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_842 & ~(_T_738 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_in_d_valid & _T_842 & ~(_T_742 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel HintAck is corrupt (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_in_d_valid & _T_842 & ~(_T_742 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_900 & ~(_T_901 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel opcode changed within multibeat operation (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_900 & ~(_T_901 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_900 & ~(_T_905 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel param changed within multibeat operation (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_900 & ~(_T_905 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_900 & ~(_T_909 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel size changed within multibeat operation (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_900 & ~(_T_909 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_900 & ~(_T_913 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel source changed within multibeat operation (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_900 & ~(_T_913 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_900 & ~(_T_917 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel address changed with multibeat operation (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_900 & ~(_T_917 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_949 & ~(_T_950 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel opcode changed within multibeat operation (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_949 & ~(_T_950 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_949 & ~(_T_954 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel param changed within multibeat operation (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_949 & ~(_T_954 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_949 & ~(_T_958 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel size changed within multibeat operation (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_949 & ~(_T_958 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_949 & ~(_T_962 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel source changed within multibeat operation (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_949 & ~(_T_962 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_949 & ~(_T_966 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel sink changed with multibeat operation (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_949 & ~(_T_966 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_949 & ~(_T_970 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel denied changed with multibeat operation (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_949 & ~(_T_970 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_1032 & ~(_T_1045 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' channel re-used a source ID (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_1032 & ~(_T_1045 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_1071 & ~(_T_1079 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel acknowledged for nothing inflight (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_1071 & ~(_T_1079 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_1071 & ~(_T_1092 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel contains improper opcode response (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_1071 & ~(_T_1092 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_1071 & ~(_T_1099 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'D' channel contains improper response size (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_1071 & ~(_T_1099 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_1109 & ~(_T_1111 | reset)) begin
          $fwrite(32'h80000002,"Assertion failed: ready check\n    at Monitor.scala:51 assert(cond, message)\n"); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_1109 & ~(_T_1111 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (~(_T_1118 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: 'A' and 'D' concurrent, despite minlatency 2 (connected at SystemBus.scala:39:55)\n    at Monitor.scala:51 assert(cond, message)\n"
            ); // @[Monitor.scala 51:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (~(_T_1118 | reset)) begin
          $fatal; // @[Monitor.scala 51:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (~(_T_1137 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: TileLink timeout expired (connected at SystemBus.scala:39:55)\n    at Monitor.scala:44 assert(cond, message)\n"
            ); // @[Monitor.scala 44:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (~(_T_1137 | reset)) begin
          $fatal; // @[Monitor.scala 44:11]
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
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  _T_883 = _RAND_0[8:0];
  _RAND_1 = {1{`RANDOM}};
  _T_894 = _RAND_1[2:0];
  _RAND_2 = {1{`RANDOM}};
  _T_895 = _RAND_2[2:0];
  _RAND_3 = {1{`RANDOM}};
  _T_896 = _RAND_3[3:0];
  _RAND_4 = {1{`RANDOM}};
  _T_897 = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  _T_898 = _RAND_5[31:0];
  _RAND_6 = {1{`RANDOM}};
  _T_931 = _RAND_6[8:0];
  _RAND_7 = {1{`RANDOM}};
  _T_942 = _RAND_7[2:0];
  _RAND_8 = {1{`RANDOM}};
  _T_943 = _RAND_8[1:0];
  _RAND_9 = {1{`RANDOM}};
  _T_944 = _RAND_9[3:0];
  _RAND_10 = {1{`RANDOM}};
  _T_945 = _RAND_10[0:0];
  _RAND_11 = {1{`RANDOM}};
  _T_946 = _RAND_11[2:0];
  _RAND_12 = {1{`RANDOM}};
  _T_947 = _RAND_12[0:0];
  _RAND_13 = {1{`RANDOM}};
  inflight = _RAND_13[0:0];
  _RAND_14 = {1{`RANDOM}};
  inflight_opcodes = _RAND_14[3:0];
  _RAND_15 = {1{`RANDOM}};
  inflight_sizes = _RAND_15[7:0];
  _RAND_16 = {1{`RANDOM}};
  _T_985 = _RAND_16[8:0];
  _RAND_17 = {1{`RANDOM}};
  _T_1003 = _RAND_17[8:0];
  _RAND_18 = {1{`RANDOM}};
  _T_1131 = _RAND_18[31:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

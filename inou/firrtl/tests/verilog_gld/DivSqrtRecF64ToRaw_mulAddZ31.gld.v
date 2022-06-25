module DivSqrtRecF64ToRaw_mulAddZ31(
  input          clock,
  input          reset,
  output         io_inReady_div,
  output         io_inReady_sqrt,
  input          io_inValid,
  input          io_sqrtOp,
  input  [64:0]  io_a,
  input  [64:0]  io_b,
  input  [2:0]   io_roundingMode,
  output [3:0]   io_usingMulAdd,
  output         io_latchMulAddA_0,
  output [53:0]  io_mulAddA_0,
  output         io_latchMulAddB_0,
  output [53:0]  io_mulAddB_0,
  output [104:0] io_mulAddC_2,
  input  [104:0] io_mulAddResult_3,
  output         io_rawOutValid_div,
  output         io_rawOutValid_sqrt,
  output [2:0]   io_roundingModeOut,
  output         io_invalidExc,
  output         io_infiniteExc,
  output         io_rawOut_isNaN,
  output         io_rawOut_isInf,
  output         io_rawOut_isZero,
  output         io_rawOut_sign,
  output [12:0]  io_rawOut_sExp,
  output [55:0]  io_rawOut_sig
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
  reg [63:0] _RAND_12;
  reg [63:0] _RAND_13;
  reg [31:0] _RAND_14;
  reg [31:0] _RAND_15;
  reg [31:0] _RAND_16;
  reg [31:0] _RAND_17;
  reg [31:0] _RAND_18;
  reg [31:0] _RAND_19;
  reg [31:0] _RAND_20;
  reg [31:0] _RAND_21;
  reg [31:0] _RAND_22;
  reg [31:0] _RAND_23;
  reg [63:0] _RAND_24;
  reg [31:0] _RAND_25;
  reg [31:0] _RAND_26;
  reg [31:0] _RAND_27;
  reg [31:0] _RAND_28;
  reg [31:0] _RAND_29;
  reg [31:0] _RAND_30;
  reg [31:0] _RAND_31;
  reg [31:0] _RAND_32;
  reg [31:0] _RAND_33;
  reg [31:0] _RAND_34;
  reg [63:0] _RAND_35;
  reg [31:0] _RAND_36;
  reg [31:0] _RAND_37;
  reg [31:0] _RAND_38;
  reg [31:0] _RAND_39;
  reg [31:0] _RAND_40;
  reg [31:0] _RAND_41;
  reg [31:0] _RAND_42;
  reg [31:0] _RAND_43;
  reg [63:0] _RAND_44;
  reg [63:0] _RAND_45;
  reg [63:0] _RAND_46;
  reg [31:0] _RAND_47;
  reg [31:0] _RAND_48;
  reg [63:0] _RAND_49;
  reg [31:0] _RAND_50;
  reg [31:0] _RAND_51;
`endif // RANDOMIZE_REG_INIT
  reg [2:0] cycleNum_A; // @[DivSqrtRecF64_mulAddZ31.scala 85:30]
  reg [3:0] cycleNum_B; // @[DivSqrtRecF64_mulAddZ31.scala 86:30]
  reg [2:0] cycleNum_C; // @[DivSqrtRecF64_mulAddZ31.scala 87:30]
  reg [2:0] cycleNum_E; // @[DivSqrtRecF64_mulAddZ31.scala 88:30]
  reg  valid_PA; // @[DivSqrtRecF64_mulAddZ31.scala 90:30]
  reg  sqrtOp_PA; // @[DivSqrtRecF64_mulAddZ31.scala 91:30]
  reg  majorExc_PA; // @[DivSqrtRecF64_mulAddZ31.scala 92:30]
  reg  isNaN_PA; // @[DivSqrtRecF64_mulAddZ31.scala 94:30]
  reg  isInf_PA; // @[DivSqrtRecF64_mulAddZ31.scala 95:30]
  reg  isZero_PA; // @[DivSqrtRecF64_mulAddZ31.scala 96:30]
  reg  sign_PA; // @[DivSqrtRecF64_mulAddZ31.scala 97:30]
  reg [12:0] sExp_PA; // @[DivSqrtRecF64_mulAddZ31.scala 98:30]
  reg [51:0] fractB_PA; // @[DivSqrtRecF64_mulAddZ31.scala 99:30]
  reg [51:0] fractA_PA; // @[DivSqrtRecF64_mulAddZ31.scala 100:30]
  reg [2:0] roundingMode_PA; // @[DivSqrtRecF64_mulAddZ31.scala 101:30]
  reg  valid_PB; // @[DivSqrtRecF64_mulAddZ31.scala 103:30]
  reg  sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 104:30]
  reg  majorExc_PB; // @[DivSqrtRecF64_mulAddZ31.scala 105:30]
  reg  isNaN_PB; // @[DivSqrtRecF64_mulAddZ31.scala 107:30]
  reg  isInf_PB; // @[DivSqrtRecF64_mulAddZ31.scala 108:30]
  reg  isZero_PB; // @[DivSqrtRecF64_mulAddZ31.scala 109:30]
  reg  sign_PB; // @[DivSqrtRecF64_mulAddZ31.scala 110:30]
  reg [12:0] sExp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 111:30]
  reg  bit0FractA_PB; // @[DivSqrtRecF64_mulAddZ31.scala 112:30]
  reg [51:0] fractB_PB; // @[DivSqrtRecF64_mulAddZ31.scala 113:30]
  reg [2:0] roundingMode_PB; // @[DivSqrtRecF64_mulAddZ31.scala 114:30]
  reg  valid_PC; // @[DivSqrtRecF64_mulAddZ31.scala 116:30]
  reg  sqrtOp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 117:30]
  reg  majorExc_PC; // @[DivSqrtRecF64_mulAddZ31.scala 118:30]
  reg  isNaN_PC; // @[DivSqrtRecF64_mulAddZ31.scala 120:30]
  reg  isInf_PC; // @[DivSqrtRecF64_mulAddZ31.scala 121:30]
  reg  isZero_PC; // @[DivSqrtRecF64_mulAddZ31.scala 122:30]
  reg  sign_PC; // @[DivSqrtRecF64_mulAddZ31.scala 123:30]
  reg [12:0] sExp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 124:30]
  reg  bit0FractA_PC; // @[DivSqrtRecF64_mulAddZ31.scala 125:30]
  reg [51:0] fractB_PC; // @[DivSqrtRecF64_mulAddZ31.scala 126:30]
  reg [2:0] roundingMode_PC; // @[DivSqrtRecF64_mulAddZ31.scala 127:30]
  reg [8:0] fractR0_A; // @[DivSqrtRecF64_mulAddZ31.scala 129:30]
  reg [9:0] hiSqrR0_A_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 131:30]
  reg [20:0] partNegSigma0_A; // @[DivSqrtRecF64_mulAddZ31.scala 132:30]
  reg [8:0] nextMulAdd9A_A; // @[DivSqrtRecF64_mulAddZ31.scala 133:30]
  reg [8:0] nextMulAdd9B_A; // @[DivSqrtRecF64_mulAddZ31.scala 134:30]
  reg [16:0] ER1_B_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 135:30]
  reg [31:0] ESqrR1_B_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 137:30]
  reg [57:0] sigX1_B; // @[DivSqrtRecF64_mulAddZ31.scala 138:30]
  reg [32:0] sqrSigma1_C; // @[DivSqrtRecF64_mulAddZ31.scala 139:30]
  reg [57:0] sigXN_C; // @[DivSqrtRecF64_mulAddZ31.scala 140:30]
  reg [30:0] u_C_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 141:30]
  reg  E_E_div; // @[DivSqrtRecF64_mulAddZ31.scala 142:30]
  reg [53:0] sigT_E; // @[DivSqrtRecF64_mulAddZ31.scala 143:30]
  reg  isNegRemT_E; // @[DivSqrtRecF64_mulAddZ31.scala 144:30]
  reg  isZeroRemT_E; // @[DivSqrtRecF64_mulAddZ31.scala 145:30]
  wire  _T_1 = ~io_sqrtOp; // @[DivSqrtRecF64_mulAddZ31.scala 163:55]
  wire  cyc_S_div = io_inReady_div & io_inValid & ~io_sqrtOp; // @[DivSqrtRecF64_mulAddZ31.scala 163:52]
  wire  cyc_S_sqrt = io_inReady_sqrt & io_inValid & io_sqrtOp; // @[DivSqrtRecF64_mulAddZ31.scala 164:52]
  wire  cyc_S = cyc_S_div | cyc_S_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 165:27]
  wire  rawA_S_isZero = io_a[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_7 = io_a[63:62] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  rawA_S_isNaN = _T_7 & io_a[61]; // @[rawFloatFromRecFN.scala 55:33]
  wire  rawA_S_isInf = _T_7 & ~io_a[61]; // @[rawFloatFromRecFN.scala 56:33]
  wire  rawA_S_sign = io_a[64]; // @[rawFloatFromRecFN.scala 58:25]
  wire [12:0] rawA_S_sExp = {1'b0,$signed(io_a[63:52])}; // @[rawFloatFromRecFN.scala 59:27]
  wire  _T_15 = ~rawA_S_isZero; // @[rawFloatFromRecFN.scala 60:39]
  wire [53:0] rawA_S_sig = {1'h0,_T_15,io_a[51:0]}; // @[Cat.scala 29:58]
  wire  rawB_S_isZero = io_b[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_23 = io_b[63:62] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  rawB_S_isNaN = _T_23 & io_b[61]; // @[rawFloatFromRecFN.scala 55:33]
  wire  rawB_S_isInf = _T_23 & ~io_b[61]; // @[rawFloatFromRecFN.scala 56:33]
  wire  rawB_S_sign = io_b[64]; // @[rawFloatFromRecFN.scala 58:25]
  wire [12:0] rawB_S_sExp = {1'b0,$signed(io_b[63:52])}; // @[rawFloatFromRecFN.scala 59:27]
  wire  _T_31 = ~rawB_S_isZero; // @[rawFloatFromRecFN.scala 60:39]
  wire [53:0] rawB_S_sig = {1'h0,_T_31,io_b[51:0]}; // @[Cat.scala 29:58]
  wire  notSigNaNIn_invalidExc_S_div = rawA_S_isZero & rawB_S_isZero | rawA_S_isInf & rawB_S_isInf; // @[DivSqrtRecF64_mulAddZ31.scala 171:42]
  wire  notSigNaNIn_invalidExc_S_sqrt = ~rawB_S_isNaN & _T_31 & rawB_S_sign; // @[DivSqrtRecF64_mulAddZ31.scala 173:43]
  wire  _T_41 = ~rawB_S_sig[51]; // @[common.scala 81:49]
  wire  _T_42 = rawB_S_isNaN & ~rawB_S_sig[51]; // @[common.scala 81:46]
  wire  _T_43 = _T_42 | notSigNaNIn_invalidExc_S_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 176:38]
  wire  _T_46 = rawA_S_isNaN & ~rawA_S_sig[51]; // @[common.scala 81:46]
  wire  _T_51 = _T_46 | _T_42 | notSigNaNIn_invalidExc_S_div; // @[DivSqrtRecF64_mulAddZ31.scala 177:66]
  wire  _T_55 = ~rawA_S_isNaN & ~rawA_S_isInf & rawB_S_isZero; // @[DivSqrtRecF64_mulAddZ31.scala 179:51]
  wire  _T_56 = _T_51 | _T_55; // @[DivSqrtRecF64_mulAddZ31.scala 178:46]
  wire  _T_57 = rawB_S_isNaN | notSigNaNIn_invalidExc_S_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 183:26]
  wire  _T_59 = rawA_S_isNaN | rawB_S_isNaN | notSigNaNIn_invalidExc_S_div; // @[DivSqrtRecF64_mulAddZ31.scala 184:42]
  wire  sign_S = _T_1 & rawA_S_sign ^ rawB_S_sign; // @[DivSqrtRecF64_mulAddZ31.scala 188:47]
  wire  specialCaseA_S = rawA_S_isNaN | rawA_S_isInf | rawA_S_isZero; // @[DivSqrtRecF64_mulAddZ31.scala 190:55]
  wire  specialCaseB_S = rawB_S_isNaN | rawB_S_isInf | rawB_S_isZero; // @[DivSqrtRecF64_mulAddZ31.scala 191:55]
  wire  _T_67 = ~specialCaseB_S; // @[DivSqrtRecF64_mulAddZ31.scala 192:48]
  wire  normalCase_S_div = ~specialCaseA_S & ~specialCaseB_S; // @[DivSqrtRecF64_mulAddZ31.scala 192:45]
  wire  normalCase_S_sqrt = _T_67 & ~rawB_S_sign; // @[DivSqrtRecF64_mulAddZ31.scala 193:46]
  wire  normalCase_S = io_sqrtOp ? normalCase_S_sqrt : normalCase_S_div; // @[DivSqrtRecF64_mulAddZ31.scala 194:27]
  wire [10:0] _T_72 = ~rawB_S_sExp[10:0]; // @[DivSqrtRecF64_mulAddZ31.scala 197:45]
  wire [11:0] _T_74 = {rawB_S_sExp[11],_T_72}; // @[DivSqrtRecF64_mulAddZ31.scala 197:66]
  wire [12:0] _GEN_52 = {{1{_T_74[11]}},_T_74}; // @[DivSqrtRecF64_mulAddZ31.scala 197:21]
  wire [13:0] sExpQuot_S_div = $signed(rawA_S_sExp) + $signed(_GEN_52); // @[DivSqrtRecF64_mulAddZ31.scala 197:21]
  wire [3:0] _T_77 = 14'she00 <= $signed(sExpQuot_S_div) ? 4'h6 : sExpQuot_S_div[12:9]; // @[DivSqrtRecF64_mulAddZ31.scala 200:16]
  wire [12:0] sSatExpQuot_S_div = {_T_77,sExpQuot_S_div[8:0]}; // @[DivSqrtRecF64_mulAddZ31.scala 205:11]
  wire  cyc_A4_div = cyc_S_div & normalCase_S_div; // @[DivSqrtRecF64_mulAddZ31.scala 209:50]
  wire  cyc_A7_sqrt = cyc_S_sqrt & normalCase_S_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 210:50]
  wire  entering_PA_normalCase = cyc_A4_div | cyc_A7_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 212:36]
  wire [1:0] _T_82 = cyc_A4_div ? 2'h3 : 2'h0; // @[DivSqrtRecF64_mulAddZ31.scala 218:16]
  wire [2:0] _T_83 = cyc_A7_sqrt ? 3'h6 : 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 219:16]
  wire [2:0] _GEN_53 = {{1'd0}, _T_82}; // @[DivSqrtRecF64_mulAddZ31.scala 218:77]
  wire [2:0] _T_84 = _GEN_53 | _T_83; // @[DivSqrtRecF64_mulAddZ31.scala 218:77]
  wire [2:0] _T_87 = cycleNum_A - 3'h1; // @[DivSqrtRecF64_mulAddZ31.scala 220:57]
  wire [2:0] _T_88 = ~entering_PA_normalCase ? _T_87 : 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 220:16]
  wire [2:0] _T_89 = _T_84 | _T_88; // @[DivSqrtRecF64_mulAddZ31.scala 219:77]
  wire  cyc_A6_sqrt = cycleNum_A == 3'h6; // @[DivSqrtRecF64_mulAddZ31.scala 224:35]
  wire  cyc_A5_sqrt = cycleNum_A == 3'h5; // @[DivSqrtRecF64_mulAddZ31.scala 225:35]
  wire  cyc_A4_sqrt = cycleNum_A == 3'h4; // @[DivSqrtRecF64_mulAddZ31.scala 226:35]
  wire  cyc_A4 = cyc_A4_sqrt | cyc_A4_div; // @[DivSqrtRecF64_mulAddZ31.scala 230:30]
  wire  cyc_A3 = cycleNum_A == 3'h3; // @[DivSqrtRecF64_mulAddZ31.scala 231:30]
  wire  cyc_A2 = cycleNum_A == 3'h2; // @[DivSqrtRecF64_mulAddZ31.scala 232:30]
  wire  cyc_A1 = cycleNum_A == 3'h1; // @[DivSqrtRecF64_mulAddZ31.scala 233:30]
  wire  _T_90 = ~sqrtOp_PA; // @[DivSqrtRecF64_mulAddZ31.scala 235:32]
  wire  cyc_A3_div = cyc_A3 & ~sqrtOp_PA; // @[DivSqrtRecF64_mulAddZ31.scala 235:29]
  wire  cyc_A2_div = cyc_A2 & _T_90; // @[DivSqrtRecF64_mulAddZ31.scala 236:29]
  wire  cyc_A1_div = cyc_A1 & _T_90; // @[DivSqrtRecF64_mulAddZ31.scala 237:29]
  wire  cyc_A3_sqrt = cyc_A3 & sqrtOp_PA; // @[DivSqrtRecF64_mulAddZ31.scala 239:30]
  wire  cyc_A1_sqrt = cyc_A1 & sqrtOp_PA; // @[DivSqrtRecF64_mulAddZ31.scala 241:30]
  wire [3:0] _T_97 = cycleNum_B - 4'h1; // @[DivSqrtRecF64_mulAddZ31.scala 247:28]
  wire  cyc_B10_sqrt = cycleNum_B == 4'ha; // @[DivSqrtRecF64_mulAddZ31.scala 251:36]
  wire  cyc_B9_sqrt = cycleNum_B == 4'h9; // @[DivSqrtRecF64_mulAddZ31.scala 252:36]
  wire  cyc_B8_sqrt = cycleNum_B == 4'h8; // @[DivSqrtRecF64_mulAddZ31.scala 253:36]
  wire  cyc_B7_sqrt = cycleNum_B == 4'h7; // @[DivSqrtRecF64_mulAddZ31.scala 254:36]
  wire  cyc_B6 = cycleNum_B == 4'h6; // @[DivSqrtRecF64_mulAddZ31.scala 256:30]
  wire  cyc_B5 = cycleNum_B == 4'h5; // @[DivSqrtRecF64_mulAddZ31.scala 257:30]
  wire  cyc_B4 = cycleNum_B == 4'h4; // @[DivSqrtRecF64_mulAddZ31.scala 258:30]
  wire  cyc_B3 = cycleNum_B == 4'h3; // @[DivSqrtRecF64_mulAddZ31.scala 259:30]
  wire  cyc_B2 = cycleNum_B == 4'h2; // @[DivSqrtRecF64_mulAddZ31.scala 260:30]
  wire  cyc_B1 = cycleNum_B == 4'h1; // @[DivSqrtRecF64_mulAddZ31.scala 261:30]
  wire  cyc_B6_div = cyc_B6 & valid_PA & _T_90; // @[DivSqrtRecF64_mulAddZ31.scala 263:41]
  wire  cyc_B4_div = cyc_B4 & valid_PA & _T_90; // @[DivSqrtRecF64_mulAddZ31.scala 265:41]
  wire  _T_105 = ~sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 266:32]
  wire  cyc_B2_div = cyc_B2 & _T_105; // @[DivSqrtRecF64_mulAddZ31.scala 267:29]
  wire  cyc_B1_div = cyc_B1 & _T_105; // @[DivSqrtRecF64_mulAddZ31.scala 268:29]
  wire  cyc_B6_sqrt = cyc_B6 & valid_PB & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 270:42]
  wire  cyc_B5_sqrt = cyc_B5 & valid_PB & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 271:42]
  wire  cyc_B4_sqrt = cyc_B4 & valid_PB & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 272:42]
  wire  cyc_B3_sqrt = cyc_B3 & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 273:30]
  wire  cyc_B2_sqrt = cyc_B2 & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 274:30]
  wire  cyc_B1_sqrt = cyc_B1 & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 275:30]
  wire [2:0] _T_115 = cycleNum_C - 3'h1; // @[DivSqrtRecF64_mulAddZ31.scala 279:70]
  wire  cyc_C6_sqrt = cycleNum_C == 3'h6; // @[DivSqrtRecF64_mulAddZ31.scala 282:35]
  wire  cyc_C5 = cycleNum_C == 3'h5; // @[DivSqrtRecF64_mulAddZ31.scala 284:30]
  wire  cyc_C4 = cycleNum_C == 3'h4; // @[DivSqrtRecF64_mulAddZ31.scala 285:30]
  wire  cyc_C3 = cycleNum_C == 3'h3; // @[DivSqrtRecF64_mulAddZ31.scala 286:30]
  wire  cyc_C2 = cycleNum_C == 3'h2; // @[DivSqrtRecF64_mulAddZ31.scala 287:30]
  wire  cyc_C1 = cycleNum_C == 3'h1; // @[DivSqrtRecF64_mulAddZ31.scala 288:30]
  wire  cyc_C5_div = cyc_C5 & _T_105; // @[DivSqrtRecF64_mulAddZ31.scala 290:29]
  wire  cyc_C4_div = cyc_C4 & _T_105; // @[DivSqrtRecF64_mulAddZ31.scala 291:29]
  wire  _T_120 = ~sqrtOp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 293:32]
  wire  cyc_C1_div = cyc_C1 & _T_120; // @[DivSqrtRecF64_mulAddZ31.scala 294:29]
  wire  cyc_C5_sqrt = cyc_C5 & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 296:30]
  wire  cyc_C4_sqrt = cyc_C4 & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 297:30]
  wire  cyc_C3_sqrt = cyc_C3 & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 298:30]
  wire  cyc_C1_sqrt = cyc_C1 & sqrtOp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 300:30]
  wire [2:0] _T_125 = cycleNum_E - 3'h1; // @[DivSqrtRecF64_mulAddZ31.scala 303:55]
  wire  cyc_E3 = cycleNum_E == 3'h3; // @[DivSqrtRecF64_mulAddZ31.scala 307:30]
  wire  cyc_E2 = cycleNum_E == 3'h2; // @[DivSqrtRecF64_mulAddZ31.scala 308:30]
  wire  cyc_E1 = cycleNum_E == 3'h1; // @[DivSqrtRecF64_mulAddZ31.scala 309:30]
  wire  cyc_E3_div = cyc_E3 & _T_120; // @[DivSqrtRecF64_mulAddZ31.scala 312:29]
  wire  cyc_E3_sqrt = cyc_E3 & sqrtOp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 317:30]
  wire  _T_168 = ~valid_PB; // @[DivSqrtRecF64_mulAddZ31.scala 389:17]
  wire  normalCase_PB = ~isNaN_PB & ~isInf_PB & ~isZero_PB; // @[DivSqrtRecF64_mulAddZ31.scala 383:50]
  wire  _T_185 = ~isNaN_PC; // @[DivSqrtRecF64_mulAddZ31.scala 417:25]
  wire  normalCase_PC = ~isNaN_PC & ~isInf_PC & ~isZero_PC; // @[DivSqrtRecF64_mulAddZ31.scala 417:50]
  wire  valid_leaving_PC = ~normalCase_PC | cyc_E1; // @[DivSqrtRecF64_mulAddZ31.scala 420:44]
  wire  ready_PC = ~valid_PC | valid_leaving_PC; // @[DivSqrtRecF64_mulAddZ31.scala 422:28]
  wire  valid_leaving_PB = normalCase_PB ? cyc_C3 : ready_PC; // @[DivSqrtRecF64_mulAddZ31.scala 387:12]
  wire  ready_PB = ~valid_PB | valid_leaving_PB; // @[DivSqrtRecF64_mulAddZ31.scala 389:28]
  wire  entering_PA = entering_PA_normalCase | cyc_S & (valid_PA | ~ready_PB); // @[DivSqrtRecF64_mulAddZ31.scala 324:32]
  wire  normalCase_PA = ~isNaN_PA & ~isInf_PA & ~isZero_PA; // @[DivSqrtRecF64_mulAddZ31.scala 346:50]
  wire  valid_normalCase_leaving_PA = cyc_B4_div | cyc_B7_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 350:50]
  wire  valid_leaving_PA = normalCase_PA ? valid_normalCase_leaving_PA : ready_PB; // @[DivSqrtRecF64_mulAddZ31.scala 352:12]
  wire  leaving_PA = valid_PA & valid_leaving_PA; // @[DivSqrtRecF64_mulAddZ31.scala 353:28]
  wire [52:0] sigA_PA = {1'h1,fractA_PA}; // @[Cat.scala 29:58]
  wire [52:0] sigB_PA = {1'h1,fractB_PA}; // @[Cat.scala 29:58]
  wire  _T_143 = ~valid_PA; // @[DivSqrtRecF64_mulAddZ31.scala 354:17]
  wire  ready_PA = ~valid_PA | valid_leaving_PA; // @[DivSqrtRecF64_mulAddZ31.scala 354:28]
  wire  _T_148 = cyc_S & ~normalCase_S & _T_143; // @[DivSqrtRecF64_mulAddZ31.scala 359:33]
  wire  leaving_PB = valid_PB & valid_leaving_PB; // @[DivSqrtRecF64_mulAddZ31.scala 388:28]
  wire  _T_152 = leaving_PB | _T_168 & ~ready_PC; // @[DivSqrtRecF64_mulAddZ31.scala 360:25]
  wire  entering_PB_S = cyc_S & ~normalCase_S & _T_143 & _T_152; // @[DivSqrtRecF64_mulAddZ31.scala 359:47]
  wire  entering_PB_normalCase = valid_PA & normalCase_PA & valid_normalCase_leaving_PA; // @[DivSqrtRecF64_mulAddZ31.scala 362:35]
  wire  entering_PB = entering_PB_S | leaving_PA; // @[DivSqrtRecF64_mulAddZ31.scala 363:37]
  wire  entering_PC_S = _T_148 & _T_168 & ready_PC; // @[DivSqrtRecF64_mulAddZ31.scala 394:61]
  wire  entering_PC_normalCase = valid_PB & normalCase_PB & cyc_C3; // @[DivSqrtRecF64_mulAddZ31.scala 396:35]
  wire  entering_PC = entering_PC_S | leaving_PB; // @[DivSqrtRecF64_mulAddZ31.scala 397:37]
  wire  leaving_PC = valid_PC & valid_leaving_PC; // @[DivSqrtRecF64_mulAddZ31.scala 421:28]
  wire [52:0] sigB_PC = {1'h1,fractB_PC}; // @[Cat.scala 29:58]
  wire  _T_195 = ~cyc_B6_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 429:38]
  wire  _T_197 = ~cyc_B5_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 429:55]
  wire  _T_199 = ~cyc_B4_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 430:13]
  wire  _T_200 = ready_PA & ~cyc_B7_sqrt & ~cyc_B6_sqrt & ~cyc_B5_sqrt & _T_199; // @[DivSqrtRecF64_mulAddZ31.scala 429:69]
  wire  _T_205 = ~cyc_B1_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 430:54]
  wire  _T_207 = ~cyc_C5; // @[DivSqrtRecF64_mulAddZ31.scala 431:13]
  wire  _T_208 = _T_200 & ~cyc_B3 & ~cyc_B2 & ~cyc_B1_sqrt & _T_207; // @[DivSqrtRecF64_mulAddZ31.scala 430:68]
  wire  _T_217 = ~cyc_B2_div; // @[DivSqrtRecF64_mulAddZ31.scala 434:13]
  wire  _T_218 = ready_PA & _T_195 & _T_197 & _T_199 & _T_217; // @[DivSqrtRecF64_mulAddZ31.scala 433:69]
  wire [51:0] zFractB_A4_div = cyc_A4_div ? rawB_S_sig[51:0] : 52'h0; // @[DivSqrtRecF64_mulAddZ31.scala 439:29]
  wire  zLinPiece_0_A4_div = cyc_A4_div & rawB_S_sig[51:49] == 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 441:41]
  wire  zLinPiece_1_A4_div = cyc_A4_div & rawB_S_sig[51:49] == 3'h1; // @[DivSqrtRecF64_mulAddZ31.scala 442:41]
  wire  zLinPiece_2_A4_div = cyc_A4_div & rawB_S_sig[51:49] == 3'h2; // @[DivSqrtRecF64_mulAddZ31.scala 443:41]
  wire  zLinPiece_3_A4_div = cyc_A4_div & rawB_S_sig[51:49] == 3'h3; // @[DivSqrtRecF64_mulAddZ31.scala 444:41]
  wire  zLinPiece_4_A4_div = cyc_A4_div & rawB_S_sig[51:49] == 3'h4; // @[DivSqrtRecF64_mulAddZ31.scala 445:41]
  wire  zLinPiece_5_A4_div = cyc_A4_div & rawB_S_sig[51:49] == 3'h5; // @[DivSqrtRecF64_mulAddZ31.scala 446:41]
  wire  zLinPiece_6_A4_div = cyc_A4_div & rawB_S_sig[51:49] == 3'h6; // @[DivSqrtRecF64_mulAddZ31.scala 447:41]
  wire  zLinPiece_7_A4_div = cyc_A4_div & rawB_S_sig[51:49] == 3'h7; // @[DivSqrtRecF64_mulAddZ31.scala 448:41]
  wire [8:0] _T_238 = zLinPiece_0_A4_div ? 9'h1c7 : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 450:12]
  wire [8:0] _T_239 = zLinPiece_1_A4_div ? 9'h16c : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 451:12]
  wire [8:0] _T_240 = _T_238 | _T_239; // @[DivSqrtRecF64_mulAddZ31.scala 450:56]
  wire [8:0] _T_241 = zLinPiece_2_A4_div ? 9'h12a : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 452:12]
  wire [8:0] _T_242 = _T_240 | _T_241; // @[DivSqrtRecF64_mulAddZ31.scala 451:56]
  wire [7:0] _T_243 = zLinPiece_3_A4_div ? 8'hf8 : 8'h0; // @[DivSqrtRecF64_mulAddZ31.scala 453:12]
  wire [8:0] _GEN_54 = {{1'd0}, _T_243}; // @[DivSqrtRecF64_mulAddZ31.scala 452:56]
  wire [8:0] _T_244 = _T_242 | _GEN_54; // @[DivSqrtRecF64_mulAddZ31.scala 452:56]
  wire [7:0] _T_245 = zLinPiece_4_A4_div ? 8'hd2 : 8'h0; // @[DivSqrtRecF64_mulAddZ31.scala 454:12]
  wire [8:0] _GEN_55 = {{1'd0}, _T_245}; // @[DivSqrtRecF64_mulAddZ31.scala 453:56]
  wire [8:0] _T_246 = _T_244 | _GEN_55; // @[DivSqrtRecF64_mulAddZ31.scala 453:56]
  wire [7:0] _T_247 = zLinPiece_5_A4_div ? 8'hb4 : 8'h0; // @[DivSqrtRecF64_mulAddZ31.scala 455:12]
  wire [8:0] _GEN_56 = {{1'd0}, _T_247}; // @[DivSqrtRecF64_mulAddZ31.scala 454:56]
  wire [8:0] _T_248 = _T_246 | _GEN_56; // @[DivSqrtRecF64_mulAddZ31.scala 454:56]
  wire [7:0] _T_249 = zLinPiece_6_A4_div ? 8'h9c : 8'h0; // @[DivSqrtRecF64_mulAddZ31.scala 456:12]
  wire [8:0] _GEN_57 = {{1'd0}, _T_249}; // @[DivSqrtRecF64_mulAddZ31.scala 455:56]
  wire [8:0] _T_250 = _T_248 | _GEN_57; // @[DivSqrtRecF64_mulAddZ31.scala 455:56]
  wire [7:0] _T_251 = zLinPiece_7_A4_div ? 8'h89 : 8'h0; // @[DivSqrtRecF64_mulAddZ31.scala 457:12]
  wire [8:0] _GEN_58 = {{1'd0}, _T_251}; // @[DivSqrtRecF64_mulAddZ31.scala 456:56]
  wire [8:0] zK1_A4_div = _T_250 | _GEN_58; // @[DivSqrtRecF64_mulAddZ31.scala 456:56]
  wire [11:0] _T_253 = zLinPiece_0_A4_div ? 12'h1c : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 459:12]
  wire [11:0] _T_255 = zLinPiece_1_A4_div ? 12'h3a2 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 460:12]
  wire [11:0] _T_256 = _T_253 | _T_255; // @[DivSqrtRecF64_mulAddZ31.scala 459:61]
  wire [11:0] _T_258 = zLinPiece_2_A4_div ? 12'h675 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 461:12]
  wire [11:0] _T_259 = _T_256 | _T_258; // @[DivSqrtRecF64_mulAddZ31.scala 460:61]
  wire [11:0] _T_261 = zLinPiece_3_A4_div ? 12'h8c6 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 462:12]
  wire [11:0] _T_262 = _T_259 | _T_261; // @[DivSqrtRecF64_mulAddZ31.scala 461:61]
  wire [11:0] _T_264 = zLinPiece_4_A4_div ? 12'hab4 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 463:12]
  wire [11:0] _T_265 = _T_262 | _T_264; // @[DivSqrtRecF64_mulAddZ31.scala 462:61]
  wire [11:0] _T_267 = zLinPiece_5_A4_div ? 12'hc56 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 464:12]
  wire [11:0] _T_268 = _T_265 | _T_267; // @[DivSqrtRecF64_mulAddZ31.scala 463:61]
  wire [11:0] _T_270 = zLinPiece_6_A4_div ? 12'hdbd : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 465:12]
  wire [11:0] _T_271 = _T_268 | _T_270; // @[DivSqrtRecF64_mulAddZ31.scala 464:61]
  wire [11:0] _T_273 = zLinPiece_7_A4_div ? 12'hef4 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 466:12]
  wire [11:0] zComplFractK0_A4_div = _T_271 | _T_273; // @[DivSqrtRecF64_mulAddZ31.scala 465:61]
  wire [51:0] zFractB_A7_sqrt = cyc_A7_sqrt ? rawB_S_sig[51:0] : 52'h0; // @[DivSqrtRecF64_mulAddZ31.scala 468:30]
  wire  _T_277 = cyc_A7_sqrt & ~rawB_S_sExp[0]; // @[DivSqrtRecF64_mulAddZ31.scala 471:21]
  wire  zQuadPiece_0_A7_sqrt = cyc_A7_sqrt & ~rawB_S_sExp[0] & _T_41; // @[DivSqrtRecF64_mulAddZ31.scala 471:41]
  wire  zQuadPiece_1_A7_sqrt = _T_277 & rawB_S_sig[51]; // @[DivSqrtRecF64_mulAddZ31.scala 473:41]
  wire  _T_285 = cyc_A7_sqrt & rawB_S_sExp[0]; // @[DivSqrtRecF64_mulAddZ31.scala 475:21]
  wire  zQuadPiece_2_A7_sqrt = cyc_A7_sqrt & rawB_S_sExp[0] & _T_41; // @[DivSqrtRecF64_mulAddZ31.scala 475:41]
  wire  zQuadPiece_3_A7_sqrt = _T_285 & rawB_S_sig[51]; // @[DivSqrtRecF64_mulAddZ31.scala 476:62]
  wire [8:0] _T_291 = zQuadPiece_0_A7_sqrt ? 9'h1c8 : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 478:12]
  wire [7:0] _T_292 = zQuadPiece_1_A7_sqrt ? 8'hc1 : 8'h0; // @[DivSqrtRecF64_mulAddZ31.scala 479:12]
  wire [8:0] _GEN_59 = {{1'd0}, _T_292}; // @[DivSqrtRecF64_mulAddZ31.scala 478:58]
  wire [8:0] _T_293 = _T_291 | _GEN_59; // @[DivSqrtRecF64_mulAddZ31.scala 478:58]
  wire [8:0] _T_294 = zQuadPiece_2_A7_sqrt ? 9'h143 : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 480:12]
  wire [8:0] _T_295 = _T_293 | _T_294; // @[DivSqrtRecF64_mulAddZ31.scala 479:58]
  wire [7:0] _T_296 = zQuadPiece_3_A7_sqrt ? 8'h89 : 8'h0; // @[DivSqrtRecF64_mulAddZ31.scala 481:12]
  wire [8:0] _GEN_60 = {{1'd0}, _T_296}; // @[DivSqrtRecF64_mulAddZ31.scala 480:58]
  wire [8:0] zK2_A7_sqrt = _T_295 | _GEN_60; // @[DivSqrtRecF64_mulAddZ31.scala 480:58]
  wire [9:0] _T_298 = zQuadPiece_0_A7_sqrt ? 10'h2f : 10'h0; // @[DivSqrtRecF64_mulAddZ31.scala 483:12]
  wire [9:0] _T_300 = zQuadPiece_1_A7_sqrt ? 10'h1df : 10'h0; // @[DivSqrtRecF64_mulAddZ31.scala 484:12]
  wire [9:0] _T_301 = _T_298 | _T_300; // @[DivSqrtRecF64_mulAddZ31.scala 483:63]
  wire [9:0] _T_303 = zQuadPiece_2_A7_sqrt ? 10'h14d : 10'h0; // @[DivSqrtRecF64_mulAddZ31.scala 485:12]
  wire [9:0] _T_304 = _T_301 | _T_303; // @[DivSqrtRecF64_mulAddZ31.scala 484:63]
  wire [9:0] _T_306 = zQuadPiece_3_A7_sqrt ? 10'h27e : 10'h0; // @[DivSqrtRecF64_mulAddZ31.scala 486:12]
  wire [9:0] zComplK1_A7_sqrt = _T_304 | _T_306; // @[DivSqrtRecF64_mulAddZ31.scala 485:63]
  wire  _T_309 = cyc_A6_sqrt & ~sExp_PA[0]; // @[DivSqrtRecF64_mulAddZ31.scala 488:44]
  wire  _T_311 = ~sigB_PA[51]; // @[DivSqrtRecF64_mulAddZ31.scala 488:63]
  wire  zQuadPiece_0_A6_sqrt = cyc_A6_sqrt & ~sExp_PA[0] & ~sigB_PA[51]; // @[DivSqrtRecF64_mulAddZ31.scala 488:60]
  wire  zQuadPiece_1_A6_sqrt = _T_309 & sigB_PA[51]; // @[DivSqrtRecF64_mulAddZ31.scala 489:60]
  wire  _T_317 = cyc_A6_sqrt & sExp_PA[0]; // @[DivSqrtRecF64_mulAddZ31.scala 490:44]
  wire  zQuadPiece_2_A6_sqrt = cyc_A6_sqrt & sExp_PA[0] & _T_311; // @[DivSqrtRecF64_mulAddZ31.scala 490:60]
  wire  zQuadPiece_3_A6_sqrt = _T_317 & sigB_PA[51]; // @[DivSqrtRecF64_mulAddZ31.scala 491:60]
  wire [12:0] _T_324 = zQuadPiece_0_A6_sqrt ? 13'h1a : 13'h0; // @[DivSqrtRecF64_mulAddZ31.scala 493:12]
  wire [12:0] _T_326 = zQuadPiece_1_A6_sqrt ? 13'hbca : 13'h0; // @[DivSqrtRecF64_mulAddZ31.scala 494:12]
  wire [12:0] _T_327 = _T_324 | _T_326; // @[DivSqrtRecF64_mulAddZ31.scala 493:64]
  wire [12:0] _T_329 = zQuadPiece_2_A6_sqrt ? 13'h12d3 : 13'h0; // @[DivSqrtRecF64_mulAddZ31.scala 495:12]
  wire [12:0] _T_330 = _T_327 | _T_329; // @[DivSqrtRecF64_mulAddZ31.scala 494:64]
  wire [12:0] _T_332 = zQuadPiece_3_A6_sqrt ? 13'h1b17 : 13'h0; // @[DivSqrtRecF64_mulAddZ31.scala 496:12]
  wire [12:0] zComplFractK0_A6_sqrt = _T_330 | _T_332; // @[DivSqrtRecF64_mulAddZ31.scala 495:64]
  wire [8:0] _T_334 = zFractB_A4_div[48:40] | zK2_A7_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 499:32]
  wire  _T_335 = ~cyc_S; // @[DivSqrtRecF64_mulAddZ31.scala 500:17]
  wire [8:0] _T_336 = ~cyc_S ? nextMulAdd9A_A : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 500:16]
  wire [8:0] mulAdd9A_A = _T_334 | _T_336; // @[DivSqrtRecF64_mulAddZ31.scala 499:46]
  wire [8:0] _T_338 = zK1_A4_div | zFractB_A7_sqrt[50:42]; // @[DivSqrtRecF64_mulAddZ31.scala 502:20]
  wire [8:0] _T_340 = _T_335 ? nextMulAdd9B_A : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 503:16]
  wire [8:0] mulAdd9B_A = _T_338 | _T_340; // @[DivSqrtRecF64_mulAddZ31.scala 502:46]
  wire [9:0] _T_342 = cyc_A7_sqrt ? 10'h3ff : 10'h0; // @[Bitwise.scala 72:12]
  wire [19:0] _T_343 = {zComplK1_A7_sqrt,_T_342}; // @[Cat.scala 29:58]
  wire [5:0] _T_345 = cyc_A6_sqrt ? 6'h3f : 6'h0; // @[Bitwise.scala 72:12]
  wire [19:0] _T_347 = {cyc_A6_sqrt,zComplFractK0_A6_sqrt,_T_345}; // @[Cat.scala 29:58]
  wire [19:0] _T_348 = _T_343 | _T_347; // @[DivSqrtRecF64_mulAddZ31.scala 506:71]
  wire [7:0] _T_350 = cyc_A4_div ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [20:0] _T_352 = {cyc_A4_div,zComplFractK0_A4_div,_T_350}; // @[Cat.scala 29:58]
  wire [20:0] _GEN_61 = {{1'd0}, _T_348}; // @[DivSqrtRecF64_mulAddZ31.scala 507:71]
  wire [20:0] _T_353 = _GEN_61 | _T_352; // @[DivSqrtRecF64_mulAddZ31.scala 507:71]
  wire [18:0] _T_354 = {fractR0_A, 10'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 509:55]
  wire [19:0] _T_355 = 19'h40000 + _T_354; // @[DivSqrtRecF64_mulAddZ31.scala 509:42]
  wire [19:0] _T_356 = cyc_A5_sqrt ? _T_355 : 20'h0; // @[DivSqrtRecF64_mulAddZ31.scala 509:12]
  wire [20:0] _GEN_62 = {{1'd0}, _T_356}; // @[DivSqrtRecF64_mulAddZ31.scala 508:71]
  wire [20:0] _T_357 = _T_353 | _GEN_62; // @[DivSqrtRecF64_mulAddZ31.scala 508:71]
  wire [10:0] _T_361 = cyc_A4_sqrt & ~hiSqrR0_A_sqrt[9] ? 11'h400 : 11'h0; // @[DivSqrtRecF64_mulAddZ31.scala 510:12]
  wire [20:0] _GEN_63 = {{10'd0}, _T_361}; // @[DivSqrtRecF64_mulAddZ31.scala 509:71]
  wire [20:0] _T_362 = _T_357 | _GEN_63; // @[DivSqrtRecF64_mulAddZ31.scala 509:71]
  wire [20:0] _T_368 = sigB_PA[46:26] + 21'h400; // @[DivSqrtRecF64_mulAddZ31.scala 512:29]
  wire [20:0] _T_369 = cyc_A4_sqrt & hiSqrR0_A_sqrt[9] | cyc_A3_div ? _T_368 : 21'h0; // @[DivSqrtRecF64_mulAddZ31.scala 511:12]
  wire [20:0] _T_370 = _T_362 | _T_369; // @[DivSqrtRecF64_mulAddZ31.scala 510:71]
  wire [20:0] _T_372 = cyc_A3_sqrt | cyc_A2 ? partNegSigma0_A : 21'h0; // @[DivSqrtRecF64_mulAddZ31.scala 515:12]
  wire [20:0] _T_373 = _T_370 | _T_372; // @[DivSqrtRecF64_mulAddZ31.scala 514:11]
  wire [24:0] _T_374 = {fractR0_A, 16'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 516:45]
  wire [24:0] _T_375 = cyc_A1_sqrt ? _T_374 : 25'h0; // @[DivSqrtRecF64_mulAddZ31.scala 516:12]
  wire [24:0] _GEN_64 = {{4'd0}, _T_373}; // @[DivSqrtRecF64_mulAddZ31.scala 515:62]
  wire [24:0] _T_376 = _GEN_64 | _T_375; // @[DivSqrtRecF64_mulAddZ31.scala 515:62]
  wire [23:0] _T_377 = {fractR0_A, 15'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 517:45]
  wire [23:0] _T_378 = cyc_A1_div ? _T_377 : 24'h0; // @[DivSqrtRecF64_mulAddZ31.scala 517:12]
  wire [24:0] _GEN_65 = {{1'd0}, _T_378}; // @[DivSqrtRecF64_mulAddZ31.scala 516:62]
  wire [24:0] mulAdd9C_A = _T_376 | _GEN_65; // @[DivSqrtRecF64_mulAddZ31.scala 516:62]
  wire [17:0] _T_379 = mulAdd9A_A * mulAdd9B_A; // @[DivSqrtRecF64_mulAddZ31.scala 518:37]
  wire [18:0] loMulAdd9Out_A = _T_379 + mulAdd9C_A[17:0]; // @[DivSqrtRecF64_mulAddZ31.scala 518:50]
  wire [6:0] _T_384 = mulAdd9C_A[24:18] + 7'h1; // @[DivSqrtRecF64_mulAddZ31.scala 521:36]
  wire [6:0] _T_386 = loMulAdd9Out_A[18] ? _T_384 : mulAdd9C_A[24:18]; // @[DivSqrtRecF64_mulAddZ31.scala 520:16]
  wire [24:0] mulAdd9Out_A = {_T_386,loMulAdd9Out_A[17:0]}; // @[Cat.scala 29:58]
  wire [14:0] _T_391 = ~mulAdd9Out_A[24:10]; // @[DivSqrtRecF64_mulAddZ31.scala 528:46]
  wire [14:0] zFractR0_A6_sqrt = cyc_A6_sqrt & mulAdd9Out_A[19] ? _T_391 : 15'h0; // @[DivSqrtRecF64_mulAddZ31.scala 528:12]
  wire [25:0] _T_393 = {mulAdd9Out_A, 1'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 532:53]
  wire [25:0] sqrR0_A5_sqrt = sExp_PA[0] ? _T_393 : {{1'd0}, mulAdd9Out_A}; // @[DivSqrtRecF64_mulAddZ31.scala 532:28]
  wire [13:0] _T_397 = ~mulAdd9Out_A[24:11]; // @[DivSqrtRecF64_mulAddZ31.scala 534:45]
  wire [13:0] zFractR0_A4_div = cyc_A4_div & mulAdd9Out_A[20] ? _T_397 : 14'h0; // @[DivSqrtRecF64_mulAddZ31.scala 534:12]
  wire [22:0] _T_401 = ~mulAdd9Out_A[24:2]; // @[DivSqrtRecF64_mulAddZ31.scala 536:41]
  wire [22:0] zSigma0_A2 = cyc_A2 & mulAdd9Out_A[11] ? _T_401 : 23'h0; // @[DivSqrtRecF64_mulAddZ31.scala 536:12]
  wire [15:0] _T_404 = sqrtOp_PA ? {{1'd0}, mulAdd9Out_A[24:10]} : mulAdd9Out_A[24:9]; // @[DivSqrtRecF64_mulAddZ31.scala 537:34]
  wire [15:0] r1_A1 = 16'h8000 | _T_404; // @[DivSqrtRecF64_mulAddZ31.scala 537:29]
  wire [16:0] _T_406 = {r1_A1, 1'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 538:44]
  wire [16:0] ER1_A1_sqrt = sExp_PA[0] ? _T_406 : {{1'd0}, r1_A1}; // @[DivSqrtRecF64_mulAddZ31.scala 538:26]
  wire [14:0] _GEN_66 = {{1'd0}, zFractR0_A4_div}; // @[DivSqrtRecF64_mulAddZ31.scala 541:39]
  wire [14:0] _T_408 = zFractR0_A6_sqrt | _GEN_66; // @[DivSqrtRecF64_mulAddZ31.scala 541:39]
  wire [14:0] _GEN_37 = cyc_A6_sqrt | cyc_A4_div ? _T_408 : {{6'd0}, fractR0_A}; // @[DivSqrtRecF64_mulAddZ31.scala 540:38 541:19 129:30]
  wire [15:0] _GEN_38 = cyc_A5_sqrt ? sqrR0_A5_sqrt[25:10] : {{6'd0}, hiSqrR0_A_sqrt}; // @[DivSqrtRecF64_mulAddZ31.scala 543:24 544:24 131:30]
  wire [24:0] _T_412 = cyc_A4_sqrt ? mulAdd9Out_A : {{9'd0}, mulAdd9Out_A[24:9]}; // @[DivSqrtRecF64_mulAddZ31.scala 547:31]
  wire [24:0] _GEN_39 = cyc_A4_sqrt | cyc_A3 ? _T_412 : {{4'd0}, partNegSigma0_A}; // @[DivSqrtRecF64_mulAddZ31.scala 546:34 547:25 132:30]
  wire  _T_415 = cyc_A7_sqrt | cyc_A6_sqrt | cyc_A5_sqrt | cyc_A4; // @[DivSqrtRecF64_mulAddZ31.scala 550:51]
  wire  _T_417 = cyc_A7_sqrt | cyc_A6_sqrt | cyc_A5_sqrt | cyc_A4 | cyc_A3 | cyc_A2; // @[DivSqrtRecF64_mulAddZ31.scala 550:71]
  wire [24:0] _T_418 = ~mulAdd9Out_A; // @[DivSqrtRecF64_mulAddZ31.scala 553:40]
  wire [13:0] _T_420 = cyc_A7_sqrt ? _T_418[24:11] : 14'h0; // @[DivSqrtRecF64_mulAddZ31.scala 553:16]
  wire [14:0] _GEN_67 = {{1'd0}, _T_420}; // @[DivSqrtRecF64_mulAddZ31.scala 553:68]
  wire [14:0] _T_421 = _GEN_67 | zFractR0_A6_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 553:68]
  wire [8:0] _T_423 = cyc_A4_sqrt ? sigB_PA[43:35] : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 555:16]
  wire [14:0] _GEN_68 = {{6'd0}, _T_423}; // @[DivSqrtRecF64_mulAddZ31.scala 554:68]
  wire [14:0] _T_424 = _T_421 | _GEN_68; // @[DivSqrtRecF64_mulAddZ31.scala 554:68]
  wire [14:0] _GEN_69 = {{6'd0}, zFractB_A4_div[43:35]}; // @[DivSqrtRecF64_mulAddZ31.scala 555:68]
  wire [14:0] _T_426 = _T_424 | _GEN_69; // @[DivSqrtRecF64_mulAddZ31.scala 555:68]
  wire [8:0] _T_429 = cyc_A5_sqrt | cyc_A3 ? sigB_PA[52:44] : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 557:16]
  wire [14:0] _GEN_70 = {{6'd0}, _T_429}; // @[DivSqrtRecF64_mulAddZ31.scala 556:68]
  wire [14:0] _T_430 = _T_426 | _GEN_70; // @[DivSqrtRecF64_mulAddZ31.scala 556:68]
  wire [22:0] _GEN_71 = {{8'd0}, _T_430}; // @[DivSqrtRecF64_mulAddZ31.scala 557:68]
  wire [22:0] _T_431 = _GEN_71 | zSigma0_A2; // @[DivSqrtRecF64_mulAddZ31.scala 557:68]
  wire [22:0] _GEN_40 = _T_417 ? _T_431 : {{14'd0}, nextMulAdd9A_A}; // @[DivSqrtRecF64_mulAddZ31.scala 551:7 552:24 133:30]
  wire [14:0] _GEN_72 = {{6'd0}, zFractB_A7_sqrt[50:42]}; // @[DivSqrtRecF64_mulAddZ31.scala 562:73]
  wire [14:0] _T_437 = _GEN_72 | zFractR0_A6_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 562:73]
  wire [8:0] _T_439 = cyc_A5_sqrt ? sqrR0_A5_sqrt[9:1] : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 564:16]
  wire [14:0] _GEN_73 = {{6'd0}, _T_439}; // @[DivSqrtRecF64_mulAddZ31.scala 563:73]
  wire [14:0] _T_440 = _T_437 | _GEN_73; // @[DivSqrtRecF64_mulAddZ31.scala 563:73]
  wire [14:0] _T_441 = _T_440 | _GEN_66; // @[DivSqrtRecF64_mulAddZ31.scala 564:73]
  wire [8:0] _T_443 = cyc_A4_sqrt ? hiSqrR0_A_sqrt[8:0] : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 566:16]
  wire [14:0] _GEN_75 = {{6'd0}, _T_443}; // @[DivSqrtRecF64_mulAddZ31.scala 565:73]
  wire [14:0] _T_444 = _T_441 | _GEN_75; // @[DivSqrtRecF64_mulAddZ31.scala 565:73]
  wire [8:0] _T_446 = {1'h1,fractR0_A[8:1]}; // @[Cat.scala 29:58]
  wire [8:0] _T_447 = cyc_A2 ? _T_446 : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 567:16]
  wire [14:0] _GEN_76 = {{6'd0}, _T_447}; // @[DivSqrtRecF64_mulAddZ31.scala 566:73]
  wire [14:0] _T_448 = _T_444 | _GEN_76; // @[DivSqrtRecF64_mulAddZ31.scala 566:73]
  wire [14:0] _GEN_41 = _T_415 | cyc_A2 ? _T_448 : {{6'd0}, nextMulAdd9B_A}; // @[DivSqrtRecF64_mulAddZ31.scala 560:74 561:24 134:30]
  wire  _T_449 = cyc_A1 | cyc_B7_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 576:16]
  wire  _T_453 = cyc_A1 | cyc_B7_sqrt | cyc_B6_div | cyc_B4 | cyc_B3 | cyc_C6_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 576:65]
  wire [52:0] _T_456 = {ER1_A1_sqrt, 36'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 579:51]
  wire [52:0] _T_457 = cyc_A1_sqrt ? _T_456 : 53'h0; // @[DivSqrtRecF64_mulAddZ31.scala 579:12]
  wire [52:0] _T_459 = cyc_B7_sqrt | cyc_A1_div ? sigB_PA : 53'h0; // @[DivSqrtRecF64_mulAddZ31.scala 580:12]
  wire [52:0] _T_460 = _T_457 | _T_459; // @[DivSqrtRecF64_mulAddZ31.scala 579:67]
  wire [52:0] _T_461 = cyc_B6_div ? sigA_PA : 53'h0; // @[DivSqrtRecF64_mulAddZ31.scala 581:12]
  wire [52:0] _T_462 = _T_460 | _T_461; // @[DivSqrtRecF64_mulAddZ31.scala 580:67]
  wire [45:0] _T_566 = ~io_mulAddResult_3[90:45]; // @[DivSqrtRecF64_mulAddZ31.scala 632:31]
  wire [45:0] zSigma1_B4 = cyc_B4 ? _T_566 : 46'h0; // @[DivSqrtRecF64_mulAddZ31.scala 632:22]
  wire [52:0] _GEN_77 = {{19'd0}, zSigma1_B4[45:12]}; // @[DivSqrtRecF64_mulAddZ31.scala 581:67]
  wire [52:0] _T_464 = _T_462 | _GEN_77; // @[DivSqrtRecF64_mulAddZ31.scala 581:67]
  wire [57:0] sigXNU_B3_CX = io_mulAddResult_3[104:47]; // @[DivSqrtRecF64_mulAddZ31.scala 634:38]
  wire [45:0] _T_467 = cyc_B3 | cyc_C6_sqrt ? sigXNU_B3_CX[57:12] : 46'h0; // @[DivSqrtRecF64_mulAddZ31.scala 584:12]
  wire [52:0] _GEN_78 = {{7'd0}, _T_467}; // @[DivSqrtRecF64_mulAddZ31.scala 582:67]
  wire [52:0] _T_468 = _T_464 | _GEN_78; // @[DivSqrtRecF64_mulAddZ31.scala 582:67]
  wire [45:0] _T_470 = {sigXN_C[57:25], 13'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 585:51]
  wire [45:0] _T_471 = cyc_C4_div ? _T_470 : 46'h0; // @[DivSqrtRecF64_mulAddZ31.scala 585:12]
  wire [52:0] _GEN_79 = {{7'd0}, _T_471}; // @[DivSqrtRecF64_mulAddZ31.scala 584:67]
  wire [52:0] _T_472 = _T_468 | _GEN_79; // @[DivSqrtRecF64_mulAddZ31.scala 584:67]
  wire [45:0] _T_473 = {u_C_sqrt, 15'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 586:44]
  wire [45:0] _T_474 = cyc_C4_sqrt ? _T_473 : 46'h0; // @[DivSqrtRecF64_mulAddZ31.scala 586:12]
  wire [52:0] _GEN_80 = {{7'd0}, _T_474}; // @[DivSqrtRecF64_mulAddZ31.scala 585:67]
  wire [52:0] _T_475 = _T_472 | _GEN_80; // @[DivSqrtRecF64_mulAddZ31.scala 585:67]
  wire [52:0] _T_476 = cyc_C1_div ? sigB_PC : 53'h0; // @[DivSqrtRecF64_mulAddZ31.scala 587:12]
  wire [52:0] _T_477 = _T_475 | _T_476; // @[DivSqrtRecF64_mulAddZ31.scala 586:67]
  wire [53:0] _T_582 = ~io_mulAddResult_3[104:51]; // @[DivSqrtRecF64_mulAddZ31.scala 643:26]
  wire [53:0] zComplSigT_C1_sqrt = cyc_C1_sqrt ? _T_582 : 54'h0; // @[DivSqrtRecF64_mulAddZ31.scala 643:12]
  wire [53:0] _GEN_81 = {{1'd0}, _T_477}; // @[DivSqrtRecF64_mulAddZ31.scala 587:67]
  wire  _T_482 = _T_449 | cyc_B6_sqrt | cyc_B4 | cyc_C6_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 590:56]
  wire [51:0] _T_485 = {r1_A1, 36'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 593:31]
  wire [51:0] _T_486 = cyc_A1 ? _T_485 : 52'h0; // @[DivSqrtRecF64_mulAddZ31.scala 593:12]
  wire [50:0] _T_487 = {ESqrR1_B_sqrt, 19'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 594:39]
  wire [50:0] _T_488 = cyc_B7_sqrt ? _T_487 : 51'h0; // @[DivSqrtRecF64_mulAddZ31.scala 594:12]
  wire [51:0] _GEN_82 = {{1'd0}, _T_488}; // @[DivSqrtRecF64_mulAddZ31.scala 593:55]
  wire [51:0] _T_489 = _T_486 | _GEN_82; // @[DivSqrtRecF64_mulAddZ31.scala 593:55]
  wire [52:0] _T_490 = {ER1_B_sqrt, 36'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 595:36]
  wire [52:0] _T_491 = cyc_B6_sqrt ? _T_490 : 53'h0; // @[DivSqrtRecF64_mulAddZ31.scala 595:12]
  wire [52:0] _GEN_83 = {{1'd0}, _T_489}; // @[DivSqrtRecF64_mulAddZ31.scala 594:55]
  wire [52:0] _T_492 = _GEN_83 | _T_491; // @[DivSqrtRecF64_mulAddZ31.scala 594:55]
  wire [52:0] _GEN_84 = {{7'd0}, zSigma1_B4}; // @[DivSqrtRecF64_mulAddZ31.scala 595:55]
  wire [52:0] _T_493 = _T_492 | _GEN_84; // @[DivSqrtRecF64_mulAddZ31.scala 595:55]
  wire [29:0] _T_495 = cyc_C6_sqrt ? sqrSigma1_C[30:1] : 30'h0; // @[DivSqrtRecF64_mulAddZ31.scala 597:12]
  wire [52:0] _GEN_85 = {{23'd0}, _T_495}; // @[DivSqrtRecF64_mulAddZ31.scala 596:55]
  wire [52:0] _T_496 = _T_493 | _GEN_85; // @[DivSqrtRecF64_mulAddZ31.scala 596:55]
  wire [32:0] _T_497 = cyc_C4 ? sqrSigma1_C : 33'h0; // @[DivSqrtRecF64_mulAddZ31.scala 598:12]
  wire [52:0] _GEN_86 = {{20'd0}, _T_497}; // @[DivSqrtRecF64_mulAddZ31.scala 597:55]
  wire [52:0] _T_498 = _T_496 | _GEN_86; // @[DivSqrtRecF64_mulAddZ31.scala 597:55]
  wire  E_C1_div = ~io_mulAddResult_3[104]; // @[DivSqrtRecF64_mulAddZ31.scala 635:20]
  wire [53:0] _T_575 = cyc_C1_div & ~E_C1_div | cyc_C1_sqrt ? _T_582 : 54'h0; // @[DivSqrtRecF64_mulAddZ31.scala 637:12]
  wire [52:0] _T_578 = ~io_mulAddResult_3[102:50]; // @[DivSqrtRecF64_mulAddZ31.scala 641:37]
  wire [52:0] _T_579 = cyc_C1_div & E_C1_div ? _T_578 : 53'h0; // @[DivSqrtRecF64_mulAddZ31.scala 641:12]
  wire [53:0] _GEN_87 = {{1'd0}, _T_579}; // @[DivSqrtRecF64_mulAddZ31.scala 640:11]
  wire [53:0] zComplSigT_C1 = _T_575 | _GEN_87; // @[DivSqrtRecF64_mulAddZ31.scala 640:11]
  wire [53:0] _GEN_88 = {{1'd0}, _T_498}; // @[DivSqrtRecF64_mulAddZ31.scala 598:55]
  wire  _T_502 = cyc_A4 | cyc_A3_div | cyc_A1_div | cyc_B10_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 602:48]
  wire  _T_506 = _T_502 | cyc_B9_sqrt | cyc_B7_sqrt | cyc_B6 | cyc_B5_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 603:70]
  wire  _T_510 = _T_506 | cyc_B3_sqrt | cyc_B2_div | cyc_B1_sqrt | cyc_C4; // @[DivSqrtRecF64_mulAddZ31.scala 604:73]
  wire  _T_512 = cyc_A3 | cyc_A2_div | cyc_B9_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 606:34]
  wire  _T_516 = _T_512 | cyc_B8_sqrt | cyc_B6 | cyc_B5 | cyc_B4_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 607:64]
  wire  _T_520 = _T_516 | cyc_B2_sqrt | cyc_B1_div | cyc_C6_sqrt | cyc_C3; // @[DivSqrtRecF64_mulAddZ31.scala 608:73]
  wire  _T_522 = cyc_A2 | cyc_A1_div | cyc_B8_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 610:34]
  wire  _T_526 = _T_522 | cyc_B7_sqrt | cyc_B5 | cyc_B4 | cyc_B3_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 611:64]
  wire  _T_529 = _T_526 | cyc_B1_sqrt | cyc_C5 | cyc_C2; // @[DivSqrtRecF64_mulAddZ31.scala 612:54]
  wire  _T_531 = io_latchMulAddA_0 | cyc_B6 | cyc_B2_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 614:41]
  wire [1:0] _T_532 = {_T_529,_T_531}; // @[Cat.scala 29:58]
  wire [1:0] _T_533 = {_T_510,_T_520}; // @[Cat.scala 29:58]
  wire [104:0] _T_535 = {sigX1_B, 47'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 618:45]
  wire [104:0] _T_536 = cyc_B1 ? _T_535 : 105'h0; // @[DivSqrtRecF64_mulAddZ31.scala 618:12]
  wire [103:0] _T_537 = {sigX1_B, 46'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 619:45]
  wire [103:0] _T_538 = cyc_C6_sqrt ? _T_537 : 104'h0; // @[DivSqrtRecF64_mulAddZ31.scala 619:12]
  wire [104:0] _GEN_89 = {{1'd0}, _T_538}; // @[DivSqrtRecF64_mulAddZ31.scala 618:66]
  wire [104:0] _T_539 = _T_536 | _GEN_89; // @[DivSqrtRecF64_mulAddZ31.scala 618:66]
  wire [104:0] _T_541 = {sigXN_C, 47'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 620:45]
  wire [104:0] _T_542 = cyc_C4_sqrt | cyc_C2 ? _T_541 : 105'h0; // @[DivSqrtRecF64_mulAddZ31.scala 620:12]
  wire [104:0] _T_543 = _T_539 | _T_542; // @[DivSqrtRecF64_mulAddZ31.scala 619:66]
  wire  _T_544 = ~E_E_div; // @[DivSqrtRecF64_mulAddZ31.scala 621:27]
  wire [53:0] _T_546 = {bit0FractA_PC, 53'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 621:51]
  wire [53:0] _T_547 = cyc_E3_div & ~E_E_div ? _T_546 : 54'h0; // @[DivSqrtRecF64_mulAddZ31.scala 621:12]
  wire [104:0] _GEN_90 = {{51'd0}, _T_547}; // @[DivSqrtRecF64_mulAddZ31.scala 620:66]
  wire [104:0] _T_548 = _T_543 | _GEN_90; // @[DivSqrtRecF64_mulAddZ31.scala 620:66]
  wire [1:0] _T_551 = {sigB_PC[0], 1'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 624:28]
  wire  _T_554 = sigB_PC[1] ^ sigB_PC[0]; // @[DivSqrtRecF64_mulAddZ31.scala 625:33]
  wire [1:0] _T_556 = {_T_554,sigB_PC[0]}; // @[Cat.scala 29:58]
  wire [1:0] _T_557 = sExp_PC[0] ? _T_551 : _T_556; // @[DivSqrtRecF64_mulAddZ31.scala 623:17]
  wire  _T_559 = ~sigT_E[0]; // @[DivSqrtRecF64_mulAddZ31.scala 626:20]
  wire [1:0] _T_560 = {_T_559, 1'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 626:32]
  wire [1:0] _T_561 = _T_557 ^ _T_560; // @[DivSqrtRecF64_mulAddZ31.scala 626:16]
  wire [55:0] _T_562 = {_T_561, 54'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 627:14]
  wire [55:0] _T_563 = cyc_E3_sqrt ? _T_562 : 56'h0; // @[DivSqrtRecF64_mulAddZ31.scala 622:12]
  wire [104:0] _GEN_91 = {{49'd0}, _T_563}; // @[DivSqrtRecF64_mulAddZ31.scala 621:66]
  wire [31:0] ESqrR1_B8_sqrt = io_mulAddResult_3[103:72]; // @[DivSqrtRecF64_mulAddZ31.scala 631:43]
  wire [32:0] sqrSigma1_B1 = io_mulAddResult_3[79:47]; // @[DivSqrtRecF64_mulAddZ31.scala 633:41]
  wire [53:0] sigT_C1 = ~zComplSigT_C1; // @[DivSqrtRecF64_mulAddZ31.scala 647:19]
  wire [55:0] remT_E2 = io_mulAddResult_3[55:0]; // @[DivSqrtRecF64_mulAddZ31.scala 648:36]
  wire  _T_595 = _T_120 | remT_E2[55:54] == 2'h0; // @[DivSqrtRecF64_mulAddZ31.scala 675:30]
  wire  _T_596 = remT_E2[53:0] == 54'h0 & _T_595; // @[DivSqrtRecF64_mulAddZ31.scala 674:42]
  wire  _T_598 = ~isZeroRemT_E; // @[DivSqrtRecF64_mulAddZ31.scala 685:41]
  wire  trueLtX_E1 = sqrtOp_PC ? ~isNegRemT_E & ~isZeroRemT_E : isNegRemT_E; // @[DivSqrtRecF64_mulAddZ31.scala 685:12]
  wire [12:0] sExpP1_PC = $signed(sExp_PC) + 13'sh1; // @[DivSqrtRecF64_mulAddZ31.scala 693:29]
  wire [54:0] sigTP1_E = sigT_E + 54'h1; // @[DivSqrtRecF64_mulAddZ31.scala 694:27]
  wire [12:0] _T_610 = _T_120 & E_E_div ? $signed(sExp_PC) : $signed(13'sh0); // @[DivSqrtRecF64_mulAddZ31.scala 708:12]
  wire [12:0] _T_614 = _T_120 & _T_544 ? $signed(sExpP1_PC) : $signed(13'sh0); // @[DivSqrtRecF64_mulAddZ31.scala 709:12]
  wire [12:0] _T_616 = $signed(_T_610) | $signed(_T_614); // @[DivSqrtRecF64_mulAddZ31.scala 708:76]
  wire [11:0] _T_617 = sExp_PC[12:1]; // @[DivSqrtRecF64_mulAddZ31.scala 710:47]
  wire [12:0] _T_618 = $signed(_T_617) + 12'sh400; // @[DivSqrtRecF64_mulAddZ31.scala 710:52]
  wire [12:0] _T_619 = sqrtOp_PC ? $signed(_T_618) : $signed(13'sh0); // @[DivSqrtRecF64_mulAddZ31.scala 710:12]
  wire [54:0] _T_622 = trueLtX_E1 ? {{1'd0}, sigT_E} : sigTP1_E; // @[DivSqrtRecF64_mulAddZ31.scala 711:29]
  assign io_inReady_div = _T_208 & ~cyc_C4; // @[DivSqrtRecF64_mulAddZ31.scala 431:22]
  assign io_inReady_sqrt = _T_218 & _T_205; // @[DivSqrtRecF64_mulAddZ31.scala 434:26]
  assign io_usingMulAdd = {_T_533,_T_532}; // @[Cat.scala 29:58]
  assign io_latchMulAddA_0 = _T_453 | cyc_C4 | cyc_C1; // @[DivSqrtRecF64_mulAddZ31.scala 577:35]
  assign io_mulAddA_0 = _GEN_81 | zComplSigT_C1_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 587:67]
  assign io_latchMulAddB_0 = _T_482 | cyc_C4 | cyc_C1; // @[DivSqrtRecF64_mulAddZ31.scala 591:35]
  assign io_mulAddB_0 = _GEN_88 | zComplSigT_C1; // @[DivSqrtRecF64_mulAddZ31.scala 598:55]
  assign io_mulAddC_2 = _T_548 | _GEN_91; // @[DivSqrtRecF64_mulAddZ31.scala 621:66]
  assign io_rawOutValid_div = leaving_PC & _T_120; // @[DivSqrtRecF64_mulAddZ31.scala 698:39]
  assign io_rawOutValid_sqrt = leaving_PC & sqrtOp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 699:39]
  assign io_roundingModeOut = roundingMode_PC; // @[DivSqrtRecF64_mulAddZ31.scala 700:25]
  assign io_invalidExc = majorExc_PC & isNaN_PC; // @[DivSqrtRecF64_mulAddZ31.scala 701:40]
  assign io_infiniteExc = majorExc_PC & _T_185; // @[DivSqrtRecF64_mulAddZ31.scala 702:40]
  assign io_rawOut_isNaN = isNaN_PC; // @[DivSqrtRecF64_mulAddZ31.scala 703:22]
  assign io_rawOut_isInf = isInf_PC; // @[DivSqrtRecF64_mulAddZ31.scala 704:22]
  assign io_rawOut_isZero = isZero_PC; // @[DivSqrtRecF64_mulAddZ31.scala 705:22]
  assign io_rawOut_sign = sign_PC; // @[DivSqrtRecF64_mulAddZ31.scala 706:20]
  assign io_rawOut_sExp = $signed(_T_616) | $signed(_T_619); // @[DivSqrtRecF64_mulAddZ31.scala 709:76]
  assign io_rawOut_sig = {_T_622,_T_598}; // @[Cat.scala 29:58]
  always @(posedge clock) begin
    if (reset) begin // @[DivSqrtRecF64_mulAddZ31.scala 85:30]
      cycleNum_A <= 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 85:30]
    end else if (entering_PA_normalCase | cycleNum_A != 3'h0) begin // @[DivSqrtRecF64_mulAddZ31.scala 216:63]
      cycleNum_A <= _T_89; // @[DivSqrtRecF64_mulAddZ31.scala 217:20]
    end
    if (reset) begin // @[DivSqrtRecF64_mulAddZ31.scala 86:30]
      cycleNum_B <= 4'h0; // @[DivSqrtRecF64_mulAddZ31.scala 86:30]
    end else if (cyc_A1 | cycleNum_B != 4'h0) begin // @[DivSqrtRecF64_mulAddZ31.scala 243:47]
      if (cyc_A1) begin // @[DivSqrtRecF64_mulAddZ31.scala 245:16]
        if (sqrtOp_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 246:20]
          cycleNum_B <= 4'ha;
        end else begin
          cycleNum_B <= 4'h6;
        end
      end else begin
        cycleNum_B <= _T_97;
      end
    end
    if (reset) begin // @[DivSqrtRecF64_mulAddZ31.scala 87:30]
      cycleNum_C <= 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 87:30]
    end else if (cyc_B1 | cycleNum_C != 3'h0) begin // @[DivSqrtRecF64_mulAddZ31.scala 277:47]
      if (cyc_B1) begin // @[DivSqrtRecF64_mulAddZ31.scala 279:16]
        if (sqrtOp_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 279:28]
          cycleNum_C <= 3'h6;
        end else begin
          cycleNum_C <= 3'h5;
        end
      end else begin
        cycleNum_C <= _T_115;
      end
    end
    if (reset) begin // @[DivSqrtRecF64_mulAddZ31.scala 88:30]
      cycleNum_E <= 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 88:30]
    end else if (cyc_C1 | cycleNum_E != 3'h0) begin // @[DivSqrtRecF64_mulAddZ31.scala 302:47]
      if (cyc_C1) begin // @[DivSqrtRecF64_mulAddZ31.scala 303:26]
        cycleNum_E <= 3'h4;
      end else begin
        cycleNum_E <= _T_125;
      end
    end
    if (reset) begin // @[DivSqrtRecF64_mulAddZ31.scala 90:30]
      valid_PA <= 1'h0; // @[DivSqrtRecF64_mulAddZ31.scala 90:30]
    end else if (entering_PA | leaving_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 326:38]
      valid_PA <= entering_PA; // @[DivSqrtRecF64_mulAddZ31.scala 327:18]
    end
    if (entering_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 329:24]
      sqrtOp_PA <= io_sqrtOp; // @[DivSqrtRecF64_mulAddZ31.scala 330:21]
    end
    if (entering_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 329:24]
      if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 175:12]
        majorExc_PA <= _T_43;
      end else begin
        majorExc_PA <= _T_56;
      end
    end
    if (entering_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 329:24]
      if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 182:12]
        isNaN_PA <= _T_57;
      end else begin
        isNaN_PA <= _T_59;
      end
    end
    if (entering_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 329:24]
      if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 186:23]
        isInf_PA <= rawB_S_isInf;
      end else begin
        isInf_PA <= rawA_S_isInf | rawB_S_isZero;
      end
    end
    if (entering_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 329:24]
      if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 187:23]
        isZero_PA <= rawB_S_isZero;
      end else begin
        isZero_PA <= rawA_S_isZero | rawB_S_isInf;
      end
    end
    if (entering_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 329:24]
      sign_PA <= sign_S; // @[DivSqrtRecF64_mulAddZ31.scala 335:21]
    end
    if (entering_PA_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 337:35]
      if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 338:23]
        sExp_PA <= rawB_S_sExp;
      end else begin
        sExp_PA <= sSatExpQuot_S_div;
      end
    end
    if (entering_PA_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 337:35]
      fractB_PA <= rawB_S_sig[51:0]; // @[DivSqrtRecF64_mulAddZ31.scala 339:19]
    end
    if (cyc_A4_div) begin // @[DivSqrtRecF64_mulAddZ31.scala 342:39]
      fractA_PA <= rawA_S_sig[51:0]; // @[DivSqrtRecF64_mulAddZ31.scala 343:19]
    end
    if (entering_PA_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 337:35]
      roundingMode_PA <= io_roundingMode; // @[DivSqrtRecF64_mulAddZ31.scala 340:25]
    end
    if (reset) begin // @[DivSqrtRecF64_mulAddZ31.scala 103:30]
      valid_PB <= 1'h0; // @[DivSqrtRecF64_mulAddZ31.scala 103:30]
    end else if (entering_PB | leaving_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 365:38]
      valid_PB <= entering_PB; // @[DivSqrtRecF64_mulAddZ31.scala 366:18]
    end
    if (entering_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 368:24]
      if (valid_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 369:27]
        sqrtOp_PB <= sqrtOp_PA;
      end else begin
        sqrtOp_PB <= io_sqrtOp;
      end
    end
    if (entering_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 368:24]
      if (valid_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 370:27]
        majorExc_PB <= majorExc_PA;
      end else if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 175:12]
        majorExc_PB <= _T_43;
      end else begin
        majorExc_PB <= _T_56;
      end
    end
    if (entering_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 368:24]
      if (valid_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 371:27]
        isNaN_PB <= isNaN_PA;
      end else if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 182:12]
        isNaN_PB <= _T_57;
      end else begin
        isNaN_PB <= _T_59;
      end
    end
    if (entering_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 368:24]
      if (valid_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 372:27]
        isInf_PB <= isInf_PA;
      end else if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 186:23]
        isInf_PB <= rawB_S_isInf;
      end else begin
        isInf_PB <= rawA_S_isInf | rawB_S_isZero;
      end
    end
    if (entering_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 368:24]
      if (valid_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 373:27]
        isZero_PB <= isZero_PA;
      end else if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 187:23]
        isZero_PB <= rawB_S_isZero;
      end else begin
        isZero_PB <= rawA_S_isZero | rawB_S_isInf;
      end
    end
    if (entering_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 368:24]
      if (valid_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 374:27]
        sign_PB <= sign_PA;
      end else begin
        sign_PB <= sign_S;
      end
    end
    if (entering_PB_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 376:35]
      sExp_PB <= sExp_PA; // @[DivSqrtRecF64_mulAddZ31.scala 377:25]
    end
    if (entering_PB_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 376:35]
      bit0FractA_PB <= fractA_PA[0]; // @[DivSqrtRecF64_mulAddZ31.scala 378:25]
    end
    if (entering_PB_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 376:35]
      fractB_PB <= fractB_PA; // @[DivSqrtRecF64_mulAddZ31.scala 379:25]
    end
    if (entering_PB_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 376:35]
      if (valid_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 380:31]
        roundingMode_PB <= roundingMode_PA;
      end else begin
        roundingMode_PB <= io_roundingMode;
      end
    end
    if (reset) begin // @[DivSqrtRecF64_mulAddZ31.scala 116:30]
      valid_PC <= 1'h0; // @[DivSqrtRecF64_mulAddZ31.scala 116:30]
    end else if (entering_PC | leaving_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 399:38]
      valid_PC <= entering_PC; // @[DivSqrtRecF64_mulAddZ31.scala 400:18]
    end
    if (entering_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 402:24]
      if (valid_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 403:27]
        sqrtOp_PC <= sqrtOp_PB;
      end else begin
        sqrtOp_PC <= io_sqrtOp;
      end
    end
    if (entering_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 402:24]
      if (valid_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 404:27]
        majorExc_PC <= majorExc_PB;
      end else if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 175:12]
        majorExc_PC <= _T_43;
      end else begin
        majorExc_PC <= _T_56;
      end
    end
    if (entering_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 402:24]
      if (valid_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 405:27]
        isNaN_PC <= isNaN_PB;
      end else if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 182:12]
        isNaN_PC <= _T_57;
      end else begin
        isNaN_PC <= _T_59;
      end
    end
    if (entering_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 402:24]
      if (valid_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 406:27]
        isInf_PC <= isInf_PB;
      end else if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 186:23]
        isInf_PC <= rawB_S_isInf;
      end else begin
        isInf_PC <= rawA_S_isInf | rawB_S_isZero;
      end
    end
    if (entering_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 402:24]
      if (valid_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 407:27]
        isZero_PC <= isZero_PB;
      end else if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 187:23]
        isZero_PC <= rawB_S_isZero;
      end else begin
        isZero_PC <= rawA_S_isZero | rawB_S_isInf;
      end
    end
    if (entering_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 402:24]
      if (valid_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 408:27]
        sign_PC <= sign_PB;
      end else begin
        sign_PC <= sign_S;
      end
    end
    if (entering_PC_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 410:35]
      sExp_PC <= sExp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 411:25]
    end
    if (entering_PC_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 410:35]
      bit0FractA_PC <= bit0FractA_PB; // @[DivSqrtRecF64_mulAddZ31.scala 412:25]
    end
    if (entering_PC_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 410:35]
      fractB_PC <= fractB_PB; // @[DivSqrtRecF64_mulAddZ31.scala 413:25]
    end
    if (entering_PC_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 410:35]
      if (valid_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 414:31]
        roundingMode_PC <= roundingMode_PB;
      end else begin
        roundingMode_PC <= io_roundingMode;
      end
    end
    fractR0_A <= _GEN_37[8:0];
    hiSqrR0_A_sqrt <= _GEN_38[9:0];
    partNegSigma0_A <= _GEN_39[20:0];
    nextMulAdd9A_A <= _GEN_40[8:0];
    nextMulAdd9B_A <= _GEN_41[8:0];
    if (cyc_A1_sqrt) begin // @[DivSqrtRecF64_mulAddZ31.scala 569:24]
      if (sExp_PA[0]) begin // @[DivSqrtRecF64_mulAddZ31.scala 538:26]
        ER1_B_sqrt <= _T_406;
      end else begin
        ER1_B_sqrt <= {{1'd0}, r1_A1};
      end
    end
    if (cyc_B8_sqrt) begin // @[DivSqrtRecF64_mulAddZ31.scala 650:24]
      ESqrR1_B_sqrt <= ESqrR1_B8_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 651:23]
    end
    if (cyc_B3) begin // @[DivSqrtRecF64_mulAddZ31.scala 653:19]
      sigX1_B <= sigXNU_B3_CX; // @[DivSqrtRecF64_mulAddZ31.scala 654:17]
    end
    if (cyc_B1) begin // @[DivSqrtRecF64_mulAddZ31.scala 656:19]
      sqrSigma1_C <= sqrSigma1_B1; // @[DivSqrtRecF64_mulAddZ31.scala 657:21]
    end
    if (cyc_C6_sqrt | cyc_C5_div | cyc_C3_sqrt) begin // @[DivSqrtRecF64_mulAddZ31.scala 660:53]
      sigXN_C <= sigXNU_B3_CX; // @[DivSqrtRecF64_mulAddZ31.scala 661:17]
    end
    if (cyc_C5_sqrt) begin // @[DivSqrtRecF64_mulAddZ31.scala 663:24]
      u_C_sqrt <= sigXNU_B3_CX[56:26]; // @[DivSqrtRecF64_mulAddZ31.scala 664:18]
    end
    if (cyc_C1) begin // @[DivSqrtRecF64_mulAddZ31.scala 666:19]
      E_E_div <= E_C1_div; // @[DivSqrtRecF64_mulAddZ31.scala 667:17]
    end
    if (cyc_C1) begin // @[DivSqrtRecF64_mulAddZ31.scala 666:19]
      sigT_E <= sigT_C1; // @[DivSqrtRecF64_mulAddZ31.scala 668:17]
    end
    if (cyc_E2) begin // @[DivSqrtRecF64_mulAddZ31.scala 671:19]
      if (sqrtOp_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 672:27]
        isNegRemT_E <= remT_E2[55];
      end else begin
        isNegRemT_E <= remT_E2[53];
      end
    end
    if (cyc_E2) begin // @[DivSqrtRecF64_mulAddZ31.scala 671:19]
      isZeroRemT_E <= _T_596; // @[DivSqrtRecF64_mulAddZ31.scala 673:22]
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
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  cycleNum_A = _RAND_0[2:0];
  _RAND_1 = {1{`RANDOM}};
  cycleNum_B = _RAND_1[3:0];
  _RAND_2 = {1{`RANDOM}};
  cycleNum_C = _RAND_2[2:0];
  _RAND_3 = {1{`RANDOM}};
  cycleNum_E = _RAND_3[2:0];
  _RAND_4 = {1{`RANDOM}};
  valid_PA = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  sqrtOp_PA = _RAND_5[0:0];
  _RAND_6 = {1{`RANDOM}};
  majorExc_PA = _RAND_6[0:0];
  _RAND_7 = {1{`RANDOM}};
  isNaN_PA = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  isInf_PA = _RAND_8[0:0];
  _RAND_9 = {1{`RANDOM}};
  isZero_PA = _RAND_9[0:0];
  _RAND_10 = {1{`RANDOM}};
  sign_PA = _RAND_10[0:0];
  _RAND_11 = {1{`RANDOM}};
  sExp_PA = _RAND_11[12:0];
  _RAND_12 = {2{`RANDOM}};
  fractB_PA = _RAND_12[51:0];
  _RAND_13 = {2{`RANDOM}};
  fractA_PA = _RAND_13[51:0];
  _RAND_14 = {1{`RANDOM}};
  roundingMode_PA = _RAND_14[2:0];
  _RAND_15 = {1{`RANDOM}};
  valid_PB = _RAND_15[0:0];
  _RAND_16 = {1{`RANDOM}};
  sqrtOp_PB = _RAND_16[0:0];
  _RAND_17 = {1{`RANDOM}};
  majorExc_PB = _RAND_17[0:0];
  _RAND_18 = {1{`RANDOM}};
  isNaN_PB = _RAND_18[0:0];
  _RAND_19 = {1{`RANDOM}};
  isInf_PB = _RAND_19[0:0];
  _RAND_20 = {1{`RANDOM}};
  isZero_PB = _RAND_20[0:0];
  _RAND_21 = {1{`RANDOM}};
  sign_PB = _RAND_21[0:0];
  _RAND_22 = {1{`RANDOM}};
  sExp_PB = _RAND_22[12:0];
  _RAND_23 = {1{`RANDOM}};
  bit0FractA_PB = _RAND_23[0:0];
  _RAND_24 = {2{`RANDOM}};
  fractB_PB = _RAND_24[51:0];
  _RAND_25 = {1{`RANDOM}};
  roundingMode_PB = _RAND_25[2:0];
  _RAND_26 = {1{`RANDOM}};
  valid_PC = _RAND_26[0:0];
  _RAND_27 = {1{`RANDOM}};
  sqrtOp_PC = _RAND_27[0:0];
  _RAND_28 = {1{`RANDOM}};
  majorExc_PC = _RAND_28[0:0];
  _RAND_29 = {1{`RANDOM}};
  isNaN_PC = _RAND_29[0:0];
  _RAND_30 = {1{`RANDOM}};
  isInf_PC = _RAND_30[0:0];
  _RAND_31 = {1{`RANDOM}};
  isZero_PC = _RAND_31[0:0];
  _RAND_32 = {1{`RANDOM}};
  sign_PC = _RAND_32[0:0];
  _RAND_33 = {1{`RANDOM}};
  sExp_PC = _RAND_33[12:0];
  _RAND_34 = {1{`RANDOM}};
  bit0FractA_PC = _RAND_34[0:0];
  _RAND_35 = {2{`RANDOM}};
  fractB_PC = _RAND_35[51:0];
  _RAND_36 = {1{`RANDOM}};
  roundingMode_PC = _RAND_36[2:0];
  _RAND_37 = {1{`RANDOM}};
  fractR0_A = _RAND_37[8:0];
  _RAND_38 = {1{`RANDOM}};
  hiSqrR0_A_sqrt = _RAND_38[9:0];
  _RAND_39 = {1{`RANDOM}};
  partNegSigma0_A = _RAND_39[20:0];
  _RAND_40 = {1{`RANDOM}};
  nextMulAdd9A_A = _RAND_40[8:0];
  _RAND_41 = {1{`RANDOM}};
  nextMulAdd9B_A = _RAND_41[8:0];
  _RAND_42 = {1{`RANDOM}};
  ER1_B_sqrt = _RAND_42[16:0];
  _RAND_43 = {1{`RANDOM}};
  ESqrR1_B_sqrt = _RAND_43[31:0];
  _RAND_44 = {2{`RANDOM}};
  sigX1_B = _RAND_44[57:0];
  _RAND_45 = {2{`RANDOM}};
  sqrSigma1_C = _RAND_45[32:0];
  _RAND_46 = {2{`RANDOM}};
  sigXN_C = _RAND_46[57:0];
  _RAND_47 = {1{`RANDOM}};
  u_C_sqrt = _RAND_47[30:0];
  _RAND_48 = {1{`RANDOM}};
  E_E_div = _RAND_48[0:0];
  _RAND_49 = {2{`RANDOM}};
  sigT_E = _RAND_49[53:0];
  _RAND_50 = {1{`RANDOM}};
  isNegRemT_E = _RAND_50[0:0];
  _RAND_51 = {1{`RANDOM}};
  isZeroRemT_E = _RAND_51[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

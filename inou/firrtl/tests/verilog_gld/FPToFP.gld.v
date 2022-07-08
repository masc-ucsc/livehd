module RoundAnyRawFNToRecFN_4(
  input         io_invalidExc,
  input         io_in_isNaN,
  input         io_in_isInf,
  input         io_in_isZero,
  input         io_in_sign,
  input  [12:0] io_in_sExp,
  input  [53:0] io_in_sig,
  input  [2:0]  io_roundingMode,
  output [32:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire  roundingMode_near_even = io_roundingMode == 3'h0; // @[RoundAnyRawFNToRecFN.scala 88:53]
  wire  roundingMode_min = io_roundingMode == 3'h2; // @[RoundAnyRawFNToRecFN.scala 90:53]
  wire  roundingMode_max = io_roundingMode == 3'h3; // @[RoundAnyRawFNToRecFN.scala 91:53]
  wire  roundingMode_near_maxMag = io_roundingMode == 3'h4; // @[RoundAnyRawFNToRecFN.scala 92:53]
  wire  roundingMode_odd = io_roundingMode == 3'h6; // @[RoundAnyRawFNToRecFN.scala 93:53]
  wire  roundMagUp = roundingMode_min & io_in_sign | roundingMode_max & ~io_in_sign; // @[RoundAnyRawFNToRecFN.scala 96:42]
  wire [13:0] sAdjustedExp = $signed(io_in_sExp) - 13'sh700; // @[RoundAnyRawFNToRecFN.scala 108:24]
  wire  _T_5 = |io_in_sig[27:0]; // @[RoundAnyRawFNToRecFN.scala 115:60]
  wire [26:0] adjustedSig = {io_in_sig[53:28],_T_5}; // @[Cat.scala 29:58]
  wire [8:0] _T_7 = ~sAdjustedExp[8:0]; // @[primitives.scala 51:21]
  wire [64:0] _T_14 = 65'sh10000000000000000 >>> _T_7[5:0]; // @[primitives.scala 77:58]
  wire [15:0] _GEN_0 = {{8'd0}, _T_14[57:50]}; // @[Bitwise.scala 103:31]
  wire [15:0] _T_20 = _GEN_0 & 16'hff; // @[Bitwise.scala 103:31]
  wire [15:0] _T_22 = {_T_14[49:42], 8'h0}; // @[Bitwise.scala 103:65]
  wire [15:0] _T_24 = _T_22 & 16'hff00; // @[Bitwise.scala 103:75]
  wire [15:0] _T_25 = _T_20 | _T_24; // @[Bitwise.scala 103:39]
  wire [15:0] _GEN_1 = {{4'd0}, _T_25[15:4]}; // @[Bitwise.scala 103:31]
  wire [15:0] _T_30 = _GEN_1 & 16'hf0f; // @[Bitwise.scala 103:31]
  wire [15:0] _T_32 = {_T_25[11:0], 4'h0}; // @[Bitwise.scala 103:65]
  wire [15:0] _T_34 = _T_32 & 16'hf0f0; // @[Bitwise.scala 103:75]
  wire [15:0] _T_35 = _T_30 | _T_34; // @[Bitwise.scala 103:39]
  wire [15:0] _GEN_2 = {{2'd0}, _T_35[15:2]}; // @[Bitwise.scala 103:31]
  wire [15:0] _T_40 = _GEN_2 & 16'h3333; // @[Bitwise.scala 103:31]
  wire [15:0] _T_42 = {_T_35[13:0], 2'h0}; // @[Bitwise.scala 103:65]
  wire [15:0] _T_44 = _T_42 & 16'hcccc; // @[Bitwise.scala 103:75]
  wire [15:0] _T_45 = _T_40 | _T_44; // @[Bitwise.scala 103:39]
  wire [15:0] _GEN_3 = {{1'd0}, _T_45[15:1]}; // @[Bitwise.scala 103:31]
  wire [15:0] _T_50 = _GEN_3 & 16'h5555; // @[Bitwise.scala 103:31]
  wire [15:0] _T_52 = {_T_45[14:0], 1'h0}; // @[Bitwise.scala 103:65]
  wire [15:0] _T_54 = _T_52 & 16'haaaa; // @[Bitwise.scala 103:75]
  wire [15:0] _T_55 = _T_50 | _T_54; // @[Bitwise.scala 103:39]
  wire [21:0] _T_72 = {_T_55,_T_14[58],_T_14[59],_T_14[60],_T_14[61],_T_14[62],_T_14[63]}; // @[Cat.scala 29:58]
  wire [21:0] _T_73 = ~_T_72; // @[primitives.scala 74:36]
  wire [21:0] _T_74 = _T_7[6] ? 22'h0 : _T_73; // @[primitives.scala 74:21]
  wire [21:0] _T_75 = ~_T_74; // @[primitives.scala 74:17]
  wire [24:0] _T_76 = {_T_75,3'h7}; // @[Cat.scala 29:58]
  wire [2:0] _T_86 = {_T_14[0],_T_14[1],_T_14[2]}; // @[Cat.scala 29:58]
  wire [2:0] _T_87 = _T_7[6] ? _T_86 : 3'h0; // @[primitives.scala 61:24]
  wire [24:0] _T_88 = _T_7[7] ? _T_76 : {{22'd0}, _T_87}; // @[primitives.scala 66:24]
  wire [24:0] _T_89 = _T_7[8] ? _T_88 : 25'h0; // @[primitives.scala 61:24]
  wire [26:0] _T_91 = {_T_89,2'h3}; // @[Cat.scala 29:58]
  wire [26:0] _T_93 = {1'h0,_T_91[26:1]}; // @[Cat.scala 29:58]
  wire [26:0] _T_94 = ~_T_93; // @[RoundAnyRawFNToRecFN.scala 161:28]
  wire [26:0] _T_95 = _T_94 & _T_91; // @[RoundAnyRawFNToRecFN.scala 161:46]
  wire [26:0] _T_96 = adjustedSig & _T_95; // @[RoundAnyRawFNToRecFN.scala 162:40]
  wire  _T_97 = |_T_96; // @[RoundAnyRawFNToRecFN.scala 162:56]
  wire [26:0] _T_98 = adjustedSig & _T_93; // @[RoundAnyRawFNToRecFN.scala 163:42]
  wire  _T_99 = |_T_98; // @[RoundAnyRawFNToRecFN.scala 163:62]
  wire  _T_100 = _T_97 | _T_99; // @[RoundAnyRawFNToRecFN.scala 164:36]
  wire  _T_101 = roundingMode_near_even | roundingMode_near_maxMag; // @[RoundAnyRawFNToRecFN.scala 167:38]
  wire  _T_102 = (roundingMode_near_even | roundingMode_near_maxMag) & _T_97; // @[RoundAnyRawFNToRecFN.scala 167:67]
  wire  _T_103 = roundMagUp & _T_100; // @[RoundAnyRawFNToRecFN.scala 169:29]
  wire  _T_104 = _T_102 | _T_103; // @[RoundAnyRawFNToRecFN.scala 168:31]
  wire [26:0] _T_105 = adjustedSig | _T_91; // @[RoundAnyRawFNToRecFN.scala 172:32]
  wire [25:0] _T_107 = _T_105[26:2] + 25'h1; // @[RoundAnyRawFNToRecFN.scala 172:49]
  wire  _T_109 = ~_T_99; // @[RoundAnyRawFNToRecFN.scala 174:30]
  wire [25:0] _T_112 = roundingMode_near_even & _T_97 & _T_109 ? _T_91[26:1] : 26'h0; // @[RoundAnyRawFNToRecFN.scala 173:25]
  wire [25:0] _T_113 = ~_T_112; // @[RoundAnyRawFNToRecFN.scala 173:21]
  wire [25:0] _T_114 = _T_107 & _T_113; // @[RoundAnyRawFNToRecFN.scala 172:61]
  wire [26:0] _T_115 = ~_T_91; // @[RoundAnyRawFNToRecFN.scala 178:32]
  wire [26:0] _T_116 = adjustedSig & _T_115; // @[RoundAnyRawFNToRecFN.scala 178:30]
  wire [25:0] _T_120 = roundingMode_odd & _T_100 ? _T_95[26:1] : 26'h0; // @[RoundAnyRawFNToRecFN.scala 179:24]
  wire [25:0] _GEN_4 = {{1'd0}, _T_116[26:2]}; // @[RoundAnyRawFNToRecFN.scala 178:47]
  wire [25:0] _T_121 = _GEN_4 | _T_120; // @[RoundAnyRawFNToRecFN.scala 178:47]
  wire [25:0] _T_122 = _T_104 ? _T_114 : _T_121; // @[RoundAnyRawFNToRecFN.scala 171:16]
  wire [2:0] _T_124 = {1'b0,$signed(_T_122[25:24])}; // @[RoundAnyRawFNToRecFN.scala 183:69]
  wire [13:0] _GEN_5 = {{11{_T_124[2]}},_T_124}; // @[RoundAnyRawFNToRecFN.scala 183:40]
  wire [14:0] _T_125 = $signed(sAdjustedExp) + $signed(_GEN_5); // @[RoundAnyRawFNToRecFN.scala 183:40]
  wire [8:0] common_expOut = _T_125[8:0]; // @[RoundAnyRawFNToRecFN.scala 185:37]
  wire [22:0] common_fractOut = _T_122[22:0]; // @[RoundAnyRawFNToRecFN.scala 189:27]
  wire [7:0] _T_130 = _T_125[14:7]; // @[RoundAnyRawFNToRecFN.scala 194:30]
  wire  common_overflow = $signed(_T_130) >= 8'sh3; // @[RoundAnyRawFNToRecFN.scala 194:50]
  wire  common_totalUnderflow = $signed(_T_125) < 15'sh6b; // @[RoundAnyRawFNToRecFN.scala 198:31]
  wire  _T_139 = |adjustedSig[1:0]; // @[RoundAnyRawFNToRecFN.scala 203:70]
  wire  _T_142 = _T_101 & adjustedSig[1]; // @[RoundAnyRawFNToRecFN.scala 205:67]
  wire  _T_143 = roundMagUp & _T_139; // @[RoundAnyRawFNToRecFN.scala 207:29]
  wire  _T_144 = _T_142 | _T_143; // @[RoundAnyRawFNToRecFN.scala 206:46]
  wire [5:0] _T_148 = sAdjustedExp[13:8]; // @[RoundAnyRawFNToRecFN.scala 218:48]
  wire  _T_154 = _T_100 & $signed(_T_148) <= 6'sh0 & _T_91[2]; // @[RoundAnyRawFNToRecFN.scala 218:74]
  wire  _T_159 = ~_T_91[3]; // @[RoundAnyRawFNToRecFN.scala 221:34]
  wire  _T_161 = _T_159 & _T_122[24]; // @[RoundAnyRawFNToRecFN.scala 224:38]
  wire  _T_163 = _T_161 & _T_97 & _T_144; // @[RoundAnyRawFNToRecFN.scala 225:60]
  wire  _T_164 = ~_T_163; // @[RoundAnyRawFNToRecFN.scala 220:27]
  wire  _T_165 = _T_154 & _T_164; // @[RoundAnyRawFNToRecFN.scala 219:76]
  wire  common_underflow = common_totalUnderflow | _T_165; // @[RoundAnyRawFNToRecFN.scala 215:40]
  wire  common_inexact = common_totalUnderflow | _T_100; // @[RoundAnyRawFNToRecFN.scala 228:49]
  wire  isNaNOut = io_invalidExc | io_in_isNaN; // @[RoundAnyRawFNToRecFN.scala 233:34]
  wire  commonCase = ~isNaNOut & ~io_in_isInf & ~io_in_isZero; // @[RoundAnyRawFNToRecFN.scala 235:61]
  wire  overflow = commonCase & common_overflow; // @[RoundAnyRawFNToRecFN.scala 236:32]
  wire  underflow = commonCase & common_underflow; // @[RoundAnyRawFNToRecFN.scala 237:32]
  wire  inexact = overflow | commonCase & common_inexact; // @[RoundAnyRawFNToRecFN.scala 238:28]
  wire  overflow_roundMagUp = _T_101 | roundMagUp; // @[RoundAnyRawFNToRecFN.scala 241:60]
  wire  pegMinNonzeroMagOut = commonCase & common_totalUnderflow & (roundMagUp | roundingMode_odd); // @[RoundAnyRawFNToRecFN.scala 243:45]
  wire  pegMaxFiniteMagOut = overflow & ~overflow_roundMagUp; // @[RoundAnyRawFNToRecFN.scala 244:39]
  wire  notNaN_isInfOut = io_in_isInf | overflow & overflow_roundMagUp; // @[RoundAnyRawFNToRecFN.scala 246:32]
  wire  signOut = isNaNOut ? 1'h0 : io_in_sign; // @[RoundAnyRawFNToRecFN.scala 248:22]
  wire [8:0] _T_179 = io_in_isZero | common_totalUnderflow ? 9'h1c0 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 251:18]
  wire [8:0] _T_180 = ~_T_179; // @[RoundAnyRawFNToRecFN.scala 251:14]
  wire [8:0] _T_181 = common_expOut & _T_180; // @[RoundAnyRawFNToRecFN.scala 250:24]
  wire [8:0] _T_183 = pegMinNonzeroMagOut ? 9'h194 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 255:18]
  wire [8:0] _T_184 = ~_T_183; // @[RoundAnyRawFNToRecFN.scala 255:14]
  wire [8:0] _T_185 = _T_181 & _T_184; // @[RoundAnyRawFNToRecFN.scala 254:17]
  wire [8:0] _T_186 = pegMaxFiniteMagOut ? 9'h80 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 259:18]
  wire [8:0] _T_187 = ~_T_186; // @[RoundAnyRawFNToRecFN.scala 259:14]
  wire [8:0] _T_188 = _T_185 & _T_187; // @[RoundAnyRawFNToRecFN.scala 258:17]
  wire [8:0] _T_189 = notNaN_isInfOut ? 9'h40 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 263:18]
  wire [8:0] _T_190 = ~_T_189; // @[RoundAnyRawFNToRecFN.scala 263:14]
  wire [8:0] _T_191 = _T_188 & _T_190; // @[RoundAnyRawFNToRecFN.scala 262:17]
  wire [8:0] _T_192 = pegMinNonzeroMagOut ? 9'h6b : 9'h0; // @[RoundAnyRawFNToRecFN.scala 267:16]
  wire [8:0] _T_193 = _T_191 | _T_192; // @[RoundAnyRawFNToRecFN.scala 266:18]
  wire [8:0] _T_194 = pegMaxFiniteMagOut ? 9'h17f : 9'h0; // @[RoundAnyRawFNToRecFN.scala 271:16]
  wire [8:0] _T_195 = _T_193 | _T_194; // @[RoundAnyRawFNToRecFN.scala 270:15]
  wire [8:0] _T_196 = notNaN_isInfOut ? 9'h180 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 275:16]
  wire [8:0] _T_197 = _T_195 | _T_196; // @[RoundAnyRawFNToRecFN.scala 274:15]
  wire [8:0] _T_198 = isNaNOut ? 9'h1c0 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 276:16]
  wire [8:0] expOut = _T_197 | _T_198; // @[RoundAnyRawFNToRecFN.scala 275:77]
  wire [22:0] _T_201 = isNaNOut ? 23'h400000 : 23'h0; // @[RoundAnyRawFNToRecFN.scala 279:16]
  wire [22:0] _T_202 = isNaNOut | io_in_isZero | common_totalUnderflow ? _T_201 : common_fractOut; // @[RoundAnyRawFNToRecFN.scala 278:12]
  wire [22:0] _T_204 = pegMaxFiniteMagOut ? 23'h7fffff : 23'h0; // @[Bitwise.scala 72:12]
  wire [22:0] fractOut = _T_202 | _T_204; // @[RoundAnyRawFNToRecFN.scala 281:11]
  wire [9:0] _T_205 = {signOut,expOut}; // @[Cat.scala 29:58]
  wire [1:0] _T_207 = {underflow,inexact}; // @[Cat.scala 29:58]
  wire [2:0] _T_209 = {io_invalidExc,1'h0,overflow}; // @[Cat.scala 29:58]
  assign io_out = {_T_205,fractOut}; // @[Cat.scala 29:58]
  assign io_exceptionFlags = {_T_209,_T_207}; // @[Cat.scala 29:58]
endmodule
module RecFNToRecFN(
  input  [64:0] io_in,
  input  [2:0]  io_roundingMode,
  output [32:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire  RoundAnyRawFNToRecFN_io_invalidExc; // @[RecFNToRecFN.scala 72:19]
  wire  RoundAnyRawFNToRecFN_io_in_isNaN; // @[RecFNToRecFN.scala 72:19]
  wire  RoundAnyRawFNToRecFN_io_in_isInf; // @[RecFNToRecFN.scala 72:19]
  wire  RoundAnyRawFNToRecFN_io_in_isZero; // @[RecFNToRecFN.scala 72:19]
  wire  RoundAnyRawFNToRecFN_io_in_sign; // @[RecFNToRecFN.scala 72:19]
  wire [12:0] RoundAnyRawFNToRecFN_io_in_sExp; // @[RecFNToRecFN.scala 72:19]
  wire [53:0] RoundAnyRawFNToRecFN_io_in_sig; // @[RecFNToRecFN.scala 72:19]
  wire [2:0] RoundAnyRawFNToRecFN_io_roundingMode; // @[RecFNToRecFN.scala 72:19]
  wire [32:0] RoundAnyRawFNToRecFN_io_out; // @[RecFNToRecFN.scala 72:19]
  wire [4:0] RoundAnyRawFNToRecFN_io_exceptionFlags; // @[RecFNToRecFN.scala 72:19]
  wire  rawIn_isZero = io_in[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_4 = io_in[63:62] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  rawIn_isNaN = _T_4 & io_in[61]; // @[rawFloatFromRecFN.scala 55:33]
  wire  _T_12 = ~rawIn_isZero; // @[rawFloatFromRecFN.scala 60:39]
  wire [1:0] _T_14 = {1'h0,_T_12}; // @[Cat.scala 29:58]
  wire [53:0] rawIn_sig = {1'h0,_T_12,io_in[51:0]}; // @[Cat.scala 29:58]
  RoundAnyRawFNToRecFN_4 RoundAnyRawFNToRecFN ( // @[RecFNToRecFN.scala 72:19]
    .io_invalidExc(RoundAnyRawFNToRecFN_io_invalidExc),
    .io_in_isNaN(RoundAnyRawFNToRecFN_io_in_isNaN),
    .io_in_isInf(RoundAnyRawFNToRecFN_io_in_isInf),
    .io_in_isZero(RoundAnyRawFNToRecFN_io_in_isZero),
    .io_in_sign(RoundAnyRawFNToRecFN_io_in_sign),
    .io_in_sExp(RoundAnyRawFNToRecFN_io_in_sExp),
    .io_in_sig(RoundAnyRawFNToRecFN_io_in_sig),
    .io_roundingMode(RoundAnyRawFNToRecFN_io_roundingMode),
    .io_out(RoundAnyRawFNToRecFN_io_out),
    .io_exceptionFlags(RoundAnyRawFNToRecFN_io_exceptionFlags)
  );
  assign io_out = RoundAnyRawFNToRecFN_io_out; // @[RecFNToRecFN.scala 85:27]
  assign io_exceptionFlags = RoundAnyRawFNToRecFN_io_exceptionFlags; // @[RecFNToRecFN.scala 86:27]
  assign RoundAnyRawFNToRecFN_io_invalidExc = rawIn_isNaN & ~rawIn_sig[51]; // @[common.scala 81:46]
  assign RoundAnyRawFNToRecFN_io_in_isNaN = _T_4 & io_in[61]; // @[rawFloatFromRecFN.scala 55:33]
  assign RoundAnyRawFNToRecFN_io_in_isInf = _T_4 & ~io_in[61]; // @[rawFloatFromRecFN.scala 56:33]
  assign RoundAnyRawFNToRecFN_io_in_isZero = io_in[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  assign RoundAnyRawFNToRecFN_io_in_sign = io_in[64]; // @[rawFloatFromRecFN.scala 58:25]
  assign RoundAnyRawFNToRecFN_io_in_sExp = {1'b0,$signed(io_in[63:52])}; // @[rawFloatFromRecFN.scala 59:27]
  assign RoundAnyRawFNToRecFN_io_in_sig = {_T_14,io_in[51:0]}; // @[Cat.scala 29:58]
  assign RoundAnyRawFNToRecFN_io_roundingMode = io_roundingMode; // @[RecFNToRecFN.scala 83:48]
endmodule
module FPToFP(
  input         clock,
  input         reset,
  input         io_in_valid,
  input         io_in_bits_ldst,
  input         io_in_bits_wen,
  input         io_in_bits_ren1,
  input         io_in_bits_ren2,
  input         io_in_bits_ren3,
  input         io_in_bits_swap12,
  input         io_in_bits_swap23,
  input         io_in_bits_singleIn,
  input         io_in_bits_singleOut,
  input         io_in_bits_fromint,
  input         io_in_bits_toint,
  input         io_in_bits_fastpipe,
  input         io_in_bits_fma,
  input         io_in_bits_div,
  input         io_in_bits_sqrt,
  input         io_in_bits_wflags,
  input  [2:0]  io_in_bits_rm,
  input  [1:0]  io_in_bits_fmaCmd,
  input  [1:0]  io_in_bits_typ,
  input  [64:0] io_in_bits_in1,
  input  [64:0] io_in_bits_in2,
  input  [64:0] io_in_bits_in3,
  output        io_out_valid,
  output [64:0] io_out_bits_data,
  output [4:0]  io_out_bits_exc,
  input         io_lt
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
  reg [95:0] _RAND_5;
  reg [95:0] _RAND_6;
  reg [31:0] _RAND_7;
  reg [95:0] _RAND_8;
  reg [31:0] _RAND_9;
  reg [31:0] _RAND_10;
  reg [95:0] _RAND_11;
  reg [31:0] _RAND_12;
  reg [31:0] _RAND_13;
  reg [95:0] _RAND_14;
  reg [31:0] _RAND_15;
`endif // RANDOMIZE_REG_INIT
  wire [64:0] RecFNToRecFN_io_in; // @[FPU.scala 563:30]
  wire [2:0] RecFNToRecFN_io_roundingMode; // @[FPU.scala 563:30]
  wire [32:0] RecFNToRecFN_io_out; // @[FPU.scala 563:30]
  wire [4:0] RecFNToRecFN_io_exceptionFlags; // @[FPU.scala 563:30]
  reg  inPipe_valid; // @[Valid.scala 117:22]
  reg  inPipe_bits_ren2; // @[Reg.scala 15:16]
  reg  inPipe_bits_singleOut; // @[Reg.scala 15:16]
  reg  inPipe_bits_wflags; // @[Reg.scala 15:16]
  reg [2:0] inPipe_bits_rm; // @[Reg.scala 15:16]
  reg [64:0] inPipe_bits_in1; // @[Reg.scala 15:16]
  reg [64:0] inPipe_bits_in2; // @[Reg.scala 15:16]
  wire [64:0] _T_1 = inPipe_bits_in1 ^ inPipe_bits_in2; // @[FPU.scala 526:48]
  wire [64:0] _T_3 = ~inPipe_bits_in2; // @[FPU.scala 526:82]
  wire [64:0] _T_4 = inPipe_bits_rm[0] ? _T_3 : inPipe_bits_in2; // @[FPU.scala 526:66]
  wire [64:0] signNum = inPipe_bits_rm[1] ? _T_1 : _T_4; // @[FPU.scala 526:20]
  wire [64:0] fsgnj = {signNum[64],inPipe_bits_in1[63:0]}; // @[Cat.scala 29:58]
  wire  _T_8 = &inPipe_bits_in1[63:61]; // @[FPU.scala 200:56]
  wire  _T_10 = &inPipe_bits_in2[63:61]; // @[FPU.scala 200:56]
  wire  _T_15 = _T_8 & ~inPipe_bits_in1[51]; // @[FPU.scala 201:34]
  wire  _T_20 = _T_10 & ~inPipe_bits_in2[51]; // @[FPU.scala 201:34]
  wire  _T_21 = _T_15 | _T_20; // @[FPU.scala 536:49]
  wire  _T_22 = _T_8 & _T_10; // @[FPU.scala 537:27]
  wire  _T_27 = _T_10 | inPipe_bits_rm[0] != io_lt & ~_T_8; // @[FPU.scala 538:24]
  wire [4:0] _T_28 = {_T_21, 4'h0}; // @[FPU.scala 539:31]
  wire [64:0] _T_29 = _T_27 ? inPipe_bits_in1 : inPipe_bits_in2; // @[FPU.scala 540:53]
  wire [64:0] _T_30 = _T_22 ? 65'he008000000000000 : _T_29; // @[FPU.scala 540:25]
  wire [4:0] _GEN_22 = inPipe_bits_wflags ? _T_28 : 5'h0; // @[FPU.scala 530:16 533:25 539:18]
  wire [64:0] _GEN_23 = inPipe_bits_wflags ? _T_30 : fsgnj; // @[FPU.scala 531:17 533:25 540:19]
  wire  outTag = ~inPipe_bits_singleOut; // @[FPU.scala 544:16]
  wire  _T_31 = ~outTag; // @[FPU.scala 547:18]
  wire [64:0] _T_57 = _T_8 ? 65'he008000000000000 : inPipe_bits_in1; // @[FPU.scala 555:24]
  wire [64:0] fsgnjMux_data = inPipe_bits_wflags & ~inPipe_bits_ren2 ? _T_57 : _GEN_23; // @[FPU.scala 552:42 556:21]
  wire [75:0] _T_36 = {fsgnjMux_data[51:0], 24'h0}; // @[FPU.scala 228:28]
  wire [11:0] _T_40 = fsgnjMux_data[63:52] + 12'h100; // @[FPU.scala 231:31]
  wire [11:0] _T_42 = _T_40 - 12'h800; // @[FPU.scala 231:48]
  wire [8:0] _T_47 = {fsgnjMux_data[63:61],_T_42[5:0]}; // @[Cat.scala 29:58]
  wire [8:0] _T_49 = fsgnjMux_data[63:61] == 3'h0 | fsgnjMux_data[63:61] >= 3'h6 ? _T_47 : _T_42[8:0]; // @[FPU.scala 232:10]
  wire [64:0] _T_52 = {fsgnjMux_data[64:33],fsgnjMux_data[64],_T_49,_T_36[75:53]}; // @[Cat.scala 29:58]
  wire [64:0] _GEN_24 = ~outTag ? _T_52 : fsgnjMux_data; // @[FPU.scala 547:34 548:16]
  wire [4:0] _T_63 = {_T_15, 4'h0}; // @[FPU.scala 557:51]
  wire [64:0] _T_69 = {fsgnjMux_data[64:33],RecFNToRecFN_io_out}; // @[Cat.scala 29:58]
  wire [4:0] fsgnjMux_exc = inPipe_bits_wflags & ~inPipe_bits_ren2 ? _T_63 : _GEN_22; // @[FPU.scala 552:42 557:20]
  reg  _T_70; // @[Valid.scala 117:22]
  reg [64:0] _T_71_data; // @[Reg.scala 15:16]
  reg [4:0] _T_71_exc; // @[Reg.scala 15:16]
  reg  _T_72; // @[Valid.scala 117:22]
  reg [64:0] _T_73_data; // @[Reg.scala 15:16]
  reg [4:0] _T_73_exc; // @[Reg.scala 15:16]
  reg  _T_74; // @[Valid.scala 117:22]
  reg [64:0] _T_75_data; // @[Reg.scala 15:16]
  reg [4:0] _T_75_exc; // @[Reg.scala 15:16]
  RecFNToRecFN RecFNToRecFN ( // @[FPU.scala 563:30]
    .io_in(RecFNToRecFN_io_in),
    .io_roundingMode(RecFNToRecFN_io_roundingMode),
    .io_out(RecFNToRecFN_io_out),
    .io_exceptionFlags(RecFNToRecFN_io_exceptionFlags)
  );
  assign io_out_valid = _T_74; // @[Valid.scala 112:21 113:17]
  assign io_out_bits_data = _T_75_data; // @[Valid.scala 112:21 114:16]
  assign io_out_bits_exc = _T_75_exc; // @[Valid.scala 112:21 114:16]
  assign RecFNToRecFN_io_in = inPipe_bits_in1; // @[Valid.scala 112:21 114:16]
  assign RecFNToRecFN_io_roundingMode = inPipe_bits_rm; // @[Valid.scala 112:21 114:16]
  always @(posedge clock) begin
    if (reset) begin // @[Valid.scala 117:22]
      inPipe_valid <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      inPipe_valid <= io_in_valid; // @[Valid.scala 117:22]
    end
    if (io_in_valid) begin // @[Reg.scala 16:19]
      inPipe_bits_ren2 <= io_in_bits_ren2; // @[Reg.scala 16:23]
    end
    if (io_in_valid) begin // @[Reg.scala 16:19]
      inPipe_bits_singleOut <= io_in_bits_singleOut; // @[Reg.scala 16:23]
    end
    if (io_in_valid) begin // @[Reg.scala 16:19]
      inPipe_bits_wflags <= io_in_bits_wflags; // @[Reg.scala 16:23]
    end
    if (io_in_valid) begin // @[Reg.scala 16:19]
      inPipe_bits_rm <= io_in_bits_rm; // @[Reg.scala 16:23]
    end
    if (io_in_valid) begin // @[Reg.scala 16:19]
      inPipe_bits_in1 <= io_in_bits_in1; // @[Reg.scala 16:23]
    end
    if (io_in_valid) begin // @[Reg.scala 16:19]
      inPipe_bits_in2 <= io_in_bits_in2; // @[Reg.scala 16:23]
    end
    if (reset) begin // @[Valid.scala 117:22]
      _T_70 <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      _T_70 <= inPipe_valid; // @[Valid.scala 117:22]
    end
    if (inPipe_valid) begin // @[Reg.scala 16:19]
      if (inPipe_bits_wflags & ~inPipe_bits_ren2) begin // @[FPU.scala 552:42]
        if (_T_31) begin // @[FPU.scala 562:120]
          _T_71_data <= _T_69; // @[FPU.scala 568:18]
        end else begin
          _T_71_data <= _GEN_24;
        end
      end else begin
        _T_71_data <= _GEN_24;
      end
    end
    if (inPipe_valid) begin // @[Reg.scala 16:19]
      if (inPipe_bits_wflags & ~inPipe_bits_ren2) begin // @[FPU.scala 552:42]
        if (_T_31) begin // @[FPU.scala 562:120]
          _T_71_exc <= RecFNToRecFN_io_exceptionFlags; // @[FPU.scala 569:17]
        end else begin
          _T_71_exc <= fsgnjMux_exc;
        end
      end else begin
        _T_71_exc <= fsgnjMux_exc;
      end
    end
    if (reset) begin // @[Valid.scala 117:22]
      _T_72 <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      _T_72 <= _T_70; // @[Valid.scala 117:22]
    end
    if (_T_70) begin // @[Reg.scala 16:19]
      _T_73_data <= _T_71_data; // @[Reg.scala 16:23]
    end
    if (_T_70) begin // @[Reg.scala 16:19]
      _T_73_exc <= _T_71_exc; // @[Reg.scala 16:23]
    end
    if (reset) begin // @[Valid.scala 117:22]
      _T_74 <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      _T_74 <= _T_72; // @[Valid.scala 117:22]
    end
    if (_T_72) begin // @[Reg.scala 16:19]
      _T_75_data <= _T_73_data; // @[Reg.scala 16:23]
    end
    if (_T_72) begin // @[Reg.scala 16:19]
      _T_75_exc <= _T_73_exc; // @[Reg.scala 16:23]
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
  inPipe_valid = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  inPipe_bits_ren2 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  inPipe_bits_singleOut = _RAND_2[0:0];
  _RAND_3 = {1{`RANDOM}};
  inPipe_bits_wflags = _RAND_3[0:0];
  _RAND_4 = {1{`RANDOM}};
  inPipe_bits_rm = _RAND_4[2:0];
  _RAND_5 = {3{`RANDOM}};
  inPipe_bits_in1 = _RAND_5[64:0];
  _RAND_6 = {3{`RANDOM}};
  inPipe_bits_in2 = _RAND_6[64:0];
  _RAND_7 = {1{`RANDOM}};
  _T_70 = _RAND_7[0:0];
  _RAND_8 = {3{`RANDOM}};
  _T_71_data = _RAND_8[64:0];
  _RAND_9 = {1{`RANDOM}};
  _T_71_exc = _RAND_9[4:0];
  _RAND_10 = {1{`RANDOM}};
  _T_72 = _RAND_10[0:0];
  _RAND_11 = {3{`RANDOM}};
  _T_73_data = _RAND_11[64:0];
  _RAND_12 = {1{`RANDOM}};
  _T_73_exc = _RAND_12[4:0];
  _RAND_13 = {1{`RANDOM}};
  _T_74 = _RAND_13[0:0];
  _RAND_14 = {3{`RANDOM}};
  _T_75_data = _RAND_14[64:0];
  _RAND_15 = {1{`RANDOM}};
  _T_75_exc = _RAND_15[4:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

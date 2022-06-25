module RoundAnyRawFNToRecFN(
  input         io_invalidExc,
  input         io_infiniteExc,
  input         io_in_isNaN,
  input         io_in_isInf,
  input         io_in_isZero,
  input         io_in_sign,
  input  [8:0]  io_in_sExp,
  input  [64:0] io_in_sig,
  input  [2:0]  io_roundingMode,
  input         io_detectTininess,
  output [32:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire  roundingMode_near_even = io_roundingMode == 3'h0; // @[RoundAnyRawFNToRecFN.scala 88:53]
  wire  roundingMode_min = io_roundingMode == 3'h2; // @[RoundAnyRawFNToRecFN.scala 90:53]
  wire  roundingMode_max = io_roundingMode == 3'h3; // @[RoundAnyRawFNToRecFN.scala 91:53]
  wire  roundingMode_near_maxMag = io_roundingMode == 3'h4; // @[RoundAnyRawFNToRecFN.scala 92:53]
  wire  roundingMode_odd = io_roundingMode == 3'h6; // @[RoundAnyRawFNToRecFN.scala 93:53]
  wire  roundMagUp = roundingMode_min & io_in_sign | roundingMode_max & ~io_in_sign; // @[RoundAnyRawFNToRecFN.scala 96:42]
  wire [9:0] _T_3 = $signed(io_in_sExp) + 9'sh80; // @[RoundAnyRawFNToRecFN.scala 102:25]
  wire [9:0] sAdjustedExp = {1'b0,$signed(_T_3[8:0])}; // @[RoundAnyRawFNToRecFN.scala 104:31]
  wire  _T_7 = |io_in_sig[38:0]; // @[RoundAnyRawFNToRecFN.scala 115:60]
  wire [26:0] adjustedSig = {io_in_sig[64:39],_T_7}; // @[Cat.scala 29:58]
  wire [26:0] _T_14 = adjustedSig & 27'h2; // @[RoundAnyRawFNToRecFN.scala 162:40]
  wire  _T_15 = |_T_14; // @[RoundAnyRawFNToRecFN.scala 162:56]
  wire [26:0] _T_16 = adjustedSig & 27'h1; // @[RoundAnyRawFNToRecFN.scala 163:42]
  wire  _T_17 = |_T_16; // @[RoundAnyRawFNToRecFN.scala 163:62]
  wire  common_inexact = _T_15 | _T_17; // @[RoundAnyRawFNToRecFN.scala 164:36]
  wire  _T_20 = (roundingMode_near_even | roundingMode_near_maxMag) & _T_15; // @[RoundAnyRawFNToRecFN.scala 167:67]
  wire  _T_21 = roundMagUp & common_inexact; // @[RoundAnyRawFNToRecFN.scala 169:29]
  wire  _T_22 = _T_20 | _T_21; // @[RoundAnyRawFNToRecFN.scala 168:31]
  wire [26:0] _T_23 = adjustedSig | 27'h3; // @[RoundAnyRawFNToRecFN.scala 172:32]
  wire [25:0] _T_25 = _T_23[26:2] + 25'h1; // @[RoundAnyRawFNToRecFN.scala 172:49]
  wire  _T_27 = ~_T_17; // @[RoundAnyRawFNToRecFN.scala 174:30]
  wire [25:0] _T_30 = roundingMode_near_even & _T_15 & _T_27 ? 26'h1 : 26'h0; // @[RoundAnyRawFNToRecFN.scala 173:25]
  wire [25:0] _T_31 = ~_T_30; // @[RoundAnyRawFNToRecFN.scala 173:21]
  wire [25:0] _T_32 = _T_25 & _T_31; // @[RoundAnyRawFNToRecFN.scala 172:61]
  wire [26:0] _T_34 = adjustedSig & 27'h7fffffc; // @[RoundAnyRawFNToRecFN.scala 178:30]
  wire [25:0] _T_38 = roundingMode_odd & common_inexact ? 26'h1 : 26'h0; // @[RoundAnyRawFNToRecFN.scala 179:24]
  wire [25:0] _GEN_0 = {{1'd0}, _T_34[26:2]}; // @[RoundAnyRawFNToRecFN.scala 178:47]
  wire [25:0] _T_39 = _GEN_0 | _T_38; // @[RoundAnyRawFNToRecFN.scala 178:47]
  wire [25:0] _T_40 = _T_22 ? _T_32 : _T_39; // @[RoundAnyRawFNToRecFN.scala 171:16]
  wire [2:0] _T_42 = {1'b0,$signed(_T_40[25:24])}; // @[RoundAnyRawFNToRecFN.scala 183:69]
  wire [9:0] _GEN_1 = {{7{_T_42[2]}},_T_42}; // @[RoundAnyRawFNToRecFN.scala 183:40]
  wire [10:0] _T_43 = $signed(sAdjustedExp) + $signed(_GEN_1); // @[RoundAnyRawFNToRecFN.scala 183:40]
  wire [8:0] common_expOut = _T_43[8:0]; // @[RoundAnyRawFNToRecFN.scala 185:37]
  wire [22:0] common_fractOut = _T_40[22:0]; // @[RoundAnyRawFNToRecFN.scala 189:27]
  wire  isNaNOut = io_invalidExc | io_in_isNaN; // @[RoundAnyRawFNToRecFN.scala 233:34]
  wire  notNaN_isSpecialInfOut = io_infiniteExc | io_in_isInf; // @[RoundAnyRawFNToRecFN.scala 234:49]
  wire  commonCase = ~isNaNOut & ~notNaN_isSpecialInfOut & ~io_in_isZero; // @[RoundAnyRawFNToRecFN.scala 235:61]
  wire  inexact = commonCase & common_inexact; // @[RoundAnyRawFNToRecFN.scala 238:43]
  wire  signOut = isNaNOut ? 1'h0 : io_in_sign; // @[RoundAnyRawFNToRecFN.scala 248:22]
  wire [8:0] _T_75 = io_in_isZero ? 9'h1c0 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 251:18]
  wire [8:0] _T_76 = ~_T_75; // @[RoundAnyRawFNToRecFN.scala 251:14]
  wire [8:0] _T_77 = common_expOut & _T_76; // @[RoundAnyRawFNToRecFN.scala 250:24]
  wire [8:0] _T_85 = notNaN_isSpecialInfOut ? 9'h40 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 263:18]
  wire [8:0] _T_86 = ~_T_85; // @[RoundAnyRawFNToRecFN.scala 263:14]
  wire [8:0] _T_87 = _T_77 & _T_86; // @[RoundAnyRawFNToRecFN.scala 262:17]
  wire [8:0] _T_92 = notNaN_isSpecialInfOut ? 9'h180 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 275:16]
  wire [8:0] _T_93 = _T_87 | _T_92; // @[RoundAnyRawFNToRecFN.scala 274:15]
  wire [8:0] _T_94 = isNaNOut ? 9'h1c0 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 276:16]
  wire [8:0] expOut = _T_93 | _T_94; // @[RoundAnyRawFNToRecFN.scala 275:77]
  wire [22:0] _T_97 = isNaNOut ? 23'h400000 : 23'h0; // @[RoundAnyRawFNToRecFN.scala 279:16]
  wire [22:0] fractOut = isNaNOut | io_in_isZero ? _T_97 : common_fractOut; // @[RoundAnyRawFNToRecFN.scala 278:12]
  wire [9:0] _T_101 = {signOut,expOut}; // @[Cat.scala 29:58]
  wire [1:0] _T_103 = {1'h0,inexact}; // @[Cat.scala 29:58]
  wire [2:0] _T_105 = {io_invalidExc,io_infiniteExc,1'h0}; // @[Cat.scala 29:58]
  assign io_out = {_T_101,fractOut}; // @[Cat.scala 29:58]
  assign io_exceptionFlags = {_T_105,_T_103}; // @[Cat.scala 29:58]
endmodule

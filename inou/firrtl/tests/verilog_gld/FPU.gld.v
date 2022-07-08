module RoundAnyRawFNToRecFN_2(
  input         io_invalidExc,
  input         io_in_isNaN,
  input         io_in_isInf,
  input         io_in_isZero,
  input         io_in_sign,
  input  [12:0] io_in_sExp,
  input  [55:0] io_in_sig,
  input  [2:0]  io_roundingMode,
  output [64:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire  roundingMode_near_even = io_roundingMode == 3'h0; // @[RoundAnyRawFNToRecFN.scala 88:53]
  wire  roundingMode_min = io_roundingMode == 3'h2; // @[RoundAnyRawFNToRecFN.scala 90:53]
  wire  roundingMode_max = io_roundingMode == 3'h3; // @[RoundAnyRawFNToRecFN.scala 91:53]
  wire  roundingMode_near_maxMag = io_roundingMode == 3'h4; // @[RoundAnyRawFNToRecFN.scala 92:53]
  wire  roundingMode_odd = io_roundingMode == 3'h6; // @[RoundAnyRawFNToRecFN.scala 93:53]
  wire  roundMagUp = roundingMode_min & io_in_sign | roundingMode_max & ~io_in_sign; // @[RoundAnyRawFNToRecFN.scala 96:42]
  wire  doShiftSigDown1 = io_in_sig[55]; // @[RoundAnyRawFNToRecFN.scala 118:61]
  wire [11:0] _T_4 = ~io_in_sExp[11:0]; // @[primitives.scala 51:21]
  wire [64:0] _T_17 = 65'sh10000000000000000 >>> _T_4[5:0]; // @[primitives.scala 77:58]
  wire [31:0] _GEN_0 = {{16'd0}, _T_17[44:29]}; // @[Bitwise.scala 103:31]
  wire [31:0] _T_23 = _GEN_0 & 32'hffff; // @[Bitwise.scala 103:31]
  wire [31:0] _T_25 = {_T_17[28:13], 16'h0}; // @[Bitwise.scala 103:65]
  wire [31:0] _T_27 = _T_25 & 32'hffff0000; // @[Bitwise.scala 103:75]
  wire [31:0] _T_28 = _T_23 | _T_27; // @[Bitwise.scala 103:39]
  wire [31:0] _GEN_1 = {{8'd0}, _T_28[31:8]}; // @[Bitwise.scala 103:31]
  wire [31:0] _T_33 = _GEN_1 & 32'hff00ff; // @[Bitwise.scala 103:31]
  wire [31:0] _T_35 = {_T_28[23:0], 8'h0}; // @[Bitwise.scala 103:65]
  wire [31:0] _T_37 = _T_35 & 32'hff00ff00; // @[Bitwise.scala 103:75]
  wire [31:0] _T_38 = _T_33 | _T_37; // @[Bitwise.scala 103:39]
  wire [31:0] _GEN_2 = {{4'd0}, _T_38[31:4]}; // @[Bitwise.scala 103:31]
  wire [31:0] _T_43 = _GEN_2 & 32'hf0f0f0f; // @[Bitwise.scala 103:31]
  wire [31:0] _T_45 = {_T_38[27:0], 4'h0}; // @[Bitwise.scala 103:65]
  wire [31:0] _T_47 = _T_45 & 32'hf0f0f0f0; // @[Bitwise.scala 103:75]
  wire [31:0] _T_48 = _T_43 | _T_47; // @[Bitwise.scala 103:39]
  wire [31:0] _GEN_3 = {{2'd0}, _T_48[31:2]}; // @[Bitwise.scala 103:31]
  wire [31:0] _T_53 = _GEN_3 & 32'h33333333; // @[Bitwise.scala 103:31]
  wire [31:0] _T_55 = {_T_48[29:0], 2'h0}; // @[Bitwise.scala 103:65]
  wire [31:0] _T_57 = _T_55 & 32'hcccccccc; // @[Bitwise.scala 103:75]
  wire [31:0] _T_58 = _T_53 | _T_57; // @[Bitwise.scala 103:39]
  wire [31:0] _GEN_4 = {{1'd0}, _T_58[31:1]}; // @[Bitwise.scala 103:31]
  wire [31:0] _T_63 = _GEN_4 & 32'h55555555; // @[Bitwise.scala 103:31]
  wire [31:0] _T_65 = {_T_58[30:0], 1'h0}; // @[Bitwise.scala 103:65]
  wire [31:0] _T_67 = _T_65 & 32'haaaaaaaa; // @[Bitwise.scala 103:75]
  wire [31:0] _T_68 = _T_63 | _T_67; // @[Bitwise.scala 103:39]
  wire [15:0] _GEN_5 = {{8'd0}, _T_17[60:53]}; // @[Bitwise.scala 103:31]
  wire [15:0] _T_74 = _GEN_5 & 16'hff; // @[Bitwise.scala 103:31]
  wire [15:0] _T_76 = {_T_17[52:45], 8'h0}; // @[Bitwise.scala 103:65]
  wire [15:0] _T_78 = _T_76 & 16'hff00; // @[Bitwise.scala 103:75]
  wire [15:0] _T_79 = _T_74 | _T_78; // @[Bitwise.scala 103:39]
  wire [15:0] _GEN_6 = {{4'd0}, _T_79[15:4]}; // @[Bitwise.scala 103:31]
  wire [15:0] _T_84 = _GEN_6 & 16'hf0f; // @[Bitwise.scala 103:31]
  wire [15:0] _T_86 = {_T_79[11:0], 4'h0}; // @[Bitwise.scala 103:65]
  wire [15:0] _T_88 = _T_86 & 16'hf0f0; // @[Bitwise.scala 103:75]
  wire [15:0] _T_89 = _T_84 | _T_88; // @[Bitwise.scala 103:39]
  wire [15:0] _GEN_7 = {{2'd0}, _T_89[15:2]}; // @[Bitwise.scala 103:31]
  wire [15:0] _T_94 = _GEN_7 & 16'h3333; // @[Bitwise.scala 103:31]
  wire [15:0] _T_96 = {_T_89[13:0], 2'h0}; // @[Bitwise.scala 103:65]
  wire [15:0] _T_98 = _T_96 & 16'hcccc; // @[Bitwise.scala 103:75]
  wire [15:0] _T_99 = _T_94 | _T_98; // @[Bitwise.scala 103:39]
  wire [15:0] _GEN_8 = {{1'd0}, _T_99[15:1]}; // @[Bitwise.scala 103:31]
  wire [15:0] _T_104 = _GEN_8 & 16'h5555; // @[Bitwise.scala 103:31]
  wire [15:0] _T_106 = {_T_99[14:0], 1'h0}; // @[Bitwise.scala 103:65]
  wire [15:0] _T_108 = _T_106 & 16'haaaa; // @[Bitwise.scala 103:75]
  wire [15:0] _T_109 = _T_104 | _T_108; // @[Bitwise.scala 103:39]
  wire [50:0] _T_118 = {_T_68,_T_109,_T_17[61],_T_17[62],_T_17[63]}; // @[Cat.scala 29:58]
  wire [50:0] _T_119 = ~_T_118; // @[primitives.scala 74:36]
  wire [50:0] _T_120 = _T_4[6] ? 51'h0 : _T_119; // @[primitives.scala 74:21]
  wire [50:0] _T_121 = ~_T_120; // @[primitives.scala 74:17]
  wire [50:0] _T_122 = ~_T_121; // @[primitives.scala 74:36]
  wire [50:0] _T_123 = _T_4[7] ? 51'h0 : _T_122; // @[primitives.scala 74:21]
  wire [50:0] _T_124 = ~_T_123; // @[primitives.scala 74:17]
  wire [50:0] _T_125 = ~_T_124; // @[primitives.scala 74:36]
  wire [50:0] _T_126 = _T_4[8] ? 51'h0 : _T_125; // @[primitives.scala 74:21]
  wire [50:0] _T_127 = ~_T_126; // @[primitives.scala 74:17]
  wire [50:0] _T_128 = ~_T_127; // @[primitives.scala 74:36]
  wire [50:0] _T_129 = _T_4[9] ? 51'h0 : _T_128; // @[primitives.scala 74:21]
  wire [50:0] _T_130 = ~_T_129; // @[primitives.scala 74:17]
  wire [53:0] _T_131 = {_T_130,3'h7}; // @[Cat.scala 29:58]
  wire [2:0] _T_147 = {_T_17[0],_T_17[1],_T_17[2]}; // @[Cat.scala 29:58]
  wire [2:0] _T_148 = _T_4[6] ? _T_147 : 3'h0; // @[primitives.scala 61:24]
  wire [2:0] _T_149 = _T_4[7] ? _T_148 : 3'h0; // @[primitives.scala 61:24]
  wire [2:0] _T_150 = _T_4[8] ? _T_149 : 3'h0; // @[primitives.scala 61:24]
  wire [2:0] _T_151 = _T_4[9] ? _T_150 : 3'h0; // @[primitives.scala 61:24]
  wire [53:0] _T_152 = _T_4[10] ? _T_131 : {{51'd0}, _T_151}; // @[primitives.scala 66:24]
  wire [53:0] _T_153 = _T_4[11] ? _T_152 : 54'h0; // @[primitives.scala 61:24]
  wire [53:0] _GEN_9 = {{53'd0}, doShiftSigDown1}; // @[RoundAnyRawFNToRecFN.scala 157:23]
  wire [53:0] _T_154 = _T_153 | _GEN_9; // @[RoundAnyRawFNToRecFN.scala 157:23]
  wire [55:0] _T_155 = {_T_154,2'h3}; // @[Cat.scala 29:58]
  wire [55:0] _T_157 = {1'h0,_T_155[55:1]}; // @[Cat.scala 29:58]
  wire [55:0] _T_158 = ~_T_157; // @[RoundAnyRawFNToRecFN.scala 161:28]
  wire [55:0] _T_159 = _T_158 & _T_155; // @[RoundAnyRawFNToRecFN.scala 161:46]
  wire [55:0] _T_160 = io_in_sig & _T_159; // @[RoundAnyRawFNToRecFN.scala 162:40]
  wire  _T_161 = |_T_160; // @[RoundAnyRawFNToRecFN.scala 162:56]
  wire [55:0] _T_162 = io_in_sig & _T_157; // @[RoundAnyRawFNToRecFN.scala 163:42]
  wire  _T_163 = |_T_162; // @[RoundAnyRawFNToRecFN.scala 163:62]
  wire  _T_164 = _T_161 | _T_163; // @[RoundAnyRawFNToRecFN.scala 164:36]
  wire  _T_165 = roundingMode_near_even | roundingMode_near_maxMag; // @[RoundAnyRawFNToRecFN.scala 167:38]
  wire  _T_166 = (roundingMode_near_even | roundingMode_near_maxMag) & _T_161; // @[RoundAnyRawFNToRecFN.scala 167:67]
  wire  _T_167 = roundMagUp & _T_164; // @[RoundAnyRawFNToRecFN.scala 169:29]
  wire  _T_168 = _T_166 | _T_167; // @[RoundAnyRawFNToRecFN.scala 168:31]
  wire [55:0] _T_169 = io_in_sig | _T_155; // @[RoundAnyRawFNToRecFN.scala 172:32]
  wire [54:0] _T_171 = _T_169[55:2] + 54'h1; // @[RoundAnyRawFNToRecFN.scala 172:49]
  wire  _T_173 = ~_T_163; // @[RoundAnyRawFNToRecFN.scala 174:30]
  wire [54:0] _T_176 = roundingMode_near_even & _T_161 & _T_173 ? _T_155[55:1] : 55'h0; // @[RoundAnyRawFNToRecFN.scala 173:25]
  wire [54:0] _T_177 = ~_T_176; // @[RoundAnyRawFNToRecFN.scala 173:21]
  wire [54:0] _T_178 = _T_171 & _T_177; // @[RoundAnyRawFNToRecFN.scala 172:61]
  wire [55:0] _T_179 = ~_T_155; // @[RoundAnyRawFNToRecFN.scala 178:32]
  wire [55:0] _T_180 = io_in_sig & _T_179; // @[RoundAnyRawFNToRecFN.scala 178:30]
  wire [54:0] _T_184 = roundingMode_odd & _T_164 ? _T_159[55:1] : 55'h0; // @[RoundAnyRawFNToRecFN.scala 179:24]
  wire [54:0] _GEN_10 = {{1'd0}, _T_180[55:2]}; // @[RoundAnyRawFNToRecFN.scala 178:47]
  wire [54:0] _T_185 = _GEN_10 | _T_184; // @[RoundAnyRawFNToRecFN.scala 178:47]
  wire [54:0] _T_186 = _T_168 ? _T_178 : _T_185; // @[RoundAnyRawFNToRecFN.scala 171:16]
  wire [2:0] _T_188 = {1'b0,$signed(_T_186[54:53])}; // @[RoundAnyRawFNToRecFN.scala 183:69]
  wire [12:0] _GEN_11 = {{10{_T_188[2]}},_T_188}; // @[RoundAnyRawFNToRecFN.scala 183:40]
  wire [13:0] _T_189 = $signed(io_in_sExp) + $signed(_GEN_11); // @[RoundAnyRawFNToRecFN.scala 183:40]
  wire [11:0] common_expOut = _T_189[11:0]; // @[RoundAnyRawFNToRecFN.scala 185:37]
  wire [51:0] common_fractOut = doShiftSigDown1 ? _T_186[52:1] : _T_186[51:0]; // @[RoundAnyRawFNToRecFN.scala 187:16]
  wire [3:0] _T_194 = _T_189[13:10]; // @[RoundAnyRawFNToRecFN.scala 194:30]
  wire  common_overflow = $signed(_T_194) >= 4'sh3; // @[RoundAnyRawFNToRecFN.scala 194:50]
  wire  common_totalUnderflow = $signed(_T_189) < 14'sh3ce; // @[RoundAnyRawFNToRecFN.scala 198:31]
  wire  _T_199 = doShiftSigDown1 ? io_in_sig[2] : io_in_sig[1]; // @[RoundAnyRawFNToRecFN.scala 201:16]
  wire  _T_204 = doShiftSigDown1 & io_in_sig[2] | |io_in_sig[1:0]; // @[RoundAnyRawFNToRecFN.scala 203:49]
  wire  _T_206 = _T_165 & _T_199; // @[RoundAnyRawFNToRecFN.scala 205:67]
  wire  _T_207 = roundMagUp & _T_204; // @[RoundAnyRawFNToRecFN.scala 207:29]
  wire  _T_208 = _T_206 | _T_207; // @[RoundAnyRawFNToRecFN.scala 206:46]
  wire  _T_211 = doShiftSigDown1 ? _T_186[54] : _T_186[53]; // @[RoundAnyRawFNToRecFN.scala 209:16]
  wire [1:0] _T_212 = io_in_sExp[12:11]; // @[RoundAnyRawFNToRecFN.scala 218:48]
  wire  _T_217 = doShiftSigDown1 ? _T_155[3] : _T_155[2]; // @[RoundAnyRawFNToRecFN.scala 219:30]
  wire  _T_218 = _T_164 & $signed(_T_212) <= 2'sh0 & _T_217; // @[RoundAnyRawFNToRecFN.scala 218:74]
  wire  _T_222 = doShiftSigDown1 ? _T_155[4] : _T_155[3]; // @[RoundAnyRawFNToRecFN.scala 221:39]
  wire  _T_223 = ~_T_222; // @[RoundAnyRawFNToRecFN.scala 221:34]
  wire  _T_225 = _T_223 & _T_211; // @[RoundAnyRawFNToRecFN.scala 224:38]
  wire  _T_227 = _T_225 & _T_161 & _T_208; // @[RoundAnyRawFNToRecFN.scala 225:60]
  wire  _T_228 = ~_T_227; // @[RoundAnyRawFNToRecFN.scala 220:27]
  wire  _T_229 = _T_218 & _T_228; // @[RoundAnyRawFNToRecFN.scala 219:76]
  wire  common_underflow = common_totalUnderflow | _T_229; // @[RoundAnyRawFNToRecFN.scala 215:40]
  wire  common_inexact = common_totalUnderflow | _T_164; // @[RoundAnyRawFNToRecFN.scala 228:49]
  wire  isNaNOut = io_invalidExc | io_in_isNaN; // @[RoundAnyRawFNToRecFN.scala 233:34]
  wire  commonCase = ~isNaNOut & ~io_in_isInf & ~io_in_isZero; // @[RoundAnyRawFNToRecFN.scala 235:61]
  wire  overflow = commonCase & common_overflow; // @[RoundAnyRawFNToRecFN.scala 236:32]
  wire  underflow = commonCase & common_underflow; // @[RoundAnyRawFNToRecFN.scala 237:32]
  wire  inexact = overflow | commonCase & common_inexact; // @[RoundAnyRawFNToRecFN.scala 238:28]
  wire  overflow_roundMagUp = _T_165 | roundMagUp; // @[RoundAnyRawFNToRecFN.scala 241:60]
  wire  pegMinNonzeroMagOut = commonCase & common_totalUnderflow & (roundMagUp | roundingMode_odd); // @[RoundAnyRawFNToRecFN.scala 243:45]
  wire  pegMaxFiniteMagOut = overflow & ~overflow_roundMagUp; // @[RoundAnyRawFNToRecFN.scala 244:39]
  wire  notNaN_isInfOut = io_in_isInf | overflow & overflow_roundMagUp; // @[RoundAnyRawFNToRecFN.scala 246:32]
  wire  signOut = isNaNOut ? 1'h0 : io_in_sign; // @[RoundAnyRawFNToRecFN.scala 248:22]
  wire [11:0] _T_243 = io_in_isZero | common_totalUnderflow ? 12'he00 : 12'h0; // @[RoundAnyRawFNToRecFN.scala 251:18]
  wire [11:0] _T_244 = ~_T_243; // @[RoundAnyRawFNToRecFN.scala 251:14]
  wire [11:0] _T_245 = common_expOut & _T_244; // @[RoundAnyRawFNToRecFN.scala 250:24]
  wire [11:0] _T_247 = pegMinNonzeroMagOut ? 12'hc31 : 12'h0; // @[RoundAnyRawFNToRecFN.scala 255:18]
  wire [11:0] _T_248 = ~_T_247; // @[RoundAnyRawFNToRecFN.scala 255:14]
  wire [11:0] _T_249 = _T_245 & _T_248; // @[RoundAnyRawFNToRecFN.scala 254:17]
  wire [11:0] _T_250 = pegMaxFiniteMagOut ? 12'h400 : 12'h0; // @[RoundAnyRawFNToRecFN.scala 259:18]
  wire [11:0] _T_251 = ~_T_250; // @[RoundAnyRawFNToRecFN.scala 259:14]
  wire [11:0] _T_252 = _T_249 & _T_251; // @[RoundAnyRawFNToRecFN.scala 258:17]
  wire [11:0] _T_253 = notNaN_isInfOut ? 12'h200 : 12'h0; // @[RoundAnyRawFNToRecFN.scala 263:18]
  wire [11:0] _T_254 = ~_T_253; // @[RoundAnyRawFNToRecFN.scala 263:14]
  wire [11:0] _T_255 = _T_252 & _T_254; // @[RoundAnyRawFNToRecFN.scala 262:17]
  wire [11:0] _T_256 = pegMinNonzeroMagOut ? 12'h3ce : 12'h0; // @[RoundAnyRawFNToRecFN.scala 267:16]
  wire [11:0] _T_257 = _T_255 | _T_256; // @[RoundAnyRawFNToRecFN.scala 266:18]
  wire [11:0] _T_258 = pegMaxFiniteMagOut ? 12'hbff : 12'h0; // @[RoundAnyRawFNToRecFN.scala 271:16]
  wire [11:0] _T_259 = _T_257 | _T_258; // @[RoundAnyRawFNToRecFN.scala 270:15]
  wire [11:0] _T_260 = notNaN_isInfOut ? 12'hc00 : 12'h0; // @[RoundAnyRawFNToRecFN.scala 275:16]
  wire [11:0] _T_261 = _T_259 | _T_260; // @[RoundAnyRawFNToRecFN.scala 274:15]
  wire [11:0] _T_262 = isNaNOut ? 12'he00 : 12'h0; // @[RoundAnyRawFNToRecFN.scala 276:16]
  wire [11:0] expOut = _T_261 | _T_262; // @[RoundAnyRawFNToRecFN.scala 275:77]
  wire [51:0] _T_265 = isNaNOut ? 52'h8000000000000 : 52'h0; // @[RoundAnyRawFNToRecFN.scala 279:16]
  wire [51:0] _T_266 = isNaNOut | io_in_isZero | common_totalUnderflow ? _T_265 : common_fractOut; // @[RoundAnyRawFNToRecFN.scala 278:12]
  wire [51:0] _T_268 = pegMaxFiniteMagOut ? 52'hfffffffffffff : 52'h0; // @[Bitwise.scala 72:12]
  wire [51:0] fractOut = _T_266 | _T_268; // @[RoundAnyRawFNToRecFN.scala 281:11]
  wire [12:0] _T_269 = {signOut,expOut}; // @[Cat.scala 29:58]
  wire [1:0] _T_271 = {underflow,inexact}; // @[Cat.scala 29:58]
  wire [2:0] _T_273 = {io_invalidExc,1'h0,overflow}; // @[Cat.scala 29:58]
  assign io_out = {_T_269,fractOut}; // @[Cat.scala 29:58]
  assign io_exceptionFlags = {_T_273,_T_271}; // @[Cat.scala 29:58]
endmodule
module RoundRawFNToRecFN(
  input         io_invalidExc,
  input         io_in_isNaN,
  input         io_in_isInf,
  input         io_in_isZero,
  input         io_in_sign,
  input  [12:0] io_in_sExp,
  input  [55:0] io_in_sig,
  input  [2:0]  io_roundingMode,
  output [64:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire  roundAnyRawFNToRecFN_io_invalidExc; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire  roundAnyRawFNToRecFN_io_in_isNaN; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire  roundAnyRawFNToRecFN_io_in_isInf; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire  roundAnyRawFNToRecFN_io_in_isZero; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire  roundAnyRawFNToRecFN_io_in_sign; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire [12:0] roundAnyRawFNToRecFN_io_in_sExp; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire [55:0] roundAnyRawFNToRecFN_io_in_sig; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire [2:0] roundAnyRawFNToRecFN_io_roundingMode; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire [64:0] roundAnyRawFNToRecFN_io_out; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire [4:0] roundAnyRawFNToRecFN_io_exceptionFlags; // @[RoundAnyRawFNToRecFN.scala 307:15]
  RoundAnyRawFNToRecFN_2 roundAnyRawFNToRecFN ( // @[RoundAnyRawFNToRecFN.scala 307:15]
    .io_invalidExc(roundAnyRawFNToRecFN_io_invalidExc),
    .io_in_isNaN(roundAnyRawFNToRecFN_io_in_isNaN),
    .io_in_isInf(roundAnyRawFNToRecFN_io_in_isInf),
    .io_in_isZero(roundAnyRawFNToRecFN_io_in_isZero),
    .io_in_sign(roundAnyRawFNToRecFN_io_in_sign),
    .io_in_sExp(roundAnyRawFNToRecFN_io_in_sExp),
    .io_in_sig(roundAnyRawFNToRecFN_io_in_sig),
    .io_roundingMode(roundAnyRawFNToRecFN_io_roundingMode),
    .io_out(roundAnyRawFNToRecFN_io_out),
    .io_exceptionFlags(roundAnyRawFNToRecFN_io_exceptionFlags)
  );
  assign io_out = roundAnyRawFNToRecFN_io_out; // @[RoundAnyRawFNToRecFN.scala 315:23]
  assign io_exceptionFlags = roundAnyRawFNToRecFN_io_exceptionFlags; // @[RoundAnyRawFNToRecFN.scala 316:23]
  assign roundAnyRawFNToRecFN_io_invalidExc = io_invalidExc; // @[RoundAnyRawFNToRecFN.scala 310:44]
  assign roundAnyRawFNToRecFN_io_in_isNaN = io_in_isNaN; // @[RoundAnyRawFNToRecFN.scala 312:44]
  assign roundAnyRawFNToRecFN_io_in_isInf = io_in_isInf; // @[RoundAnyRawFNToRecFN.scala 312:44]
  assign roundAnyRawFNToRecFN_io_in_isZero = io_in_isZero; // @[RoundAnyRawFNToRecFN.scala 312:44]
  assign roundAnyRawFNToRecFN_io_in_sign = io_in_sign; // @[RoundAnyRawFNToRecFN.scala 312:44]
  assign roundAnyRawFNToRecFN_io_in_sExp = io_in_sExp; // @[RoundAnyRawFNToRecFN.scala 312:44]
  assign roundAnyRawFNToRecFN_io_in_sig = io_in_sig; // @[RoundAnyRawFNToRecFN.scala 312:44]
  assign roundAnyRawFNToRecFN_io_roundingMode = io_roundingMode; // @[RoundAnyRawFNToRecFN.scala 313:44]
endmodule
module MulAddRecFNToRaw_preMul(
  input  [1:0]   io_op,
  input  [64:0]  io_a,
  input  [64:0]  io_b,
  input  [64:0]  io_c,
  output [52:0]  io_mulAddA,
  output [52:0]  io_mulAddB,
  output [105:0] io_mulAddC,
  output         io_toPostMul_isSigNaNAny,
  output         io_toPostMul_isNaNAOrB,
  output         io_toPostMul_isInfA,
  output         io_toPostMul_isZeroA,
  output         io_toPostMul_isInfB,
  output         io_toPostMul_isZeroB,
  output         io_toPostMul_signProd,
  output         io_toPostMul_isNaNC,
  output         io_toPostMul_isInfC,
  output         io_toPostMul_isZeroC,
  output [12:0]  io_toPostMul_sExpSum,
  output         io_toPostMul_doSubMags,
  output         io_toPostMul_CIsDominant,
  output [5:0]   io_toPostMul_CDom_CAlignDist,
  output [54:0]  io_toPostMul_highAlignedSigC,
  output         io_toPostMul_bit0AlignedSigC
);
  wire  rawA_isZero = io_a[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_4 = io_a[63:62] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  rawA_isNaN = _T_4 & io_a[61]; // @[rawFloatFromRecFN.scala 55:33]
  wire  rawA_sign = io_a[64]; // @[rawFloatFromRecFN.scala 58:25]
  wire [12:0] rawA_sExp = {1'b0,$signed(io_a[63:52])}; // @[rawFloatFromRecFN.scala 59:27]
  wire  _T_12 = ~rawA_isZero; // @[rawFloatFromRecFN.scala 60:39]
  wire [53:0] rawA_sig = {1'h0,_T_12,io_a[51:0]}; // @[Cat.scala 29:58]
  wire  rawB_isZero = io_b[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_20 = io_b[63:62] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  rawB_isNaN = _T_20 & io_b[61]; // @[rawFloatFromRecFN.scala 55:33]
  wire  rawB_sign = io_b[64]; // @[rawFloatFromRecFN.scala 58:25]
  wire [12:0] rawB_sExp = {1'b0,$signed(io_b[63:52])}; // @[rawFloatFromRecFN.scala 59:27]
  wire  _T_28 = ~rawB_isZero; // @[rawFloatFromRecFN.scala 60:39]
  wire [53:0] rawB_sig = {1'h0,_T_28,io_b[51:0]}; // @[Cat.scala 29:58]
  wire  rawC_isZero = io_c[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_36 = io_c[63:62] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  rawC_isNaN = _T_36 & io_c[61]; // @[rawFloatFromRecFN.scala 55:33]
  wire  rawC_sign = io_c[64]; // @[rawFloatFromRecFN.scala 58:25]
  wire [12:0] rawC_sExp = {1'b0,$signed(io_c[63:52])}; // @[rawFloatFromRecFN.scala 59:27]
  wire  _T_44 = ~rawC_isZero; // @[rawFloatFromRecFN.scala 60:39]
  wire [53:0] rawC_sig = {1'h0,_T_44,io_c[51:0]}; // @[Cat.scala 29:58]
  wire  signProd = rawA_sign ^ rawB_sign ^ io_op[1]; // @[MulAddRecFN.scala 98:42]
  wire [13:0] _T_50 = $signed(rawA_sExp) + $signed(rawB_sExp); // @[MulAddRecFN.scala 101:19]
  wire [13:0] sExpAlignedProd = $signed(_T_50) - 14'sh7c8; // @[MulAddRecFN.scala 101:32]
  wire  doSubMags = signProd ^ rawC_sign ^ io_op[0]; // @[MulAddRecFN.scala 103:42]
  wire [13:0] _GEN_0 = {{1{rawC_sExp[12]}},rawC_sExp}; // @[MulAddRecFN.scala 107:42]
  wire [13:0] sNatCAlignDist = $signed(sExpAlignedProd) - $signed(_GEN_0); // @[MulAddRecFN.scala 107:42]
  wire [12:0] posNatCAlignDist = sNatCAlignDist[12:0]; // @[MulAddRecFN.scala 108:42]
  wire  isMinCAlign = rawA_isZero | rawB_isZero | $signed(sNatCAlignDist) < 14'sh0; // @[MulAddRecFN.scala 109:50]
  wire  CIsDominant = _T_44 & (isMinCAlign | posNatCAlignDist <= 13'h35); // @[MulAddRecFN.scala 111:23]
  wire [7:0] _T_64 = posNatCAlignDist < 13'ha1 ? posNatCAlignDist[7:0] : 8'ha1; // @[MulAddRecFN.scala 115:16]
  wire [7:0] CAlignDist = isMinCAlign ? 8'h0 : _T_64; // @[MulAddRecFN.scala 113:12]
  wire [53:0] _T_65 = ~rawC_sig; // @[MulAddRecFN.scala 121:28]
  wire [53:0] _T_66 = doSubMags ? _T_65 : rawC_sig; // @[MulAddRecFN.scala 121:16]
  wire [110:0] _T_68 = doSubMags ? 111'h7fffffffffffffffffffffffffff : 111'h0; // @[Bitwise.scala 72:12]
  wire [164:0] _T_70 = {_T_66,_T_68}; // @[MulAddRecFN.scala 123:11]
  wire [164:0] mainAlignedSigC = $signed(_T_70) >>> CAlignDist; // @[MulAddRecFN.scala 123:17]
  wire  _T_74 = |rawC_sig[3:0]; // @[primitives.scala 121:54]
  wire  _T_76 = |rawC_sig[7:4]; // @[primitives.scala 121:54]
  wire  _T_78 = |rawC_sig[11:8]; // @[primitives.scala 121:54]
  wire  _T_80 = |rawC_sig[15:12]; // @[primitives.scala 121:54]
  wire  _T_82 = |rawC_sig[19:16]; // @[primitives.scala 121:54]
  wire  _T_84 = |rawC_sig[23:20]; // @[primitives.scala 121:54]
  wire  _T_86 = |rawC_sig[27:24]; // @[primitives.scala 121:54]
  wire  _T_88 = |rawC_sig[31:28]; // @[primitives.scala 121:54]
  wire  _T_90 = |rawC_sig[35:32]; // @[primitives.scala 121:54]
  wire  _T_92 = |rawC_sig[39:36]; // @[primitives.scala 121:54]
  wire  _T_94 = |rawC_sig[43:40]; // @[primitives.scala 121:54]
  wire  _T_96 = |rawC_sig[47:44]; // @[primitives.scala 121:54]
  wire  _T_98 = |rawC_sig[51:48]; // @[primitives.scala 121:54]
  wire  _T_100 = |rawC_sig[53:52]; // @[primitives.scala 124:57]
  wire [6:0] _T_106 = {_T_86,_T_84,_T_82,_T_80,_T_78,_T_76,_T_74}; // @[primitives.scala 125:20]
  wire [13:0] _T_113 = {_T_100,_T_98,_T_96,_T_94,_T_92,_T_90,_T_88,_T_106}; // @[primitives.scala 125:20]
  wire [64:0] _T_115 = 65'sh10000000000000000 >>> CAlignDist[7:2]; // @[primitives.scala 77:58]
  wire [7:0] _GEN_1 = {{4'd0}, _T_115[31:28]}; // @[Bitwise.scala 103:31]
  wire [7:0] _T_121 = _GEN_1 & 8'hf; // @[Bitwise.scala 103:31]
  wire [7:0] _T_123 = {_T_115[27:24], 4'h0}; // @[Bitwise.scala 103:65]
  wire [7:0] _T_125 = _T_123 & 8'hf0; // @[Bitwise.scala 103:75]
  wire [7:0] _T_126 = _T_121 | _T_125; // @[Bitwise.scala 103:39]
  wire [7:0] _GEN_2 = {{2'd0}, _T_126[7:2]}; // @[Bitwise.scala 103:31]
  wire [7:0] _T_131 = _GEN_2 & 8'h33; // @[Bitwise.scala 103:31]
  wire [7:0] _T_133 = {_T_126[5:0], 2'h0}; // @[Bitwise.scala 103:65]
  wire [7:0] _T_135 = _T_133 & 8'hcc; // @[Bitwise.scala 103:75]
  wire [7:0] _T_136 = _T_131 | _T_135; // @[Bitwise.scala 103:39]
  wire [7:0] _GEN_3 = {{1'd0}, _T_136[7:1]}; // @[Bitwise.scala 103:31]
  wire [7:0] _T_141 = _GEN_3 & 8'h55; // @[Bitwise.scala 103:31]
  wire [7:0] _T_143 = {_T_136[6:0], 1'h0}; // @[Bitwise.scala 103:65]
  wire [7:0] _T_145 = _T_143 & 8'haa; // @[Bitwise.scala 103:75]
  wire [7:0] _T_146 = _T_141 | _T_145; // @[Bitwise.scala 103:39]
  wire [12:0] _T_160 = {_T_146,_T_115[32],_T_115[33],_T_115[34],_T_115[35],_T_115[36]}; // @[Cat.scala 29:58]
  wire [13:0] _GEN_4 = {{1'd0}, _T_160}; // @[MulAddRecFN.scala 125:68]
  wire [13:0] _T_161 = _T_113 & _GEN_4; // @[MulAddRecFN.scala 125:68]
  wire  reduced4CExtra = |_T_161; // @[MulAddRecFN.scala 133:11]
  wire  _T_166 = &mainAlignedSigC[2:0] & ~reduced4CExtra; // @[MulAddRecFN.scala 137:44]
  wire  _T_169 = |mainAlignedSigC[2:0] | reduced4CExtra; // @[MulAddRecFN.scala 138:44]
  wire  _T_170 = doSubMags ? _T_166 : _T_169; // @[MulAddRecFN.scala 136:16]
  wire [161:0] _T_171 = mainAlignedSigC[164:3]; // @[Cat.scala 29:58]
  wire [162:0] alignedSigC = {_T_171,_T_170}; // @[Cat.scala 29:58]
  wire  _T_175 = rawA_isNaN & ~rawA_sig[51]; // @[common.scala 81:46]
  wire  _T_178 = rawB_isNaN & ~rawB_sig[51]; // @[common.scala 81:46]
  wire  _T_182 = rawC_isNaN & ~rawC_sig[51]; // @[common.scala 81:46]
  wire [13:0] _T_187 = $signed(sExpAlignedProd) - 14'sh35; // @[MulAddRecFN.scala 161:53]
  wire [13:0] _T_188 = CIsDominant ? $signed({{1{rawC_sExp[12]}},rawC_sExp}) : $signed(_T_187); // @[MulAddRecFN.scala 161:12]
  assign io_mulAddA = rawA_sig[52:0]; // @[MulAddRecFN.scala 144:16]
  assign io_mulAddB = rawB_sig[52:0]; // @[MulAddRecFN.scala 145:16]
  assign io_mulAddC = alignedSigC[106:1]; // @[MulAddRecFN.scala 146:30]
  assign io_toPostMul_isSigNaNAny = _T_175 | _T_178 | _T_182; // @[MulAddRecFN.scala 149:58]
  assign io_toPostMul_isNaNAOrB = rawA_isNaN | rawB_isNaN; // @[MulAddRecFN.scala 151:42]
  assign io_toPostMul_isInfA = _T_4 & ~io_a[61]; // @[rawFloatFromRecFN.scala 56:33]
  assign io_toPostMul_isZeroA = io_a[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  assign io_toPostMul_isInfB = _T_20 & ~io_b[61]; // @[rawFloatFromRecFN.scala 56:33]
  assign io_toPostMul_isZeroB = io_b[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  assign io_toPostMul_signProd = rawA_sign ^ rawB_sign ^ io_op[1]; // @[MulAddRecFN.scala 98:42]
  assign io_toPostMul_isNaNC = _T_36 & io_c[61]; // @[rawFloatFromRecFN.scala 55:33]
  assign io_toPostMul_isInfC = _T_36 & ~io_c[61]; // @[rawFloatFromRecFN.scala 56:33]
  assign io_toPostMul_isZeroC = io_c[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  assign io_toPostMul_sExpSum = _T_188[12:0]; // @[MulAddRecFN.scala 160:28]
  assign io_toPostMul_doSubMags = signProd ^ rawC_sign ^ io_op[0]; // @[MulAddRecFN.scala 103:42]
  assign io_toPostMul_CIsDominant = _T_44 & (isMinCAlign | posNatCAlignDist <= 13'h35); // @[MulAddRecFN.scala 111:23]
  assign io_toPostMul_CDom_CAlignDist = CAlignDist[5:0]; // @[MulAddRecFN.scala 164:47]
  assign io_toPostMul_highAlignedSigC = alignedSigC[161:107]; // @[MulAddRecFN.scala 166:20]
  assign io_toPostMul_bit0AlignedSigC = alignedSigC[0]; // @[MulAddRecFN.scala 167:48]
endmodule
module MulAddRecFNToRaw_postMul(
  input          io_fromPreMul_isSigNaNAny,
  input          io_fromPreMul_isNaNAOrB,
  input          io_fromPreMul_isInfA,
  input          io_fromPreMul_isZeroA,
  input          io_fromPreMul_isInfB,
  input          io_fromPreMul_isZeroB,
  input          io_fromPreMul_signProd,
  input          io_fromPreMul_isNaNC,
  input          io_fromPreMul_isInfC,
  input          io_fromPreMul_isZeroC,
  input  [12:0]  io_fromPreMul_sExpSum,
  input          io_fromPreMul_doSubMags,
  input          io_fromPreMul_CIsDominant,
  input  [5:0]   io_fromPreMul_CDom_CAlignDist,
  input  [54:0]  io_fromPreMul_highAlignedSigC,
  input          io_fromPreMul_bit0AlignedSigC,
  input  [106:0] io_mulAddResult,
  input  [2:0]   io_roundingMode,
  output         io_invalidExc,
  output         io_rawOut_isNaN,
  output         io_rawOut_isInf,
  output         io_rawOut_isZero,
  output         io_rawOut_sign,
  output [12:0]  io_rawOut_sExp,
  output [55:0]  io_rawOut_sig
);
  wire  roundingMode_min = io_roundingMode == 3'h2; // @[MulAddRecFN.scala 188:45]
  wire  CDom_sign = io_fromPreMul_signProd ^ io_fromPreMul_doSubMags; // @[MulAddRecFN.scala 192:42]
  wire [54:0] _T_2 = io_fromPreMul_highAlignedSigC + 55'h1; // @[MulAddRecFN.scala 195:47]
  wire [54:0] _T_3 = io_mulAddResult[106] ? _T_2 : io_fromPreMul_highAlignedSigC; // @[MulAddRecFN.scala 194:16]
  wire [161:0] sigSum = {_T_3,io_mulAddResult[105:0],io_fromPreMul_bit0AlignedSigC}; // @[Cat.scala 29:58]
  wire [1:0] _T_6 = {1'b0,$signed(io_fromPreMul_doSubMags)}; // @[MulAddRecFN.scala 205:69]
  wire [12:0] _GEN_0 = {{11{_T_6[1]}},_T_6}; // @[MulAddRecFN.scala 205:43]
  wire [12:0] CDom_sExp = $signed(io_fromPreMul_sExpSum) - $signed(_GEN_0); // @[MulAddRecFN.scala 205:43]
  wire [107:0] _T_10 = ~sigSum[161:54]; // @[MulAddRecFN.scala 208:13]
  wire [107:0] _T_14 = {1'h0,io_fromPreMul_highAlignedSigC[54:53],sigSum[159:55]}; // @[Cat.scala 29:58]
  wire [107:0] CDom_absSigSum = io_fromPreMul_doSubMags ? _T_10 : _T_14; // @[MulAddRecFN.scala 207:12]
  wire [52:0] _T_16 = ~sigSum[53:1]; // @[MulAddRecFN.scala 217:14]
  wire  _T_17 = |_T_16; // @[MulAddRecFN.scala 217:36]
  wire  _T_19 = |sigSum[54:1]; // @[MulAddRecFN.scala 218:37]
  wire  CDom_absSigSumExtra = io_fromPreMul_doSubMags ? _T_17 : _T_19; // @[MulAddRecFN.scala 216:12]
  wire [170:0] _GEN_11 = {{63'd0}, CDom_absSigSum}; // @[MulAddRecFN.scala 221:24]
  wire [170:0] _T_20 = _GEN_11 << io_fromPreMul_CDom_CAlignDist; // @[MulAddRecFN.scala 221:24]
  wire [57:0] CDom_mainSig = _T_20[107:50]; // @[MulAddRecFN.scala 221:56]
  wire [54:0] _T_22 = {CDom_absSigSum[52:0], 2'h0}; // @[MulAddRecFN.scala 224:53]
  wire  _T_25 = |_T_22[3:0]; // @[primitives.scala 121:54]
  wire  _T_27 = |_T_22[7:4]; // @[primitives.scala 121:54]
  wire  _T_29 = |_T_22[11:8]; // @[primitives.scala 121:54]
  wire  _T_31 = |_T_22[15:12]; // @[primitives.scala 121:54]
  wire  _T_33 = |_T_22[19:16]; // @[primitives.scala 121:54]
  wire  _T_35 = |_T_22[23:20]; // @[primitives.scala 121:54]
  wire  _T_37 = |_T_22[27:24]; // @[primitives.scala 121:54]
  wire  _T_39 = |_T_22[31:28]; // @[primitives.scala 121:54]
  wire  _T_41 = |_T_22[35:32]; // @[primitives.scala 121:54]
  wire  _T_43 = |_T_22[39:36]; // @[primitives.scala 121:54]
  wire  _T_45 = |_T_22[43:40]; // @[primitives.scala 121:54]
  wire  _T_47 = |_T_22[47:44]; // @[primitives.scala 121:54]
  wire  _T_49 = |_T_22[51:48]; // @[primitives.scala 121:54]
  wire  _T_51 = |_T_22[54:52]; // @[primitives.scala 124:57]
  wire [6:0] _T_57 = {_T_37,_T_35,_T_33,_T_31,_T_29,_T_27,_T_25}; // @[primitives.scala 125:20]
  wire [13:0] _T_64 = {_T_51,_T_49,_T_47,_T_45,_T_43,_T_41,_T_39,_T_57}; // @[primitives.scala 125:20]
  wire [3:0] _T_66 = ~io_fromPreMul_CDom_CAlignDist[5:2]; // @[primitives.scala 51:21]
  wire [16:0] _T_67 = 17'sh10000 >>> _T_66; // @[primitives.scala 77:58]
  wire [7:0] _GEN_1 = {{4'd0}, _T_67[8:5]}; // @[Bitwise.scala 103:31]
  wire [7:0] _T_73 = _GEN_1 & 8'hf; // @[Bitwise.scala 103:31]
  wire [7:0] _T_75 = {_T_67[4:1], 4'h0}; // @[Bitwise.scala 103:65]
  wire [7:0] _T_77 = _T_75 & 8'hf0; // @[Bitwise.scala 103:75]
  wire [7:0] _T_78 = _T_73 | _T_77; // @[Bitwise.scala 103:39]
  wire [7:0] _GEN_2 = {{2'd0}, _T_78[7:2]}; // @[Bitwise.scala 103:31]
  wire [7:0] _T_83 = _GEN_2 & 8'h33; // @[Bitwise.scala 103:31]
  wire [7:0] _T_85 = {_T_78[5:0], 2'h0}; // @[Bitwise.scala 103:65]
  wire [7:0] _T_87 = _T_85 & 8'hcc; // @[Bitwise.scala 103:75]
  wire [7:0] _T_88 = _T_83 | _T_87; // @[Bitwise.scala 103:39]
  wire [7:0] _GEN_3 = {{1'd0}, _T_88[7:1]}; // @[Bitwise.scala 103:31]
  wire [7:0] _T_93 = _GEN_3 & 8'h55; // @[Bitwise.scala 103:31]
  wire [7:0] _T_95 = {_T_88[6:0], 1'h0}; // @[Bitwise.scala 103:65]
  wire [7:0] _T_97 = _T_95 & 8'haa; // @[Bitwise.scala 103:75]
  wire [7:0] _T_98 = _T_93 | _T_97; // @[Bitwise.scala 103:39]
  wire [12:0] _T_112 = {_T_98,_T_67[9],_T_67[10],_T_67[11],_T_67[12],_T_67[13]}; // @[Cat.scala 29:58]
  wire [13:0] _GEN_4 = {{1'd0}, _T_112}; // @[MulAddRecFN.scala 224:72]
  wire [13:0] _T_113 = _T_64 & _GEN_4; // @[MulAddRecFN.scala 224:72]
  wire  CDom_reduced4SigExtra = |_T_113; // @[MulAddRecFN.scala 225:73]
  wire  _T_118 = |CDom_mainSig[2:0] | CDom_reduced4SigExtra | CDom_absSigSumExtra; // @[MulAddRecFN.scala 228:61]
  wire [55:0] CDom_sig = {CDom_mainSig[57:3],_T_118}; // @[Cat.scala 29:58]
  wire  notCDom_signSigSum = sigSum[109]; // @[MulAddRecFN.scala 234:36]
  wire [108:0] _T_120 = ~sigSum[108:0]; // @[MulAddRecFN.scala 237:13]
  wire [108:0] _GEN_5 = {{108'd0}, io_fromPreMul_doSubMags}; // @[MulAddRecFN.scala 238:41]
  wire [108:0] _T_123 = sigSum[108:0] + _GEN_5; // @[MulAddRecFN.scala 238:41]
  wire [108:0] notCDom_absSigSum = notCDom_signSigSum ? _T_120 : _T_123; // @[MulAddRecFN.scala 236:12]
  wire  _T_126 = |notCDom_absSigSum[1:0]; // @[primitives.scala 104:54]
  wire  _T_128 = |notCDom_absSigSum[3:2]; // @[primitives.scala 104:54]
  wire  _T_130 = |notCDom_absSigSum[5:4]; // @[primitives.scala 104:54]
  wire  _T_132 = |notCDom_absSigSum[7:6]; // @[primitives.scala 104:54]
  wire  _T_134 = |notCDom_absSigSum[9:8]; // @[primitives.scala 104:54]
  wire  _T_136 = |notCDom_absSigSum[11:10]; // @[primitives.scala 104:54]
  wire  _T_138 = |notCDom_absSigSum[13:12]; // @[primitives.scala 104:54]
  wire  _T_140 = |notCDom_absSigSum[15:14]; // @[primitives.scala 104:54]
  wire  _T_142 = |notCDom_absSigSum[17:16]; // @[primitives.scala 104:54]
  wire  _T_144 = |notCDom_absSigSum[19:18]; // @[primitives.scala 104:54]
  wire  _T_146 = |notCDom_absSigSum[21:20]; // @[primitives.scala 104:54]
  wire  _T_148 = |notCDom_absSigSum[23:22]; // @[primitives.scala 104:54]
  wire  _T_150 = |notCDom_absSigSum[25:24]; // @[primitives.scala 104:54]
  wire  _T_152 = |notCDom_absSigSum[27:26]; // @[primitives.scala 104:54]
  wire  _T_154 = |notCDom_absSigSum[29:28]; // @[primitives.scala 104:54]
  wire  _T_156 = |notCDom_absSigSum[31:30]; // @[primitives.scala 104:54]
  wire  _T_158 = |notCDom_absSigSum[33:32]; // @[primitives.scala 104:54]
  wire  _T_160 = |notCDom_absSigSum[35:34]; // @[primitives.scala 104:54]
  wire  _T_162 = |notCDom_absSigSum[37:36]; // @[primitives.scala 104:54]
  wire  _T_164 = |notCDom_absSigSum[39:38]; // @[primitives.scala 104:54]
  wire  _T_166 = |notCDom_absSigSum[41:40]; // @[primitives.scala 104:54]
  wire  _T_168 = |notCDom_absSigSum[43:42]; // @[primitives.scala 104:54]
  wire  _T_170 = |notCDom_absSigSum[45:44]; // @[primitives.scala 104:54]
  wire  _T_172 = |notCDom_absSigSum[47:46]; // @[primitives.scala 104:54]
  wire  _T_174 = |notCDom_absSigSum[49:48]; // @[primitives.scala 104:54]
  wire  _T_176 = |notCDom_absSigSum[51:50]; // @[primitives.scala 104:54]
  wire  _T_178 = |notCDom_absSigSum[53:52]; // @[primitives.scala 104:54]
  wire  _T_180 = |notCDom_absSigSum[55:54]; // @[primitives.scala 104:54]
  wire  _T_182 = |notCDom_absSigSum[57:56]; // @[primitives.scala 104:54]
  wire  _T_184 = |notCDom_absSigSum[59:58]; // @[primitives.scala 104:54]
  wire  _T_186 = |notCDom_absSigSum[61:60]; // @[primitives.scala 104:54]
  wire  _T_188 = |notCDom_absSigSum[63:62]; // @[primitives.scala 104:54]
  wire  _T_190 = |notCDom_absSigSum[65:64]; // @[primitives.scala 104:54]
  wire  _T_192 = |notCDom_absSigSum[67:66]; // @[primitives.scala 104:54]
  wire  _T_194 = |notCDom_absSigSum[69:68]; // @[primitives.scala 104:54]
  wire  _T_196 = |notCDom_absSigSum[71:70]; // @[primitives.scala 104:54]
  wire  _T_198 = |notCDom_absSigSum[73:72]; // @[primitives.scala 104:54]
  wire  _T_200 = |notCDom_absSigSum[75:74]; // @[primitives.scala 104:54]
  wire  _T_202 = |notCDom_absSigSum[77:76]; // @[primitives.scala 104:54]
  wire  _T_204 = |notCDom_absSigSum[79:78]; // @[primitives.scala 104:54]
  wire  _T_206 = |notCDom_absSigSum[81:80]; // @[primitives.scala 104:54]
  wire  _T_208 = |notCDom_absSigSum[83:82]; // @[primitives.scala 104:54]
  wire  _T_210 = |notCDom_absSigSum[85:84]; // @[primitives.scala 104:54]
  wire  _T_212 = |notCDom_absSigSum[87:86]; // @[primitives.scala 104:54]
  wire  _T_214 = |notCDom_absSigSum[89:88]; // @[primitives.scala 104:54]
  wire  _T_216 = |notCDom_absSigSum[91:90]; // @[primitives.scala 104:54]
  wire  _T_218 = |notCDom_absSigSum[93:92]; // @[primitives.scala 104:54]
  wire  _T_220 = |notCDom_absSigSum[95:94]; // @[primitives.scala 104:54]
  wire  _T_222 = |notCDom_absSigSum[97:96]; // @[primitives.scala 104:54]
  wire  _T_224 = |notCDom_absSigSum[99:98]; // @[primitives.scala 104:54]
  wire  _T_226 = |notCDom_absSigSum[101:100]; // @[primitives.scala 104:54]
  wire  _T_228 = |notCDom_absSigSum[103:102]; // @[primitives.scala 104:54]
  wire  _T_230 = |notCDom_absSigSum[105:104]; // @[primitives.scala 104:54]
  wire  _T_232 = |notCDom_absSigSum[107:106]; // @[primitives.scala 104:54]
  wire  _T_234 = |notCDom_absSigSum[108]; // @[primitives.scala 107:57]
  wire [5:0] _T_239 = {_T_136,_T_134,_T_132,_T_130,_T_128,_T_126}; // @[primitives.scala 108:20]
  wire [12:0] _T_246 = {_T_150,_T_148,_T_146,_T_144,_T_142,_T_140,_T_138,_T_239}; // @[primitives.scala 108:20]
  wire [6:0] _T_252 = {_T_164,_T_162,_T_160,_T_158,_T_156,_T_154,_T_152}; // @[primitives.scala 108:20]
  wire [26:0] _T_260 = {_T_178,_T_176,_T_174,_T_172,_T_170,_T_168,_T_166,_T_252,_T_246}; // @[primitives.scala 108:20]
  wire [6:0] _T_266 = {_T_192,_T_190,_T_188,_T_186,_T_184,_T_182,_T_180}; // @[primitives.scala 108:20]
  wire [13:0] _T_273 = {_T_206,_T_204,_T_202,_T_200,_T_198,_T_196,_T_194,_T_266}; // @[primitives.scala 108:20]
  wire [6:0] _T_279 = {_T_220,_T_218,_T_216,_T_214,_T_212,_T_210,_T_208}; // @[primitives.scala 108:20]
  wire [54:0] notCDom_reduced2AbsSigSum = {_T_234,_T_232,_T_230,_T_228,_T_226,_T_224,_T_222,_T_279,_T_273,_T_260}; // @[primitives.scala 108:20]
  wire [5:0] _T_343 = notCDom_reduced2AbsSigSum[1] ? 6'h35 : 6'h36; // @[Mux.scala 47:69]
  wire [5:0] _T_344 = notCDom_reduced2AbsSigSum[2] ? 6'h34 : _T_343; // @[Mux.scala 47:69]
  wire [5:0] _T_345 = notCDom_reduced2AbsSigSum[3] ? 6'h33 : _T_344; // @[Mux.scala 47:69]
  wire [5:0] _T_346 = notCDom_reduced2AbsSigSum[4] ? 6'h32 : _T_345; // @[Mux.scala 47:69]
  wire [5:0] _T_347 = notCDom_reduced2AbsSigSum[5] ? 6'h31 : _T_346; // @[Mux.scala 47:69]
  wire [5:0] _T_348 = notCDom_reduced2AbsSigSum[6] ? 6'h30 : _T_347; // @[Mux.scala 47:69]
  wire [5:0] _T_349 = notCDom_reduced2AbsSigSum[7] ? 6'h2f : _T_348; // @[Mux.scala 47:69]
  wire [5:0] _T_350 = notCDom_reduced2AbsSigSum[8] ? 6'h2e : _T_349; // @[Mux.scala 47:69]
  wire [5:0] _T_351 = notCDom_reduced2AbsSigSum[9] ? 6'h2d : _T_350; // @[Mux.scala 47:69]
  wire [5:0] _T_352 = notCDom_reduced2AbsSigSum[10] ? 6'h2c : _T_351; // @[Mux.scala 47:69]
  wire [5:0] _T_353 = notCDom_reduced2AbsSigSum[11] ? 6'h2b : _T_352; // @[Mux.scala 47:69]
  wire [5:0] _T_354 = notCDom_reduced2AbsSigSum[12] ? 6'h2a : _T_353; // @[Mux.scala 47:69]
  wire [5:0] _T_355 = notCDom_reduced2AbsSigSum[13] ? 6'h29 : _T_354; // @[Mux.scala 47:69]
  wire [5:0] _T_356 = notCDom_reduced2AbsSigSum[14] ? 6'h28 : _T_355; // @[Mux.scala 47:69]
  wire [5:0] _T_357 = notCDom_reduced2AbsSigSum[15] ? 6'h27 : _T_356; // @[Mux.scala 47:69]
  wire [5:0] _T_358 = notCDom_reduced2AbsSigSum[16] ? 6'h26 : _T_357; // @[Mux.scala 47:69]
  wire [5:0] _T_359 = notCDom_reduced2AbsSigSum[17] ? 6'h25 : _T_358; // @[Mux.scala 47:69]
  wire [5:0] _T_360 = notCDom_reduced2AbsSigSum[18] ? 6'h24 : _T_359; // @[Mux.scala 47:69]
  wire [5:0] _T_361 = notCDom_reduced2AbsSigSum[19] ? 6'h23 : _T_360; // @[Mux.scala 47:69]
  wire [5:0] _T_362 = notCDom_reduced2AbsSigSum[20] ? 6'h22 : _T_361; // @[Mux.scala 47:69]
  wire [5:0] _T_363 = notCDom_reduced2AbsSigSum[21] ? 6'h21 : _T_362; // @[Mux.scala 47:69]
  wire [5:0] _T_364 = notCDom_reduced2AbsSigSum[22] ? 6'h20 : _T_363; // @[Mux.scala 47:69]
  wire [5:0] _T_365 = notCDom_reduced2AbsSigSum[23] ? 6'h1f : _T_364; // @[Mux.scala 47:69]
  wire [5:0] _T_366 = notCDom_reduced2AbsSigSum[24] ? 6'h1e : _T_365; // @[Mux.scala 47:69]
  wire [5:0] _T_367 = notCDom_reduced2AbsSigSum[25] ? 6'h1d : _T_366; // @[Mux.scala 47:69]
  wire [5:0] _T_368 = notCDom_reduced2AbsSigSum[26] ? 6'h1c : _T_367; // @[Mux.scala 47:69]
  wire [5:0] _T_369 = notCDom_reduced2AbsSigSum[27] ? 6'h1b : _T_368; // @[Mux.scala 47:69]
  wire [5:0] _T_370 = notCDom_reduced2AbsSigSum[28] ? 6'h1a : _T_369; // @[Mux.scala 47:69]
  wire [5:0] _T_371 = notCDom_reduced2AbsSigSum[29] ? 6'h19 : _T_370; // @[Mux.scala 47:69]
  wire [5:0] _T_372 = notCDom_reduced2AbsSigSum[30] ? 6'h18 : _T_371; // @[Mux.scala 47:69]
  wire [5:0] _T_373 = notCDom_reduced2AbsSigSum[31] ? 6'h17 : _T_372; // @[Mux.scala 47:69]
  wire [5:0] _T_374 = notCDom_reduced2AbsSigSum[32] ? 6'h16 : _T_373; // @[Mux.scala 47:69]
  wire [5:0] _T_375 = notCDom_reduced2AbsSigSum[33] ? 6'h15 : _T_374; // @[Mux.scala 47:69]
  wire [5:0] _T_376 = notCDom_reduced2AbsSigSum[34] ? 6'h14 : _T_375; // @[Mux.scala 47:69]
  wire [5:0] _T_377 = notCDom_reduced2AbsSigSum[35] ? 6'h13 : _T_376; // @[Mux.scala 47:69]
  wire [5:0] _T_378 = notCDom_reduced2AbsSigSum[36] ? 6'h12 : _T_377; // @[Mux.scala 47:69]
  wire [5:0] _T_379 = notCDom_reduced2AbsSigSum[37] ? 6'h11 : _T_378; // @[Mux.scala 47:69]
  wire [5:0] _T_380 = notCDom_reduced2AbsSigSum[38] ? 6'h10 : _T_379; // @[Mux.scala 47:69]
  wire [5:0] _T_381 = notCDom_reduced2AbsSigSum[39] ? 6'hf : _T_380; // @[Mux.scala 47:69]
  wire [5:0] _T_382 = notCDom_reduced2AbsSigSum[40] ? 6'he : _T_381; // @[Mux.scala 47:69]
  wire [5:0] _T_383 = notCDom_reduced2AbsSigSum[41] ? 6'hd : _T_382; // @[Mux.scala 47:69]
  wire [5:0] _T_384 = notCDom_reduced2AbsSigSum[42] ? 6'hc : _T_383; // @[Mux.scala 47:69]
  wire [5:0] _T_385 = notCDom_reduced2AbsSigSum[43] ? 6'hb : _T_384; // @[Mux.scala 47:69]
  wire [5:0] _T_386 = notCDom_reduced2AbsSigSum[44] ? 6'ha : _T_385; // @[Mux.scala 47:69]
  wire [5:0] _T_387 = notCDom_reduced2AbsSigSum[45] ? 6'h9 : _T_386; // @[Mux.scala 47:69]
  wire [5:0] _T_388 = notCDom_reduced2AbsSigSum[46] ? 6'h8 : _T_387; // @[Mux.scala 47:69]
  wire [5:0] _T_389 = notCDom_reduced2AbsSigSum[47] ? 6'h7 : _T_388; // @[Mux.scala 47:69]
  wire [5:0] _T_390 = notCDom_reduced2AbsSigSum[48] ? 6'h6 : _T_389; // @[Mux.scala 47:69]
  wire [5:0] _T_391 = notCDom_reduced2AbsSigSum[49] ? 6'h5 : _T_390; // @[Mux.scala 47:69]
  wire [5:0] _T_392 = notCDom_reduced2AbsSigSum[50] ? 6'h4 : _T_391; // @[Mux.scala 47:69]
  wire [5:0] _T_393 = notCDom_reduced2AbsSigSum[51] ? 6'h3 : _T_392; // @[Mux.scala 47:69]
  wire [5:0] _T_394 = notCDom_reduced2AbsSigSum[52] ? 6'h2 : _T_393; // @[Mux.scala 47:69]
  wire [5:0] _T_395 = notCDom_reduced2AbsSigSum[53] ? 6'h1 : _T_394; // @[Mux.scala 47:69]
  wire [5:0] notCDom_normDistReduced2 = notCDom_reduced2AbsSigSum[54] ? 6'h0 : _T_395; // @[Mux.scala 47:69]
  wire [6:0] notCDom_nearNormDist = {notCDom_normDistReduced2, 1'h0}; // @[MulAddRecFN.scala 242:56]
  wire [7:0] _T_396 = {1'b0,$signed(notCDom_nearNormDist)}; // @[MulAddRecFN.scala 243:69]
  wire [12:0] _GEN_6 = {{5{_T_396[7]}},_T_396}; // @[MulAddRecFN.scala 243:46]
  wire [12:0] notCDom_sExp = $signed(io_fromPreMul_sExpSum) - $signed(_GEN_6); // @[MulAddRecFN.scala 243:46]
  wire [235:0] _GEN_12 = {{127'd0}, notCDom_absSigSum}; // @[MulAddRecFN.scala 245:27]
  wire [235:0] _T_399 = _GEN_12 << notCDom_nearNormDist; // @[MulAddRecFN.scala 245:27]
  wire [57:0] notCDom_mainSig = _T_399[109:52]; // @[MulAddRecFN.scala 245:50]
  wire  _T_404 = |notCDom_reduced2AbsSigSum[1:0]; // @[primitives.scala 104:54]
  wire  _T_406 = |notCDom_reduced2AbsSigSum[3:2]; // @[primitives.scala 104:54]
  wire  _T_408 = |notCDom_reduced2AbsSigSum[5:4]; // @[primitives.scala 104:54]
  wire  _T_410 = |notCDom_reduced2AbsSigSum[7:6]; // @[primitives.scala 104:54]
  wire  _T_412 = |notCDom_reduced2AbsSigSum[9:8]; // @[primitives.scala 104:54]
  wire  _T_414 = |notCDom_reduced2AbsSigSum[11:10]; // @[primitives.scala 104:54]
  wire  _T_416 = |notCDom_reduced2AbsSigSum[13:12]; // @[primitives.scala 104:54]
  wire  _T_418 = |notCDom_reduced2AbsSigSum[15:14]; // @[primitives.scala 104:54]
  wire  _T_420 = |notCDom_reduced2AbsSigSum[17:16]; // @[primitives.scala 104:54]
  wire  _T_422 = |notCDom_reduced2AbsSigSum[19:18]; // @[primitives.scala 104:54]
  wire  _T_424 = |notCDom_reduced2AbsSigSum[21:20]; // @[primitives.scala 104:54]
  wire  _T_426 = |notCDom_reduced2AbsSigSum[23:22]; // @[primitives.scala 104:54]
  wire  _T_428 = |notCDom_reduced2AbsSigSum[25:24]; // @[primitives.scala 104:54]
  wire  _T_430 = |notCDom_reduced2AbsSigSum[26]; // @[primitives.scala 107:57]
  wire [6:0] _T_436 = {_T_416,_T_414,_T_412,_T_410,_T_408,_T_406,_T_404}; // @[primitives.scala 108:20]
  wire [13:0] _T_443 = {_T_430,_T_428,_T_426,_T_424,_T_422,_T_420,_T_418,_T_436}; // @[primitives.scala 108:20]
  wire [4:0] _T_445 = ~notCDom_normDistReduced2[5:1]; // @[primitives.scala 51:21]
  wire [32:0] _T_446 = 33'sh100000000 >>> _T_445; // @[primitives.scala 77:58]
  wire [7:0] _GEN_7 = {{4'd0}, _T_446[8:5]}; // @[Bitwise.scala 103:31]
  wire [7:0] _T_452 = _GEN_7 & 8'hf; // @[Bitwise.scala 103:31]
  wire [7:0] _T_454 = {_T_446[4:1], 4'h0}; // @[Bitwise.scala 103:65]
  wire [7:0] _T_456 = _T_454 & 8'hf0; // @[Bitwise.scala 103:75]
  wire [7:0] _T_457 = _T_452 | _T_456; // @[Bitwise.scala 103:39]
  wire [7:0] _GEN_8 = {{2'd0}, _T_457[7:2]}; // @[Bitwise.scala 103:31]
  wire [7:0] _T_462 = _GEN_8 & 8'h33; // @[Bitwise.scala 103:31]
  wire [7:0] _T_464 = {_T_457[5:0], 2'h0}; // @[Bitwise.scala 103:65]
  wire [7:0] _T_466 = _T_464 & 8'hcc; // @[Bitwise.scala 103:75]
  wire [7:0] _T_467 = _T_462 | _T_466; // @[Bitwise.scala 103:39]
  wire [7:0] _GEN_9 = {{1'd0}, _T_467[7:1]}; // @[Bitwise.scala 103:31]
  wire [7:0] _T_472 = _GEN_9 & 8'h55; // @[Bitwise.scala 103:31]
  wire [7:0] _T_474 = {_T_467[6:0], 1'h0}; // @[Bitwise.scala 103:65]
  wire [7:0] _T_476 = _T_474 & 8'haa; // @[Bitwise.scala 103:75]
  wire [7:0] _T_477 = _T_472 | _T_476; // @[Bitwise.scala 103:39]
  wire [12:0] _T_491 = {_T_477,_T_446[9],_T_446[10],_T_446[11],_T_446[12],_T_446[13]}; // @[Cat.scala 29:58]
  wire [13:0] _GEN_10 = {{1'd0}, _T_491}; // @[MulAddRecFN.scala 249:78]
  wire [13:0] _T_492 = _T_443 & _GEN_10; // @[MulAddRecFN.scala 249:78]
  wire  notCDom_reduced4SigExtra = |_T_492; // @[MulAddRecFN.scala 251:11]
  wire  _T_496 = |notCDom_mainSig[2:0] | notCDom_reduced4SigExtra; // @[MulAddRecFN.scala 254:39]
  wire [55:0] notCDom_sig = {notCDom_mainSig[57:3],_T_496}; // @[Cat.scala 29:58]
  wire  notCDom_completeCancellation = notCDom_sig[55:54] == 2'h0; // @[MulAddRecFN.scala 257:50]
  wire  _T_498 = io_fromPreMul_signProd ^ notCDom_signSigSum; // @[MulAddRecFN.scala 261:36]
  wire  notCDom_sign = notCDom_completeCancellation ? roundingMode_min : _T_498; // @[MulAddRecFN.scala 259:12]
  wire  notNaN_isInfProd = io_fromPreMul_isInfA | io_fromPreMul_isInfB; // @[MulAddRecFN.scala 266:49]
  wire  notNaN_isInfOut = notNaN_isInfProd | io_fromPreMul_isInfC; // @[MulAddRecFN.scala 267:44]
  wire  notNaN_addZeros = (io_fromPreMul_isZeroA | io_fromPreMul_isZeroB) & io_fromPreMul_isZeroC; // @[MulAddRecFN.scala 269:58]
  wire  _T_500 = io_fromPreMul_isInfA & io_fromPreMul_isZeroB; // @[MulAddRecFN.scala 274:31]
  wire  _T_501 = io_fromPreMul_isSigNaNAny | _T_500; // @[MulAddRecFN.scala 273:35]
  wire  _T_502 = io_fromPreMul_isZeroA & io_fromPreMul_isInfB; // @[MulAddRecFN.scala 275:32]
  wire  _T_503 = _T_501 | _T_502; // @[MulAddRecFN.scala 274:57]
  wire  _T_506 = ~io_fromPreMul_isNaNAOrB & notNaN_isInfProd; // @[MulAddRecFN.scala 276:36]
  wire  _T_507 = _T_506 & io_fromPreMul_isInfC; // @[MulAddRecFN.scala 277:61]
  wire  _T_508 = _T_507 & io_fromPreMul_doSubMags; // @[MulAddRecFN.scala 278:35]
  wire  _T_512 = ~io_fromPreMul_CIsDominant & notCDom_completeCancellation; // @[MulAddRecFN.scala 285:42]
  wire  _T_515 = io_fromPreMul_isInfC & CDom_sign; // @[MulAddRecFN.scala 288:31]
  wire  _T_516 = notNaN_isInfProd & io_fromPreMul_signProd | _T_515; // @[MulAddRecFN.scala 287:54]
  wire  _T_519 = notNaN_addZeros & ~roundingMode_min & io_fromPreMul_signProd; // @[MulAddRecFN.scala 289:48]
  wire  _T_520 = _T_519 & CDom_sign; // @[MulAddRecFN.scala 290:36]
  wire  _T_521 = _T_516 | _T_520; // @[MulAddRecFN.scala 288:43]
  wire  _T_523 = io_fromPreMul_signProd | CDom_sign; // @[MulAddRecFN.scala 292:37]
  wire  _T_524 = notNaN_addZeros & roundingMode_min & _T_523; // @[MulAddRecFN.scala 291:46]
  wire  _T_525 = _T_521 | _T_524; // @[MulAddRecFN.scala 290:48]
  wire  _T_529 = io_fromPreMul_CIsDominant ? CDom_sign : notCDom_sign; // @[MulAddRecFN.scala 294:17]
  wire  _T_530 = ~notNaN_isInfOut & ~notNaN_addZeros & _T_529; // @[MulAddRecFN.scala 293:49]
  assign io_invalidExc = _T_503 | _T_508; // @[MulAddRecFN.scala 275:57]
  assign io_rawOut_isNaN = io_fromPreMul_isNaNAOrB | io_fromPreMul_isNaNC; // @[MulAddRecFN.scala 280:48]
  assign io_rawOut_isInf = notNaN_isInfProd | io_fromPreMul_isInfC; // @[MulAddRecFN.scala 267:44]
  assign io_rawOut_isZero = notNaN_addZeros | _T_512; // @[MulAddRecFN.scala 284:25]
  assign io_rawOut_sign = _T_525 | _T_530; // @[MulAddRecFN.scala 292:50]
  assign io_rawOut_sExp = io_fromPreMul_CIsDominant ? $signed(CDom_sExp) : $signed(notCDom_sExp); // @[MulAddRecFN.scala 295:26]
  assign io_rawOut_sig = io_fromPreMul_CIsDominant ? CDom_sig : notCDom_sig; // @[MulAddRecFN.scala 296:25]
endmodule
module MulAddRecFNPipe(
  input         clock,
  input         reset,
  input         io_validin,
  input  [1:0]  io_op,
  input  [64:0] io_a,
  input  [64:0] io_b,
  input  [64:0] io_c,
  input  [2:0]  io_roundingMode,
  output [64:0] io_out,
  output [4:0]  io_exceptionFlags,
  output        io_validout
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
  reg [63:0] _RAND_14;
  reg [31:0] _RAND_15;
  reg [127:0] _RAND_16;
  reg [31:0] _RAND_17;
  reg [31:0] _RAND_18;
  reg [31:0] _RAND_19;
  reg [31:0] _RAND_20;
  reg [31:0] _RAND_21;
  reg [31:0] _RAND_22;
  reg [31:0] _RAND_23;
  reg [31:0] _RAND_24;
  reg [31:0] _RAND_25;
  reg [63:0] _RAND_26;
  reg [31:0] _RAND_27;
  reg [31:0] _RAND_28;
`endif // RANDOMIZE_REG_INIT
  wire [1:0] mulAddRecFNToRaw_preMul_io_op; // @[FPU.scala 597:41]
  wire [64:0] mulAddRecFNToRaw_preMul_io_a; // @[FPU.scala 597:41]
  wire [64:0] mulAddRecFNToRaw_preMul_io_b; // @[FPU.scala 597:41]
  wire [64:0] mulAddRecFNToRaw_preMul_io_c; // @[FPU.scala 597:41]
  wire [52:0] mulAddRecFNToRaw_preMul_io_mulAddA; // @[FPU.scala 597:41]
  wire [52:0] mulAddRecFNToRaw_preMul_io_mulAddB; // @[FPU.scala 597:41]
  wire [105:0] mulAddRecFNToRaw_preMul_io_mulAddC; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isSigNaNAny; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isNaNAOrB; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isInfA; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isZeroA; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isInfB; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isZeroB; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_signProd; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isNaNC; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isInfC; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isZeroC; // @[FPU.scala 597:41]
  wire [12:0] mulAddRecFNToRaw_preMul_io_toPostMul_sExpSum; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_doSubMags; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_CIsDominant; // @[FPU.scala 597:41]
  wire [5:0] mulAddRecFNToRaw_preMul_io_toPostMul_CDom_CAlignDist; // @[FPU.scala 597:41]
  wire [54:0] mulAddRecFNToRaw_preMul_io_toPostMul_highAlignedSigC; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_bit0AlignedSigC; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isSigNaNAny; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isNaNAOrB; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isInfA; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroA; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isInfB; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroB; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_signProd; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isNaNC; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isInfC; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroC; // @[FPU.scala 598:42]
  wire [12:0] mulAddRecFNToRaw_postMul_io_fromPreMul_sExpSum; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_doSubMags; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_CIsDominant; // @[FPU.scala 598:42]
  wire [5:0] mulAddRecFNToRaw_postMul_io_fromPreMul_CDom_CAlignDist; // @[FPU.scala 598:42]
  wire [54:0] mulAddRecFNToRaw_postMul_io_fromPreMul_highAlignedSigC; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_bit0AlignedSigC; // @[FPU.scala 598:42]
  wire [106:0] mulAddRecFNToRaw_postMul_io_mulAddResult; // @[FPU.scala 598:42]
  wire [2:0] mulAddRecFNToRaw_postMul_io_roundingMode; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_invalidExc; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_rawOut_isNaN; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_rawOut_isInf; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_rawOut_isZero; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_rawOut_sign; // @[FPU.scala 598:42]
  wire [12:0] mulAddRecFNToRaw_postMul_io_rawOut_sExp; // @[FPU.scala 598:42]
  wire [55:0] mulAddRecFNToRaw_postMul_io_rawOut_sig; // @[FPU.scala 598:42]
  wire  roundRawFNToRecFN_io_invalidExc; // @[FPU.scala 625:35]
  wire  roundRawFNToRecFN_io_in_isNaN; // @[FPU.scala 625:35]
  wire  roundRawFNToRecFN_io_in_isInf; // @[FPU.scala 625:35]
  wire  roundRawFNToRecFN_io_in_isZero; // @[FPU.scala 625:35]
  wire  roundRawFNToRecFN_io_in_sign; // @[FPU.scala 625:35]
  wire [12:0] roundRawFNToRecFN_io_in_sExp; // @[FPU.scala 625:35]
  wire [55:0] roundRawFNToRecFN_io_in_sig; // @[FPU.scala 625:35]
  wire [2:0] roundRawFNToRecFN_io_roundingMode; // @[FPU.scala 625:35]
  wire [64:0] roundRawFNToRecFN_io_out; // @[FPU.scala 625:35]
  wire [4:0] roundRawFNToRecFN_io_exceptionFlags; // @[FPU.scala 625:35]
  wire [105:0] _T = mulAddRecFNToRaw_preMul_io_mulAddA * mulAddRecFNToRaw_preMul_io_mulAddB; // @[FPU.scala 606:45]
  wire [106:0] mulAddResult = _T + mulAddRecFNToRaw_preMul_io_mulAddC; // @[FPU.scala 607:50]
  reg  _T_2_isSigNaNAny; // @[Reg.scala 15:16]
  reg  _T_2_isNaNAOrB; // @[Reg.scala 15:16]
  reg  _T_2_isInfA; // @[Reg.scala 15:16]
  reg  _T_2_isZeroA; // @[Reg.scala 15:16]
  reg  _T_2_isInfB; // @[Reg.scala 15:16]
  reg  _T_2_isZeroB; // @[Reg.scala 15:16]
  reg  _T_2_signProd; // @[Reg.scala 15:16]
  reg  _T_2_isNaNC; // @[Reg.scala 15:16]
  reg  _T_2_isInfC; // @[Reg.scala 15:16]
  reg  _T_2_isZeroC; // @[Reg.scala 15:16]
  reg [12:0] _T_2_sExpSum; // @[Reg.scala 15:16]
  reg  _T_2_doSubMags; // @[Reg.scala 15:16]
  reg  _T_2_CIsDominant; // @[Reg.scala 15:16]
  reg [5:0] _T_2_CDom_CAlignDist; // @[Reg.scala 15:16]
  reg [54:0] _T_2_highAlignedSigC; // @[Reg.scala 15:16]
  reg  _T_2_bit0AlignedSigC; // @[Reg.scala 15:16]
  reg [106:0] _T_5; // @[Reg.scala 15:16]
  reg [2:0] _T_8; // @[Reg.scala 15:16]
  reg [2:0] roundingMode_stage0; // @[Reg.scala 15:16]
  reg  valid_stage0; // @[Valid.scala 117:22]
  reg  _T_20; // @[Reg.scala 15:16]
  reg  _T_23_isNaN; // @[Reg.scala 15:16]
  reg  _T_23_isInf; // @[Reg.scala 15:16]
  reg  _T_23_isZero; // @[Reg.scala 15:16]
  reg  _T_23_sign; // @[Reg.scala 15:16]
  reg [12:0] _T_23_sExp; // @[Reg.scala 15:16]
  reg [55:0] _T_23_sig; // @[Reg.scala 15:16]
  reg [2:0] _T_26; // @[Reg.scala 15:16]
  reg  _T_31; // @[Valid.scala 117:22]
  MulAddRecFNToRaw_preMul mulAddRecFNToRaw_preMul ( // @[FPU.scala 597:41]
    .io_op(mulAddRecFNToRaw_preMul_io_op),
    .io_a(mulAddRecFNToRaw_preMul_io_a),
    .io_b(mulAddRecFNToRaw_preMul_io_b),
    .io_c(mulAddRecFNToRaw_preMul_io_c),
    .io_mulAddA(mulAddRecFNToRaw_preMul_io_mulAddA),
    .io_mulAddB(mulAddRecFNToRaw_preMul_io_mulAddB),
    .io_mulAddC(mulAddRecFNToRaw_preMul_io_mulAddC),
    .io_toPostMul_isSigNaNAny(mulAddRecFNToRaw_preMul_io_toPostMul_isSigNaNAny),
    .io_toPostMul_isNaNAOrB(mulAddRecFNToRaw_preMul_io_toPostMul_isNaNAOrB),
    .io_toPostMul_isInfA(mulAddRecFNToRaw_preMul_io_toPostMul_isInfA),
    .io_toPostMul_isZeroA(mulAddRecFNToRaw_preMul_io_toPostMul_isZeroA),
    .io_toPostMul_isInfB(mulAddRecFNToRaw_preMul_io_toPostMul_isInfB),
    .io_toPostMul_isZeroB(mulAddRecFNToRaw_preMul_io_toPostMul_isZeroB),
    .io_toPostMul_signProd(mulAddRecFNToRaw_preMul_io_toPostMul_signProd),
    .io_toPostMul_isNaNC(mulAddRecFNToRaw_preMul_io_toPostMul_isNaNC),
    .io_toPostMul_isInfC(mulAddRecFNToRaw_preMul_io_toPostMul_isInfC),
    .io_toPostMul_isZeroC(mulAddRecFNToRaw_preMul_io_toPostMul_isZeroC),
    .io_toPostMul_sExpSum(mulAddRecFNToRaw_preMul_io_toPostMul_sExpSum),
    .io_toPostMul_doSubMags(mulAddRecFNToRaw_preMul_io_toPostMul_doSubMags),
    .io_toPostMul_CIsDominant(mulAddRecFNToRaw_preMul_io_toPostMul_CIsDominant),
    .io_toPostMul_CDom_CAlignDist(mulAddRecFNToRaw_preMul_io_toPostMul_CDom_CAlignDist),
    .io_toPostMul_highAlignedSigC(mulAddRecFNToRaw_preMul_io_toPostMul_highAlignedSigC),
    .io_toPostMul_bit0AlignedSigC(mulAddRecFNToRaw_preMul_io_toPostMul_bit0AlignedSigC)
  );
  MulAddRecFNToRaw_postMul mulAddRecFNToRaw_postMul ( // @[FPU.scala 598:42]
    .io_fromPreMul_isSigNaNAny(mulAddRecFNToRaw_postMul_io_fromPreMul_isSigNaNAny),
    .io_fromPreMul_isNaNAOrB(mulAddRecFNToRaw_postMul_io_fromPreMul_isNaNAOrB),
    .io_fromPreMul_isInfA(mulAddRecFNToRaw_postMul_io_fromPreMul_isInfA),
    .io_fromPreMul_isZeroA(mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroA),
    .io_fromPreMul_isInfB(mulAddRecFNToRaw_postMul_io_fromPreMul_isInfB),
    .io_fromPreMul_isZeroB(mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroB),
    .io_fromPreMul_signProd(mulAddRecFNToRaw_postMul_io_fromPreMul_signProd),
    .io_fromPreMul_isNaNC(mulAddRecFNToRaw_postMul_io_fromPreMul_isNaNC),
    .io_fromPreMul_isInfC(mulAddRecFNToRaw_postMul_io_fromPreMul_isInfC),
    .io_fromPreMul_isZeroC(mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroC),
    .io_fromPreMul_sExpSum(mulAddRecFNToRaw_postMul_io_fromPreMul_sExpSum),
    .io_fromPreMul_doSubMags(mulAddRecFNToRaw_postMul_io_fromPreMul_doSubMags),
    .io_fromPreMul_CIsDominant(mulAddRecFNToRaw_postMul_io_fromPreMul_CIsDominant),
    .io_fromPreMul_CDom_CAlignDist(mulAddRecFNToRaw_postMul_io_fromPreMul_CDom_CAlignDist),
    .io_fromPreMul_highAlignedSigC(mulAddRecFNToRaw_postMul_io_fromPreMul_highAlignedSigC),
    .io_fromPreMul_bit0AlignedSigC(mulAddRecFNToRaw_postMul_io_fromPreMul_bit0AlignedSigC),
    .io_mulAddResult(mulAddRecFNToRaw_postMul_io_mulAddResult),
    .io_roundingMode(mulAddRecFNToRaw_postMul_io_roundingMode),
    .io_invalidExc(mulAddRecFNToRaw_postMul_io_invalidExc),
    .io_rawOut_isNaN(mulAddRecFNToRaw_postMul_io_rawOut_isNaN),
    .io_rawOut_isInf(mulAddRecFNToRaw_postMul_io_rawOut_isInf),
    .io_rawOut_isZero(mulAddRecFNToRaw_postMul_io_rawOut_isZero),
    .io_rawOut_sign(mulAddRecFNToRaw_postMul_io_rawOut_sign),
    .io_rawOut_sExp(mulAddRecFNToRaw_postMul_io_rawOut_sExp),
    .io_rawOut_sig(mulAddRecFNToRaw_postMul_io_rawOut_sig)
  );
  RoundRawFNToRecFN roundRawFNToRecFN ( // @[FPU.scala 625:35]
    .io_invalidExc(roundRawFNToRecFN_io_invalidExc),
    .io_in_isNaN(roundRawFNToRecFN_io_in_isNaN),
    .io_in_isInf(roundRawFNToRecFN_io_in_isInf),
    .io_in_isZero(roundRawFNToRecFN_io_in_isZero),
    .io_in_sign(roundRawFNToRecFN_io_in_sign),
    .io_in_sExp(roundRawFNToRecFN_io_in_sExp),
    .io_in_sig(roundRawFNToRecFN_io_in_sig),
    .io_roundingMode(roundRawFNToRecFN_io_roundingMode),
    .io_out(roundRawFNToRecFN_io_out),
    .io_exceptionFlags(roundRawFNToRecFN_io_exceptionFlags)
  );
  assign io_out = roundRawFNToRecFN_io_out; // @[FPU.scala 636:23]
  assign io_exceptionFlags = roundRawFNToRecFN_io_exceptionFlags; // @[FPU.scala 637:23]
  assign io_validout = _T_31; // @[Valid.scala 112:21 113:17]
  assign mulAddRecFNToRaw_preMul_io_op = io_op; // @[FPU.scala 600:35]
  assign mulAddRecFNToRaw_preMul_io_a = io_a; // @[FPU.scala 601:35]
  assign mulAddRecFNToRaw_preMul_io_b = io_b; // @[FPU.scala 602:35]
  assign mulAddRecFNToRaw_preMul_io_c = io_c; // @[FPU.scala 603:35]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isSigNaNAny = _T_2_isSigNaNAny; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isNaNAOrB = _T_2_isNaNAOrB; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isInfA = _T_2_isInfA; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroA = _T_2_isZeroA; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isInfB = _T_2_isInfB; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroB = _T_2_isZeroB; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_signProd = _T_2_signProd; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isNaNC = _T_2_isNaNC; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isInfC = _T_2_isInfC; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroC = _T_2_isZeroC; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_sExpSum = _T_2_sExpSum; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_doSubMags = _T_2_doSubMags; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_CIsDominant = _T_2_CIsDominant; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_CDom_CAlignDist = _T_2_CDom_CAlignDist; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_highAlignedSigC = _T_2_highAlignedSigC; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_bit0AlignedSigC = _T_2_bit0AlignedSigC; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_mulAddResult = _T_5; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_roundingMode = _T_8; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_invalidExc = _T_20; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_in_isNaN = _T_23_isNaN; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_in_isInf = _T_23_isInf; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_in_isZero = _T_23_isZero; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_in_sign = _T_23_sign; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_in_sExp = _T_23_sExp; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_in_sig = _T_23_sig; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_roundingMode = _T_26; // @[Valid.scala 112:21 114:16]
  always @(posedge clock) begin
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isSigNaNAny <= mulAddRecFNToRaw_preMul_io_toPostMul_isSigNaNAny; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isNaNAOrB <= mulAddRecFNToRaw_preMul_io_toPostMul_isNaNAOrB; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isInfA <= mulAddRecFNToRaw_preMul_io_toPostMul_isInfA; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isZeroA <= mulAddRecFNToRaw_preMul_io_toPostMul_isZeroA; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isInfB <= mulAddRecFNToRaw_preMul_io_toPostMul_isInfB; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isZeroB <= mulAddRecFNToRaw_preMul_io_toPostMul_isZeroB; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_signProd <= mulAddRecFNToRaw_preMul_io_toPostMul_signProd; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isNaNC <= mulAddRecFNToRaw_preMul_io_toPostMul_isNaNC; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isInfC <= mulAddRecFNToRaw_preMul_io_toPostMul_isInfC; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isZeroC <= mulAddRecFNToRaw_preMul_io_toPostMul_isZeroC; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_sExpSum <= mulAddRecFNToRaw_preMul_io_toPostMul_sExpSum; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_doSubMags <= mulAddRecFNToRaw_preMul_io_toPostMul_doSubMags; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_CIsDominant <= mulAddRecFNToRaw_preMul_io_toPostMul_CIsDominant; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_CDom_CAlignDist <= mulAddRecFNToRaw_preMul_io_toPostMul_CDom_CAlignDist; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_highAlignedSigC <= mulAddRecFNToRaw_preMul_io_toPostMul_highAlignedSigC; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_bit0AlignedSigC <= mulAddRecFNToRaw_preMul_io_toPostMul_bit0AlignedSigC; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_5 <= mulAddResult; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_8 <= io_roundingMode; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      roundingMode_stage0 <= io_roundingMode; // @[Reg.scala 16:23]
    end
    if (reset) begin // @[Valid.scala 117:22]
      valid_stage0 <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      valid_stage0 <= io_validin; // @[Valid.scala 117:22]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_20 <= mulAddRecFNToRaw_postMul_io_invalidExc; // @[Reg.scala 16:23]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_23_isNaN <= mulAddRecFNToRaw_postMul_io_rawOut_isNaN; // @[Reg.scala 16:23]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_23_isInf <= mulAddRecFNToRaw_postMul_io_rawOut_isInf; // @[Reg.scala 16:23]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_23_isZero <= mulAddRecFNToRaw_postMul_io_rawOut_isZero; // @[Reg.scala 16:23]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_23_sign <= mulAddRecFNToRaw_postMul_io_rawOut_sign; // @[Reg.scala 16:23]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_23_sExp <= mulAddRecFNToRaw_postMul_io_rawOut_sExp; // @[Reg.scala 16:23]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_23_sig <= mulAddRecFNToRaw_postMul_io_rawOut_sig; // @[Reg.scala 16:23]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_26 <= roundingMode_stage0; // @[Reg.scala 16:23]
    end
    if (reset) begin // @[Valid.scala 117:22]
      _T_31 <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      _T_31 <= valid_stage0; // @[Valid.scala 117:22]
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
  _T_2_isSigNaNAny = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  _T_2_isNaNAOrB = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  _T_2_isInfA = _RAND_2[0:0];
  _RAND_3 = {1{`RANDOM}};
  _T_2_isZeroA = _RAND_3[0:0];
  _RAND_4 = {1{`RANDOM}};
  _T_2_isInfB = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  _T_2_isZeroB = _RAND_5[0:0];
  _RAND_6 = {1{`RANDOM}};
  _T_2_signProd = _RAND_6[0:0];
  _RAND_7 = {1{`RANDOM}};
  _T_2_isNaNC = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  _T_2_isInfC = _RAND_8[0:0];
  _RAND_9 = {1{`RANDOM}};
  _T_2_isZeroC = _RAND_9[0:0];
  _RAND_10 = {1{`RANDOM}};
  _T_2_sExpSum = _RAND_10[12:0];
  _RAND_11 = {1{`RANDOM}};
  _T_2_doSubMags = _RAND_11[0:0];
  _RAND_12 = {1{`RANDOM}};
  _T_2_CIsDominant = _RAND_12[0:0];
  _RAND_13 = {1{`RANDOM}};
  _T_2_CDom_CAlignDist = _RAND_13[5:0];
  _RAND_14 = {2{`RANDOM}};
  _T_2_highAlignedSigC = _RAND_14[54:0];
  _RAND_15 = {1{`RANDOM}};
  _T_2_bit0AlignedSigC = _RAND_15[0:0];
  _RAND_16 = {4{`RANDOM}};
  _T_5 = _RAND_16[106:0];
  _RAND_17 = {1{`RANDOM}};
  _T_8 = _RAND_17[2:0];
  _RAND_18 = {1{`RANDOM}};
  roundingMode_stage0 = _RAND_18[2:0];
  _RAND_19 = {1{`RANDOM}};
  valid_stage0 = _RAND_19[0:0];
  _RAND_20 = {1{`RANDOM}};
  _T_20 = _RAND_20[0:0];
  _RAND_21 = {1{`RANDOM}};
  _T_23_isNaN = _RAND_21[0:0];
  _RAND_22 = {1{`RANDOM}};
  _T_23_isInf = _RAND_22[0:0];
  _RAND_23 = {1{`RANDOM}};
  _T_23_isZero = _RAND_23[0:0];
  _RAND_24 = {1{`RANDOM}};
  _T_23_sign = _RAND_24[0:0];
  _RAND_25 = {1{`RANDOM}};
  _T_23_sExp = _RAND_25[12:0];
  _RAND_26 = {2{`RANDOM}};
  _T_23_sig = _RAND_26[55:0];
  _RAND_27 = {1{`RANDOM}};
  _T_26 = _RAND_27[2:0];
  _RAND_28 = {1{`RANDOM}};
  _T_31 = _RAND_28[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module FPUFMAPipe(
  input         clock,
  input         reset,
  input         io_in_valid,
  input         io_in_bits_ren3,
  input         io_in_bits_swap23,
  input  [2:0]  io_in_bits_rm,
  input  [1:0]  io_in_bits_fmaCmd,
  input  [64:0] io_in_bits_in1,
  input  [64:0] io_in_bits_in2,
  input  [64:0] io_in_bits_in3,
  output        io_out_valid,
  output [64:0] io_out_bits_data,
  output [4:0]  io_out_bits_exc
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [95:0] _RAND_3;
  reg [95:0] _RAND_4;
  reg [95:0] _RAND_5;
  reg [31:0] _RAND_6;
  reg [95:0] _RAND_7;
  reg [31:0] _RAND_8;
`endif // RANDOMIZE_REG_INIT
  wire  fma_clock; // @[FPU.scala 661:19]
  wire  fma_reset; // @[FPU.scala 661:19]
  wire  fma_io_validin; // @[FPU.scala 661:19]
  wire [1:0] fma_io_op; // @[FPU.scala 661:19]
  wire [64:0] fma_io_a; // @[FPU.scala 661:19]
  wire [64:0] fma_io_b; // @[FPU.scala 661:19]
  wire [64:0] fma_io_c; // @[FPU.scala 661:19]
  wire [2:0] fma_io_roundingMode; // @[FPU.scala 661:19]
  wire [64:0] fma_io_out; // @[FPU.scala 661:19]
  wire [4:0] fma_io_exceptionFlags; // @[FPU.scala 661:19]
  wire  fma_io_validout; // @[FPU.scala 661:19]
  reg  valid; // @[FPU.scala 649:18]
  reg [2:0] in_rm; // @[FPU.scala 650:15]
  reg [1:0] in_fmaCmd; // @[FPU.scala 650:15]
  reg [64:0] in_in1; // @[FPU.scala 650:15]
  reg [64:0] in_in2; // @[FPU.scala 650:15]
  reg [64:0] in_in3; // @[FPU.scala 650:15]
  wire [64:0] _T_1 = io_in_bits_in1 ^ io_in_bits_in2; // @[FPU.scala 653:32]
  wire [64:0] _T_3 = _T_1 & 65'h10000000000000000; // @[FPU.scala 653:50]
  wire [64:0] _T_7 = fma_io_out & 65'h1efefffffffffffff; // @[FPU.scala 358:25]
  wire  _T_9 = &fma_io_out[63:61]; // @[FPU.scala 200:56]
  reg  _T_11; // @[Valid.scala 117:22]
  reg [64:0] _T_12_data; // @[Reg.scala 15:16]
  reg [4:0] _T_12_exc; // @[Reg.scala 15:16]
  wire [4:0] res_exc = fma_io_exceptionFlags; // @[FPU.scala 670:17 672:11]
  MulAddRecFNPipe fma ( // @[FPU.scala 661:19]
    .clock(fma_clock),
    .reset(fma_reset),
    .io_validin(fma_io_validin),
    .io_op(fma_io_op),
    .io_a(fma_io_a),
    .io_b(fma_io_b),
    .io_c(fma_io_c),
    .io_roundingMode(fma_io_roundingMode),
    .io_out(fma_io_out),
    .io_exceptionFlags(fma_io_exceptionFlags),
    .io_validout(fma_io_validout)
  );
  assign io_out_valid = _T_11; // @[Valid.scala 112:21 113:17]
  assign io_out_bits_data = _T_12_data; // @[Valid.scala 112:21 114:16]
  assign io_out_bits_exc = _T_12_exc; // @[Valid.scala 112:21 114:16]
  assign fma_clock = clock;
  assign fma_reset = reset;
  assign fma_io_validin = valid; // @[FPU.scala 662:18]
  assign fma_io_op = in_fmaCmd; // @[FPU.scala 663:13]
  assign fma_io_a = in_in1; // @[FPU.scala 666:12]
  assign fma_io_b = in_in2; // @[FPU.scala 667:12]
  assign fma_io_c = in_in3; // @[FPU.scala 668:12]
  assign fma_io_roundingMode = in_rm; // @[FPU.scala 664:23]
  always @(posedge clock) begin
    valid <= io_in_valid; // @[FPU.scala 649:18]
    if (io_in_valid) begin // @[FPU.scala 651:22]
      in_rm <= io_in_bits_rm; // @[FPU.scala 656:8]
    end
    if (io_in_valid) begin // @[FPU.scala 651:22]
      in_fmaCmd <= io_in_bits_fmaCmd; // @[FPU.scala 656:8]
    end
    if (io_in_valid) begin // @[FPU.scala 651:22]
      in_in1 <= io_in_bits_in1; // @[FPU.scala 656:8]
    end
    if (io_in_valid) begin // @[FPU.scala 651:22]
      if (io_in_bits_swap23) begin // @[FPU.scala 657:23]
        in_in2 <= 65'h8000000000000000; // @[FPU.scala 657:32]
      end else begin
        in_in2 <= io_in_bits_in2; // @[FPU.scala 656:8]
      end
    end
    if (io_in_valid) begin // @[FPU.scala 651:22]
      if (~(io_in_bits_ren3 | io_in_bits_swap23)) begin // @[FPU.scala 658:37]
        in_in3 <= _T_3; // @[FPU.scala 658:46]
      end else begin
        in_in3 <= io_in_bits_in3; // @[FPU.scala 656:8]
      end
    end
    if (reset) begin // @[Valid.scala 117:22]
      _T_11 <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      _T_11 <= fma_io_validout; // @[Valid.scala 117:22]
    end
    if (fma_io_validout) begin // @[Reg.scala 16:19]
      if (_T_9) begin // @[FPU.scala 359:10]
        _T_12_data <= _T_7;
      end else begin
        _T_12_data <= fma_io_out;
      end
    end
    if (fma_io_validout) begin // @[Reg.scala 16:19]
      _T_12_exc <= res_exc; // @[Reg.scala 16:23]
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
  valid = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  in_rm = _RAND_1[2:0];
  _RAND_2 = {1{`RANDOM}};
  in_fmaCmd = _RAND_2[1:0];
  _RAND_3 = {3{`RANDOM}};
  in_in1 = _RAND_3[64:0];
  _RAND_4 = {3{`RANDOM}};
  in_in2 = _RAND_4[64:0];
  _RAND_5 = {3{`RANDOM}};
  in_in3 = _RAND_5[64:0];
  _RAND_6 = {1{`RANDOM}};
  _T_11 = _RAND_6[0:0];
  _RAND_7 = {3{`RANDOM}};
  _T_12_data = _RAND_7[64:0];
  _RAND_8 = {1{`RANDOM}};
  _T_12_exc = _RAND_8[4:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module FMADecoder_2(
  input  [6:0] io_uopc,
  output [1:0] io_cmd
);
  wire [6:0] _T = io_uopc & 7'h27; // @[Decode.scala 14:65]
  wire  _T_1 = _T == 7'h0; // @[Decode.scala 14:121]
  wire [6:0] _T_2 = io_uopc & 7'h12; // @[Decode.scala 14:65]
  wire  _T_3 = _T_2 == 7'h2; // @[Decode.scala 14:121]
  wire [6:0] _T_4 = io_uopc & 7'hb; // @[Decode.scala 14:65]
  wire  _T_5 = _T_4 == 7'hb; // @[Decode.scala 14:121]
  wire [6:0] _T_6 = io_uopc & 7'he; // @[Decode.scala 14:65]
  wire  _T_7 = _T_6 == 7'he; // @[Decode.scala 14:121]
  wire  _T_11 = _T_1 | _T_3 | _T_5 | _T_7; // @[Decode.scala 15:30]
  wire [6:0] _T_12 = io_uopc & 7'h13; // @[Decode.scala 14:65]
  wire  _T_13 = _T_12 == 7'h0; // @[Decode.scala 14:121]
  wire  _T_15 = _T_12 == 7'h3; // @[Decode.scala 14:121]
  wire [6:0] _T_16 = io_uopc & 7'hf; // @[Decode.scala 14:65]
  wire  _T_17 = _T_16 == 7'hf; // @[Decode.scala 14:121]
  wire  _T_20 = _T_13 | _T_15 | _T_17; // @[Decode.scala 15:30]
  assign io_cmd = {_T_20,_T_11}; // @[Cat.scala 29:58]
endmodule
module MulAddRecFNToRaw_postMul_1(
  input         io_fromPreMul_isSigNaNAny,
  input         io_fromPreMul_isNaNAOrB,
  input         io_fromPreMul_isInfA,
  input         io_fromPreMul_isZeroA,
  input         io_fromPreMul_isInfB,
  input         io_fromPreMul_isZeroB,
  input         io_fromPreMul_signProd,
  input         io_fromPreMul_isNaNC,
  input         io_fromPreMul_isInfC,
  input         io_fromPreMul_isZeroC,
  input  [9:0]  io_fromPreMul_sExpSum,
  input         io_fromPreMul_doSubMags,
  input         io_fromPreMul_CIsDominant,
  input  [4:0]  io_fromPreMul_CDom_CAlignDist,
  input  [25:0] io_fromPreMul_highAlignedSigC,
  input         io_fromPreMul_bit0AlignedSigC,
  input  [48:0] io_mulAddResult,
  input  [2:0]  io_roundingMode,
  output        io_invalidExc,
  output        io_rawOut_isNaN,
  output        io_rawOut_isInf,
  output        io_rawOut_isZero,
  output        io_rawOut_sign,
  output [9:0]  io_rawOut_sExp,
  output [26:0] io_rawOut_sig
);
  wire  roundingMode_min = io_roundingMode == 3'h2; // @[MulAddRecFN.scala 188:45]
  wire  CDom_sign = io_fromPreMul_signProd ^ io_fromPreMul_doSubMags; // @[MulAddRecFN.scala 192:42]
  wire [25:0] _T_2 = io_fromPreMul_highAlignedSigC + 26'h1; // @[MulAddRecFN.scala 195:47]
  wire [25:0] _T_3 = io_mulAddResult[48] ? _T_2 : io_fromPreMul_highAlignedSigC; // @[MulAddRecFN.scala 194:16]
  wire [74:0] sigSum = {_T_3,io_mulAddResult[47:0],io_fromPreMul_bit0AlignedSigC}; // @[Cat.scala 29:58]
  wire [1:0] _T_6 = {1'b0,$signed(io_fromPreMul_doSubMags)}; // @[MulAddRecFN.scala 205:69]
  wire [9:0] _GEN_0 = {{8{_T_6[1]}},_T_6}; // @[MulAddRecFN.scala 205:43]
  wire [9:0] CDom_sExp = $signed(io_fromPreMul_sExpSum) - $signed(_GEN_0); // @[MulAddRecFN.scala 205:43]
  wire [49:0] _T_10 = ~sigSum[74:25]; // @[MulAddRecFN.scala 208:13]
  wire [49:0] _T_14 = {1'h0,io_fromPreMul_highAlignedSigC[25:24],sigSum[72:26]}; // @[Cat.scala 29:58]
  wire [49:0] CDom_absSigSum = io_fromPreMul_doSubMags ? _T_10 : _T_14; // @[MulAddRecFN.scala 207:12]
  wire [23:0] _T_16 = ~sigSum[24:1]; // @[MulAddRecFN.scala 217:14]
  wire  _T_17 = |_T_16; // @[MulAddRecFN.scala 217:36]
  wire  _T_19 = |sigSum[25:1]; // @[MulAddRecFN.scala 218:37]
  wire  CDom_absSigSumExtra = io_fromPreMul_doSubMags ? _T_17 : _T_19; // @[MulAddRecFN.scala 216:12]
  wire [80:0] _GEN_5 = {{31'd0}, CDom_absSigSum}; // @[MulAddRecFN.scala 221:24]
  wire [80:0] _T_20 = _GEN_5 << io_fromPreMul_CDom_CAlignDist; // @[MulAddRecFN.scala 221:24]
  wire [28:0] CDom_mainSig = _T_20[49:21]; // @[MulAddRecFN.scala 221:56]
  wire [26:0] _T_22 = {CDom_absSigSum[23:0], 3'h0}; // @[MulAddRecFN.scala 224:53]
  wire  _T_25 = |_T_22[3:0]; // @[primitives.scala 121:54]
  wire  _T_27 = |_T_22[7:4]; // @[primitives.scala 121:54]
  wire  _T_29 = |_T_22[11:8]; // @[primitives.scala 121:54]
  wire  _T_31 = |_T_22[15:12]; // @[primitives.scala 121:54]
  wire  _T_33 = |_T_22[19:16]; // @[primitives.scala 121:54]
  wire  _T_35 = |_T_22[23:20]; // @[primitives.scala 121:54]
  wire  _T_37 = |_T_22[26:24]; // @[primitives.scala 124:57]
  wire [6:0] _T_43 = {_T_37,_T_35,_T_33,_T_31,_T_29,_T_27,_T_25}; // @[primitives.scala 125:20]
  wire [2:0] _T_45 = ~io_fromPreMul_CDom_CAlignDist[4:2]; // @[primitives.scala 51:21]
  wire [8:0] _T_46 = 9'sh100 >>> _T_45; // @[primitives.scala 77:58]
  wire [5:0] _T_62 = {_T_46[1],_T_46[2],_T_46[3],_T_46[4],_T_46[5],_T_46[6]}; // @[Cat.scala 29:58]
  wire [6:0] _GEN_1 = {{1'd0}, _T_62}; // @[MulAddRecFN.scala 224:72]
  wire [6:0] _T_63 = _T_43 & _GEN_1; // @[MulAddRecFN.scala 224:72]
  wire  CDom_reduced4SigExtra = |_T_63; // @[MulAddRecFN.scala 225:73]
  wire  _T_68 = |CDom_mainSig[2:0] | CDom_reduced4SigExtra | CDom_absSigSumExtra; // @[MulAddRecFN.scala 228:61]
  wire [26:0] CDom_sig = {CDom_mainSig[28:3],_T_68}; // @[Cat.scala 29:58]
  wire  notCDom_signSigSum = sigSum[51]; // @[MulAddRecFN.scala 234:36]
  wire [50:0] _T_70 = ~sigSum[50:0]; // @[MulAddRecFN.scala 237:13]
  wire [50:0] _GEN_2 = {{50'd0}, io_fromPreMul_doSubMags}; // @[MulAddRecFN.scala 238:41]
  wire [50:0] _T_73 = sigSum[50:0] + _GEN_2; // @[MulAddRecFN.scala 238:41]
  wire [50:0] notCDom_absSigSum = notCDom_signSigSum ? _T_70 : _T_73; // @[MulAddRecFN.scala 236:12]
  wire  _T_76 = |notCDom_absSigSum[1:0]; // @[primitives.scala 104:54]
  wire  _T_78 = |notCDom_absSigSum[3:2]; // @[primitives.scala 104:54]
  wire  _T_80 = |notCDom_absSigSum[5:4]; // @[primitives.scala 104:54]
  wire  _T_82 = |notCDom_absSigSum[7:6]; // @[primitives.scala 104:54]
  wire  _T_84 = |notCDom_absSigSum[9:8]; // @[primitives.scala 104:54]
  wire  _T_86 = |notCDom_absSigSum[11:10]; // @[primitives.scala 104:54]
  wire  _T_88 = |notCDom_absSigSum[13:12]; // @[primitives.scala 104:54]
  wire  _T_90 = |notCDom_absSigSum[15:14]; // @[primitives.scala 104:54]
  wire  _T_92 = |notCDom_absSigSum[17:16]; // @[primitives.scala 104:54]
  wire  _T_94 = |notCDom_absSigSum[19:18]; // @[primitives.scala 104:54]
  wire  _T_96 = |notCDom_absSigSum[21:20]; // @[primitives.scala 104:54]
  wire  _T_98 = |notCDom_absSigSum[23:22]; // @[primitives.scala 104:54]
  wire  _T_100 = |notCDom_absSigSum[25:24]; // @[primitives.scala 104:54]
  wire  _T_102 = |notCDom_absSigSum[27:26]; // @[primitives.scala 104:54]
  wire  _T_104 = |notCDom_absSigSum[29:28]; // @[primitives.scala 104:54]
  wire  _T_106 = |notCDom_absSigSum[31:30]; // @[primitives.scala 104:54]
  wire  _T_108 = |notCDom_absSigSum[33:32]; // @[primitives.scala 104:54]
  wire  _T_110 = |notCDom_absSigSum[35:34]; // @[primitives.scala 104:54]
  wire  _T_112 = |notCDom_absSigSum[37:36]; // @[primitives.scala 104:54]
  wire  _T_114 = |notCDom_absSigSum[39:38]; // @[primitives.scala 104:54]
  wire  _T_116 = |notCDom_absSigSum[41:40]; // @[primitives.scala 104:54]
  wire  _T_118 = |notCDom_absSigSum[43:42]; // @[primitives.scala 104:54]
  wire  _T_120 = |notCDom_absSigSum[45:44]; // @[primitives.scala 104:54]
  wire  _T_122 = |notCDom_absSigSum[47:46]; // @[primitives.scala 104:54]
  wire  _T_124 = |notCDom_absSigSum[49:48]; // @[primitives.scala 104:54]
  wire  _T_126 = |notCDom_absSigSum[50]; // @[primitives.scala 107:57]
  wire [5:0] _T_131 = {_T_86,_T_84,_T_82,_T_80,_T_78,_T_76}; // @[primitives.scala 108:20]
  wire [12:0] _T_138 = {_T_100,_T_98,_T_96,_T_94,_T_92,_T_90,_T_88,_T_131}; // @[primitives.scala 108:20]
  wire [5:0] _T_143 = {_T_112,_T_110,_T_108,_T_106,_T_104,_T_102}; // @[primitives.scala 108:20]
  wire [25:0] notCDom_reduced2AbsSigSum = {_T_126,_T_124,_T_122,_T_120,_T_118,_T_116,_T_114,_T_143,_T_138}; // @[primitives.scala 108:20]
  wire [4:0] _T_177 = notCDom_reduced2AbsSigSum[1] ? 5'h18 : 5'h19; // @[Mux.scala 47:69]
  wire [4:0] _T_178 = notCDom_reduced2AbsSigSum[2] ? 5'h17 : _T_177; // @[Mux.scala 47:69]
  wire [4:0] _T_179 = notCDom_reduced2AbsSigSum[3] ? 5'h16 : _T_178; // @[Mux.scala 47:69]
  wire [4:0] _T_180 = notCDom_reduced2AbsSigSum[4] ? 5'h15 : _T_179; // @[Mux.scala 47:69]
  wire [4:0] _T_181 = notCDom_reduced2AbsSigSum[5] ? 5'h14 : _T_180; // @[Mux.scala 47:69]
  wire [4:0] _T_182 = notCDom_reduced2AbsSigSum[6] ? 5'h13 : _T_181; // @[Mux.scala 47:69]
  wire [4:0] _T_183 = notCDom_reduced2AbsSigSum[7] ? 5'h12 : _T_182; // @[Mux.scala 47:69]
  wire [4:0] _T_184 = notCDom_reduced2AbsSigSum[8] ? 5'h11 : _T_183; // @[Mux.scala 47:69]
  wire [4:0] _T_185 = notCDom_reduced2AbsSigSum[9] ? 5'h10 : _T_184; // @[Mux.scala 47:69]
  wire [4:0] _T_186 = notCDom_reduced2AbsSigSum[10] ? 5'hf : _T_185; // @[Mux.scala 47:69]
  wire [4:0] _T_187 = notCDom_reduced2AbsSigSum[11] ? 5'he : _T_186; // @[Mux.scala 47:69]
  wire [4:0] _T_188 = notCDom_reduced2AbsSigSum[12] ? 5'hd : _T_187; // @[Mux.scala 47:69]
  wire [4:0] _T_189 = notCDom_reduced2AbsSigSum[13] ? 5'hc : _T_188; // @[Mux.scala 47:69]
  wire [4:0] _T_190 = notCDom_reduced2AbsSigSum[14] ? 5'hb : _T_189; // @[Mux.scala 47:69]
  wire [4:0] _T_191 = notCDom_reduced2AbsSigSum[15] ? 5'ha : _T_190; // @[Mux.scala 47:69]
  wire [4:0] _T_192 = notCDom_reduced2AbsSigSum[16] ? 5'h9 : _T_191; // @[Mux.scala 47:69]
  wire [4:0] _T_193 = notCDom_reduced2AbsSigSum[17] ? 5'h8 : _T_192; // @[Mux.scala 47:69]
  wire [4:0] _T_194 = notCDom_reduced2AbsSigSum[18] ? 5'h7 : _T_193; // @[Mux.scala 47:69]
  wire [4:0] _T_195 = notCDom_reduced2AbsSigSum[19] ? 5'h6 : _T_194; // @[Mux.scala 47:69]
  wire [4:0] _T_196 = notCDom_reduced2AbsSigSum[20] ? 5'h5 : _T_195; // @[Mux.scala 47:69]
  wire [4:0] _T_197 = notCDom_reduced2AbsSigSum[21] ? 5'h4 : _T_196; // @[Mux.scala 47:69]
  wire [4:0] _T_198 = notCDom_reduced2AbsSigSum[22] ? 5'h3 : _T_197; // @[Mux.scala 47:69]
  wire [4:0] _T_199 = notCDom_reduced2AbsSigSum[23] ? 5'h2 : _T_198; // @[Mux.scala 47:69]
  wire [4:0] _T_200 = notCDom_reduced2AbsSigSum[24] ? 5'h1 : _T_199; // @[Mux.scala 47:69]
  wire [4:0] notCDom_normDistReduced2 = notCDom_reduced2AbsSigSum[25] ? 5'h0 : _T_200; // @[Mux.scala 47:69]
  wire [5:0] notCDom_nearNormDist = {notCDom_normDistReduced2, 1'h0}; // @[MulAddRecFN.scala 242:56]
  wire [6:0] _T_201 = {1'b0,$signed(notCDom_nearNormDist)}; // @[MulAddRecFN.scala 243:69]
  wire [9:0] _GEN_3 = {{3{_T_201[6]}},_T_201}; // @[MulAddRecFN.scala 243:46]
  wire [9:0] notCDom_sExp = $signed(io_fromPreMul_sExpSum) - $signed(_GEN_3); // @[MulAddRecFN.scala 243:46]
  wire [113:0] _GEN_6 = {{63'd0}, notCDom_absSigSum}; // @[MulAddRecFN.scala 245:27]
  wire [113:0] _T_204 = _GEN_6 << notCDom_nearNormDist; // @[MulAddRecFN.scala 245:27]
  wire [28:0] notCDom_mainSig = _T_204[51:23]; // @[MulAddRecFN.scala 245:50]
  wire  _T_209 = |notCDom_reduced2AbsSigSum[1:0]; // @[primitives.scala 104:54]
  wire  _T_211 = |notCDom_reduced2AbsSigSum[3:2]; // @[primitives.scala 104:54]
  wire  _T_213 = |notCDom_reduced2AbsSigSum[5:4]; // @[primitives.scala 104:54]
  wire  _T_215 = |notCDom_reduced2AbsSigSum[7:6]; // @[primitives.scala 104:54]
  wire  _T_217 = |notCDom_reduced2AbsSigSum[9:8]; // @[primitives.scala 104:54]
  wire  _T_219 = |notCDom_reduced2AbsSigSum[11:10]; // @[primitives.scala 104:54]
  wire  _T_221 = |notCDom_reduced2AbsSigSum[12]; // @[primitives.scala 107:57]
  wire [6:0] _T_227 = {_T_221,_T_219,_T_217,_T_215,_T_213,_T_211,_T_209}; // @[primitives.scala 108:20]
  wire [3:0] _T_229 = ~notCDom_normDistReduced2[4:1]; // @[primitives.scala 51:21]
  wire [16:0] _T_230 = 17'sh10000 >>> _T_229; // @[primitives.scala 77:58]
  wire [5:0] _T_246 = {_T_230[1],_T_230[2],_T_230[3],_T_230[4],_T_230[5],_T_230[6]}; // @[Cat.scala 29:58]
  wire [6:0] _GEN_4 = {{1'd0}, _T_246}; // @[MulAddRecFN.scala 249:78]
  wire [6:0] _T_247 = _T_227 & _GEN_4; // @[MulAddRecFN.scala 249:78]
  wire  notCDom_reduced4SigExtra = |_T_247; // @[MulAddRecFN.scala 251:11]
  wire  _T_251 = |notCDom_mainSig[2:0] | notCDom_reduced4SigExtra; // @[MulAddRecFN.scala 254:39]
  wire [26:0] notCDom_sig = {notCDom_mainSig[28:3],_T_251}; // @[Cat.scala 29:58]
  wire  notCDom_completeCancellation = notCDom_sig[26:25] == 2'h0; // @[MulAddRecFN.scala 257:50]
  wire  _T_253 = io_fromPreMul_signProd ^ notCDom_signSigSum; // @[MulAddRecFN.scala 261:36]
  wire  notCDom_sign = notCDom_completeCancellation ? roundingMode_min : _T_253; // @[MulAddRecFN.scala 259:12]
  wire  notNaN_isInfProd = io_fromPreMul_isInfA | io_fromPreMul_isInfB; // @[MulAddRecFN.scala 266:49]
  wire  notNaN_isInfOut = notNaN_isInfProd | io_fromPreMul_isInfC; // @[MulAddRecFN.scala 267:44]
  wire  notNaN_addZeros = (io_fromPreMul_isZeroA | io_fromPreMul_isZeroB) & io_fromPreMul_isZeroC; // @[MulAddRecFN.scala 269:58]
  wire  _T_255 = io_fromPreMul_isInfA & io_fromPreMul_isZeroB; // @[MulAddRecFN.scala 274:31]
  wire  _T_256 = io_fromPreMul_isSigNaNAny | _T_255; // @[MulAddRecFN.scala 273:35]
  wire  _T_257 = io_fromPreMul_isZeroA & io_fromPreMul_isInfB; // @[MulAddRecFN.scala 275:32]
  wire  _T_258 = _T_256 | _T_257; // @[MulAddRecFN.scala 274:57]
  wire  _T_261 = ~io_fromPreMul_isNaNAOrB & notNaN_isInfProd; // @[MulAddRecFN.scala 276:36]
  wire  _T_262 = _T_261 & io_fromPreMul_isInfC; // @[MulAddRecFN.scala 277:61]
  wire  _T_263 = _T_262 & io_fromPreMul_doSubMags; // @[MulAddRecFN.scala 278:35]
  wire  _T_267 = ~io_fromPreMul_CIsDominant & notCDom_completeCancellation; // @[MulAddRecFN.scala 285:42]
  wire  _T_270 = io_fromPreMul_isInfC & CDom_sign; // @[MulAddRecFN.scala 288:31]
  wire  _T_271 = notNaN_isInfProd & io_fromPreMul_signProd | _T_270; // @[MulAddRecFN.scala 287:54]
  wire  _T_274 = notNaN_addZeros & ~roundingMode_min & io_fromPreMul_signProd; // @[MulAddRecFN.scala 289:48]
  wire  _T_275 = _T_274 & CDom_sign; // @[MulAddRecFN.scala 290:36]
  wire  _T_276 = _T_271 | _T_275; // @[MulAddRecFN.scala 288:43]
  wire  _T_278 = io_fromPreMul_signProd | CDom_sign; // @[MulAddRecFN.scala 292:37]
  wire  _T_279 = notNaN_addZeros & roundingMode_min & _T_278; // @[MulAddRecFN.scala 291:46]
  wire  _T_280 = _T_276 | _T_279; // @[MulAddRecFN.scala 290:48]
  wire  _T_284 = io_fromPreMul_CIsDominant ? CDom_sign : notCDom_sign; // @[MulAddRecFN.scala 294:17]
  wire  _T_285 = ~notNaN_isInfOut & ~notNaN_addZeros & _T_284; // @[MulAddRecFN.scala 293:49]
  assign io_invalidExc = _T_258 | _T_263; // @[MulAddRecFN.scala 275:57]
  assign io_rawOut_isNaN = io_fromPreMul_isNaNAOrB | io_fromPreMul_isNaNC; // @[MulAddRecFN.scala 280:48]
  assign io_rawOut_isInf = notNaN_isInfProd | io_fromPreMul_isInfC; // @[MulAddRecFN.scala 267:44]
  assign io_rawOut_isZero = notNaN_addZeros | _T_267; // @[MulAddRecFN.scala 284:25]
  assign io_rawOut_sign = _T_280 | _T_285; // @[MulAddRecFN.scala 292:50]
  assign io_rawOut_sExp = io_fromPreMul_CIsDominant ? $signed(CDom_sExp) : $signed(notCDom_sExp); // @[MulAddRecFN.scala 295:26]
  assign io_rawOut_sig = io_fromPreMul_CIsDominant ? CDom_sig : notCDom_sig; // @[MulAddRecFN.scala 296:25]
endmodule
module RoundAnyRawFNToRecFN_3(
  input         io_invalidExc,
  input         io_in_isNaN,
  input         io_in_isInf,
  input         io_in_isZero,
  input         io_in_sign,
  input  [9:0]  io_in_sExp,
  input  [26:0] io_in_sig,
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
  wire  doShiftSigDown1 = io_in_sig[26]; // @[RoundAnyRawFNToRecFN.scala 118:61]
  wire [8:0] _T_4 = ~io_in_sExp[8:0]; // @[primitives.scala 51:21]
  wire [64:0] _T_11 = 65'sh10000000000000000 >>> _T_4[5:0]; // @[primitives.scala 77:58]
  wire [15:0] _GEN_0 = {{8'd0}, _T_11[57:50]}; // @[Bitwise.scala 103:31]
  wire [15:0] _T_17 = _GEN_0 & 16'hff; // @[Bitwise.scala 103:31]
  wire [15:0] _T_19 = {_T_11[49:42], 8'h0}; // @[Bitwise.scala 103:65]
  wire [15:0] _T_21 = _T_19 & 16'hff00; // @[Bitwise.scala 103:75]
  wire [15:0] _T_22 = _T_17 | _T_21; // @[Bitwise.scala 103:39]
  wire [15:0] _GEN_1 = {{4'd0}, _T_22[15:4]}; // @[Bitwise.scala 103:31]
  wire [15:0] _T_27 = _GEN_1 & 16'hf0f; // @[Bitwise.scala 103:31]
  wire [15:0] _T_29 = {_T_22[11:0], 4'h0}; // @[Bitwise.scala 103:65]
  wire [15:0] _T_31 = _T_29 & 16'hf0f0; // @[Bitwise.scala 103:75]
  wire [15:0] _T_32 = _T_27 | _T_31; // @[Bitwise.scala 103:39]
  wire [15:0] _GEN_2 = {{2'd0}, _T_32[15:2]}; // @[Bitwise.scala 103:31]
  wire [15:0] _T_37 = _GEN_2 & 16'h3333; // @[Bitwise.scala 103:31]
  wire [15:0] _T_39 = {_T_32[13:0], 2'h0}; // @[Bitwise.scala 103:65]
  wire [15:0] _T_41 = _T_39 & 16'hcccc; // @[Bitwise.scala 103:75]
  wire [15:0] _T_42 = _T_37 | _T_41; // @[Bitwise.scala 103:39]
  wire [15:0] _GEN_3 = {{1'd0}, _T_42[15:1]}; // @[Bitwise.scala 103:31]
  wire [15:0] _T_47 = _GEN_3 & 16'h5555; // @[Bitwise.scala 103:31]
  wire [15:0] _T_49 = {_T_42[14:0], 1'h0}; // @[Bitwise.scala 103:65]
  wire [15:0] _T_51 = _T_49 & 16'haaaa; // @[Bitwise.scala 103:75]
  wire [15:0] _T_52 = _T_47 | _T_51; // @[Bitwise.scala 103:39]
  wire [21:0] _T_69 = {_T_52,_T_11[58],_T_11[59],_T_11[60],_T_11[61],_T_11[62],_T_11[63]}; // @[Cat.scala 29:58]
  wire [21:0] _T_70 = ~_T_69; // @[primitives.scala 74:36]
  wire [21:0] _T_71 = _T_4[6] ? 22'h0 : _T_70; // @[primitives.scala 74:21]
  wire [21:0] _T_72 = ~_T_71; // @[primitives.scala 74:17]
  wire [24:0] _T_73 = {_T_72,3'h7}; // @[Cat.scala 29:58]
  wire [2:0] _T_83 = {_T_11[0],_T_11[1],_T_11[2]}; // @[Cat.scala 29:58]
  wire [2:0] _T_84 = _T_4[6] ? _T_83 : 3'h0; // @[primitives.scala 61:24]
  wire [24:0] _T_85 = _T_4[7] ? _T_73 : {{22'd0}, _T_84}; // @[primitives.scala 66:24]
  wire [24:0] _T_86 = _T_4[8] ? _T_85 : 25'h0; // @[primitives.scala 61:24]
  wire [24:0] _GEN_4 = {{24'd0}, doShiftSigDown1}; // @[RoundAnyRawFNToRecFN.scala 157:23]
  wire [24:0] _T_87 = _T_86 | _GEN_4; // @[RoundAnyRawFNToRecFN.scala 157:23]
  wire [26:0] _T_88 = {_T_87,2'h3}; // @[Cat.scala 29:58]
  wire [26:0] _T_90 = {1'h0,_T_88[26:1]}; // @[Cat.scala 29:58]
  wire [26:0] _T_91 = ~_T_90; // @[RoundAnyRawFNToRecFN.scala 161:28]
  wire [26:0] _T_92 = _T_91 & _T_88; // @[RoundAnyRawFNToRecFN.scala 161:46]
  wire [26:0] _T_93 = io_in_sig & _T_92; // @[RoundAnyRawFNToRecFN.scala 162:40]
  wire  _T_94 = |_T_93; // @[RoundAnyRawFNToRecFN.scala 162:56]
  wire [26:0] _T_95 = io_in_sig & _T_90; // @[RoundAnyRawFNToRecFN.scala 163:42]
  wire  _T_96 = |_T_95; // @[RoundAnyRawFNToRecFN.scala 163:62]
  wire  _T_97 = _T_94 | _T_96; // @[RoundAnyRawFNToRecFN.scala 164:36]
  wire  _T_98 = roundingMode_near_even | roundingMode_near_maxMag; // @[RoundAnyRawFNToRecFN.scala 167:38]
  wire  _T_99 = (roundingMode_near_even | roundingMode_near_maxMag) & _T_94; // @[RoundAnyRawFNToRecFN.scala 167:67]
  wire  _T_100 = roundMagUp & _T_97; // @[RoundAnyRawFNToRecFN.scala 169:29]
  wire  _T_101 = _T_99 | _T_100; // @[RoundAnyRawFNToRecFN.scala 168:31]
  wire [26:0] _T_102 = io_in_sig | _T_88; // @[RoundAnyRawFNToRecFN.scala 172:32]
  wire [25:0] _T_104 = _T_102[26:2] + 25'h1; // @[RoundAnyRawFNToRecFN.scala 172:49]
  wire  _T_106 = ~_T_96; // @[RoundAnyRawFNToRecFN.scala 174:30]
  wire [25:0] _T_109 = roundingMode_near_even & _T_94 & _T_106 ? _T_88[26:1] : 26'h0; // @[RoundAnyRawFNToRecFN.scala 173:25]
  wire [25:0] _T_110 = ~_T_109; // @[RoundAnyRawFNToRecFN.scala 173:21]
  wire [25:0] _T_111 = _T_104 & _T_110; // @[RoundAnyRawFNToRecFN.scala 172:61]
  wire [26:0] _T_112 = ~_T_88; // @[RoundAnyRawFNToRecFN.scala 178:32]
  wire [26:0] _T_113 = io_in_sig & _T_112; // @[RoundAnyRawFNToRecFN.scala 178:30]
  wire [25:0] _T_117 = roundingMode_odd & _T_97 ? _T_92[26:1] : 26'h0; // @[RoundAnyRawFNToRecFN.scala 179:24]
  wire [25:0] _GEN_5 = {{1'd0}, _T_113[26:2]}; // @[RoundAnyRawFNToRecFN.scala 178:47]
  wire [25:0] _T_118 = _GEN_5 | _T_117; // @[RoundAnyRawFNToRecFN.scala 178:47]
  wire [25:0] _T_119 = _T_101 ? _T_111 : _T_118; // @[RoundAnyRawFNToRecFN.scala 171:16]
  wire [2:0] _T_121 = {1'b0,$signed(_T_119[25:24])}; // @[RoundAnyRawFNToRecFN.scala 183:69]
  wire [9:0] _GEN_6 = {{7{_T_121[2]}},_T_121}; // @[RoundAnyRawFNToRecFN.scala 183:40]
  wire [10:0] _T_122 = $signed(io_in_sExp) + $signed(_GEN_6); // @[RoundAnyRawFNToRecFN.scala 183:40]
  wire [8:0] common_expOut = _T_122[8:0]; // @[RoundAnyRawFNToRecFN.scala 185:37]
  wire [22:0] common_fractOut = doShiftSigDown1 ? _T_119[23:1] : _T_119[22:0]; // @[RoundAnyRawFNToRecFN.scala 187:16]
  wire [3:0] _T_127 = _T_122[10:7]; // @[RoundAnyRawFNToRecFN.scala 194:30]
  wire  common_overflow = $signed(_T_127) >= 4'sh3; // @[RoundAnyRawFNToRecFN.scala 194:50]
  wire  common_totalUnderflow = $signed(_T_122) < 11'sh6b; // @[RoundAnyRawFNToRecFN.scala 198:31]
  wire  _T_132 = doShiftSigDown1 ? io_in_sig[2] : io_in_sig[1]; // @[RoundAnyRawFNToRecFN.scala 201:16]
  wire  _T_137 = doShiftSigDown1 & io_in_sig[2] | |io_in_sig[1:0]; // @[RoundAnyRawFNToRecFN.scala 203:49]
  wire  _T_139 = _T_98 & _T_132; // @[RoundAnyRawFNToRecFN.scala 205:67]
  wire  _T_140 = roundMagUp & _T_137; // @[RoundAnyRawFNToRecFN.scala 207:29]
  wire  _T_141 = _T_139 | _T_140; // @[RoundAnyRawFNToRecFN.scala 206:46]
  wire  _T_144 = doShiftSigDown1 ? _T_119[25] : _T_119[24]; // @[RoundAnyRawFNToRecFN.scala 209:16]
  wire [1:0] _T_145 = io_in_sExp[9:8]; // @[RoundAnyRawFNToRecFN.scala 218:48]
  wire  _T_150 = doShiftSigDown1 ? _T_88[3] : _T_88[2]; // @[RoundAnyRawFNToRecFN.scala 219:30]
  wire  _T_151 = _T_97 & $signed(_T_145) <= 2'sh0 & _T_150; // @[RoundAnyRawFNToRecFN.scala 218:74]
  wire  _T_155 = doShiftSigDown1 ? _T_88[4] : _T_88[3]; // @[RoundAnyRawFNToRecFN.scala 221:39]
  wire  _T_156 = ~_T_155; // @[RoundAnyRawFNToRecFN.scala 221:34]
  wire  _T_158 = _T_156 & _T_144; // @[RoundAnyRawFNToRecFN.scala 224:38]
  wire  _T_160 = _T_158 & _T_94 & _T_141; // @[RoundAnyRawFNToRecFN.scala 225:60]
  wire  _T_161 = ~_T_160; // @[RoundAnyRawFNToRecFN.scala 220:27]
  wire  _T_162 = _T_151 & _T_161; // @[RoundAnyRawFNToRecFN.scala 219:76]
  wire  common_underflow = common_totalUnderflow | _T_162; // @[RoundAnyRawFNToRecFN.scala 215:40]
  wire  common_inexact = common_totalUnderflow | _T_97; // @[RoundAnyRawFNToRecFN.scala 228:49]
  wire  isNaNOut = io_invalidExc | io_in_isNaN; // @[RoundAnyRawFNToRecFN.scala 233:34]
  wire  commonCase = ~isNaNOut & ~io_in_isInf & ~io_in_isZero; // @[RoundAnyRawFNToRecFN.scala 235:61]
  wire  overflow = commonCase & common_overflow; // @[RoundAnyRawFNToRecFN.scala 236:32]
  wire  underflow = commonCase & common_underflow; // @[RoundAnyRawFNToRecFN.scala 237:32]
  wire  inexact = overflow | commonCase & common_inexact; // @[RoundAnyRawFNToRecFN.scala 238:28]
  wire  overflow_roundMagUp = _T_98 | roundMagUp; // @[RoundAnyRawFNToRecFN.scala 241:60]
  wire  pegMinNonzeroMagOut = commonCase & common_totalUnderflow & (roundMagUp | roundingMode_odd); // @[RoundAnyRawFNToRecFN.scala 243:45]
  wire  pegMaxFiniteMagOut = overflow & ~overflow_roundMagUp; // @[RoundAnyRawFNToRecFN.scala 244:39]
  wire  notNaN_isInfOut = io_in_isInf | overflow & overflow_roundMagUp; // @[RoundAnyRawFNToRecFN.scala 246:32]
  wire  signOut = isNaNOut ? 1'h0 : io_in_sign; // @[RoundAnyRawFNToRecFN.scala 248:22]
  wire [8:0] _T_176 = io_in_isZero | common_totalUnderflow ? 9'h1c0 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 251:18]
  wire [8:0] _T_177 = ~_T_176; // @[RoundAnyRawFNToRecFN.scala 251:14]
  wire [8:0] _T_178 = common_expOut & _T_177; // @[RoundAnyRawFNToRecFN.scala 250:24]
  wire [8:0] _T_180 = pegMinNonzeroMagOut ? 9'h194 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 255:18]
  wire [8:0] _T_181 = ~_T_180; // @[RoundAnyRawFNToRecFN.scala 255:14]
  wire [8:0] _T_182 = _T_178 & _T_181; // @[RoundAnyRawFNToRecFN.scala 254:17]
  wire [8:0] _T_183 = pegMaxFiniteMagOut ? 9'h80 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 259:18]
  wire [8:0] _T_184 = ~_T_183; // @[RoundAnyRawFNToRecFN.scala 259:14]
  wire [8:0] _T_185 = _T_182 & _T_184; // @[RoundAnyRawFNToRecFN.scala 258:17]
  wire [8:0] _T_186 = notNaN_isInfOut ? 9'h40 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 263:18]
  wire [8:0] _T_187 = ~_T_186; // @[RoundAnyRawFNToRecFN.scala 263:14]
  wire [8:0] _T_188 = _T_185 & _T_187; // @[RoundAnyRawFNToRecFN.scala 262:17]
  wire [8:0] _T_189 = pegMinNonzeroMagOut ? 9'h6b : 9'h0; // @[RoundAnyRawFNToRecFN.scala 267:16]
  wire [8:0] _T_190 = _T_188 | _T_189; // @[RoundAnyRawFNToRecFN.scala 266:18]
  wire [8:0] _T_191 = pegMaxFiniteMagOut ? 9'h17f : 9'h0; // @[RoundAnyRawFNToRecFN.scala 271:16]
  wire [8:0] _T_192 = _T_190 | _T_191; // @[RoundAnyRawFNToRecFN.scala 270:15]
  wire [8:0] _T_193 = notNaN_isInfOut ? 9'h180 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 275:16]
  wire [8:0] _T_194 = _T_192 | _T_193; // @[RoundAnyRawFNToRecFN.scala 274:15]
  wire [8:0] _T_195 = isNaNOut ? 9'h1c0 : 9'h0; // @[RoundAnyRawFNToRecFN.scala 276:16]
  wire [8:0] expOut = _T_194 | _T_195; // @[RoundAnyRawFNToRecFN.scala 275:77]
  wire [22:0] _T_198 = isNaNOut ? 23'h400000 : 23'h0; // @[RoundAnyRawFNToRecFN.scala 279:16]
  wire [22:0] _T_199 = isNaNOut | io_in_isZero | common_totalUnderflow ? _T_198 : common_fractOut; // @[RoundAnyRawFNToRecFN.scala 278:12]
  wire [22:0] _T_201 = pegMaxFiniteMagOut ? 23'h7fffff : 23'h0; // @[Bitwise.scala 72:12]
  wire [22:0] fractOut = _T_199 | _T_201; // @[RoundAnyRawFNToRecFN.scala 281:11]
  wire [9:0] _T_202 = {signOut,expOut}; // @[Cat.scala 29:58]
  wire [1:0] _T_204 = {underflow,inexact}; // @[Cat.scala 29:58]
  wire [2:0] _T_206 = {io_invalidExc,1'h0,overflow}; // @[Cat.scala 29:58]
  assign io_out = {_T_202,fractOut}; // @[Cat.scala 29:58]
  assign io_exceptionFlags = {_T_206,_T_204}; // @[Cat.scala 29:58]
endmodule
module RoundRawFNToRecFN_1(
  input         io_invalidExc,
  input         io_in_isNaN,
  input         io_in_isInf,
  input         io_in_isZero,
  input         io_in_sign,
  input  [9:0]  io_in_sExp,
  input  [26:0] io_in_sig,
  input  [2:0]  io_roundingMode,
  output [32:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire  roundAnyRawFNToRecFN_io_invalidExc; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire  roundAnyRawFNToRecFN_io_in_isNaN; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire  roundAnyRawFNToRecFN_io_in_isInf; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire  roundAnyRawFNToRecFN_io_in_isZero; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire  roundAnyRawFNToRecFN_io_in_sign; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire [9:0] roundAnyRawFNToRecFN_io_in_sExp; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire [26:0] roundAnyRawFNToRecFN_io_in_sig; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire [2:0] roundAnyRawFNToRecFN_io_roundingMode; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire [32:0] roundAnyRawFNToRecFN_io_out; // @[RoundAnyRawFNToRecFN.scala 307:15]
  wire [4:0] roundAnyRawFNToRecFN_io_exceptionFlags; // @[RoundAnyRawFNToRecFN.scala 307:15]
  RoundAnyRawFNToRecFN_3 roundAnyRawFNToRecFN ( // @[RoundAnyRawFNToRecFN.scala 307:15]
    .io_invalidExc(roundAnyRawFNToRecFN_io_invalidExc),
    .io_in_isNaN(roundAnyRawFNToRecFN_io_in_isNaN),
    .io_in_isInf(roundAnyRawFNToRecFN_io_in_isInf),
    .io_in_isZero(roundAnyRawFNToRecFN_io_in_isZero),
    .io_in_sign(roundAnyRawFNToRecFN_io_in_sign),
    .io_in_sExp(roundAnyRawFNToRecFN_io_in_sExp),
    .io_in_sig(roundAnyRawFNToRecFN_io_in_sig),
    .io_roundingMode(roundAnyRawFNToRecFN_io_roundingMode),
    .io_out(roundAnyRawFNToRecFN_io_out),
    .io_exceptionFlags(roundAnyRawFNToRecFN_io_exceptionFlags)
  );
  assign io_out = roundAnyRawFNToRecFN_io_out; // @[RoundAnyRawFNToRecFN.scala 315:23]
  assign io_exceptionFlags = roundAnyRawFNToRecFN_io_exceptionFlags; // @[RoundAnyRawFNToRecFN.scala 316:23]
  assign roundAnyRawFNToRecFN_io_invalidExc = io_invalidExc; // @[RoundAnyRawFNToRecFN.scala 310:44]
  assign roundAnyRawFNToRecFN_io_in_isNaN = io_in_isNaN; // @[RoundAnyRawFNToRecFN.scala 312:44]
  assign roundAnyRawFNToRecFN_io_in_isInf = io_in_isInf; // @[RoundAnyRawFNToRecFN.scala 312:44]
  assign roundAnyRawFNToRecFN_io_in_isZero = io_in_isZero; // @[RoundAnyRawFNToRecFN.scala 312:44]
  assign roundAnyRawFNToRecFN_io_in_sign = io_in_sign; // @[RoundAnyRawFNToRecFN.scala 312:44]
  assign roundAnyRawFNToRecFN_io_in_sExp = io_in_sExp; // @[RoundAnyRawFNToRecFN.scala 312:44]
  assign roundAnyRawFNToRecFN_io_in_sig = io_in_sig; // @[RoundAnyRawFNToRecFN.scala 312:44]
  assign roundAnyRawFNToRecFN_io_roundingMode = io_roundingMode; // @[RoundAnyRawFNToRecFN.scala 313:44]
endmodule
module MulAddRecFNToRaw_preMul_1(
  input  [1:0]  io_op,
  input  [32:0] io_a,
  input  [32:0] io_b,
  input  [32:0] io_c,
  output [23:0] io_mulAddA,
  output [23:0] io_mulAddB,
  output [47:0] io_mulAddC,
  output        io_toPostMul_isSigNaNAny,
  output        io_toPostMul_isNaNAOrB,
  output        io_toPostMul_isInfA,
  output        io_toPostMul_isZeroA,
  output        io_toPostMul_isInfB,
  output        io_toPostMul_isZeroB,
  output        io_toPostMul_signProd,
  output        io_toPostMul_isNaNC,
  output        io_toPostMul_isInfC,
  output        io_toPostMul_isZeroC,
  output [9:0]  io_toPostMul_sExpSum,
  output        io_toPostMul_doSubMags,
  output        io_toPostMul_CIsDominant,
  output [4:0]  io_toPostMul_CDom_CAlignDist,
  output [25:0] io_toPostMul_highAlignedSigC,
  output        io_toPostMul_bit0AlignedSigC
);
  wire  rawA_isZero = io_a[31:29] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_4 = io_a[31:30] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  rawA_isNaN = _T_4 & io_a[29]; // @[rawFloatFromRecFN.scala 55:33]
  wire  rawA_sign = io_a[32]; // @[rawFloatFromRecFN.scala 58:25]
  wire [9:0] rawA_sExp = {1'b0,$signed(io_a[31:23])}; // @[rawFloatFromRecFN.scala 59:27]
  wire  _T_12 = ~rawA_isZero; // @[rawFloatFromRecFN.scala 60:39]
  wire [24:0] rawA_sig = {1'h0,_T_12,io_a[22:0]}; // @[Cat.scala 29:58]
  wire  rawB_isZero = io_b[31:29] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_20 = io_b[31:30] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  rawB_isNaN = _T_20 & io_b[29]; // @[rawFloatFromRecFN.scala 55:33]
  wire  rawB_sign = io_b[32]; // @[rawFloatFromRecFN.scala 58:25]
  wire [9:0] rawB_sExp = {1'b0,$signed(io_b[31:23])}; // @[rawFloatFromRecFN.scala 59:27]
  wire  _T_28 = ~rawB_isZero; // @[rawFloatFromRecFN.scala 60:39]
  wire [24:0] rawB_sig = {1'h0,_T_28,io_b[22:0]}; // @[Cat.scala 29:58]
  wire  rawC_isZero = io_c[31:29] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_36 = io_c[31:30] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  rawC_isNaN = _T_36 & io_c[29]; // @[rawFloatFromRecFN.scala 55:33]
  wire  rawC_sign = io_c[32]; // @[rawFloatFromRecFN.scala 58:25]
  wire [9:0] rawC_sExp = {1'b0,$signed(io_c[31:23])}; // @[rawFloatFromRecFN.scala 59:27]
  wire  _T_44 = ~rawC_isZero; // @[rawFloatFromRecFN.scala 60:39]
  wire [24:0] rawC_sig = {1'h0,_T_44,io_c[22:0]}; // @[Cat.scala 29:58]
  wire  signProd = rawA_sign ^ rawB_sign ^ io_op[1]; // @[MulAddRecFN.scala 98:42]
  wire [10:0] _T_50 = $signed(rawA_sExp) + $signed(rawB_sExp); // @[MulAddRecFN.scala 101:19]
  wire [10:0] sExpAlignedProd = $signed(_T_50) - 11'she5; // @[MulAddRecFN.scala 101:32]
  wire  doSubMags = signProd ^ rawC_sign ^ io_op[0]; // @[MulAddRecFN.scala 103:42]
  wire [10:0] _GEN_0 = {{1{rawC_sExp[9]}},rawC_sExp}; // @[MulAddRecFN.scala 107:42]
  wire [10:0] sNatCAlignDist = $signed(sExpAlignedProd) - $signed(_GEN_0); // @[MulAddRecFN.scala 107:42]
  wire [9:0] posNatCAlignDist = sNatCAlignDist[9:0]; // @[MulAddRecFN.scala 108:42]
  wire  isMinCAlign = rawA_isZero | rawB_isZero | $signed(sNatCAlignDist) < 11'sh0; // @[MulAddRecFN.scala 109:50]
  wire  CIsDominant = _T_44 & (isMinCAlign | posNatCAlignDist <= 10'h18); // @[MulAddRecFN.scala 111:23]
  wire [6:0] _T_64 = posNatCAlignDist < 10'h4a ? posNatCAlignDist[6:0] : 7'h4a; // @[MulAddRecFN.scala 115:16]
  wire [6:0] CAlignDist = isMinCAlign ? 7'h0 : _T_64; // @[MulAddRecFN.scala 113:12]
  wire [24:0] _T_65 = ~rawC_sig; // @[MulAddRecFN.scala 121:28]
  wire [24:0] _T_66 = doSubMags ? _T_65 : rawC_sig; // @[MulAddRecFN.scala 121:16]
  wire [52:0] _T_68 = doSubMags ? 53'h1fffffffffffff : 53'h0; // @[Bitwise.scala 72:12]
  wire [77:0] _T_70 = {_T_66,_T_68}; // @[MulAddRecFN.scala 123:11]
  wire [77:0] mainAlignedSigC = $signed(_T_70) >>> CAlignDist; // @[MulAddRecFN.scala 123:17]
  wire [26:0] _T_71 = {rawC_sig, 2'h0}; // @[MulAddRecFN.scala 125:30]
  wire  _T_74 = |_T_71[3:0]; // @[primitives.scala 121:54]
  wire  _T_76 = |_T_71[7:4]; // @[primitives.scala 121:54]
  wire  _T_78 = |_T_71[11:8]; // @[primitives.scala 121:54]
  wire  _T_80 = |_T_71[15:12]; // @[primitives.scala 121:54]
  wire  _T_82 = |_T_71[19:16]; // @[primitives.scala 121:54]
  wire  _T_84 = |_T_71[23:20]; // @[primitives.scala 121:54]
  wire  _T_86 = |_T_71[26:24]; // @[primitives.scala 124:57]
  wire [6:0] _T_92 = {_T_86,_T_84,_T_82,_T_80,_T_78,_T_76,_T_74}; // @[primitives.scala 125:20]
  wire [32:0] _T_94 = 33'sh100000000 >>> CAlignDist[6:2]; // @[primitives.scala 77:58]
  wire [5:0] _T_110 = {_T_94[14],_T_94[15],_T_94[16],_T_94[17],_T_94[18],_T_94[19]}; // @[Cat.scala 29:58]
  wire [6:0] _GEN_1 = {{1'd0}, _T_110}; // @[MulAddRecFN.scala 125:68]
  wire [6:0] _T_111 = _T_92 & _GEN_1; // @[MulAddRecFN.scala 125:68]
  wire  reduced4CExtra = |_T_111; // @[MulAddRecFN.scala 133:11]
  wire  _T_116 = &mainAlignedSigC[2:0] & ~reduced4CExtra; // @[MulAddRecFN.scala 137:44]
  wire  _T_119 = |mainAlignedSigC[2:0] | reduced4CExtra; // @[MulAddRecFN.scala 138:44]
  wire  _T_120 = doSubMags ? _T_116 : _T_119; // @[MulAddRecFN.scala 136:16]
  wire [74:0] _T_121 = mainAlignedSigC[77:3]; // @[Cat.scala 29:58]
  wire [75:0] alignedSigC = {_T_121,_T_120}; // @[Cat.scala 29:58]
  wire  _T_125 = rawA_isNaN & ~rawA_sig[22]; // @[common.scala 81:46]
  wire  _T_128 = rawB_isNaN & ~rawB_sig[22]; // @[common.scala 81:46]
  wire  _T_132 = rawC_isNaN & ~rawC_sig[22]; // @[common.scala 81:46]
  wire [10:0] _T_137 = $signed(sExpAlignedProd) - 11'sh18; // @[MulAddRecFN.scala 161:53]
  wire [10:0] _T_138 = CIsDominant ? $signed({{1{rawC_sExp[9]}},rawC_sExp}) : $signed(_T_137); // @[MulAddRecFN.scala 161:12]
  assign io_mulAddA = rawA_sig[23:0]; // @[MulAddRecFN.scala 144:16]
  assign io_mulAddB = rawB_sig[23:0]; // @[MulAddRecFN.scala 145:16]
  assign io_mulAddC = alignedSigC[48:1]; // @[MulAddRecFN.scala 146:30]
  assign io_toPostMul_isSigNaNAny = _T_125 | _T_128 | _T_132; // @[MulAddRecFN.scala 149:58]
  assign io_toPostMul_isNaNAOrB = rawA_isNaN | rawB_isNaN; // @[MulAddRecFN.scala 151:42]
  assign io_toPostMul_isInfA = _T_4 & ~io_a[29]; // @[rawFloatFromRecFN.scala 56:33]
  assign io_toPostMul_isZeroA = io_a[31:29] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  assign io_toPostMul_isInfB = _T_20 & ~io_b[29]; // @[rawFloatFromRecFN.scala 56:33]
  assign io_toPostMul_isZeroB = io_b[31:29] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  assign io_toPostMul_signProd = rawA_sign ^ rawB_sign ^ io_op[1]; // @[MulAddRecFN.scala 98:42]
  assign io_toPostMul_isNaNC = _T_36 & io_c[29]; // @[rawFloatFromRecFN.scala 55:33]
  assign io_toPostMul_isInfC = _T_36 & ~io_c[29]; // @[rawFloatFromRecFN.scala 56:33]
  assign io_toPostMul_isZeroC = io_c[31:29] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  assign io_toPostMul_sExpSum = _T_138[9:0]; // @[MulAddRecFN.scala 160:28]
  assign io_toPostMul_doSubMags = signProd ^ rawC_sign ^ io_op[0]; // @[MulAddRecFN.scala 103:42]
  assign io_toPostMul_CIsDominant = _T_44 & (isMinCAlign | posNatCAlignDist <= 10'h18); // @[MulAddRecFN.scala 111:23]
  assign io_toPostMul_CDom_CAlignDist = CAlignDist[4:0]; // @[MulAddRecFN.scala 164:47]
  assign io_toPostMul_highAlignedSigC = alignedSigC[74:49]; // @[MulAddRecFN.scala 166:20]
  assign io_toPostMul_bit0AlignedSigC = alignedSigC[0]; // @[MulAddRecFN.scala 167:48]
endmodule
module MulAddRecFNPipe_1(
  input         clock,
  input         reset,
  input         io_validin,
  input  [1:0]  io_op,
  input  [32:0] io_a,
  input  [32:0] io_b,
  input  [32:0] io_c,
  input  [2:0]  io_roundingMode,
  output [32:0] io_out,
  output [4:0]  io_exceptionFlags,
  output        io_validout
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
  reg [63:0] _RAND_16;
  reg [31:0] _RAND_17;
  reg [31:0] _RAND_18;
  reg [31:0] _RAND_19;
  reg [31:0] _RAND_20;
  reg [31:0] _RAND_21;
  reg [31:0] _RAND_22;
  reg [31:0] _RAND_23;
  reg [31:0] _RAND_24;
  reg [31:0] _RAND_25;
  reg [31:0] _RAND_26;
  reg [31:0] _RAND_27;
  reg [31:0] _RAND_28;
`endif // RANDOMIZE_REG_INIT
  wire [1:0] mulAddRecFNToRaw_preMul_io_op; // @[FPU.scala 597:41]
  wire [32:0] mulAddRecFNToRaw_preMul_io_a; // @[FPU.scala 597:41]
  wire [32:0] mulAddRecFNToRaw_preMul_io_b; // @[FPU.scala 597:41]
  wire [32:0] mulAddRecFNToRaw_preMul_io_c; // @[FPU.scala 597:41]
  wire [23:0] mulAddRecFNToRaw_preMul_io_mulAddA; // @[FPU.scala 597:41]
  wire [23:0] mulAddRecFNToRaw_preMul_io_mulAddB; // @[FPU.scala 597:41]
  wire [47:0] mulAddRecFNToRaw_preMul_io_mulAddC; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isSigNaNAny; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isNaNAOrB; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isInfA; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isZeroA; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isInfB; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isZeroB; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_signProd; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isNaNC; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isInfC; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_isZeroC; // @[FPU.scala 597:41]
  wire [9:0] mulAddRecFNToRaw_preMul_io_toPostMul_sExpSum; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_doSubMags; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_CIsDominant; // @[FPU.scala 597:41]
  wire [4:0] mulAddRecFNToRaw_preMul_io_toPostMul_CDom_CAlignDist; // @[FPU.scala 597:41]
  wire [25:0] mulAddRecFNToRaw_preMul_io_toPostMul_highAlignedSigC; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_preMul_io_toPostMul_bit0AlignedSigC; // @[FPU.scala 597:41]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isSigNaNAny; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isNaNAOrB; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isInfA; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroA; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isInfB; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroB; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_signProd; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isNaNC; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isInfC; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroC; // @[FPU.scala 598:42]
  wire [9:0] mulAddRecFNToRaw_postMul_io_fromPreMul_sExpSum; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_doSubMags; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_CIsDominant; // @[FPU.scala 598:42]
  wire [4:0] mulAddRecFNToRaw_postMul_io_fromPreMul_CDom_CAlignDist; // @[FPU.scala 598:42]
  wire [25:0] mulAddRecFNToRaw_postMul_io_fromPreMul_highAlignedSigC; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_fromPreMul_bit0AlignedSigC; // @[FPU.scala 598:42]
  wire [48:0] mulAddRecFNToRaw_postMul_io_mulAddResult; // @[FPU.scala 598:42]
  wire [2:0] mulAddRecFNToRaw_postMul_io_roundingMode; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_invalidExc; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_rawOut_isNaN; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_rawOut_isInf; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_rawOut_isZero; // @[FPU.scala 598:42]
  wire  mulAddRecFNToRaw_postMul_io_rawOut_sign; // @[FPU.scala 598:42]
  wire [9:0] mulAddRecFNToRaw_postMul_io_rawOut_sExp; // @[FPU.scala 598:42]
  wire [26:0] mulAddRecFNToRaw_postMul_io_rawOut_sig; // @[FPU.scala 598:42]
  wire  roundRawFNToRecFN_io_invalidExc; // @[FPU.scala 625:35]
  wire  roundRawFNToRecFN_io_in_isNaN; // @[FPU.scala 625:35]
  wire  roundRawFNToRecFN_io_in_isInf; // @[FPU.scala 625:35]
  wire  roundRawFNToRecFN_io_in_isZero; // @[FPU.scala 625:35]
  wire  roundRawFNToRecFN_io_in_sign; // @[FPU.scala 625:35]
  wire [9:0] roundRawFNToRecFN_io_in_sExp; // @[FPU.scala 625:35]
  wire [26:0] roundRawFNToRecFN_io_in_sig; // @[FPU.scala 625:35]
  wire [2:0] roundRawFNToRecFN_io_roundingMode; // @[FPU.scala 625:35]
  wire [32:0] roundRawFNToRecFN_io_out; // @[FPU.scala 625:35]
  wire [4:0] roundRawFNToRecFN_io_exceptionFlags; // @[FPU.scala 625:35]
  wire [47:0] _T = mulAddRecFNToRaw_preMul_io_mulAddA * mulAddRecFNToRaw_preMul_io_mulAddB; // @[FPU.scala 606:45]
  wire [48:0] mulAddResult = _T + mulAddRecFNToRaw_preMul_io_mulAddC; // @[FPU.scala 607:50]
  reg  _T_2_isSigNaNAny; // @[Reg.scala 15:16]
  reg  _T_2_isNaNAOrB; // @[Reg.scala 15:16]
  reg  _T_2_isInfA; // @[Reg.scala 15:16]
  reg  _T_2_isZeroA; // @[Reg.scala 15:16]
  reg  _T_2_isInfB; // @[Reg.scala 15:16]
  reg  _T_2_isZeroB; // @[Reg.scala 15:16]
  reg  _T_2_signProd; // @[Reg.scala 15:16]
  reg  _T_2_isNaNC; // @[Reg.scala 15:16]
  reg  _T_2_isInfC; // @[Reg.scala 15:16]
  reg  _T_2_isZeroC; // @[Reg.scala 15:16]
  reg [9:0] _T_2_sExpSum; // @[Reg.scala 15:16]
  reg  _T_2_doSubMags; // @[Reg.scala 15:16]
  reg  _T_2_CIsDominant; // @[Reg.scala 15:16]
  reg [4:0] _T_2_CDom_CAlignDist; // @[Reg.scala 15:16]
  reg [25:0] _T_2_highAlignedSigC; // @[Reg.scala 15:16]
  reg  _T_2_bit0AlignedSigC; // @[Reg.scala 15:16]
  reg [48:0] _T_5; // @[Reg.scala 15:16]
  reg [2:0] _T_8; // @[Reg.scala 15:16]
  reg [2:0] roundingMode_stage0; // @[Reg.scala 15:16]
  reg  valid_stage0; // @[Valid.scala 117:22]
  reg  _T_20; // @[Reg.scala 15:16]
  reg  _T_23_isNaN; // @[Reg.scala 15:16]
  reg  _T_23_isInf; // @[Reg.scala 15:16]
  reg  _T_23_isZero; // @[Reg.scala 15:16]
  reg  _T_23_sign; // @[Reg.scala 15:16]
  reg [9:0] _T_23_sExp; // @[Reg.scala 15:16]
  reg [26:0] _T_23_sig; // @[Reg.scala 15:16]
  reg [2:0] _T_26; // @[Reg.scala 15:16]
  reg  _T_31; // @[Valid.scala 117:22]
  MulAddRecFNToRaw_preMul_1 mulAddRecFNToRaw_preMul ( // @[FPU.scala 597:41]
    .io_op(mulAddRecFNToRaw_preMul_io_op),
    .io_a(mulAddRecFNToRaw_preMul_io_a),
    .io_b(mulAddRecFNToRaw_preMul_io_b),
    .io_c(mulAddRecFNToRaw_preMul_io_c),
    .io_mulAddA(mulAddRecFNToRaw_preMul_io_mulAddA),
    .io_mulAddB(mulAddRecFNToRaw_preMul_io_mulAddB),
    .io_mulAddC(mulAddRecFNToRaw_preMul_io_mulAddC),
    .io_toPostMul_isSigNaNAny(mulAddRecFNToRaw_preMul_io_toPostMul_isSigNaNAny),
    .io_toPostMul_isNaNAOrB(mulAddRecFNToRaw_preMul_io_toPostMul_isNaNAOrB),
    .io_toPostMul_isInfA(mulAddRecFNToRaw_preMul_io_toPostMul_isInfA),
    .io_toPostMul_isZeroA(mulAddRecFNToRaw_preMul_io_toPostMul_isZeroA),
    .io_toPostMul_isInfB(mulAddRecFNToRaw_preMul_io_toPostMul_isInfB),
    .io_toPostMul_isZeroB(mulAddRecFNToRaw_preMul_io_toPostMul_isZeroB),
    .io_toPostMul_signProd(mulAddRecFNToRaw_preMul_io_toPostMul_signProd),
    .io_toPostMul_isNaNC(mulAddRecFNToRaw_preMul_io_toPostMul_isNaNC),
    .io_toPostMul_isInfC(mulAddRecFNToRaw_preMul_io_toPostMul_isInfC),
    .io_toPostMul_isZeroC(mulAddRecFNToRaw_preMul_io_toPostMul_isZeroC),
    .io_toPostMul_sExpSum(mulAddRecFNToRaw_preMul_io_toPostMul_sExpSum),
    .io_toPostMul_doSubMags(mulAddRecFNToRaw_preMul_io_toPostMul_doSubMags),
    .io_toPostMul_CIsDominant(mulAddRecFNToRaw_preMul_io_toPostMul_CIsDominant),
    .io_toPostMul_CDom_CAlignDist(mulAddRecFNToRaw_preMul_io_toPostMul_CDom_CAlignDist),
    .io_toPostMul_highAlignedSigC(mulAddRecFNToRaw_preMul_io_toPostMul_highAlignedSigC),
    .io_toPostMul_bit0AlignedSigC(mulAddRecFNToRaw_preMul_io_toPostMul_bit0AlignedSigC)
  );
  MulAddRecFNToRaw_postMul_1 mulAddRecFNToRaw_postMul ( // @[FPU.scala 598:42]
    .io_fromPreMul_isSigNaNAny(mulAddRecFNToRaw_postMul_io_fromPreMul_isSigNaNAny),
    .io_fromPreMul_isNaNAOrB(mulAddRecFNToRaw_postMul_io_fromPreMul_isNaNAOrB),
    .io_fromPreMul_isInfA(mulAddRecFNToRaw_postMul_io_fromPreMul_isInfA),
    .io_fromPreMul_isZeroA(mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroA),
    .io_fromPreMul_isInfB(mulAddRecFNToRaw_postMul_io_fromPreMul_isInfB),
    .io_fromPreMul_isZeroB(mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroB),
    .io_fromPreMul_signProd(mulAddRecFNToRaw_postMul_io_fromPreMul_signProd),
    .io_fromPreMul_isNaNC(mulAddRecFNToRaw_postMul_io_fromPreMul_isNaNC),
    .io_fromPreMul_isInfC(mulAddRecFNToRaw_postMul_io_fromPreMul_isInfC),
    .io_fromPreMul_isZeroC(mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroC),
    .io_fromPreMul_sExpSum(mulAddRecFNToRaw_postMul_io_fromPreMul_sExpSum),
    .io_fromPreMul_doSubMags(mulAddRecFNToRaw_postMul_io_fromPreMul_doSubMags),
    .io_fromPreMul_CIsDominant(mulAddRecFNToRaw_postMul_io_fromPreMul_CIsDominant),
    .io_fromPreMul_CDom_CAlignDist(mulAddRecFNToRaw_postMul_io_fromPreMul_CDom_CAlignDist),
    .io_fromPreMul_highAlignedSigC(mulAddRecFNToRaw_postMul_io_fromPreMul_highAlignedSigC),
    .io_fromPreMul_bit0AlignedSigC(mulAddRecFNToRaw_postMul_io_fromPreMul_bit0AlignedSigC),
    .io_mulAddResult(mulAddRecFNToRaw_postMul_io_mulAddResult),
    .io_roundingMode(mulAddRecFNToRaw_postMul_io_roundingMode),
    .io_invalidExc(mulAddRecFNToRaw_postMul_io_invalidExc),
    .io_rawOut_isNaN(mulAddRecFNToRaw_postMul_io_rawOut_isNaN),
    .io_rawOut_isInf(mulAddRecFNToRaw_postMul_io_rawOut_isInf),
    .io_rawOut_isZero(mulAddRecFNToRaw_postMul_io_rawOut_isZero),
    .io_rawOut_sign(mulAddRecFNToRaw_postMul_io_rawOut_sign),
    .io_rawOut_sExp(mulAddRecFNToRaw_postMul_io_rawOut_sExp),
    .io_rawOut_sig(mulAddRecFNToRaw_postMul_io_rawOut_sig)
  );
  RoundRawFNToRecFN_1 roundRawFNToRecFN ( // @[FPU.scala 625:35]
    .io_invalidExc(roundRawFNToRecFN_io_invalidExc),
    .io_in_isNaN(roundRawFNToRecFN_io_in_isNaN),
    .io_in_isInf(roundRawFNToRecFN_io_in_isInf),
    .io_in_isZero(roundRawFNToRecFN_io_in_isZero),
    .io_in_sign(roundRawFNToRecFN_io_in_sign),
    .io_in_sExp(roundRawFNToRecFN_io_in_sExp),
    .io_in_sig(roundRawFNToRecFN_io_in_sig),
    .io_roundingMode(roundRawFNToRecFN_io_roundingMode),
    .io_out(roundRawFNToRecFN_io_out),
    .io_exceptionFlags(roundRawFNToRecFN_io_exceptionFlags)
  );
  assign io_out = roundRawFNToRecFN_io_out; // @[FPU.scala 636:23]
  assign io_exceptionFlags = roundRawFNToRecFN_io_exceptionFlags; // @[FPU.scala 637:23]
  assign io_validout = _T_31; // @[Valid.scala 112:21 113:17]
  assign mulAddRecFNToRaw_preMul_io_op = io_op; // @[FPU.scala 600:35]
  assign mulAddRecFNToRaw_preMul_io_a = io_a; // @[FPU.scala 601:35]
  assign mulAddRecFNToRaw_preMul_io_b = io_b; // @[FPU.scala 602:35]
  assign mulAddRecFNToRaw_preMul_io_c = io_c; // @[FPU.scala 603:35]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isSigNaNAny = _T_2_isSigNaNAny; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isNaNAOrB = _T_2_isNaNAOrB; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isInfA = _T_2_isInfA; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroA = _T_2_isZeroA; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isInfB = _T_2_isInfB; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroB = _T_2_isZeroB; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_signProd = _T_2_signProd; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isNaNC = _T_2_isNaNC; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isInfC = _T_2_isInfC; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_isZeroC = _T_2_isZeroC; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_sExpSum = _T_2_sExpSum; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_doSubMags = _T_2_doSubMags; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_CIsDominant = _T_2_CIsDominant; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_CDom_CAlignDist = _T_2_CDom_CAlignDist; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_highAlignedSigC = _T_2_highAlignedSigC; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_fromPreMul_bit0AlignedSigC = _T_2_bit0AlignedSigC; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_mulAddResult = _T_5; // @[Valid.scala 112:21 114:16]
  assign mulAddRecFNToRaw_postMul_io_roundingMode = _T_8; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_invalidExc = _T_20; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_in_isNaN = _T_23_isNaN; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_in_isInf = _T_23_isInf; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_in_isZero = _T_23_isZero; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_in_sign = _T_23_sign; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_in_sExp = _T_23_sExp; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_in_sig = _T_23_sig; // @[Valid.scala 112:21 114:16]
  assign roundRawFNToRecFN_io_roundingMode = _T_26; // @[Valid.scala 112:21 114:16]
  always @(posedge clock) begin
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isSigNaNAny <= mulAddRecFNToRaw_preMul_io_toPostMul_isSigNaNAny; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isNaNAOrB <= mulAddRecFNToRaw_preMul_io_toPostMul_isNaNAOrB; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isInfA <= mulAddRecFNToRaw_preMul_io_toPostMul_isInfA; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isZeroA <= mulAddRecFNToRaw_preMul_io_toPostMul_isZeroA; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isInfB <= mulAddRecFNToRaw_preMul_io_toPostMul_isInfB; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isZeroB <= mulAddRecFNToRaw_preMul_io_toPostMul_isZeroB; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_signProd <= mulAddRecFNToRaw_preMul_io_toPostMul_signProd; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isNaNC <= mulAddRecFNToRaw_preMul_io_toPostMul_isNaNC; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isInfC <= mulAddRecFNToRaw_preMul_io_toPostMul_isInfC; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_isZeroC <= mulAddRecFNToRaw_preMul_io_toPostMul_isZeroC; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_sExpSum <= mulAddRecFNToRaw_preMul_io_toPostMul_sExpSum; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_doSubMags <= mulAddRecFNToRaw_preMul_io_toPostMul_doSubMags; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_CIsDominant <= mulAddRecFNToRaw_preMul_io_toPostMul_CIsDominant; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_CDom_CAlignDist <= mulAddRecFNToRaw_preMul_io_toPostMul_CDom_CAlignDist; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_highAlignedSigC <= mulAddRecFNToRaw_preMul_io_toPostMul_highAlignedSigC; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_2_bit0AlignedSigC <= mulAddRecFNToRaw_preMul_io_toPostMul_bit0AlignedSigC; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_5 <= mulAddResult; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      _T_8 <= io_roundingMode; // @[Reg.scala 16:23]
    end
    if (io_validin) begin // @[Reg.scala 16:19]
      roundingMode_stage0 <= io_roundingMode; // @[Reg.scala 16:23]
    end
    if (reset) begin // @[Valid.scala 117:22]
      valid_stage0 <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      valid_stage0 <= io_validin; // @[Valid.scala 117:22]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_20 <= mulAddRecFNToRaw_postMul_io_invalidExc; // @[Reg.scala 16:23]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_23_isNaN <= mulAddRecFNToRaw_postMul_io_rawOut_isNaN; // @[Reg.scala 16:23]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_23_isInf <= mulAddRecFNToRaw_postMul_io_rawOut_isInf; // @[Reg.scala 16:23]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_23_isZero <= mulAddRecFNToRaw_postMul_io_rawOut_isZero; // @[Reg.scala 16:23]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_23_sign <= mulAddRecFNToRaw_postMul_io_rawOut_sign; // @[Reg.scala 16:23]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_23_sExp <= mulAddRecFNToRaw_postMul_io_rawOut_sExp; // @[Reg.scala 16:23]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_23_sig <= mulAddRecFNToRaw_postMul_io_rawOut_sig; // @[Reg.scala 16:23]
    end
    if (valid_stage0) begin // @[Reg.scala 16:19]
      _T_26 <= roundingMode_stage0; // @[Reg.scala 16:23]
    end
    if (reset) begin // @[Valid.scala 117:22]
      _T_31 <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      _T_31 <= valid_stage0; // @[Valid.scala 117:22]
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
  _T_2_isSigNaNAny = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  _T_2_isNaNAOrB = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  _T_2_isInfA = _RAND_2[0:0];
  _RAND_3 = {1{`RANDOM}};
  _T_2_isZeroA = _RAND_3[0:0];
  _RAND_4 = {1{`RANDOM}};
  _T_2_isInfB = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  _T_2_isZeroB = _RAND_5[0:0];
  _RAND_6 = {1{`RANDOM}};
  _T_2_signProd = _RAND_6[0:0];
  _RAND_7 = {1{`RANDOM}};
  _T_2_isNaNC = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  _T_2_isInfC = _RAND_8[0:0];
  _RAND_9 = {1{`RANDOM}};
  _T_2_isZeroC = _RAND_9[0:0];
  _RAND_10 = {1{`RANDOM}};
  _T_2_sExpSum = _RAND_10[9:0];
  _RAND_11 = {1{`RANDOM}};
  _T_2_doSubMags = _RAND_11[0:0];
  _RAND_12 = {1{`RANDOM}};
  _T_2_CIsDominant = _RAND_12[0:0];
  _RAND_13 = {1{`RANDOM}};
  _T_2_CDom_CAlignDist = _RAND_13[4:0];
  _RAND_14 = {1{`RANDOM}};
  _T_2_highAlignedSigC = _RAND_14[25:0];
  _RAND_15 = {1{`RANDOM}};
  _T_2_bit0AlignedSigC = _RAND_15[0:0];
  _RAND_16 = {2{`RANDOM}};
  _T_5 = _RAND_16[48:0];
  _RAND_17 = {1{`RANDOM}};
  _T_8 = _RAND_17[2:0];
  _RAND_18 = {1{`RANDOM}};
  roundingMode_stage0 = _RAND_18[2:0];
  _RAND_19 = {1{`RANDOM}};
  valid_stage0 = _RAND_19[0:0];
  _RAND_20 = {1{`RANDOM}};
  _T_20 = _RAND_20[0:0];
  _RAND_21 = {1{`RANDOM}};
  _T_23_isNaN = _RAND_21[0:0];
  _RAND_22 = {1{`RANDOM}};
  _T_23_isInf = _RAND_22[0:0];
  _RAND_23 = {1{`RANDOM}};
  _T_23_isZero = _RAND_23[0:0];
  _RAND_24 = {1{`RANDOM}};
  _T_23_sign = _RAND_24[0:0];
  _RAND_25 = {1{`RANDOM}};
  _T_23_sExp = _RAND_25[9:0];
  _RAND_26 = {1{`RANDOM}};
  _T_23_sig = _RAND_26[26:0];
  _RAND_27 = {1{`RANDOM}};
  _T_26 = _RAND_27[2:0];
  _RAND_28 = {1{`RANDOM}};
  _T_31 = _RAND_28[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module FPUFMAPipe_1(
  input         clock,
  input         reset,
  input         io_in_valid,
  input         io_in_bits_ren3,
  input         io_in_bits_swap23,
  input  [2:0]  io_in_bits_rm,
  input  [1:0]  io_in_bits_fmaCmd,
  input  [64:0] io_in_bits_in1,
  input  [64:0] io_in_bits_in2,
  input  [64:0] io_in_bits_in3,
  output        io_out_valid,
  output [64:0] io_out_bits_data,
  output [4:0]  io_out_bits_exc
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [95:0] _RAND_3;
  reg [95:0] _RAND_4;
  reg [95:0] _RAND_5;
  reg [31:0] _RAND_6;
  reg [95:0] _RAND_7;
  reg [31:0] _RAND_8;
`endif // RANDOMIZE_REG_INIT
  wire  fma_clock; // @[FPU.scala 661:19]
  wire  fma_reset; // @[FPU.scala 661:19]
  wire  fma_io_validin; // @[FPU.scala 661:19]
  wire [1:0] fma_io_op; // @[FPU.scala 661:19]
  wire [32:0] fma_io_a; // @[FPU.scala 661:19]
  wire [32:0] fma_io_b; // @[FPU.scala 661:19]
  wire [32:0] fma_io_c; // @[FPU.scala 661:19]
  wire [2:0] fma_io_roundingMode; // @[FPU.scala 661:19]
  wire [32:0] fma_io_out; // @[FPU.scala 661:19]
  wire [4:0] fma_io_exceptionFlags; // @[FPU.scala 661:19]
  wire  fma_io_validout; // @[FPU.scala 661:19]
  reg  valid; // @[FPU.scala 649:18]
  reg [2:0] in_rm; // @[FPU.scala 650:15]
  reg [1:0] in_fmaCmd; // @[FPU.scala 650:15]
  reg [64:0] in_in1; // @[FPU.scala 650:15]
  reg [64:0] in_in2; // @[FPU.scala 650:15]
  reg [64:0] in_in3; // @[FPU.scala 650:15]
  wire [64:0] _T_1 = io_in_bits_in1 ^ io_in_bits_in2; // @[FPU.scala 653:32]
  wire [64:0] _T_3 = _T_1 & 65'h100000000; // @[FPU.scala 653:50]
  reg  _T_6; // @[Valid.scala 117:22]
  reg [64:0] _T_7_data; // @[Reg.scala 15:16]
  reg [4:0] _T_7_exc; // @[Reg.scala 15:16]
  wire [4:0] res_exc = fma_io_exceptionFlags; // @[FPU.scala 670:17 672:11]
  wire [64:0] res_data = {{32'd0}, fma_io_out}; // @[FPU.scala 670:17 671:12]
  MulAddRecFNPipe_1 fma ( // @[FPU.scala 661:19]
    .clock(fma_clock),
    .reset(fma_reset),
    .io_validin(fma_io_validin),
    .io_op(fma_io_op),
    .io_a(fma_io_a),
    .io_b(fma_io_b),
    .io_c(fma_io_c),
    .io_roundingMode(fma_io_roundingMode),
    .io_out(fma_io_out),
    .io_exceptionFlags(fma_io_exceptionFlags),
    .io_validout(fma_io_validout)
  );
  assign io_out_valid = _T_6; // @[Valid.scala 112:21 113:17]
  assign io_out_bits_data = _T_7_data; // @[Valid.scala 112:21 114:16]
  assign io_out_bits_exc = _T_7_exc; // @[Valid.scala 112:21 114:16]
  assign fma_clock = clock;
  assign fma_reset = reset;
  assign fma_io_validin = valid; // @[FPU.scala 662:18]
  assign fma_io_op = in_fmaCmd; // @[FPU.scala 663:13]
  assign fma_io_a = in_in1[32:0]; // @[FPU.scala 666:12]
  assign fma_io_b = in_in2[32:0]; // @[FPU.scala 667:12]
  assign fma_io_c = in_in3[32:0]; // @[FPU.scala 668:12]
  assign fma_io_roundingMode = in_rm; // @[FPU.scala 664:23]
  always @(posedge clock) begin
    valid <= io_in_valid; // @[FPU.scala 649:18]
    if (io_in_valid) begin // @[FPU.scala 651:22]
      in_rm <= io_in_bits_rm; // @[FPU.scala 656:8]
    end
    if (io_in_valid) begin // @[FPU.scala 651:22]
      in_fmaCmd <= io_in_bits_fmaCmd; // @[FPU.scala 656:8]
    end
    if (io_in_valid) begin // @[FPU.scala 651:22]
      in_in1 <= io_in_bits_in1; // @[FPU.scala 656:8]
    end
    if (io_in_valid) begin // @[FPU.scala 651:22]
      if (io_in_bits_swap23) begin // @[FPU.scala 657:23]
        in_in2 <= 65'h80000000; // @[FPU.scala 657:32]
      end else begin
        in_in2 <= io_in_bits_in2; // @[FPU.scala 656:8]
      end
    end
    if (io_in_valid) begin // @[FPU.scala 651:22]
      if (~(io_in_bits_ren3 | io_in_bits_swap23)) begin // @[FPU.scala 658:37]
        in_in3 <= _T_3; // @[FPU.scala 658:46]
      end else begin
        in_in3 <= io_in_bits_in3; // @[FPU.scala 656:8]
      end
    end
    if (reset) begin // @[Valid.scala 117:22]
      _T_6 <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      _T_6 <= fma_io_validout; // @[Valid.scala 117:22]
    end
    if (fma_io_validout) begin // @[Reg.scala 16:19]
      _T_7_data <= res_data; // @[Reg.scala 16:23]
    end
    if (fma_io_validout) begin // @[Reg.scala 16:19]
      _T_7_exc <= res_exc; // @[Reg.scala 16:23]
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
  valid = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  in_rm = _RAND_1[2:0];
  _RAND_2 = {1{`RANDOM}};
  in_fmaCmd = _RAND_2[1:0];
  _RAND_3 = {3{`RANDOM}};
  in_in1 = _RAND_3[64:0];
  _RAND_4 = {3{`RANDOM}};
  in_in2 = _RAND_4[64:0];
  _RAND_5 = {3{`RANDOM}};
  in_in3 = _RAND_5[64:0];
  _RAND_6 = {1{`RANDOM}};
  _T_6 = _RAND_6[0:0];
  _RAND_7 = {3{`RANDOM}};
  _T_7_data = _RAND_7[64:0];
  _RAND_8 = {1{`RANDOM}};
  _T_7_exc = _RAND_8[4:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
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
  input         io_in_bits_ren2,
  input         io_in_bits_singleOut,
  input         io_in_bits_wflags,
  input  [2:0]  io_in_bits_rm,
  input  [64:0] io_in_bits_in1,
  input  [64:0] io_in_bits_in2,
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
module CompareRecFN(
  input  [64:0] io_a,
  input  [64:0] io_b,
  input         io_signaling,
  output        io_lt,
  output        io_eq,
  output [4:0]  io_exceptionFlags
);
  wire  rawA_isZero = io_a[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_4 = io_a[63:62] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  rawA_isNaN = _T_4 & io_a[61]; // @[rawFloatFromRecFN.scala 55:33]
  wire  rawA_isInf = _T_4 & ~io_a[61]; // @[rawFloatFromRecFN.scala 56:33]
  wire  rawA_sign = io_a[64]; // @[rawFloatFromRecFN.scala 58:25]
  wire [12:0] rawA_sExp = {1'b0,$signed(io_a[63:52])}; // @[rawFloatFromRecFN.scala 59:27]
  wire  _T_12 = ~rawA_isZero; // @[rawFloatFromRecFN.scala 60:39]
  wire [53:0] rawA_sig = {1'h0,_T_12,io_a[51:0]}; // @[Cat.scala 29:58]
  wire  rawB_isZero = io_b[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_20 = io_b[63:62] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  rawB_isNaN = _T_20 & io_b[61]; // @[rawFloatFromRecFN.scala 55:33]
  wire  rawB_isInf = _T_20 & ~io_b[61]; // @[rawFloatFromRecFN.scala 56:33]
  wire  rawB_sign = io_b[64]; // @[rawFloatFromRecFN.scala 58:25]
  wire [12:0] rawB_sExp = {1'b0,$signed(io_b[63:52])}; // @[rawFloatFromRecFN.scala 59:27]
  wire  _T_28 = ~rawB_isZero; // @[rawFloatFromRecFN.scala 60:39]
  wire [53:0] rawB_sig = {1'h0,_T_28,io_b[51:0]}; // @[Cat.scala 29:58]
  wire  ordered = ~rawA_isNaN & ~rawB_isNaN; // @[CompareRecFN.scala 57:32]
  wire  bothInfs = rawA_isInf & rawB_isInf; // @[CompareRecFN.scala 58:33]
  wire  bothZeros = rawA_isZero & rawB_isZero; // @[CompareRecFN.scala 59:33]
  wire  eqExps = $signed(rawA_sExp) == $signed(rawB_sExp); // @[CompareRecFN.scala 60:29]
  wire  common_ltMags = $signed(rawA_sExp) < $signed(rawB_sExp) | eqExps & rawA_sig < rawB_sig; // @[CompareRecFN.scala 62:33]
  wire  common_eqMags = eqExps & rawA_sig == rawB_sig; // @[CompareRecFN.scala 63:32]
  wire  _T_39 = ~rawB_sign; // @[CompareRecFN.scala 67:28]
  wire  _T_47 = _T_39 & common_ltMags; // @[CompareRecFN.scala 70:41]
  wire  _T_48 = rawA_sign & ~common_ltMags & ~common_eqMags | _T_47; // @[CompareRecFN.scala 69:74]
  wire  _T_49 = ~bothInfs & _T_48; // @[CompareRecFN.scala 68:30]
  wire  _T_50 = rawA_sign & ~rawB_sign | _T_49; // @[CompareRecFN.scala 67:41]
  wire  ordered_lt = ~bothZeros & _T_50; // @[CompareRecFN.scala 66:21]
  wire  ordered_eq = bothZeros | rawA_sign == rawB_sign & (bothInfs | common_eqMags); // @[CompareRecFN.scala 72:19]
  wire  _T_56 = rawA_isNaN & ~rawA_sig[51]; // @[common.scala 81:46]
  wire  _T_59 = rawB_isNaN & ~rawB_sig[51]; // @[common.scala 81:46]
  wire  _T_62 = io_signaling & ~ordered; // @[CompareRecFN.scala 76:27]
  wire  invalid = _T_56 | _T_59 | _T_62; // @[CompareRecFN.scala 75:58]
  assign io_lt = ordered & ordered_lt; // @[CompareRecFN.scala 78:22]
  assign io_eq = ordered & ordered_eq; // @[CompareRecFN.scala 79:22]
  assign io_exceptionFlags = {invalid,4'h0}; // @[Cat.scala 29:58]
endmodule
module RecFNToIN_1(
  input  [64:0] io_in,
  input  [2:0]  io_roundingMode,
  input         io_signedOut,
  output [2:0]  io_intExceptionFlags
);
  wire  rawIn_isZero = io_in[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_4 = io_in[63:62] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  rawIn_isNaN = _T_4 & io_in[61]; // @[rawFloatFromRecFN.scala 55:33]
  wire  rawIn_isInf = _T_4 & ~io_in[61]; // @[rawFloatFromRecFN.scala 56:33]
  wire  rawIn_sign = io_in[64]; // @[rawFloatFromRecFN.scala 58:25]
  wire [12:0] rawIn_sExp = {1'b0,$signed(io_in[63:52])}; // @[rawFloatFromRecFN.scala 59:27]
  wire  _T_12 = ~rawIn_isZero; // @[rawFloatFromRecFN.scala 60:39]
  wire [53:0] rawIn_sig = {1'h0,_T_12,io_in[51:0]}; // @[Cat.scala 29:58]
  wire  magGeOne = rawIn_sExp[11]; // @[RecFNToIN.scala 58:30]
  wire [10:0] posExp = rawIn_sExp[10:0]; // @[RecFNToIN.scala 59:28]
  wire  magJustBelowOne = ~magGeOne & &posExp; // @[RecFNToIN.scala 60:37]
  wire  roundingMode_near_even = io_roundingMode == 3'h0; // @[RecFNToIN.scala 64:53]
  wire  roundingMode_min = io_roundingMode == 3'h2; // @[RecFNToIN.scala 66:53]
  wire  roundingMode_max = io_roundingMode == 3'h3; // @[RecFNToIN.scala 67:53]
  wire  roundingMode_near_maxMag = io_roundingMode == 3'h4; // @[RecFNToIN.scala 68:53]
  wire  roundingMode_odd = io_roundingMode == 3'h6; // @[RecFNToIN.scala 69:53]
  wire [52:0] _T_19 = {magGeOne,rawIn_sig[51:0]}; // @[Cat.scala 29:58]
  wire [4:0] _T_21 = magGeOne ? rawIn_sExp[4:0] : 5'h0; // @[RecFNToIN.scala 81:16]
  wire [83:0] _GEN_0 = {{31'd0}, _T_19}; // @[RecFNToIN.scala 80:50]
  wire [83:0] shiftedSig = _GEN_0 << _T_21; // @[RecFNToIN.scala 80:50]
  wire  _T_24 = |shiftedSig[50:0]; // @[RecFNToIN.scala 86:69]
  wire [33:0] alignedSig = {shiftedSig[83:51],_T_24}; // @[Cat.scala 29:58]
  wire [31:0] unroundedInt = alignedSig[33:2]; // @[RecFNToIN.scala 87:54]
  wire  _T_27 = |alignedSig[1:0]; // @[RecFNToIN.scala 89:57]
  wire  common_inexact = magGeOne ? |alignedSig[1:0] : _T_12; // @[RecFNToIN.scala 89:29]
  wire  _T_37 = magJustBelowOne & _T_27; // @[RecFNToIN.scala 92:26]
  wire  roundIncr_near_even = magGeOne & (&alignedSig[2:1] | &alignedSig[1:0]) | _T_37; // @[RecFNToIN.scala 91:78]
  wire  roundIncr_near_maxMag = magGeOne & alignedSig[1] | magJustBelowOne; // @[RecFNToIN.scala 93:61]
  wire  _T_41 = roundingMode_near_maxMag & roundIncr_near_maxMag; // @[RecFNToIN.scala 96:35]
  wire  _T_42 = roundingMode_near_even & roundIncr_near_even | _T_41; // @[RecFNToIN.scala 95:61]
  wire  _T_44 = rawIn_sign & common_inexact; // @[RecFNToIN.scala 98:26]
  wire  _T_45 = (roundingMode_min | roundingMode_odd) & _T_44; // @[RecFNToIN.scala 97:49]
  wire  _T_46 = _T_42 | _T_45; // @[RecFNToIN.scala 96:61]
  wire  _T_49 = roundingMode_max & (~rawIn_sign & common_inexact); // @[RecFNToIN.scala 99:27]
  wire  roundIncr = _T_46 | _T_49; // @[RecFNToIN.scala 98:46]
  wire  magGeOne_atOverflowEdge = posExp == 11'h1f; // @[RecFNToIN.scala 107:43]
  wire  roundCarryBut2 = &unroundedInt[29:0] & roundIncr; // @[RecFNToIN.scala 110:61]
  wire  _T_61 = |unroundedInt[30:0] | roundIncr; // @[RecFNToIN.scala 117:64]
  wire  _T_62 = magGeOne_atOverflowEdge & _T_61; // @[RecFNToIN.scala 116:49]
  wire  _T_64 = posExp == 11'h1e & roundCarryBut2; // @[RecFNToIN.scala 119:62]
  wire  _T_65 = magGeOne_atOverflowEdge | _T_64; // @[RecFNToIN.scala 118:49]
  wire  _T_66 = rawIn_sign ? _T_62 : _T_65; // @[RecFNToIN.scala 115:24]
  wire  _T_68 = magGeOne_atOverflowEdge & unroundedInt[30]; // @[RecFNToIN.scala 122:50]
  wire  _T_69 = _T_68 & roundCarryBut2; // @[RecFNToIN.scala 123:57]
  wire  _T_70 = rawIn_sign | _T_69; // @[RecFNToIN.scala 121:32]
  wire  _T_71 = io_signedOut ? _T_66 : _T_70; // @[RecFNToIN.scala 114:20]
  wire  _T_72 = posExp >= 11'h20 | _T_71; // @[RecFNToIN.scala 113:40]
  wire  _T_75 = ~io_signedOut & rawIn_sign & roundIncr; // @[RecFNToIN.scala 125:41]
  wire  common_overflow = magGeOne ? _T_72 : _T_75; // @[RecFNToIN.scala 112:12]
  wire  invalidExc = rawIn_isNaN | rawIn_isInf; // @[RecFNToIN.scala 130:34]
  wire  _T_76 = ~invalidExc; // @[RecFNToIN.scala 131:20]
  wire  overflow = ~invalidExc & common_overflow; // @[RecFNToIN.scala 131:32]
  wire  inexact = _T_76 & ~common_overflow & common_inexact; // @[RecFNToIN.scala 132:52]
  wire [1:0] _T_87 = {invalidExc,overflow}; // @[Cat.scala 29:58]
  assign io_intExceptionFlags = {_T_87,inexact}; // @[Cat.scala 29:58]
endmodule
module RecFNToIN(
  input  [64:0] io_in,
  input  [2:0]  io_roundingMode,
  input         io_signedOut,
  output [63:0] io_out,
  output [2:0]  io_intExceptionFlags
);
  wire  rawIn_isZero = io_in[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_4 = io_in[63:62] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  rawIn_isNaN = _T_4 & io_in[61]; // @[rawFloatFromRecFN.scala 55:33]
  wire  rawIn_isInf = _T_4 & ~io_in[61]; // @[rawFloatFromRecFN.scala 56:33]
  wire  rawIn_sign = io_in[64]; // @[rawFloatFromRecFN.scala 58:25]
  wire [12:0] rawIn_sExp = {1'b0,$signed(io_in[63:52])}; // @[rawFloatFromRecFN.scala 59:27]
  wire  _T_12 = ~rawIn_isZero; // @[rawFloatFromRecFN.scala 60:39]
  wire [53:0] rawIn_sig = {1'h0,_T_12,io_in[51:0]}; // @[Cat.scala 29:58]
  wire  magGeOne = rawIn_sExp[11]; // @[RecFNToIN.scala 58:30]
  wire [10:0] posExp = rawIn_sExp[10:0]; // @[RecFNToIN.scala 59:28]
  wire  magJustBelowOne = ~magGeOne & &posExp; // @[RecFNToIN.scala 60:37]
  wire  roundingMode_near_even = io_roundingMode == 3'h0; // @[RecFNToIN.scala 64:53]
  wire  roundingMode_min = io_roundingMode == 3'h2; // @[RecFNToIN.scala 66:53]
  wire  roundingMode_max = io_roundingMode == 3'h3; // @[RecFNToIN.scala 67:53]
  wire  roundingMode_near_maxMag = io_roundingMode == 3'h4; // @[RecFNToIN.scala 68:53]
  wire  roundingMode_odd = io_roundingMode == 3'h6; // @[RecFNToIN.scala 69:53]
  wire [52:0] _T_19 = {magGeOne,rawIn_sig[51:0]}; // @[Cat.scala 29:58]
  wire [5:0] _T_21 = magGeOne ? rawIn_sExp[5:0] : 6'h0; // @[RecFNToIN.scala 81:16]
  wire [115:0] _GEN_2 = {{63'd0}, _T_19}; // @[RecFNToIN.scala 80:50]
  wire [115:0] shiftedSig = _GEN_2 << _T_21; // @[RecFNToIN.scala 80:50]
  wire  _T_24 = |shiftedSig[50:0]; // @[RecFNToIN.scala 86:69]
  wire [65:0] alignedSig = {shiftedSig[115:51],_T_24}; // @[Cat.scala 29:58]
  wire [63:0] unroundedInt = alignedSig[65:2]; // @[RecFNToIN.scala 87:54]
  wire  _T_27 = |alignedSig[1:0]; // @[RecFNToIN.scala 89:57]
  wire  common_inexact = magGeOne ? |alignedSig[1:0] : _T_12; // @[RecFNToIN.scala 89:29]
  wire  _T_37 = magJustBelowOne & _T_27; // @[RecFNToIN.scala 92:26]
  wire  roundIncr_near_even = magGeOne & (&alignedSig[2:1] | &alignedSig[1:0]) | _T_37; // @[RecFNToIN.scala 91:78]
  wire  roundIncr_near_maxMag = magGeOne & alignedSig[1] | magJustBelowOne; // @[RecFNToIN.scala 93:61]
  wire  _T_41 = roundingMode_near_maxMag & roundIncr_near_maxMag; // @[RecFNToIN.scala 96:35]
  wire  _T_42 = roundingMode_near_even & roundIncr_near_even | _T_41; // @[RecFNToIN.scala 95:61]
  wire  _T_44 = rawIn_sign & common_inexact; // @[RecFNToIN.scala 98:26]
  wire  _T_45 = (roundingMode_min | roundingMode_odd) & _T_44; // @[RecFNToIN.scala 97:49]
  wire  _T_46 = _T_42 | _T_45; // @[RecFNToIN.scala 96:61]
  wire  _T_49 = roundingMode_max & (~rawIn_sign & common_inexact); // @[RecFNToIN.scala 99:27]
  wire  roundIncr = _T_46 | _T_49; // @[RecFNToIN.scala 98:46]
  wire [63:0] _T_50 = ~unroundedInt; // @[RecFNToIN.scala 100:45]
  wire [63:0] complUnroundedInt = rawIn_sign ? _T_50 : unroundedInt; // @[RecFNToIN.scala 100:32]
  wire [63:0] _T_53 = complUnroundedInt + 64'h1; // @[RecFNToIN.scala 103:31]
  wire [63:0] _T_54 = roundIncr ^ rawIn_sign ? _T_53 : complUnroundedInt; // @[RecFNToIN.scala 102:12]
  wire  _T_55 = roundingMode_odd & common_inexact; // @[RecFNToIN.scala 105:31]
  wire [63:0] _GEN_0 = {{63'd0}, _T_55}; // @[RecFNToIN.scala 105:11]
  wire [63:0] roundedInt = _T_54 | _GEN_0; // @[RecFNToIN.scala 105:11]
  wire  magGeOne_atOverflowEdge = posExp == 11'h3f; // @[RecFNToIN.scala 107:43]
  wire  roundCarryBut2 = &unroundedInt[61:0] & roundIncr; // @[RecFNToIN.scala 110:61]
  wire  _T_61 = |unroundedInt[62:0] | roundIncr; // @[RecFNToIN.scala 117:64]
  wire  _T_62 = magGeOne_atOverflowEdge & _T_61; // @[RecFNToIN.scala 116:49]
  wire  _T_64 = posExp == 11'h3e & roundCarryBut2; // @[RecFNToIN.scala 119:62]
  wire  _T_65 = magGeOne_atOverflowEdge | _T_64; // @[RecFNToIN.scala 118:49]
  wire  _T_66 = rawIn_sign ? _T_62 : _T_65; // @[RecFNToIN.scala 115:24]
  wire  _T_68 = magGeOne_atOverflowEdge & unroundedInt[62]; // @[RecFNToIN.scala 122:50]
  wire  _T_69 = _T_68 & roundCarryBut2; // @[RecFNToIN.scala 123:57]
  wire  _T_70 = rawIn_sign | _T_69; // @[RecFNToIN.scala 121:32]
  wire  _T_71 = io_signedOut ? _T_66 : _T_70; // @[RecFNToIN.scala 114:20]
  wire  _T_72 = posExp >= 11'h40 | _T_71; // @[RecFNToIN.scala 113:40]
  wire  _T_75 = ~io_signedOut & rawIn_sign & roundIncr; // @[RecFNToIN.scala 125:41]
  wire  common_overflow = magGeOne ? _T_72 : _T_75; // @[RecFNToIN.scala 112:12]
  wire  invalidExc = rawIn_isNaN | rawIn_isInf; // @[RecFNToIN.scala 130:34]
  wire  _T_76 = ~invalidExc; // @[RecFNToIN.scala 131:20]
  wire  overflow = ~invalidExc & common_overflow; // @[RecFNToIN.scala 131:32]
  wire  inexact = _T_76 & ~common_overflow & common_inexact; // @[RecFNToIN.scala 132:52]
  wire  excSign = ~rawIn_isNaN & rawIn_sign; // @[RecFNToIN.scala 134:32]
  wire [63:0] _T_82 = io_signedOut == excSign ? 64'h8000000000000000 : 64'h0; // @[RecFNToIN.scala 136:12]
  wire [62:0] _T_84 = ~excSign ? 63'h7fffffffffffffff : 63'h0; // @[RecFNToIN.scala 140:12]
  wire [63:0] _GEN_1 = {{1'd0}, _T_84}; // @[RecFNToIN.scala 139:11]
  wire [63:0] excOut = _T_82 | _GEN_1; // @[RecFNToIN.scala 139:11]
  wire [1:0] _T_87 = {invalidExc,overflow}; // @[Cat.scala 29:58]
  assign io_out = invalidExc | common_overflow ? excOut : roundedInt; // @[RecFNToIN.scala 142:18]
  assign io_intExceptionFlags = {_T_87,inexact}; // @[Cat.scala 29:58]
endmodule
module FPToInt(
  input         clock,
  input         io_in_valid,
  input         io_in_bits_ren2,
  input         io_in_bits_singleOut,
  input         io_in_bits_wflags,
  input  [2:0]  io_in_bits_rm,
  input  [1:0]  io_in_bits_typ,
  input  [64:0] io_in_bits_in1,
  input  [64:0] io_in_bits_in2,
  output        io_out_bits_lt,
  output [63:0] io_out_bits_toint,
  output [4:0]  io_out_bits_exc
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
  reg [95:0] _RAND_5;
  reg [95:0] _RAND_6;
`endif // RANDOMIZE_REG_INIT
  wire [64:0] dcmp_io_a; // @[FPU.scala 415:20]
  wire [64:0] dcmp_io_b; // @[FPU.scala 415:20]
  wire  dcmp_io_signaling; // @[FPU.scala 415:20]
  wire  dcmp_io_lt; // @[FPU.scala 415:20]
  wire  dcmp_io_eq; // @[FPU.scala 415:20]
  wire [4:0] dcmp_io_exceptionFlags; // @[FPU.scala 415:20]
  wire [64:0] RecFNToIN_io_in; // @[FPU.scala 442:24]
  wire [2:0] RecFNToIN_io_roundingMode; // @[FPU.scala 442:24]
  wire  RecFNToIN_io_signedOut; // @[FPU.scala 442:24]
  wire [63:0] RecFNToIN_io_out; // @[FPU.scala 442:24]
  wire [2:0] RecFNToIN_io_intExceptionFlags; // @[FPU.scala 442:24]
  wire [64:0] RecFNToIN_1_io_in; // @[FPU.scala 452:30]
  wire [2:0] RecFNToIN_1_io_roundingMode; // @[FPU.scala 452:30]
  wire  RecFNToIN_1_io_signedOut; // @[FPU.scala 452:30]
  wire [2:0] RecFNToIN_1_io_intExceptionFlags; // @[FPU.scala 452:30]
  reg  in_ren2; // @[Reg.scala 15:16]
  reg  in_singleOut; // @[Reg.scala 15:16]
  reg  in_wflags; // @[Reg.scala 15:16]
  reg [2:0] in_rm; // @[Reg.scala 15:16]
  reg [1:0] in_typ; // @[Reg.scala 15:16]
  reg [64:0] in_in1; // @[Reg.scala 15:16]
  reg [64:0] in_in2; // @[Reg.scala 15:16]
  wire  tag = ~in_singleOut; // @[FPU.scala 420:13]
  wire  _T_4 = in_in1[63:61] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_6 = in_in1[63:62] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  _T_9 = _T_6 & in_in1[61]; // @[rawFloatFromRecFN.scala 55:33]
  wire  _T_12 = _T_6 & ~in_in1[61]; // @[rawFloatFromRecFN.scala 56:33]
  wire [12:0] _T_14 = {1'b0,$signed(in_in1[63:52])}; // @[rawFloatFromRecFN.scala 59:27]
  wire  _T_15 = ~_T_4; // @[rawFloatFromRecFN.scala 60:39]
  wire [53:0] _T_18 = {1'h0,_T_15,in_in1[51:0]}; // @[Cat.scala 29:58]
  wire  _T_19 = $signed(_T_14) < 13'sh402; // @[fNFromRecFN.scala 50:39]
  wire [5:0] _T_22 = 6'h1 - _T_14[5:0]; // @[fNFromRecFN.scala 51:39]
  wire [52:0] _T_24 = _T_18[53:1] >> _T_22; // @[fNFromRecFN.scala 52:42]
  wire [10:0] _T_28 = _T_14[10:0] - 11'h401; // @[fNFromRecFN.scala 57:45]
  wire [10:0] _T_29 = _T_19 ? 11'h0 : _T_28; // @[fNFromRecFN.scala 55:16]
  wire  _T_30 = _T_9 | _T_12; // @[fNFromRecFN.scala 59:44]
  wire [10:0] _T_32 = _T_30 ? 11'h7ff : 11'h0; // @[Bitwise.scala 72:12]
  wire [10:0] _T_33 = _T_29 | _T_32; // @[fNFromRecFN.scala 59:15]
  wire [51:0] _T_35 = _T_12 ? 52'h0 : _T_18[51:0]; // @[fNFromRecFN.scala 63:20]
  wire [51:0] _T_36 = _T_19 ? _T_24[51:0] : _T_35; // @[fNFromRecFN.scala 61:16]
  wire [63:0] _T_38 = {in_in1[64],_T_33,_T_36}; // @[Cat.scala 29:58]
  wire [32:0] _T_43 = {in_in1[31],in_in1[52],in_in1[30:0]}; // @[Cat.scala 29:58]
  wire  _T_46 = _T_43[31:29] == 3'h0; // @[rawFloatFromRecFN.scala 51:54]
  wire  _T_48 = _T_43[31:30] == 2'h3; // @[rawFloatFromRecFN.scala 52:54]
  wire  _T_51 = _T_48 & _T_43[29]; // @[rawFloatFromRecFN.scala 55:33]
  wire  _T_54 = _T_48 & ~_T_43[29]; // @[rawFloatFromRecFN.scala 56:33]
  wire [9:0] _T_56 = {1'b0,$signed(_T_43[31:23])}; // @[rawFloatFromRecFN.scala 59:27]
  wire  _T_57 = ~_T_46; // @[rawFloatFromRecFN.scala 60:39]
  wire [24:0] _T_60 = {1'h0,_T_57,_T_43[22:0]}; // @[Cat.scala 29:58]
  wire  _T_61 = $signed(_T_56) < 10'sh82; // @[fNFromRecFN.scala 50:39]
  wire [4:0] _T_64 = 5'h1 - _T_56[4:0]; // @[fNFromRecFN.scala 51:39]
  wire [23:0] _T_66 = _T_60[24:1] >> _T_64; // @[fNFromRecFN.scala 52:42]
  wire [7:0] _T_70 = _T_56[7:0] - 8'h81; // @[fNFromRecFN.scala 57:45]
  wire [7:0] _T_71 = _T_61 ? 8'h0 : _T_70; // @[fNFromRecFN.scala 55:16]
  wire  _T_72 = _T_51 | _T_54; // @[fNFromRecFN.scala 59:44]
  wire [7:0] _T_74 = _T_72 ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_75 = _T_71 | _T_74; // @[fNFromRecFN.scala 59:15]
  wire [22:0] _T_77 = _T_54 ? 23'h0 : _T_60[22:0]; // @[fNFromRecFN.scala 63:20]
  wire [22:0] _T_78 = _T_61 ? _T_66[22:0] : _T_77; // @[fNFromRecFN.scala 61:16]
  wire [31:0] _T_80 = {_T_43[32],_T_75,_T_78}; // @[Cat.scala 29:58]
  wire  _T_83 = &in_in1[63:61]; // @[FPU.scala 200:56]
  wire [31:0] _T_85 = _T_83 ? _T_80 : _T_38[31:0]; // @[FPU.scala 391:44]
  wire [63:0] store = {_T_38[63:32],_T_85}; // @[Cat.scala 29:58]
  wire  _T_247 = RecFNToIN_io_intExceptionFlags[2] | RecFNToIN_1_io_intExceptionFlags[1]; // @[FPU.scala 459:54]
  wire  _T_239 = in_in1[64] & ~_T_83; // @[FPU.scala 457:59]
  wire  _T_240 = RecFNToIN_io_signedOut == _T_239; // @[FPU.scala 458:46]
  wire  _T_241 = ~_T_239; // @[FPU.scala 458:69]
  wire [30:0] _T_243 = _T_241 ? 31'h7fffffff : 31'h0; // @[Bitwise.scala 72:12]
  wire [63:0] _T_249 = {RecFNToIN_io_out[63:32],_T_240,_T_243}; // @[Cat.scala 29:58]
  wire [63:0] _GEN_24 = _T_247 ? _T_249 : RecFNToIN_io_out; // @[FPU.scala 446:13 460:{26,34}]
  wire [63:0] _GEN_25 = ~in_typ[1] ? _GEN_24 : RecFNToIN_io_out; // @[FPU.scala 446:13 451:30]
  wire [2:0] _T_216 = ~in_rm; // @[FPU.scala 435:15]
  wire [1:0] _T_217 = {dcmp_io_lt,dcmp_io_eq}; // @[Cat.scala 29:58]
  wire [2:0] _GEN_33 = {{1'd0}, _T_217}; // @[FPU.scala 435:22]
  wire [2:0] _T_218 = _T_216 & _GEN_33; // @[FPU.scala 435:22]
  wire [63:0] _T_221 = {store[63:32], 32'h0}; // @[FPU.scala 435:77]
  wire [63:0] _GEN_34 = {{63'd0}, |_T_218}; // @[FPU.scala 435:57]
  wire [63:0] _T_222 = _GEN_34 | _T_221; // @[FPU.scala 435:57]
  wire [63:0] _GEN_28 = ~in_ren2 ? _GEN_25 : _T_222; // @[FPU.scala 435:11 439:21]
  wire  _T_189 = _T_83 & in_in1[51]; // @[FPU.scala 216:24]
  wire  _T_187 = _T_83 & ~in_in1[51]; // @[FPU.scala 215:24]
  wire  _T_168 = in_in1[63:62] == 2'h3; // @[FPU.scala 207:28]
  wire  _T_183 = _T_168 & ~in_in1[61]; // @[FPU.scala 213:27]
  wire  _T_190 = ~in_in1[64]; // @[FPU.scala 218:34]
  wire  _T_191 = _T_183 & ~in_in1[64]; // @[FPU.scala 218:31]
  wire  _T_175 = in_in1[63:62] == 2'h1; // @[FPU.scala 211:27]
  wire  _T_170 = in_in1[61:52] < 10'h2; // @[FPU.scala 209:55]
  wire  _T_179 = in_in1[63:62] == 2'h1 & ~_T_170 | in_in1[63:62] == 2'h2; // @[FPU.scala 211:61]
  wire  _T_193 = _T_179 & ~in_in1[64]; // @[FPU.scala 218:50]
  wire  _T_174 = in_in1[63:61] == 3'h1 | _T_175 & _T_170; // @[FPU.scala 210:40]
  wire  _T_195 = _T_174 & _T_190; // @[FPU.scala 219:21]
  wire  _T_180 = in_in1[63:61] == 3'h0; // @[FPU.scala 212:23]
  wire  _T_197 = _T_180 & _T_190; // @[FPU.scala 219:38]
  wire  _T_198 = _T_180 & in_in1[64]; // @[FPU.scala 219:55]
  wire  _T_199 = _T_174 & in_in1[64]; // @[FPU.scala 220:21]
  wire  _T_200 = _T_179 & in_in1[64]; // @[FPU.scala 220:39]
  wire  _T_201 = _T_183 & in_in1[64]; // @[FPU.scala 220:54]
  wire [9:0] _T_210 = {_T_189,_T_187,_T_191,_T_193,_T_195,_T_197,_T_198,_T_199,_T_200,_T_201}; // @[Cat.scala 29:58]
  wire [11:0] _T_107 = in_in1[63:52] + 12'h100; // @[FPU.scala 231:31]
  wire [11:0] _T_109 = _T_107 - 12'h800; // @[FPU.scala 231:48]
  wire [8:0] _T_114 = {in_in1[63:61],_T_109[5:0]}; // @[Cat.scala 29:58]
  wire [8:0] _T_116 = _T_4 | in_in1[63:61] >= 3'h6 ? _T_114 : _T_109[8:0]; // @[FPU.scala 232:10]
  wire [75:0] _T_103 = {in_in1[51:0], 24'h0}; // @[FPU.scala 228:28]
  wire [32:0] _T_118 = {in_in1[64],_T_116,_T_103[75:53]}; // @[Cat.scala 29:58]
  wire  _T_138 = &_T_118[31:29]; // @[FPU.scala 214:22]
  wire  _T_143 = _T_138 & _T_118[22]; // @[FPU.scala 216:24]
  wire  _T_141 = _T_138 & ~_T_118[22]; // @[FPU.scala 215:24]
  wire  _T_122 = _T_118[31:30] == 2'h3; // @[FPU.scala 207:28]
  wire  _T_137 = _T_122 & ~_T_118[29]; // @[FPU.scala 213:27]
  wire  _T_144 = ~_T_118[32]; // @[FPU.scala 218:34]
  wire  _T_145 = _T_137 & ~_T_118[32]; // @[FPU.scala 218:31]
  wire  _T_129 = _T_118[31:30] == 2'h1; // @[FPU.scala 211:27]
  wire  _T_124 = _T_118[29:23] < 7'h2; // @[FPU.scala 209:55]
  wire  _T_133 = _T_118[31:30] == 2'h1 & ~_T_124 | _T_118[31:30] == 2'h2; // @[FPU.scala 211:61]
  wire  _T_147 = _T_133 & ~_T_118[32]; // @[FPU.scala 218:50]
  wire  _T_128 = _T_118[31:29] == 3'h1 | _T_129 & _T_124; // @[FPU.scala 210:40]
  wire  _T_149 = _T_128 & _T_144; // @[FPU.scala 219:21]
  wire  _T_134 = _T_118[31:29] == 3'h0; // @[FPU.scala 212:23]
  wire  _T_151 = _T_134 & _T_144; // @[FPU.scala 219:38]
  wire  _T_152 = _T_134 & _T_118[32]; // @[FPU.scala 219:55]
  wire  _T_153 = _T_128 & _T_118[32]; // @[FPU.scala 220:21]
  wire  _T_154 = _T_133 & _T_118[32]; // @[FPU.scala 220:39]
  wire  _T_155 = _T_137 & _T_118[32]; // @[FPU.scala 220:54]
  wire [9:0] _T_164 = {_T_143,_T_141,_T_145,_T_147,_T_149,_T_151,_T_152,_T_153,_T_154,_T_155}; // @[Cat.scala 29:58]
  wire [9:0] _T_212 = tag ? _T_210 : _T_164; // @[package.scala 32:76]
  wire [63:0] _GEN_35 = {{54'd0}, _T_212}; // @[FPU.scala 430:27]
  wire [63:0] _T_215 = _GEN_35 | _T_221; // @[FPU.scala 430:27]
  wire [63:0] _GEN_22 = in_rm[0] ? _T_215 : store; // @[FPU.scala 428:19 430:11]
  wire [63:0] toint = in_wflags ? _GEN_28 : _GEN_22; // @[FPU.scala 434:20]
  wire [31:0] _T_94 = toint[31] ? 32'hffffffff : 32'h0; // @[Bitwise.scala 72:12]
  wire [63:0] _T_95 = {_T_94,toint[31:0]}; // @[Cat.scala 29:58]
  wire  _GEN_27 = ~in_ren2 & in_typ[1]; // @[FPU.scala 437:13 439:21 441:15]
  wire  _GEN_23 = in_rm[0] ? 1'h0 : tag; // @[FPU.scala 428:19 431:13]
  wire  intType = in_wflags ? _GEN_27 : _GEN_23; // @[FPU.scala 434:20]
  wire  _T_228 = |RecFNToIN_io_intExceptionFlags[2:1]; // @[FPU.scala 447:62]
  wire [4:0] _T_231 = {_T_228,3'h0,RecFNToIN_io_intExceptionFlags[0]}; // @[Cat.scala 29:58]
  wire  _T_252 = ~_T_247 & RecFNToIN_io_intExceptionFlags[0]; // @[FPU.scala 461:64]
  wire [4:0] _T_254 = {_T_247,3'h0,_T_252}; // @[Cat.scala 29:58]
  wire [4:0] _GEN_26 = ~in_typ[1] ? _T_254 : _T_231; // @[FPU.scala 447:23 451:30 461:27]
  wire [4:0] _GEN_29 = ~in_ren2 ? _GEN_26 : dcmp_io_exceptionFlags; // @[FPU.scala 436:21 439:21]
  CompareRecFN dcmp ( // @[FPU.scala 415:20]
    .io_a(dcmp_io_a),
    .io_b(dcmp_io_b),
    .io_signaling(dcmp_io_signaling),
    .io_lt(dcmp_io_lt),
    .io_eq(dcmp_io_eq),
    .io_exceptionFlags(dcmp_io_exceptionFlags)
  );
  RecFNToIN RecFNToIN ( // @[FPU.scala 442:24]
    .io_in(RecFNToIN_io_in),
    .io_roundingMode(RecFNToIN_io_roundingMode),
    .io_signedOut(RecFNToIN_io_signedOut),
    .io_out(RecFNToIN_io_out),
    .io_intExceptionFlags(RecFNToIN_io_intExceptionFlags)
  );
  RecFNToIN_1 RecFNToIN_1 ( // @[FPU.scala 452:30]
    .io_in(RecFNToIN_1_io_in),
    .io_roundingMode(RecFNToIN_1_io_roundingMode),
    .io_signedOut(RecFNToIN_1_io_signedOut),
    .io_intExceptionFlags(RecFNToIN_1_io_intExceptionFlags)
  );
  assign io_out_bits_lt = dcmp_io_lt | $signed(dcmp_io_a) < 65'sh0 & $signed(dcmp_io_b) >= 65'sh0; // @[FPU.scala 468:32]
  assign io_out_bits_toint = intType ? toint : _T_95; // @[package.scala 32:76]
  assign io_out_bits_exc = in_wflags ? _GEN_29 : 5'h0; // @[FPU.scala 426:19 434:20]
  assign dcmp_io_a = in_in1; // @[FPU.scala 416:13]
  assign dcmp_io_b = in_in2; // @[FPU.scala 417:13]
  assign dcmp_io_signaling = ~in_rm[1]; // @[FPU.scala 418:24]
  assign RecFNToIN_io_in = in_in1; // @[FPU.scala 443:18]
  assign RecFNToIN_io_roundingMode = in_rm; // @[FPU.scala 444:28]
  assign RecFNToIN_io_signedOut = ~in_typ[0]; // @[FPU.scala 445:28]
  assign RecFNToIN_1_io_in = in_in1; // @[FPU.scala 453:24]
  assign RecFNToIN_1_io_roundingMode = in_rm; // @[FPU.scala 454:34]
  assign RecFNToIN_1_io_signedOut = ~in_typ[0]; // @[FPU.scala 455:34]
  always @(posedge clock) begin
    if (io_in_valid) begin // @[Reg.scala 16:19]
      in_ren2 <= io_in_bits_ren2; // @[Reg.scala 16:23]
    end
    if (io_in_valid) begin // @[Reg.scala 16:19]
      in_singleOut <= io_in_bits_singleOut; // @[Reg.scala 16:23]
    end
    if (io_in_valid) begin // @[Reg.scala 16:19]
      in_wflags <= io_in_bits_wflags; // @[Reg.scala 16:23]
    end
    if (io_in_valid) begin // @[Reg.scala 16:19]
      in_rm <= io_in_bits_rm; // @[Reg.scala 16:23]
    end
    if (io_in_valid) begin // @[Reg.scala 16:19]
      in_typ <= io_in_bits_typ; // @[Reg.scala 16:23]
    end
    if (io_in_valid) begin // @[Reg.scala 16:19]
      in_in1 <= io_in_bits_in1; // @[Reg.scala 16:23]
    end
    if (io_in_valid) begin // @[Reg.scala 16:19]
      in_in2 <= io_in_bits_in2; // @[Reg.scala 16:23]
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
  in_ren2 = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  in_singleOut = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  in_wflags = _RAND_2[0:0];
  _RAND_3 = {1{`RANDOM}};
  in_rm = _RAND_3[2:0];
  _RAND_4 = {1{`RANDOM}};
  in_typ = _RAND_4[1:0];
  _RAND_5 = {3{`RANDOM}};
  in_in1 = _RAND_5[64:0];
  _RAND_6 = {3{`RANDOM}};
  in_in2 = _RAND_6[64:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module UOPCodeFPUDecoder_1(
  input  [6:0] io_uopc,
  output       io_sigs_ren2,
  output       io_sigs_ren3,
  output       io_sigs_swap23,
  output       io_sigs_singleIn,
  output       io_sigs_singleOut,
  output       io_sigs_toint,
  output       io_sigs_fastpipe,
  output       io_sigs_fma,
  output       io_sigs_wflags
);
  wire [6:0] _T = io_uopc & 7'h76; // @[Decode.scala 14:65]
  wire  _T_1 = _T == 7'h46; // @[Decode.scala 14:121]
  wire [6:0] _T_2 = io_uopc & 7'h6c; // @[Decode.scala 14:65]
  wire [6:0] _T_6 = io_uopc & 7'h72; // @[Decode.scala 14:65]
  wire  _T_7 = _T_6 == 7'h50; // @[Decode.scala 14:121]
  wire [6:0] _T_8 = io_uopc & 7'h7c; // @[Decode.scala 14:65]
  wire  _T_9 = _T_8 == 7'h60; // @[Decode.scala 14:121]
  wire [6:0] _T_10 = io_uopc & 7'h7b; // @[Decode.scala 14:65]
  wire  _T_11 = _T_10 == 7'h60; // @[Decode.scala 14:121]
  wire [6:0] _T_14 = io_uopc & 7'h6a; // @[Decode.scala 14:65]
  wire  _T_15 = _T_14 == 7'h4a; // @[Decode.scala 14:121]
  wire [6:0] _T_23 = io_uopc & 7'h6e; // @[Decode.scala 14:65]
  wire  _T_24 = _T_23 == 7'h48; // @[Decode.scala 14:121]
  wire [6:0] _T_25 = io_uopc & 7'h75; // @[Decode.scala 14:65]
  wire  _T_26 = _T_25 == 7'h55; // @[Decode.scala 14:121]
  wire [6:0] _T_27 = io_uopc & 7'h78; // @[Decode.scala 14:65]
  wire  _T_28 = _T_27 == 7'h58; // @[Decode.scala 14:121]
  wire [6:0] _T_34 = io_uopc & 7'h7d; // @[Decode.scala 14:65]
  wire  _T_35 = _T_34 == 7'h5d; // @[Decode.scala 14:121]
  wire [6:0] _T_36 = io_uopc & 7'h7e; // @[Decode.scala 14:65]
  wire  _T_37 = _T_36 == 7'h5e; // @[Decode.scala 14:121]
  wire  _T_41 = io_uopc == 7'h57; // @[Decode.scala 14:121]
  wire  _T_43 = _T_34 == 7'h58; // @[Decode.scala 14:121]
  wire  _T_45 = _T_36 == 7'h5a; // @[Decode.scala 14:121]
  wire  _T_48 = io_uopc == 7'h4b; // @[Decode.scala 14:121]
  wire  _T_50 = _T_34 == 7'h50; // @[Decode.scala 14:121]
  wire [6:0] _T_51 = io_uopc & 7'h77; // @[Decode.scala 14:65]
  wire  _T_52 = _T_51 == 7'h57; // @[Decode.scala 14:121]
  wire  _T_53 = io_uopc == 7'h60; // @[Decode.scala 14:121]
  wire [6:0] _T_54 = io_uopc & 7'h6f; // @[Decode.scala 14:65]
  wire  _T_55 = _T_54 == 7'h44; // @[Decode.scala 14:121]
  wire  _T_57 = _T_54 == 7'h4e; // @[Decode.scala 14:121]
  wire  _T_59 = _T_10 == 7'h59; // @[Decode.scala 14:121]
  wire  _T_61 = _T_54 == 7'h48; // @[Decode.scala 14:121]
  wire  _T_63 = _T_51 == 7'h44; // @[Decode.scala 14:121]
  wire  _T_73 = _T_51 == 7'h46; // @[Decode.scala 14:121]
  wire [6:0] _T_74 = io_uopc & 7'h79; // @[Decode.scala 14:65]
  wire  _T_75 = _T_74 == 7'h48; // @[Decode.scala 14:121]
  wire  _T_77 = _T_10 == 7'h50; // @[Decode.scala 14:121]
  wire  _T_89 = _T_8 == 7'h50; // @[Decode.scala 14:121]
  wire  _T_92 = _T_8 == 7'h48; // @[Decode.scala 14:121]
  wire  _T_94 = _T_36 == 7'h54; // @[Decode.scala 14:121]
  wire  _T_100 = _T_2 == 7'h4c; // @[Decode.scala 14:121]
  assign io_sigs_ren2 = _T_24 | _T_7 | _T_26 | _T_28 | _T_9 | _T_11; // @[Decode.scala 15:30]
  assign io_sigs_ren3 = _T_35 | _T_37 | _T_9 | _T_11; // @[Decode.scala 15:30]
  assign io_sigs_swap23 = _T_41 | _T_43 | _T_45; // @[Decode.scala 15:30]
  assign io_sigs_singleIn = _T_48 | _T_50 | _T_52 | _T_53 | _T_55 | _T_57 | _T_59 | _T_61 | _T_63; // @[Decode.scala 15:30]
  assign io_sigs_singleOut = _T_73 | _T_75 | _T_50 | _T_77 | _T_52 | _T_53 | _T_59 | _T_61 | _T_57; // @[Decode.scala 15:30]
  assign io_sigs_toint = _T_1 | _T_89; // @[Decode.scala 15:30]
  assign io_sigs_fastpipe = _T_92 | _T_94; // @[Decode.scala 15:30]
  assign io_sigs_fma = _T_52 | _T_28 | _T_9 | _T_11; // @[Decode.scala 15:30]
  assign io_sigs_wflags = _T_15 | _T_100 | _T_7 | _T_26 | _T_9 | _T_11; // @[Decode.scala 15:30]
endmodule
module FPU(
  input         clock,
  input         reset,
  input         io_req_valid,
  input  [6:0]  io_req_bits_uop_uopc,
  input  [31:0] io_req_bits_uop_inst,
  input  [31:0] io_req_bits_uop_debug_inst,
  input         io_req_bits_uop_is_rvc,
  input  [39:0] io_req_bits_uop_debug_pc,
  input  [2:0]  io_req_bits_uop_iq_type,
  input  [9:0]  io_req_bits_uop_fu_code,
  input  [3:0]  io_req_bits_uop_ctrl_br_type,
  input  [1:0]  io_req_bits_uop_ctrl_op1_sel,
  input  [2:0]  io_req_bits_uop_ctrl_op2_sel,
  input  [2:0]  io_req_bits_uop_ctrl_imm_sel,
  input  [3:0]  io_req_bits_uop_ctrl_op_fcn,
  input         io_req_bits_uop_ctrl_fcn_dw,
  input  [2:0]  io_req_bits_uop_ctrl_csr_cmd,
  input         io_req_bits_uop_ctrl_is_load,
  input         io_req_bits_uop_ctrl_is_sta,
  input         io_req_bits_uop_ctrl_is_std,
  input  [1:0]  io_req_bits_uop_iw_state,
  input         io_req_bits_uop_iw_p1_poisoned,
  input         io_req_bits_uop_iw_p2_poisoned,
  input         io_req_bits_uop_is_br,
  input         io_req_bits_uop_is_jalr,
  input         io_req_bits_uop_is_jal,
  input         io_req_bits_uop_is_sfb,
  input  [7:0]  io_req_bits_uop_br_mask,
  input  [2:0]  io_req_bits_uop_br_tag,
  input  [3:0]  io_req_bits_uop_ftq_idx,
  input         io_req_bits_uop_edge_inst,
  input  [5:0]  io_req_bits_uop_pc_lob,
  input         io_req_bits_uop_taken,
  input  [19:0] io_req_bits_uop_imm_packed,
  input  [11:0] io_req_bits_uop_csr_addr,
  input  [4:0]  io_req_bits_uop_rob_idx,
  input  [2:0]  io_req_bits_uop_ldq_idx,
  input  [2:0]  io_req_bits_uop_stq_idx,
  input  [1:0]  io_req_bits_uop_rxq_idx,
  input  [5:0]  io_req_bits_uop_pdst,
  input  [5:0]  io_req_bits_uop_prs1,
  input  [5:0]  io_req_bits_uop_prs2,
  input  [5:0]  io_req_bits_uop_prs3,
  input  [3:0]  io_req_bits_uop_ppred,
  input         io_req_bits_uop_prs1_busy,
  input         io_req_bits_uop_prs2_busy,
  input         io_req_bits_uop_prs3_busy,
  input         io_req_bits_uop_ppred_busy,
  input  [5:0]  io_req_bits_uop_stale_pdst,
  input         io_req_bits_uop_exception,
  input  [63:0] io_req_bits_uop_exc_cause,
  input         io_req_bits_uop_bypassable,
  input  [4:0]  io_req_bits_uop_mem_cmd,
  input  [1:0]  io_req_bits_uop_mem_size,
  input         io_req_bits_uop_mem_signed,
  input         io_req_bits_uop_is_fence,
  input         io_req_bits_uop_is_fencei,
  input         io_req_bits_uop_is_amo,
  input         io_req_bits_uop_uses_ldq,
  input         io_req_bits_uop_uses_stq,
  input         io_req_bits_uop_is_sys_pc2epc,
  input         io_req_bits_uop_is_unique,
  input         io_req_bits_uop_flush_on_commit,
  input         io_req_bits_uop_ldst_is_rs1,
  input  [5:0]  io_req_bits_uop_ldst,
  input  [5:0]  io_req_bits_uop_lrs1,
  input  [5:0]  io_req_bits_uop_lrs2,
  input  [5:0]  io_req_bits_uop_lrs3,
  input         io_req_bits_uop_ldst_val,
  input  [1:0]  io_req_bits_uop_dst_rtype,
  input  [1:0]  io_req_bits_uop_lrs1_rtype,
  input  [1:0]  io_req_bits_uop_lrs2_rtype,
  input         io_req_bits_uop_frs3_en,
  input         io_req_bits_uop_fp_val,
  input         io_req_bits_uop_fp_single,
  input         io_req_bits_uop_xcpt_pf_if,
  input         io_req_bits_uop_xcpt_ae_if,
  input         io_req_bits_uop_xcpt_ma_if,
  input         io_req_bits_uop_bp_debug_if,
  input         io_req_bits_uop_bp_xcpt_if,
  input  [1:0]  io_req_bits_uop_debug_fsrc,
  input  [1:0]  io_req_bits_uop_debug_tsrc,
  input  [64:0] io_req_bits_rs1_data,
  input  [64:0] io_req_bits_rs2_data,
  input  [64:0] io_req_bits_rs3_data,
  input  [2:0]  io_req_bits_fcsr_rm,
  output        io_resp_valid,
  output [6:0]  io_resp_bits_uop_uopc,
  output [31:0] io_resp_bits_uop_inst,
  output [31:0] io_resp_bits_uop_debug_inst,
  output        io_resp_bits_uop_is_rvc,
  output [39:0] io_resp_bits_uop_debug_pc,
  output [2:0]  io_resp_bits_uop_iq_type,
  output [9:0]  io_resp_bits_uop_fu_code,
  output [3:0]  io_resp_bits_uop_ctrl_br_type,
  output [1:0]  io_resp_bits_uop_ctrl_op1_sel,
  output [2:0]  io_resp_bits_uop_ctrl_op2_sel,
  output [2:0]  io_resp_bits_uop_ctrl_imm_sel,
  output [3:0]  io_resp_bits_uop_ctrl_op_fcn,
  output        io_resp_bits_uop_ctrl_fcn_dw,
  output [2:0]  io_resp_bits_uop_ctrl_csr_cmd,
  output        io_resp_bits_uop_ctrl_is_load,
  output        io_resp_bits_uop_ctrl_is_sta,
  output        io_resp_bits_uop_ctrl_is_std,
  output [1:0]  io_resp_bits_uop_iw_state,
  output        io_resp_bits_uop_iw_p1_poisoned,
  output        io_resp_bits_uop_iw_p2_poisoned,
  output        io_resp_bits_uop_is_br,
  output        io_resp_bits_uop_is_jalr,
  output        io_resp_bits_uop_is_jal,
  output        io_resp_bits_uop_is_sfb,
  output [7:0]  io_resp_bits_uop_br_mask,
  output [2:0]  io_resp_bits_uop_br_tag,
  output [3:0]  io_resp_bits_uop_ftq_idx,
  output        io_resp_bits_uop_edge_inst,
  output [5:0]  io_resp_bits_uop_pc_lob,
  output        io_resp_bits_uop_taken,
  output [19:0] io_resp_bits_uop_imm_packed,
  output [11:0] io_resp_bits_uop_csr_addr,
  output [4:0]  io_resp_bits_uop_rob_idx,
  output [2:0]  io_resp_bits_uop_ldq_idx,
  output [2:0]  io_resp_bits_uop_stq_idx,
  output [1:0]  io_resp_bits_uop_rxq_idx,
  output [5:0]  io_resp_bits_uop_pdst,
  output [5:0]  io_resp_bits_uop_prs1,
  output [5:0]  io_resp_bits_uop_prs2,
  output [5:0]  io_resp_bits_uop_prs3,
  output [3:0]  io_resp_bits_uop_ppred,
  output        io_resp_bits_uop_prs1_busy,
  output        io_resp_bits_uop_prs2_busy,
  output        io_resp_bits_uop_prs3_busy,
  output        io_resp_bits_uop_ppred_busy,
  output [5:0]  io_resp_bits_uop_stale_pdst,
  output        io_resp_bits_uop_exception,
  output [63:0] io_resp_bits_uop_exc_cause,
  output        io_resp_bits_uop_bypassable,
  output [4:0]  io_resp_bits_uop_mem_cmd,
  output [1:0]  io_resp_bits_uop_mem_size,
  output        io_resp_bits_uop_mem_signed,
  output        io_resp_bits_uop_is_fence,
  output        io_resp_bits_uop_is_fencei,
  output        io_resp_bits_uop_is_amo,
  output        io_resp_bits_uop_uses_ldq,
  output        io_resp_bits_uop_uses_stq,
  output        io_resp_bits_uop_is_sys_pc2epc,
  output        io_resp_bits_uop_is_unique,
  output        io_resp_bits_uop_flush_on_commit,
  output        io_resp_bits_uop_ldst_is_rs1,
  output [5:0]  io_resp_bits_uop_ldst,
  output [5:0]  io_resp_bits_uop_lrs1,
  output [5:0]  io_resp_bits_uop_lrs2,
  output [5:0]  io_resp_bits_uop_lrs3,
  output        io_resp_bits_uop_ldst_val,
  output [1:0]  io_resp_bits_uop_dst_rtype,
  output [1:0]  io_resp_bits_uop_lrs1_rtype,
  output [1:0]  io_resp_bits_uop_lrs2_rtype,
  output        io_resp_bits_uop_frs3_en,
  output        io_resp_bits_uop_fp_val,
  output        io_resp_bits_uop_fp_single,
  output        io_resp_bits_uop_xcpt_pf_if,
  output        io_resp_bits_uop_xcpt_ae_if,
  output        io_resp_bits_uop_xcpt_ma_if,
  output        io_resp_bits_uop_bp_debug_if,
  output        io_resp_bits_uop_bp_xcpt_if,
  output [1:0]  io_resp_bits_uop_debug_fsrc,
  output [1:0]  io_resp_bits_uop_debug_tsrc,
  output [64:0] io_resp_bits_data,
  output        io_resp_bits_predicated,
  output        io_resp_bits_fflags_valid,
  output [6:0]  io_resp_bits_fflags_bits_uop_uopc,
  output [31:0] io_resp_bits_fflags_bits_uop_inst,
  output [31:0] io_resp_bits_fflags_bits_uop_debug_inst,
  output        io_resp_bits_fflags_bits_uop_is_rvc,
  output [39:0] io_resp_bits_fflags_bits_uop_debug_pc,
  output [2:0]  io_resp_bits_fflags_bits_uop_iq_type,
  output [9:0]  io_resp_bits_fflags_bits_uop_fu_code,
  output [3:0]  io_resp_bits_fflags_bits_uop_ctrl_br_type,
  output [1:0]  io_resp_bits_fflags_bits_uop_ctrl_op1_sel,
  output [2:0]  io_resp_bits_fflags_bits_uop_ctrl_op2_sel,
  output [2:0]  io_resp_bits_fflags_bits_uop_ctrl_imm_sel,
  output [3:0]  io_resp_bits_fflags_bits_uop_ctrl_op_fcn,
  output        io_resp_bits_fflags_bits_uop_ctrl_fcn_dw,
  output [2:0]  io_resp_bits_fflags_bits_uop_ctrl_csr_cmd,
  output        io_resp_bits_fflags_bits_uop_ctrl_is_load,
  output        io_resp_bits_fflags_bits_uop_ctrl_is_sta,
  output        io_resp_bits_fflags_bits_uop_ctrl_is_std,
  output [1:0]  io_resp_bits_fflags_bits_uop_iw_state,
  output        io_resp_bits_fflags_bits_uop_iw_p1_poisoned,
  output        io_resp_bits_fflags_bits_uop_iw_p2_poisoned,
  output        io_resp_bits_fflags_bits_uop_is_br,
  output        io_resp_bits_fflags_bits_uop_is_jalr,
  output        io_resp_bits_fflags_bits_uop_is_jal,
  output        io_resp_bits_fflags_bits_uop_is_sfb,
  output [7:0]  io_resp_bits_fflags_bits_uop_br_mask,
  output [2:0]  io_resp_bits_fflags_bits_uop_br_tag,
  output [3:0]  io_resp_bits_fflags_bits_uop_ftq_idx,
  output        io_resp_bits_fflags_bits_uop_edge_inst,
  output [5:0]  io_resp_bits_fflags_bits_uop_pc_lob,
  output        io_resp_bits_fflags_bits_uop_taken,
  output [19:0] io_resp_bits_fflags_bits_uop_imm_packed,
  output [11:0] io_resp_bits_fflags_bits_uop_csr_addr,
  output [4:0]  io_resp_bits_fflags_bits_uop_rob_idx,
  output [2:0]  io_resp_bits_fflags_bits_uop_ldq_idx,
  output [2:0]  io_resp_bits_fflags_bits_uop_stq_idx,
  output [1:0]  io_resp_bits_fflags_bits_uop_rxq_idx,
  output [5:0]  io_resp_bits_fflags_bits_uop_pdst,
  output [5:0]  io_resp_bits_fflags_bits_uop_prs1,
  output [5:0]  io_resp_bits_fflags_bits_uop_prs2,
  output [5:0]  io_resp_bits_fflags_bits_uop_prs3,
  output [3:0]  io_resp_bits_fflags_bits_uop_ppred,
  output        io_resp_bits_fflags_bits_uop_prs1_busy,
  output        io_resp_bits_fflags_bits_uop_prs2_busy,
  output        io_resp_bits_fflags_bits_uop_prs3_busy,
  output        io_resp_bits_fflags_bits_uop_ppred_busy,
  output [5:0]  io_resp_bits_fflags_bits_uop_stale_pdst,
  output        io_resp_bits_fflags_bits_uop_exception,
  output [63:0] io_resp_bits_fflags_bits_uop_exc_cause,
  output        io_resp_bits_fflags_bits_uop_bypassable,
  output [4:0]  io_resp_bits_fflags_bits_uop_mem_cmd,
  output [1:0]  io_resp_bits_fflags_bits_uop_mem_size,
  output        io_resp_bits_fflags_bits_uop_mem_signed,
  output        io_resp_bits_fflags_bits_uop_is_fence,
  output        io_resp_bits_fflags_bits_uop_is_fencei,
  output        io_resp_bits_fflags_bits_uop_is_amo,
  output        io_resp_bits_fflags_bits_uop_uses_ldq,
  output        io_resp_bits_fflags_bits_uop_uses_stq,
  output        io_resp_bits_fflags_bits_uop_is_sys_pc2epc,
  output        io_resp_bits_fflags_bits_uop_is_unique,
  output        io_resp_bits_fflags_bits_uop_flush_on_commit,
  output        io_resp_bits_fflags_bits_uop_ldst_is_rs1,
  output [5:0]  io_resp_bits_fflags_bits_uop_ldst,
  output [5:0]  io_resp_bits_fflags_bits_uop_lrs1,
  output [5:0]  io_resp_bits_fflags_bits_uop_lrs2,
  output [5:0]  io_resp_bits_fflags_bits_uop_lrs3,
  output        io_resp_bits_fflags_bits_uop_ldst_val,
  output [1:0]  io_resp_bits_fflags_bits_uop_dst_rtype,
  output [1:0]  io_resp_bits_fflags_bits_uop_lrs1_rtype,
  output [1:0]  io_resp_bits_fflags_bits_uop_lrs2_rtype,
  output        io_resp_bits_fflags_bits_uop_frs3_en,
  output        io_resp_bits_fflags_bits_uop_fp_val,
  output        io_resp_bits_fflags_bits_uop_fp_single,
  output        io_resp_bits_fflags_bits_uop_xcpt_pf_if,
  output        io_resp_bits_fflags_bits_uop_xcpt_ae_if,
  output        io_resp_bits_fflags_bits_uop_xcpt_ma_if,
  output        io_resp_bits_fflags_bits_uop_bp_debug_if,
  output        io_resp_bits_fflags_bits_uop_bp_xcpt_if,
  output [1:0]  io_resp_bits_fflags_bits_uop_debug_fsrc,
  output [1:0]  io_resp_bits_fflags_bits_uop_debug_tsrc,
  output [4:0]  io_resp_bits_fflags_bits_flags
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [63:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
  reg [63:0] _RAND_5;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_7;
  reg [63:0] _RAND_8;
  reg [31:0] _RAND_9;
  reg [31:0] _RAND_10;
  reg [31:0] _RAND_11;
  reg [31:0] _RAND_12;
  reg [31:0] _RAND_13;
  reg [31:0] _RAND_14;
  reg [31:0] _RAND_15;
  reg [31:0] _RAND_16;
`endif // RANDOMIZE_REG_INIT
  wire [6:0] fp_decoder_io_uopc; // @[fpu.scala 181:26]
  wire  fp_decoder_io_sigs_ren2; // @[fpu.scala 181:26]
  wire  fp_decoder_io_sigs_ren3; // @[fpu.scala 181:26]
  wire  fp_decoder_io_sigs_swap23; // @[fpu.scala 181:26]
  wire  fp_decoder_io_sigs_singleIn; // @[fpu.scala 181:26]
  wire  fp_decoder_io_sigs_singleOut; // @[fpu.scala 181:26]
  wire  fp_decoder_io_sigs_toint; // @[fpu.scala 181:26]
  wire  fp_decoder_io_sigs_fastpipe; // @[fpu.scala 181:26]
  wire  fp_decoder_io_sigs_fma; // @[fpu.scala 181:26]
  wire  fp_decoder_io_sigs_wflags; // @[fpu.scala 181:26]
  wire  dfma_clock; // @[fpu.scala 203:20]
  wire  dfma_reset; // @[fpu.scala 203:20]
  wire  dfma_io_in_valid; // @[fpu.scala 203:20]
  wire  dfma_io_in_bits_ren3; // @[fpu.scala 203:20]
  wire  dfma_io_in_bits_swap23; // @[fpu.scala 203:20]
  wire [2:0] dfma_io_in_bits_rm; // @[fpu.scala 203:20]
  wire [1:0] dfma_io_in_bits_fmaCmd; // @[fpu.scala 203:20]
  wire [64:0] dfma_io_in_bits_in1; // @[fpu.scala 203:20]
  wire [64:0] dfma_io_in_bits_in2; // @[fpu.scala 203:20]
  wire [64:0] dfma_io_in_bits_in3; // @[fpu.scala 203:20]
  wire  dfma_io_out_valid; // @[fpu.scala 203:20]
  wire [64:0] dfma_io_out_bits_data; // @[fpu.scala 203:20]
  wire [4:0] dfma_io_out_bits_exc; // @[fpu.scala 203:20]
  wire [6:0] FMADecoder_io_uopc; // @[fpu.scala 197:29]
  wire [1:0] FMADecoder_io_cmd; // @[fpu.scala 197:29]
  wire  sfma_clock; // @[fpu.scala 207:20]
  wire  sfma_reset; // @[fpu.scala 207:20]
  wire  sfma_io_in_valid; // @[fpu.scala 207:20]
  wire  sfma_io_in_bits_ren3; // @[fpu.scala 207:20]
  wire  sfma_io_in_bits_swap23; // @[fpu.scala 207:20]
  wire [2:0] sfma_io_in_bits_rm; // @[fpu.scala 207:20]
  wire [1:0] sfma_io_in_bits_fmaCmd; // @[fpu.scala 207:20]
  wire [64:0] sfma_io_in_bits_in1; // @[fpu.scala 207:20]
  wire [64:0] sfma_io_in_bits_in2; // @[fpu.scala 207:20]
  wire [64:0] sfma_io_in_bits_in3; // @[fpu.scala 207:20]
  wire  sfma_io_out_valid; // @[fpu.scala 207:20]
  wire [64:0] sfma_io_out_bits_data; // @[fpu.scala 207:20]
  wire [4:0] sfma_io_out_bits_exc; // @[fpu.scala 207:20]
  wire [6:0] FMADecoder_1_io_uopc; // @[fpu.scala 197:29]
  wire [1:0] FMADecoder_1_io_cmd; // @[fpu.scala 197:29]
  wire  fpiu_clock; // @[fpu.scala 211:20]
  wire  fpiu_io_in_valid; // @[fpu.scala 211:20]
  wire  fpiu_io_in_bits_ren2; // @[fpu.scala 211:20]
  wire  fpiu_io_in_bits_singleOut; // @[fpu.scala 211:20]
  wire  fpiu_io_in_bits_wflags; // @[fpu.scala 211:20]
  wire [2:0] fpiu_io_in_bits_rm; // @[fpu.scala 211:20]
  wire [1:0] fpiu_io_in_bits_typ; // @[fpu.scala 211:20]
  wire [64:0] fpiu_io_in_bits_in1; // @[fpu.scala 211:20]
  wire [64:0] fpiu_io_in_bits_in2; // @[fpu.scala 211:20]
  wire  fpiu_io_out_bits_lt; // @[fpu.scala 211:20]
  wire [63:0] fpiu_io_out_bits_toint; // @[fpu.scala 211:20]
  wire [4:0] fpiu_io_out_bits_exc; // @[fpu.scala 211:20]
  wire [6:0] FMADecoder_2_io_uopc; // @[fpu.scala 197:29]
  wire [1:0] FMADecoder_2_io_cmd; // @[fpu.scala 197:29]
  wire  fpmu_clock; // @[fpu.scala 220:20]
  wire  fpmu_reset; // @[fpu.scala 220:20]
  wire  fpmu_io_in_valid; // @[fpu.scala 220:20]
  wire  fpmu_io_in_bits_ren2; // @[fpu.scala 220:20]
  wire  fpmu_io_in_bits_singleOut; // @[fpu.scala 220:20]
  wire  fpmu_io_in_bits_wflags; // @[fpu.scala 220:20]
  wire [2:0] fpmu_io_in_bits_rm; // @[fpu.scala 220:20]
  wire [64:0] fpmu_io_in_bits_in1; // @[fpu.scala 220:20]
  wire [64:0] fpmu_io_in_bits_in2; // @[fpu.scala 220:20]
  wire  fpmu_io_out_valid; // @[fpu.scala 220:20]
  wire [64:0] fpmu_io_out_bits_data; // @[fpu.scala 220:20]
  wire [4:0] fpmu_io_out_bits_exc; // @[fpu.scala 220:20]
  wire  fpmu_io_lt; // @[fpu.scala 220:20]
  wire  _T_3 = io_req_valid & fp_decoder_io_sigs_fma; // @[fpu.scala 204:36]
  wire  _T_4 = ~fp_decoder_io_sigs_singleOut; // @[fpu.scala 204:54]
  wire  _T_7 = ~fp_decoder_io_sigs_singleIn; // @[fpu.scala 188:15]
  wire [32:0] _T_12 = {io_req_bits_rs1_data[31],io_req_bits_rs1_data[52],io_req_bits_rs1_data[30:0]}; // @[Cat.scala 29:58]
  wire [75:0] _T_16 = {_T_12[22:0], 53'h0}; // @[FPU.scala 228:28]
  wire [11:0] _GEN_85 = {{3'd0}, _T_12[31:23]}; // @[FPU.scala 231:31]
  wire [11:0] _T_20 = _GEN_85 + 12'h800; // @[FPU.scala 231:31]
  wire [11:0] _T_22 = _T_20 - 12'h100; // @[FPU.scala 231:48]
  wire [11:0] _T_27 = {_T_12[31:29],_T_22[8:0]}; // @[Cat.scala 29:58]
  wire [11:0] _T_29 = _T_12[31:29] == 3'h0 | _T_12[31:29] >= 3'h6 ? _T_27 : _T_22; // @[FPU.scala 232:10]
  wire [64:0] _T_31 = {_T_12[32],_T_29,_T_16[75:24]}; // @[Cat.scala 29:58]
  wire  _T_33 = &io_req_bits_rs1_data[64:60]; // @[FPU.scala 277:84]
  wire [32:0] _T_41 = {io_req_bits_rs2_data[31],io_req_bits_rs2_data[52],io_req_bits_rs2_data[30:0]}; // @[Cat.scala 29:58]
  wire [75:0] _T_45 = {_T_41[22:0], 53'h0}; // @[FPU.scala 228:28]
  wire [11:0] _GEN_86 = {{3'd0}, _T_41[31:23]}; // @[FPU.scala 231:31]
  wire [11:0] _T_49 = _GEN_86 + 12'h800; // @[FPU.scala 231:31]
  wire [11:0] _T_51 = _T_49 - 12'h100; // @[FPU.scala 231:48]
  wire [11:0] _T_56 = {_T_41[31:29],_T_51[8:0]}; // @[Cat.scala 29:58]
  wire [11:0] _T_58 = _T_41[31:29] == 3'h0 | _T_41[31:29] >= 3'h6 ? _T_56 : _T_51; // @[FPU.scala 232:10]
  wire [64:0] _T_60 = {_T_41[32],_T_58,_T_45[75:24]}; // @[Cat.scala 29:58]
  wire  _T_62 = &io_req_bits_rs2_data[64:60]; // @[FPU.scala 277:84]
  wire [32:0] _T_70 = {io_req_bits_rs3_data[31],io_req_bits_rs3_data[52],io_req_bits_rs3_data[30:0]}; // @[Cat.scala 29:58]
  wire  _T_91 = &io_req_bits_rs3_data[64:60]; // @[FPU.scala 277:84]
  wire [32:0] _T_127 = _T_33 ? 33'h0 : 33'he0400000; // @[FPU.scala 317:31]
  wire [32:0] _T_128 = _T_12 | _T_127; // @[FPU.scala 317:26]
  wire [32:0] _T_156 = _T_62 ? 33'h0 : 33'he0400000; // @[FPU.scala 317:31]
  wire [32:0] _T_157 = _T_41 | _T_156; // @[FPU.scala 317:26]
  wire [32:0] _T_185 = _T_91 ? 33'h0 : 33'he0400000; // @[FPU.scala 317:31]
  wire [32:0] _T_186 = _T_70 | _T_185; // @[FPU.scala 317:26]
  wire [64:0] _T_98_in2 = {{32'd0}, _T_157}; // @[fpu.scala 187:19 192:13]
  wire  _T_221 = _T_7 | _T_33; // @[package.scala 32:76]
  wire [64:0] _T_223 = _T_7 ? io_req_bits_rs1_data : _T_31; // @[package.scala 32:76]
  wire  _T_253 = _T_7 | _T_62; // @[package.scala 32:76]
  wire [64:0] _T_255 = _T_7 ? io_req_bits_rs2_data : _T_60; // @[package.scala 32:76]
  reg  _T_292; // @[fpu.scala 214:30]
  reg  fpiu_outPipe_valid; // @[Valid.scala 117:22]
  reg [63:0] fpiu_outPipe_bits_toint; // @[Reg.scala 15:16]
  reg [4:0] fpiu_outPipe_bits_exc; // @[Reg.scala 15:16]
  reg  fpiu_outPipe_valid_1; // @[Valid.scala 117:22]
  reg [63:0] fpiu_outPipe_bits_1_toint; // @[Reg.scala 15:16]
  reg [4:0] fpiu_outPipe_bits_1_exc; // @[Reg.scala 15:16]
  reg  fpiu_outPipe_valid_2; // @[Valid.scala 117:22]
  reg [63:0] fpiu_outPipe_bits_2_toint; // @[Reg.scala 15:16]
  reg [4:0] fpiu_outPipe_bits_2_exc; // @[Reg.scala 15:16]
  wire  _T_293 = io_req_valid & fp_decoder_io_sigs_fastpipe; // @[fpu.scala 221:36]
  reg  _T_296; // @[Valid.scala 117:22]
  reg  _T_297; // @[Reg.scala 15:16]
  reg  _T_298; // @[Valid.scala 117:22]
  reg  _T_299; // @[Reg.scala 15:16]
  reg  _T_300; // @[Valid.scala 117:22]
  reg  _T_301; // @[Reg.scala 15:16]
  reg  _T_303; // @[Reg.scala 15:16]
  wire  _T_305 = fpiu_outPipe_valid_2 | fpmu_io_out_valid; // @[fpu.scala 227:35]
  wire  _T_306 = _T_305 | sfma_io_out_valid; // @[fpu.scala 228:38]
  wire [64:0] _T_322 = dfma_io_out_bits_data; // @[package.scala 32:76]
  wire [64:0] _T_332 = {5'h1f,7'h7f,sfma_io_out_bits_data[31],20'hfffff,sfma_io_out_bits_data[32],sfma_io_out_bits_data[
    30:0]}; // @[Cat.scala 29:58]
  wire [64:0] _T_347 = {5'h1f,7'h7f,fpmu_io_out_bits_data[31],20'hfffff,fpmu_io_out_bits_data[32],fpmu_io_out_bits_data[
    30:0]}; // @[Cat.scala 29:58]
  wire [64:0] _T_352 = _T_303 ? fpmu_io_out_bits_data : _T_347; // @[package.scala 32:76]
  wire [64:0] fpiu_result_data = {{1'd0}, fpiu_outPipe_bits_2_toint}; // @[fpu.scala 216:26 217:20]
  wire [64:0] _T_353 = fpiu_outPipe_valid_2 ? fpiu_result_data : _T_352; // @[fpu.scala 234:8]
  wire [64:0] _T_354 = sfma_io_out_valid ? _T_332 : _T_353; // @[fpu.scala 233:8]
  wire [4:0] _T_355 = fpiu_outPipe_valid_2 ? fpiu_outPipe_bits_2_exc : fpmu_io_out_bits_exc; // @[fpu.scala 240:8]
  wire [4:0] _T_356 = sfma_io_out_valid ? sfma_io_out_bits_exc : _T_355; // @[fpu.scala 239:8]
  UOPCodeFPUDecoder_1 fp_decoder ( // @[fpu.scala 181:26]
    .io_uopc(fp_decoder_io_uopc),
    .io_sigs_ren2(fp_decoder_io_sigs_ren2),
    .io_sigs_ren3(fp_decoder_io_sigs_ren3),
    .io_sigs_swap23(fp_decoder_io_sigs_swap23),
    .io_sigs_singleIn(fp_decoder_io_sigs_singleIn),
    .io_sigs_singleOut(fp_decoder_io_sigs_singleOut),
    .io_sigs_toint(fp_decoder_io_sigs_toint),
    .io_sigs_fastpipe(fp_decoder_io_sigs_fastpipe),
    .io_sigs_fma(fp_decoder_io_sigs_fma),
    .io_sigs_wflags(fp_decoder_io_sigs_wflags)
  );
  FPUFMAPipe dfma ( // @[fpu.scala 203:20]
    .clock(dfma_clock),
    .reset(dfma_reset),
    .io_in_valid(dfma_io_in_valid),
    .io_in_bits_ren3(dfma_io_in_bits_ren3),
    .io_in_bits_swap23(dfma_io_in_bits_swap23),
    .io_in_bits_rm(dfma_io_in_bits_rm),
    .io_in_bits_fmaCmd(dfma_io_in_bits_fmaCmd),
    .io_in_bits_in1(dfma_io_in_bits_in1),
    .io_in_bits_in2(dfma_io_in_bits_in2),
    .io_in_bits_in3(dfma_io_in_bits_in3),
    .io_out_valid(dfma_io_out_valid),
    .io_out_bits_data(dfma_io_out_bits_data),
    .io_out_bits_exc(dfma_io_out_bits_exc)
  );
  FMADecoder_2 FMADecoder ( // @[fpu.scala 197:29]
    .io_uopc(FMADecoder_io_uopc),
    .io_cmd(FMADecoder_io_cmd)
  );
  FPUFMAPipe_1 sfma ( // @[fpu.scala 207:20]
    .clock(sfma_clock),
    .reset(sfma_reset),
    .io_in_valid(sfma_io_in_valid),
    .io_in_bits_ren3(sfma_io_in_bits_ren3),
    .io_in_bits_swap23(sfma_io_in_bits_swap23),
    .io_in_bits_rm(sfma_io_in_bits_rm),
    .io_in_bits_fmaCmd(sfma_io_in_bits_fmaCmd),
    .io_in_bits_in1(sfma_io_in_bits_in1),
    .io_in_bits_in2(sfma_io_in_bits_in2),
    .io_in_bits_in3(sfma_io_in_bits_in3),
    .io_out_valid(sfma_io_out_valid),
    .io_out_bits_data(sfma_io_out_bits_data),
    .io_out_bits_exc(sfma_io_out_bits_exc)
  );
  FMADecoder_2 FMADecoder_1 ( // @[fpu.scala 197:29]
    .io_uopc(FMADecoder_1_io_uopc),
    .io_cmd(FMADecoder_1_io_cmd)
  );
  FPToInt fpiu ( // @[fpu.scala 211:20]
    .clock(fpiu_clock),
    .io_in_valid(fpiu_io_in_valid),
    .io_in_bits_ren2(fpiu_io_in_bits_ren2),
    .io_in_bits_singleOut(fpiu_io_in_bits_singleOut),
    .io_in_bits_wflags(fpiu_io_in_bits_wflags),
    .io_in_bits_rm(fpiu_io_in_bits_rm),
    .io_in_bits_typ(fpiu_io_in_bits_typ),
    .io_in_bits_in1(fpiu_io_in_bits_in1),
    .io_in_bits_in2(fpiu_io_in_bits_in2),
    .io_out_bits_lt(fpiu_io_out_bits_lt),
    .io_out_bits_toint(fpiu_io_out_bits_toint),
    .io_out_bits_exc(fpiu_io_out_bits_exc)
  );
  FMADecoder_2 FMADecoder_2 ( // @[fpu.scala 197:29]
    .io_uopc(FMADecoder_2_io_uopc),
    .io_cmd(FMADecoder_2_io_cmd)
  );
  FPToFP fpmu ( // @[fpu.scala 220:20]
    .clock(fpmu_clock),
    .reset(fpmu_reset),
    .io_in_valid(fpmu_io_in_valid),
    .io_in_bits_ren2(fpmu_io_in_bits_ren2),
    .io_in_bits_singleOut(fpmu_io_in_bits_singleOut),
    .io_in_bits_wflags(fpmu_io_in_bits_wflags),
    .io_in_bits_rm(fpmu_io_in_bits_rm),
    .io_in_bits_in1(fpmu_io_in_bits_in1),
    .io_in_bits_in2(fpmu_io_in_bits_in2),
    .io_out_valid(fpmu_io_out_valid),
    .io_out_bits_data(fpmu_io_out_bits_data),
    .io_out_bits_exc(fpmu_io_out_bits_exc),
    .io_lt(fpmu_io_lt)
  );
  assign io_resp_valid = _T_306 | dfma_io_out_valid; // @[fpu.scala 229:38]
  assign io_resp_bits_uop_uopc = 7'h0;
  assign io_resp_bits_uop_inst = 32'h0;
  assign io_resp_bits_uop_debug_inst = 32'h0;
  assign io_resp_bits_uop_is_rvc = 1'h0;
  assign io_resp_bits_uop_debug_pc = 40'h0;
  assign io_resp_bits_uop_iq_type = 3'h0;
  assign io_resp_bits_uop_fu_code = 10'h0;
  assign io_resp_bits_uop_ctrl_br_type = 4'h0;
  assign io_resp_bits_uop_ctrl_op1_sel = 2'h0;
  assign io_resp_bits_uop_ctrl_op2_sel = 3'h0;
  assign io_resp_bits_uop_ctrl_imm_sel = 3'h0;
  assign io_resp_bits_uop_ctrl_op_fcn = 4'h0;
  assign io_resp_bits_uop_ctrl_fcn_dw = 1'h0;
  assign io_resp_bits_uop_ctrl_csr_cmd = 3'h0;
  assign io_resp_bits_uop_ctrl_is_load = 1'h0;
  assign io_resp_bits_uop_ctrl_is_sta = 1'h0;
  assign io_resp_bits_uop_ctrl_is_std = 1'h0;
  assign io_resp_bits_uop_iw_state = 2'h0;
  assign io_resp_bits_uop_iw_p1_poisoned = 1'h0;
  assign io_resp_bits_uop_iw_p2_poisoned = 1'h0;
  assign io_resp_bits_uop_is_br = 1'h0;
  assign io_resp_bits_uop_is_jalr = 1'h0;
  assign io_resp_bits_uop_is_jal = 1'h0;
  assign io_resp_bits_uop_is_sfb = 1'h0;
  assign io_resp_bits_uop_br_mask = 8'h0;
  assign io_resp_bits_uop_br_tag = 3'h0;
  assign io_resp_bits_uop_ftq_idx = 4'h0;
  assign io_resp_bits_uop_edge_inst = 1'h0;
  assign io_resp_bits_uop_pc_lob = 6'h0;
  assign io_resp_bits_uop_taken = 1'h0;
  assign io_resp_bits_uop_imm_packed = 20'h0;
  assign io_resp_bits_uop_csr_addr = 12'h0;
  assign io_resp_bits_uop_rob_idx = 5'h0;
  assign io_resp_bits_uop_ldq_idx = 3'h0;
  assign io_resp_bits_uop_stq_idx = 3'h0;
  assign io_resp_bits_uop_rxq_idx = 2'h0;
  assign io_resp_bits_uop_pdst = 6'h0;
  assign io_resp_bits_uop_prs1 = 6'h0;
  assign io_resp_bits_uop_prs2 = 6'h0;
  assign io_resp_bits_uop_prs3 = 6'h0;
  assign io_resp_bits_uop_ppred = 4'h0;
  assign io_resp_bits_uop_prs1_busy = 1'h0;
  assign io_resp_bits_uop_prs2_busy = 1'h0;
  assign io_resp_bits_uop_prs3_busy = 1'h0;
  assign io_resp_bits_uop_ppred_busy = 1'h0;
  assign io_resp_bits_uop_stale_pdst = 6'h0;
  assign io_resp_bits_uop_exception = 1'h0;
  assign io_resp_bits_uop_exc_cause = 64'h0;
  assign io_resp_bits_uop_bypassable = 1'h0;
  assign io_resp_bits_uop_mem_cmd = 5'h0;
  assign io_resp_bits_uop_mem_size = 2'h0;
  assign io_resp_bits_uop_mem_signed = 1'h0;
  assign io_resp_bits_uop_is_fence = 1'h0;
  assign io_resp_bits_uop_is_fencei = 1'h0;
  assign io_resp_bits_uop_is_amo = 1'h0;
  assign io_resp_bits_uop_uses_ldq = 1'h0;
  assign io_resp_bits_uop_uses_stq = 1'h0;
  assign io_resp_bits_uop_is_sys_pc2epc = 1'h0;
  assign io_resp_bits_uop_is_unique = 1'h0;
  assign io_resp_bits_uop_flush_on_commit = 1'h0;
  assign io_resp_bits_uop_ldst_is_rs1 = 1'h0;
  assign io_resp_bits_uop_ldst = 6'h0;
  assign io_resp_bits_uop_lrs1 = 6'h0;
  assign io_resp_bits_uop_lrs2 = 6'h0;
  assign io_resp_bits_uop_lrs3 = 6'h0;
  assign io_resp_bits_uop_ldst_val = 1'h0;
  assign io_resp_bits_uop_dst_rtype = 2'h0;
  assign io_resp_bits_uop_lrs1_rtype = 2'h0;
  assign io_resp_bits_uop_lrs2_rtype = 2'h0;
  assign io_resp_bits_uop_frs3_en = 1'h0;
  assign io_resp_bits_uop_fp_val = 1'h0;
  assign io_resp_bits_uop_fp_single = 1'h0;
  assign io_resp_bits_uop_xcpt_pf_if = 1'h0;
  assign io_resp_bits_uop_xcpt_ae_if = 1'h0;
  assign io_resp_bits_uop_xcpt_ma_if = 1'h0;
  assign io_resp_bits_uop_bp_debug_if = 1'h0;
  assign io_resp_bits_uop_bp_xcpt_if = 1'h0;
  assign io_resp_bits_uop_debug_fsrc = 2'h0;
  assign io_resp_bits_uop_debug_tsrc = 2'h0;
  assign io_resp_bits_data = dfma_io_out_valid ? _T_322 : _T_354; // @[fpu.scala 232:8]
  assign io_resp_bits_predicated = 1'h0;
  assign io_resp_bits_fflags_valid = io_resp_valid; // @[fpu.scala 244:34]
  assign io_resp_bits_fflags_bits_uop_uopc = 7'h0;
  assign io_resp_bits_fflags_bits_uop_inst = 32'h0;
  assign io_resp_bits_fflags_bits_uop_debug_inst = 32'h0;
  assign io_resp_bits_fflags_bits_uop_is_rvc = 1'h0;
  assign io_resp_bits_fflags_bits_uop_debug_pc = 40'h0;
  assign io_resp_bits_fflags_bits_uop_iq_type = 3'h0;
  assign io_resp_bits_fflags_bits_uop_fu_code = 10'h0;
  assign io_resp_bits_fflags_bits_uop_ctrl_br_type = 4'h0;
  assign io_resp_bits_fflags_bits_uop_ctrl_op1_sel = 2'h0;
  assign io_resp_bits_fflags_bits_uop_ctrl_op2_sel = 3'h0;
  assign io_resp_bits_fflags_bits_uop_ctrl_imm_sel = 3'h0;
  assign io_resp_bits_fflags_bits_uop_ctrl_op_fcn = 4'h0;
  assign io_resp_bits_fflags_bits_uop_ctrl_fcn_dw = 1'h0;
  assign io_resp_bits_fflags_bits_uop_ctrl_csr_cmd = 3'h0;
  assign io_resp_bits_fflags_bits_uop_ctrl_is_load = 1'h0;
  assign io_resp_bits_fflags_bits_uop_ctrl_is_sta = 1'h0;
  assign io_resp_bits_fflags_bits_uop_ctrl_is_std = 1'h0;
  assign io_resp_bits_fflags_bits_uop_iw_state = 2'h0;
  assign io_resp_bits_fflags_bits_uop_iw_p1_poisoned = 1'h0;
  assign io_resp_bits_fflags_bits_uop_iw_p2_poisoned = 1'h0;
  assign io_resp_bits_fflags_bits_uop_is_br = 1'h0;
  assign io_resp_bits_fflags_bits_uop_is_jalr = 1'h0;
  assign io_resp_bits_fflags_bits_uop_is_jal = 1'h0;
  assign io_resp_bits_fflags_bits_uop_is_sfb = 1'h0;
  assign io_resp_bits_fflags_bits_uop_br_mask = 8'h0;
  assign io_resp_bits_fflags_bits_uop_br_tag = 3'h0;
  assign io_resp_bits_fflags_bits_uop_ftq_idx = 4'h0;
  assign io_resp_bits_fflags_bits_uop_edge_inst = 1'h0;
  assign io_resp_bits_fflags_bits_uop_pc_lob = 6'h0;
  assign io_resp_bits_fflags_bits_uop_taken = 1'h0;
  assign io_resp_bits_fflags_bits_uop_imm_packed = 20'h0;
  assign io_resp_bits_fflags_bits_uop_csr_addr = 12'h0;
  assign io_resp_bits_fflags_bits_uop_rob_idx = 5'h0;
  assign io_resp_bits_fflags_bits_uop_ldq_idx = 3'h0;
  assign io_resp_bits_fflags_bits_uop_stq_idx = 3'h0;
  assign io_resp_bits_fflags_bits_uop_rxq_idx = 2'h0;
  assign io_resp_bits_fflags_bits_uop_pdst = 6'h0;
  assign io_resp_bits_fflags_bits_uop_prs1 = 6'h0;
  assign io_resp_bits_fflags_bits_uop_prs2 = 6'h0;
  assign io_resp_bits_fflags_bits_uop_prs3 = 6'h0;
  assign io_resp_bits_fflags_bits_uop_ppred = 4'h0;
  assign io_resp_bits_fflags_bits_uop_prs1_busy = 1'h0;
  assign io_resp_bits_fflags_bits_uop_prs2_busy = 1'h0;
  assign io_resp_bits_fflags_bits_uop_prs3_busy = 1'h0;
  assign io_resp_bits_fflags_bits_uop_ppred_busy = 1'h0;
  assign io_resp_bits_fflags_bits_uop_stale_pdst = 6'h0;
  assign io_resp_bits_fflags_bits_uop_exception = 1'h0;
  assign io_resp_bits_fflags_bits_uop_exc_cause = 64'h0;
  assign io_resp_bits_fflags_bits_uop_bypassable = 1'h0;
  assign io_resp_bits_fflags_bits_uop_mem_cmd = 5'h0;
  assign io_resp_bits_fflags_bits_uop_mem_size = 2'h0;
  assign io_resp_bits_fflags_bits_uop_mem_signed = 1'h0;
  assign io_resp_bits_fflags_bits_uop_is_fence = 1'h0;
  assign io_resp_bits_fflags_bits_uop_is_fencei = 1'h0;
  assign io_resp_bits_fflags_bits_uop_is_amo = 1'h0;
  assign io_resp_bits_fflags_bits_uop_uses_ldq = 1'h0;
  assign io_resp_bits_fflags_bits_uop_uses_stq = 1'h0;
  assign io_resp_bits_fflags_bits_uop_is_sys_pc2epc = 1'h0;
  assign io_resp_bits_fflags_bits_uop_is_unique = 1'h0;
  assign io_resp_bits_fflags_bits_uop_flush_on_commit = 1'h0;
  assign io_resp_bits_fflags_bits_uop_ldst_is_rs1 = 1'h0;
  assign io_resp_bits_fflags_bits_uop_ldst = 6'h0;
  assign io_resp_bits_fflags_bits_uop_lrs1 = 6'h0;
  assign io_resp_bits_fflags_bits_uop_lrs2 = 6'h0;
  assign io_resp_bits_fflags_bits_uop_lrs3 = 6'h0;
  assign io_resp_bits_fflags_bits_uop_ldst_val = 1'h0;
  assign io_resp_bits_fflags_bits_uop_dst_rtype = 2'h0;
  assign io_resp_bits_fflags_bits_uop_lrs1_rtype = 2'h0;
  assign io_resp_bits_fflags_bits_uop_lrs2_rtype = 2'h0;
  assign io_resp_bits_fflags_bits_uop_frs3_en = 1'h0;
  assign io_resp_bits_fflags_bits_uop_fp_val = 1'h0;
  assign io_resp_bits_fflags_bits_uop_fp_single = 1'h0;
  assign io_resp_bits_fflags_bits_uop_xcpt_pf_if = 1'h0;
  assign io_resp_bits_fflags_bits_uop_xcpt_ae_if = 1'h0;
  assign io_resp_bits_fflags_bits_uop_xcpt_ma_if = 1'h0;
  assign io_resp_bits_fflags_bits_uop_bp_debug_if = 1'h0;
  assign io_resp_bits_fflags_bits_uop_bp_xcpt_if = 1'h0;
  assign io_resp_bits_fflags_bits_uop_debug_fsrc = 2'h0;
  assign io_resp_bits_fflags_bits_uop_debug_tsrc = 2'h0;
  assign io_resp_bits_fflags_bits_flags = dfma_io_out_valid ? dfma_io_out_bits_exc : _T_356; // @[fpu.scala 238:8]
  assign fp_decoder_io_uopc = io_req_bits_uop_uopc; // @[fpu.scala 182:22]
  assign dfma_clock = clock;
  assign dfma_reset = reset;
  assign dfma_io_in_valid = io_req_valid & fp_decoder_io_sigs_fma & ~fp_decoder_io_sigs_singleOut; // @[fpu.scala 204:51]
  assign dfma_io_in_bits_ren3 = fp_decoder_io_sigs_ren3; // @[fpu.scala 187:19 189:9]
  assign dfma_io_in_bits_swap23 = fp_decoder_io_sigs_swap23; // @[fpu.scala 187:19 189:9]
  assign dfma_io_in_bits_rm = io_req_bits_uop_imm_packed[2:0] == 3'h7 ? io_req_bits_fcsr_rm : io_req_bits_uop_imm_packed
    [2:0]; // @[fpu.scala 184:18]
  assign dfma_io_in_bits_fmaCmd = FMADecoder_io_cmd; // @[fpu.scala 187:19 199:16]
  assign dfma_io_in_bits_in1 = io_req_bits_rs1_data; // @[FPU.scala 317:26]
  assign dfma_io_in_bits_in2 = io_req_bits_rs2_data; // @[FPU.scala 317:26]
  assign dfma_io_in_bits_in3 = fp_decoder_io_sigs_swap23 ? io_req_bits_rs2_data : io_req_bits_rs3_data; // @[fpu.scala 193:13 194:{27,37}]
  assign FMADecoder_io_uopc = io_req_bits_uop_uopc; // @[fpu.scala 198:25]
  assign sfma_clock = clock;
  assign sfma_reset = reset;
  assign sfma_io_in_valid = _T_3 & fp_decoder_io_sigs_singleOut; // @[fpu.scala 208:51]
  assign sfma_io_in_bits_ren3 = fp_decoder_io_sigs_ren3; // @[fpu.scala 187:19 189:9]
  assign sfma_io_in_bits_swap23 = fp_decoder_io_sigs_swap23; // @[fpu.scala 187:19 189:9]
  assign sfma_io_in_bits_rm = io_req_bits_uop_imm_packed[2:0] == 3'h7 ? io_req_bits_fcsr_rm : io_req_bits_uop_imm_packed
    [2:0]; // @[fpu.scala 184:18]
  assign sfma_io_in_bits_fmaCmd = FMADecoder_1_io_cmd; // @[fpu.scala 187:19 199:16]
  assign sfma_io_in_bits_in1 = {{32'd0}, _T_128}; // @[fpu.scala 187:19 191:13]
  assign sfma_io_in_bits_in2 = {{32'd0}, _T_157}; // @[fpu.scala 187:19 192:13]
  assign sfma_io_in_bits_in3 = fp_decoder_io_sigs_swap23 ? _T_98_in2 : {{32'd0}, _T_186}; // @[fpu.scala 193:13 194:{27,37}]
  assign FMADecoder_1_io_uopc = io_req_bits_uop_uopc; // @[fpu.scala 198:25]
  assign fpiu_clock = clock;
  assign fpiu_io_in_valid = io_req_valid & (fp_decoder_io_sigs_toint | fp_decoder_io_sigs_fastpipe &
    fp_decoder_io_sigs_wflags); // @[fpu.scala 212:36]
  assign fpiu_io_in_bits_ren2 = fp_decoder_io_sigs_ren2; // @[fpu.scala 187:19 189:9]
  assign fpiu_io_in_bits_singleOut = fp_decoder_io_sigs_singleOut; // @[fpu.scala 187:19 189:9]
  assign fpiu_io_in_bits_wflags = fp_decoder_io_sigs_wflags; // @[fpu.scala 187:19 189:9]
  assign fpiu_io_in_bits_rm = io_req_bits_uop_imm_packed[2:0] == 3'h7 ? io_req_bits_fcsr_rm : io_req_bits_uop_imm_packed
    [2:0]; // @[fpu.scala 184:18]
  assign fpiu_io_in_bits_typ = io_req_bits_uop_imm_packed[9:8]; // @[util.scala 295:59]
  assign fpiu_io_in_bits_in1 = _T_221 ? _T_223 : 65'he008000000000000; // @[FPU.scala 314:10]
  assign fpiu_io_in_bits_in2 = _T_253 ? _T_255 : 65'he008000000000000; // @[FPU.scala 314:10]
  assign FMADecoder_2_io_uopc = io_req_bits_uop_uopc; // @[fpu.scala 198:25]
  assign fpmu_clock = clock;
  assign fpmu_reset = reset;
  assign fpmu_io_in_valid = io_req_valid & fp_decoder_io_sigs_fastpipe; // @[fpu.scala 221:36]
  assign fpmu_io_in_bits_ren2 = fpiu_io_in_bits_ren2; // @[fpu.scala 222:19]
  assign fpmu_io_in_bits_singleOut = fpiu_io_in_bits_singleOut; // @[fpu.scala 222:19]
  assign fpmu_io_in_bits_wflags = fpiu_io_in_bits_wflags; // @[fpu.scala 222:19]
  assign fpmu_io_in_bits_rm = fpiu_io_in_bits_rm; // @[fpu.scala 222:19]
  assign fpmu_io_in_bits_in1 = fpiu_io_in_bits_in1; // @[fpu.scala 222:19]
  assign fpmu_io_in_bits_in2 = fpiu_io_in_bits_in2; // @[fpu.scala 222:19]
  assign fpmu_io_lt = fpiu_io_out_bits_lt; // @[fpu.scala 223:14]
  always @(posedge clock) begin
    _T_292 <= fpiu_io_in_valid & ~fp_decoder_io_sigs_fastpipe; // @[fpu.scala 214:48]
    if (reset) begin // @[Valid.scala 117:22]
      fpiu_outPipe_valid <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      fpiu_outPipe_valid <= _T_292; // @[Valid.scala 117:22]
    end
    if (_T_292) begin // @[Reg.scala 16:19]
      fpiu_outPipe_bits_toint <= fpiu_io_out_bits_toint; // @[Reg.scala 16:23]
    end
    if (_T_292) begin // @[Reg.scala 16:19]
      fpiu_outPipe_bits_exc <= fpiu_io_out_bits_exc; // @[Reg.scala 16:23]
    end
    if (reset) begin // @[Valid.scala 117:22]
      fpiu_outPipe_valid_1 <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      fpiu_outPipe_valid_1 <= fpiu_outPipe_valid; // @[Valid.scala 117:22]
    end
    if (fpiu_outPipe_valid) begin // @[Reg.scala 16:19]
      fpiu_outPipe_bits_1_toint <= fpiu_outPipe_bits_toint; // @[Reg.scala 16:23]
    end
    if (fpiu_outPipe_valid) begin // @[Reg.scala 16:19]
      fpiu_outPipe_bits_1_exc <= fpiu_outPipe_bits_exc; // @[Reg.scala 16:23]
    end
    if (reset) begin // @[Valid.scala 117:22]
      fpiu_outPipe_valid_2 <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      fpiu_outPipe_valid_2 <= fpiu_outPipe_valid_1; // @[Valid.scala 117:22]
    end
    if (fpiu_outPipe_valid_1) begin // @[Reg.scala 16:19]
      fpiu_outPipe_bits_2_toint <= fpiu_outPipe_bits_1_toint; // @[Reg.scala 16:23]
    end
    if (fpiu_outPipe_valid_1) begin // @[Reg.scala 16:19]
      fpiu_outPipe_bits_2_exc <= fpiu_outPipe_bits_1_exc; // @[Reg.scala 16:23]
    end
    if (reset) begin // @[Valid.scala 117:22]
      _T_296 <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      _T_296 <= _T_293; // @[Valid.scala 117:22]
    end
    if (_T_293) begin // @[Reg.scala 16:19]
      _T_297 <= _T_4; // @[Reg.scala 16:23]
    end
    if (reset) begin // @[Valid.scala 117:22]
      _T_298 <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      _T_298 <= _T_296; // @[Valid.scala 117:22]
    end
    if (_T_296) begin // @[Reg.scala 16:19]
      _T_299 <= _T_297; // @[Reg.scala 16:23]
    end
    if (reset) begin // @[Valid.scala 117:22]
      _T_300 <= 1'h0; // @[Valid.scala 117:22]
    end else begin
      _T_300 <= _T_298; // @[Valid.scala 117:22]
    end
    if (_T_298) begin // @[Reg.scala 16:19]
      _T_301 <= _T_299; // @[Reg.scala 16:23]
    end
    if (_T_300) begin // @[Reg.scala 16:19]
      _T_303 <= _T_301; // @[Reg.scala 16:23]
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
  _T_292 = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  fpiu_outPipe_valid = _RAND_1[0:0];
  _RAND_2 = {2{`RANDOM}};
  fpiu_outPipe_bits_toint = _RAND_2[63:0];
  _RAND_3 = {1{`RANDOM}};
  fpiu_outPipe_bits_exc = _RAND_3[4:0];
  _RAND_4 = {1{`RANDOM}};
  fpiu_outPipe_valid_1 = _RAND_4[0:0];
  _RAND_5 = {2{`RANDOM}};
  fpiu_outPipe_bits_1_toint = _RAND_5[63:0];
  _RAND_6 = {1{`RANDOM}};
  fpiu_outPipe_bits_1_exc = _RAND_6[4:0];
  _RAND_7 = {1{`RANDOM}};
  fpiu_outPipe_valid_2 = _RAND_7[0:0];
  _RAND_8 = {2{`RANDOM}};
  fpiu_outPipe_bits_2_toint = _RAND_8[63:0];
  _RAND_9 = {1{`RANDOM}};
  fpiu_outPipe_bits_2_exc = _RAND_9[4:0];
  _RAND_10 = {1{`RANDOM}};
  _T_296 = _RAND_10[0:0];
  _RAND_11 = {1{`RANDOM}};
  _T_297 = _RAND_11[0:0];
  _RAND_12 = {1{`RANDOM}};
  _T_298 = _RAND_12[0:0];
  _RAND_13 = {1{`RANDOM}};
  _T_299 = _RAND_13[0:0];
  _RAND_14 = {1{`RANDOM}};
  _T_300 = _RAND_14[0:0];
  _RAND_15 = {1{`RANDOM}};
  _T_301 = _RAND_15[0:0];
  _RAND_16 = {1{`RANDOM}};
  _T_303 = _RAND_16[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule

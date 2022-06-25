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

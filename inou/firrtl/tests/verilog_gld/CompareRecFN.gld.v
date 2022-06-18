module CompareRecFN(
  input  [64:0] io_a,
  input  [64:0] io_b,
  input         io_signaling,
  output        io_lt,
  output        io_eq,
  output        io_gt,
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
  assign io_gt = ordered & ~ordered_lt & ~ordered_eq; // @[CompareRecFN.scala 80:38]
  assign io_exceptionFlags = {invalid,4'h0}; // @[Cat.scala 29:58]
endmodule

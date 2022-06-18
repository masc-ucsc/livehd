module UOPCodeFDivDecoder(
  input        clock,
  input        reset,
  input  [6:0] io_uopc,
  output       io_sigs_ldst,
  output       io_sigs_wen,
  output       io_sigs_ren1,
  output       io_sigs_ren2,
  output       io_sigs_ren3,
  output       io_sigs_swap12,
  output       io_sigs_swap23,
  output       io_sigs_singleIn,
  output       io_sigs_singleOut,
  output       io_sigs_fromint,
  output       io_sigs_toint,
  output       io_sigs_fastpipe,
  output       io_sigs_fma,
  output       io_sigs_div,
  output       io_sigs_sqrt,
  output       io_sigs_wflags
);
  wire [6:0] _T_2 = io_uopc & 7'ha; // @[Decode.scala 14:65]
  wire  _T_3 = _T_2 == 7'h0; // @[Decode.scala 14:121]
  wire [6:0] _T_4 = io_uopc & 7'h9; // @[Decode.scala 14:65]
  wire  _T_5 = _T_4 == 7'h0; // @[Decode.scala 14:121]
  wire [6:0] _T_7 = io_uopc & 7'h1; // @[Decode.scala 14:65]
  wire [6:0] _T_10 = io_uopc & 7'h4; // @[Decode.scala 14:65]
  wire  _T_11 = _T_10 == 7'h0; // @[Decode.scala 14:121]
  wire [6:0] _T_12 = io_uopc & 7'h3; // @[Decode.scala 14:65]
  wire  _T_13 = _T_12 == 7'h3; // @[Decode.scala 14:121]
  assign io_sigs_ldst = 1'h0; // @[fdiv.scala 61:40]
  assign io_sigs_wen = 1'h0; // @[fdiv.scala 61:40]
  assign io_sigs_ren1 = 1'h1; // @[Decode.scala 15:30]
  assign io_sigs_ren2 = _T_3 | _T_5; // @[Decode.scala 15:30]
  assign io_sigs_ren3 = 1'h0; // @[fdiv.scala 61:40]
  assign io_sigs_swap12 = 1'h0; // @[fdiv.scala 61:40]
  assign io_sigs_swap23 = 1'h0; // @[fdiv.scala 61:40]
  assign io_sigs_singleIn = _T_7 == 7'h1; // @[Decode.scala 14:121]
  assign io_sigs_singleOut = _T_7 == 7'h1; // @[Decode.scala 14:121]
  assign io_sigs_fromint = 1'h0; // @[fdiv.scala 61:40]
  assign io_sigs_toint = 1'h0; // @[fdiv.scala 61:40]
  assign io_sigs_fastpipe = 1'h0; // @[fdiv.scala 61:40]
  assign io_sigs_fma = 1'h0; // @[fdiv.scala 61:40]
  assign io_sigs_div = _T_3 | _T_5; // @[Decode.scala 15:30]
  assign io_sigs_sqrt = _T_11 | _T_13; // @[Decode.scala 15:30]
  assign io_sigs_wflags = 1'h1; // @[Decode.scala 15:30]
endmodule

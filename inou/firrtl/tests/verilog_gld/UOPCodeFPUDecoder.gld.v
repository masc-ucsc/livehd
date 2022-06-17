module UOPCodeFPUDecoder(
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
  wire [6:0] _T = io_uopc & 7'h76; // @[Decode.scala 14:65]
  wire  _T_1 = _T == 7'h46; // @[Decode.scala 14:121]
  wire [6:0] _T_2 = io_uopc & 7'h6c; // @[Decode.scala 14:65]
  wire  _T_3 = _T_2 == 7'h48; // @[Decode.scala 14:121]
  wire [6:0] _T_4 = io_uopc & 7'h74; // @[Decode.scala 14:65]
  wire  _T_5 = _T_4 == 7'h50; // @[Decode.scala 14:121]
  wire [6:0] _T_6 = io_uopc & 7'h72; // @[Decode.scala 14:65]
  wire  _T_7 = _T_6 == 7'h50; // @[Decode.scala 14:121]
  wire [6:0] _T_8 = io_uopc & 7'h7c; // @[Decode.scala 14:65]
  wire  _T_9 = _T_8 == 7'h60; // @[Decode.scala 14:121]
  wire [6:0] _T_10 = io_uopc & 7'h7b; // @[Decode.scala 14:65]
  wire  _T_11 = _T_10 == 7'h60; // @[Decode.scala 14:121]
  wire [6:0] _T_12 = io_uopc & 7'h71; // @[Decode.scala 14:65]
  wire  _T_13 = _T_12 == 7'h51; // @[Decode.scala 14:121]
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
  assign io_sigs_ldst = 1'h0; // @[fpu.scala 117:40]
  assign io_sigs_wen = 1'h0; // @[fpu.scala 117:40]
  assign io_sigs_ren1 = _T_1 | _T_3 | _T_5 | _T_7 | _T_9 | _T_11 | _T_13 | _T_15; // @[Decode.scala 15:30]
  assign io_sigs_ren2 = _T_24 | _T_7 | _T_26 | _T_28 | _T_9 | _T_11; // @[Decode.scala 15:30]
  assign io_sigs_ren3 = _T_35 | _T_37 | _T_9 | _T_11; // @[Decode.scala 15:30]
  assign io_sigs_swap12 = 1'h0; // @[fpu.scala 117:40]
  assign io_sigs_swap23 = _T_41 | _T_43 | _T_45; // @[Decode.scala 15:30]
  assign io_sigs_singleIn = _T_48 | _T_50 | _T_52 | _T_53 | _T_55 | _T_57 | _T_59 | _T_61 | _T_63; // @[Decode.scala 15:30]
  assign io_sigs_singleOut = _T_73 | _T_75 | _T_50 | _T_77 | _T_52 | _T_53 | _T_59 | _T_61 | _T_57; // @[Decode.scala 15:30]
  assign io_sigs_fromint = _T == 7'h44; // @[Decode.scala 14:121]
  assign io_sigs_toint = _T_1 | _T_89; // @[Decode.scala 15:30]
  assign io_sigs_fastpipe = _T_92 | _T_94; // @[Decode.scala 15:30]
  assign io_sigs_fma = _T_52 | _T_28 | _T_9 | _T_11; // @[Decode.scala 15:30]
  assign io_sigs_div = 1'h0; // @[fpu.scala 117:40]
  assign io_sigs_sqrt = 1'h0; // @[fpu.scala 117:40]
  assign io_sigs_wflags = _T_15 | _T_100 | _T_7 | _T_26 | _T_9 | _T_11; // @[Decode.scala 15:30]
endmodule

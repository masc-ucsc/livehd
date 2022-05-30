module Sub(
  input  [3:0] io_a,
  output [3:0] io_b
);
  assign io_b = io_a + 4'h1; // @[SubModule.scala 29:16]
endmodule
module Top(
  input        clock,
  input        reset,
  input  [3:0] io_inp,
  output [3:0] io_out
);
  wire [3:0] sub_io_a; // @[SubModule.scala 18:30]
  wire [3:0] sub_io_b; // @[SubModule.scala 18:30]
  Sub sub ( // @[SubModule.scala 18:30]
    .io_a(sub_io_a),
    .io_b(sub_io_b)
  );
  assign io_out = sub_io_b;
  assign sub_io_a = io_inp;
endmodule

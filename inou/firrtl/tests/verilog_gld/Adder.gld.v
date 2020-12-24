module FullAdder(
  input   io_a,
  input   io_b,
  input   io_cin,
  output  io_sum,
  output  io_cout
);
  wire  a_xor_b = io_a ^ io_b; // @[FullAdder.scala 16:22]
  wire  a_and_b = io_a & io_b; // @[FullAdder.scala 19:22]
  wire  b_and_cin = io_b & io_cin; // @[FullAdder.scala 20:24]
  wire  a_and_cin = io_a & io_cin; // @[FullAdder.scala 21:24]
  assign io_sum = a_xor_b ^ io_cin; // @[FullAdder.scala 17:21]
  assign io_cout = a_and_b | b_and_cin | a_and_cin; // @[FullAdder.scala 22:34]
endmodule
module Adder(
  input        clock,
  input        reset,
  input  [7:0] io_A,
  input  [7:0] io_B,
  input        io_Cin,
  output [7:0] io_Sum,
  output       io_Cout
);
  wire  FullAdder_io_a; // @[Adder.scala 19:35]
  wire  FullAdder_io_b; // @[Adder.scala 19:35]
  wire  FullAdder_io_cin; // @[Adder.scala 19:35]
  wire  FullAdder_io_sum; // @[Adder.scala 19:35]
  wire  FullAdder_io_cout; // @[Adder.scala 19:35]
  wire  FullAdder_1_io_a; // @[Adder.scala 19:35]
  wire  FullAdder_1_io_b; // @[Adder.scala 19:35]
  wire  FullAdder_1_io_cin; // @[Adder.scala 19:35]
  wire  FullAdder_1_io_sum; // @[Adder.scala 19:35]
  wire  FullAdder_1_io_cout; // @[Adder.scala 19:35]
  wire  FullAdder_2_io_a; // @[Adder.scala 19:35]
  wire  FullAdder_2_io_b; // @[Adder.scala 19:35]
  wire  FullAdder_2_io_cin; // @[Adder.scala 19:35]
  wire  FullAdder_2_io_sum; // @[Adder.scala 19:35]
  wire  FullAdder_2_io_cout; // @[Adder.scala 19:35]
  wire  FullAdder_3_io_a; // @[Adder.scala 19:35]
  wire  FullAdder_3_io_b; // @[Adder.scala 19:35]
  wire  FullAdder_3_io_cin; // @[Adder.scala 19:35]
  wire  FullAdder_3_io_sum; // @[Adder.scala 19:35]
  wire  FullAdder_3_io_cout; // @[Adder.scala 19:35]
  wire  FullAdder_4_io_a; // @[Adder.scala 19:35]
  wire  FullAdder_4_io_b; // @[Adder.scala 19:35]
  wire  FullAdder_4_io_cin; // @[Adder.scala 19:35]
  wire  FullAdder_4_io_sum; // @[Adder.scala 19:35]
  wire  FullAdder_4_io_cout; // @[Adder.scala 19:35]
  wire  FullAdder_5_io_a; // @[Adder.scala 19:35]
  wire  FullAdder_5_io_b; // @[Adder.scala 19:35]
  wire  FullAdder_5_io_cin; // @[Adder.scala 19:35]
  wire  FullAdder_5_io_sum; // @[Adder.scala 19:35]
  wire  FullAdder_5_io_cout; // @[Adder.scala 19:35]
  wire  FullAdder_6_io_a; // @[Adder.scala 19:35]
  wire  FullAdder_6_io_b; // @[Adder.scala 19:35]
  wire  FullAdder_6_io_cin; // @[Adder.scala 19:35]
  wire  FullAdder_6_io_sum; // @[Adder.scala 19:35]
  wire  FullAdder_6_io_cout; // @[Adder.scala 19:35]
  wire  FullAdder_7_io_a; // @[Adder.scala 19:35]
  wire  FullAdder_7_io_b; // @[Adder.scala 19:35]
  wire  FullAdder_7_io_cin; // @[Adder.scala 19:35]
  wire  FullAdder_7_io_sum; // @[Adder.scala 19:35]
  wire  FullAdder_7_io_cout; // @[Adder.scala 19:35]
  wire  sum_0 = FullAdder_io_sum; // @[Adder.scala 32:26]
  wire  sum_1 = FullAdder_1_io_sum; // @[Adder.scala 32:26]
  wire  sum_2 = FullAdder_2_io_sum; // @[Adder.scala 32:26]
  wire  sum_3 = FullAdder_3_io_sum; // @[Adder.scala 32:26]
  wire  sum_4 = FullAdder_4_io_sum; // @[Adder.scala 32:26]
  wire  sum_5 = FullAdder_5_io_sum; // @[Adder.scala 32:26]
  wire  sum_6 = FullAdder_6_io_sum; // @[Adder.scala 32:26]
  wire  sum_7 = FullAdder_7_io_sum; // @[Adder.scala 32:26]
  wire [3:0] _T_26 = {sum_3,sum_2,sum_1,sum_0}; // @[Adder.scala 34:17]
  wire [3:0] _T_29 = {sum_7,sum_6,sum_5,sum_4}; // @[Adder.scala 34:17]
  FullAdder FullAdder ( // @[Adder.scala 19:35]
    .io_a(FullAdder_io_a),
    .io_b(FullAdder_io_b),
    .io_cin(FullAdder_io_cin),
    .io_sum(FullAdder_io_sum),
    .io_cout(FullAdder_io_cout)
  );
  FullAdder FullAdder_1 ( // @[Adder.scala 19:35]
    .io_a(FullAdder_1_io_a),
    .io_b(FullAdder_1_io_b),
    .io_cin(FullAdder_1_io_cin),
    .io_sum(FullAdder_1_io_sum),
    .io_cout(FullAdder_1_io_cout)
  );
  FullAdder FullAdder_2 ( // @[Adder.scala 19:35]
    .io_a(FullAdder_2_io_a),
    .io_b(FullAdder_2_io_b),
    .io_cin(FullAdder_2_io_cin),
    .io_sum(FullAdder_2_io_sum),
    .io_cout(FullAdder_2_io_cout)
  );
  FullAdder FullAdder_3 ( // @[Adder.scala 19:35]
    .io_a(FullAdder_3_io_a),
    .io_b(FullAdder_3_io_b),
    .io_cin(FullAdder_3_io_cin),
    .io_sum(FullAdder_3_io_sum),
    .io_cout(FullAdder_3_io_cout)
  );
  FullAdder FullAdder_4 ( // @[Adder.scala 19:35]
    .io_a(FullAdder_4_io_a),
    .io_b(FullAdder_4_io_b),
    .io_cin(FullAdder_4_io_cin),
    .io_sum(FullAdder_4_io_sum),
    .io_cout(FullAdder_4_io_cout)
  );
  FullAdder FullAdder_5 ( // @[Adder.scala 19:35]
    .io_a(FullAdder_5_io_a),
    .io_b(FullAdder_5_io_b),
    .io_cin(FullAdder_5_io_cin),
    .io_sum(FullAdder_5_io_sum),
    .io_cout(FullAdder_5_io_cout)
  );
  FullAdder FullAdder_6 ( // @[Adder.scala 19:35]
    .io_a(FullAdder_6_io_a),
    .io_b(FullAdder_6_io_b),
    .io_cin(FullAdder_6_io_cin),
    .io_sum(FullAdder_6_io_sum),
    .io_cout(FullAdder_6_io_cout)
  );
  FullAdder FullAdder_7 ( // @[Adder.scala 19:35]
    .io_a(FullAdder_7_io_a),
    .io_b(FullAdder_7_io_b),
    .io_cin(FullAdder_7_io_cin),
    .io_sum(FullAdder_7_io_sum),
    .io_cout(FullAdder_7_io_cout)
  );
  assign io_Sum = {_T_29,_T_26}; // @[Adder.scala 34:17]
  assign io_Cout = FullAdder_7_io_cout; // @[Adder.scala 20:19 Adder.scala 31:16]
  assign FullAdder_io_a = io_A[0]; // @[Adder.scala 28:21]
  assign FullAdder_io_b = io_B[0]; // @[Adder.scala 29:21]
  assign FullAdder_io_cin = io_Cin; // @[Adder.scala 20:19 Adder.scala 24:12]
  assign FullAdder_1_io_a = io_A[1]; // @[Adder.scala 28:21]
  assign FullAdder_1_io_b = io_B[1]; // @[Adder.scala 29:21]
  assign FullAdder_1_io_cin = FullAdder_io_cout; // @[Adder.scala 20:19 Adder.scala 31:16]
  assign FullAdder_2_io_a = io_A[2]; // @[Adder.scala 28:21]
  assign FullAdder_2_io_b = io_B[2]; // @[Adder.scala 29:21]
  assign FullAdder_2_io_cin = FullAdder_1_io_cout; // @[Adder.scala 20:19 Adder.scala 31:16]
  assign FullAdder_3_io_a = io_A[3]; // @[Adder.scala 28:21]
  assign FullAdder_3_io_b = io_B[3]; // @[Adder.scala 29:21]
  assign FullAdder_3_io_cin = FullAdder_2_io_cout; // @[Adder.scala 20:19 Adder.scala 31:16]
  assign FullAdder_4_io_a = io_A[4]; // @[Adder.scala 28:21]
  assign FullAdder_4_io_b = io_B[4]; // @[Adder.scala 29:21]
  assign FullAdder_4_io_cin = FullAdder_3_io_cout; // @[Adder.scala 20:19 Adder.scala 31:16]
  assign FullAdder_5_io_a = io_A[5]; // @[Adder.scala 28:21]
  assign FullAdder_5_io_b = io_B[5]; // @[Adder.scala 29:21]
  assign FullAdder_5_io_cin = FullAdder_4_io_cout; // @[Adder.scala 20:19 Adder.scala 31:16]
  assign FullAdder_6_io_a = io_A[6]; // @[Adder.scala 28:21]
  assign FullAdder_6_io_b = io_B[6]; // @[Adder.scala 29:21]
  assign FullAdder_6_io_cin = FullAdder_5_io_cout; // @[Adder.scala 20:19 Adder.scala 31:16]
  assign FullAdder_7_io_a = io_A[7]; // @[Adder.scala 28:21]
  assign FullAdder_7_io_b = io_B[7]; // @[Adder.scala 29:21]
  assign FullAdder_7_io_cin = FullAdder_6_io_cout; // @[Adder.scala 20:19 Adder.scala 31:16]
endmodule

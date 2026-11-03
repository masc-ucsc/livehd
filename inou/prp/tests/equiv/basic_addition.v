// Canonical paired regression; original parser forms are retained below.
module basic_addition(
  input [7:0] a, b, output [7:0] sum, difference, canceled, negated, output signed [7:0]
  signed_sum, signed_difference, signed_canceled, signed_negated, output [4:0] mixed_five, output
  signed [5:0] mixed_six, output [2:0] mixed_three, output signed [5:0] narrow_signed, output
  [9:0] wide_unsigned, output signed [9:0] wide_signed, output [8:0] sign_extended_bits, output
  signed [7:0] dead_term
);
  add arithmetic(a, b, sum, difference, canceled, negated, signed_sum, signed_difference, signed_canceled, signed_negated);
  add1 mixed_sign(a[3:0], b[3:0], mixed_five, mixed_six, mixed_three);
  add2 signed_growth(a[6:0], b, narrow_signed, wide_unsigned, wide_signed, sign_extended_bits);
  simple_add unused_expression(a, b, dead_term);
endmodule

// Former fixture: inou/yosys/tests/add.v
module add(input [7:0] a, input [7:0] b,
  output [7:0] c,
  output [7:0] d,
  output [7:0] e,
  output [7:0] ei,
  output signed [7:0] f,
  output signed [7:0] g,
  output signed [7:0] h,
  output signed [7:0] hi
);

  assign c = a + b;
  assign d = a - b;
  assign e = a + b - a;
  assign ei = - a - b;

  wire signed [7:0] as = a;
  wire signed [7:0] bs = b;

  assign f = as + bs;
  assign g = as - bs;
  assign h = as + bs - as;
  assign hi = -as - bs;

endmodule


// Former fixture: inou/yosys/tests/add1.v
module add1(input signed [3:0] a, input [3:0] b, output [4:0] c1, output signed [6:1] c2, output [2:0] c3);
  assign c1 = a + b;
  assign c2 = a + b;
  assign c3 = a + b;
endmodule

// Former fixture: inou/yosys/tests/add2.v
module add2(input [6:0] a, input [7:0] b,
  output signed [5:0] hs,
  output [9:0] hu,
  output signed [9:0] js,
  output [8:0] ju
);

  wire signed [6:0] as = a;
  wire signed [7:0] bs = b;

  assign ju = as + bs;
  assign js = as + bs;

  assign hs = as + bs - as;
  assign hu = as + bs - as;

endmodule


// Former fixture: inou/yosys/tests/simple_add.v
module simple_add(input [7:0] a, input [7:0] b,
  output signed [7:0] h
);

  wire signed [7:0] as = a;
  wire signed [7:0] bs = b;

  wire [7:0] f = as + bs;
  assign h = as + bs - as;

endmodule

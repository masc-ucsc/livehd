// Canonical paired regression; original parser forms are retained below.
module basic_shifts(
  input [7:0] a, output [7:0] right_one, left_one, unsigned_arithmetic, signed_arithmetic,
  right_seven, left_six, right_five, signed_four
);
  shift ordinary(a, right_one, left_one, unsigned_arithmetic, signed_arithmetic);
  nshift negative_counts(a, right_seven, left_six, right_five, signed_four);
endmodule

// Former fixture: inou/yosys/tests/shift.v

module shift (input [7:0] a, output [7:0] b, output [7:0] c, output [7:0] d, output [7:0] e);

assign b = a >> 1;
assign c = a << 1;
assign d = a >>> 1;
assign e = $signed(a) >>> 1;

endmodule

// Former fixture: inou/yosys/tests/nshift.v

// Shift by a negative *constant* count. Verilog shift amounts are unsigned, so
// a narrow negative literal masks to its unsigned bit pattern (e.g. 3'sb111 is
// -1, used as the count it is 7). The slang reader must produce that defined
// result, not nil/error, and match yosys for LEC.

module nshift (input [7:0] a, output [7:0] b, output [7:0] c, output [7:0] d, output [7:0] e);

assign b = a >> 3'sb111;            // count masks to 7
assign c = a << 3'sb110;            // count masks to 6
assign d = a >>> 3'sb101;           // logical (a unsigned), count 5
assign e = $signed(a) >>> 3'sb100;  // arithmetic, count 4

endmodule

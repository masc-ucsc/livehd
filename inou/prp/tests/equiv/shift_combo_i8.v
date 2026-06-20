// Golden for shift_combo_i8 (2i-issues E). 8-bit assignment context: (a<<b)
// keeps the low 8 bits (wrap) and (a>>>b) is an arithmetic right shift.
module top(input signed [7:0] a, input [1:0] b, output signed [7:0] r);
  assign r = (a << b) ^ (a >>> b);
endmodule

// Golden for shift_nested_sra (2i-issues E residual). 8-bit assignment context:
// both arithmetic right shifts are sign-preserving; the left shift wraps.
module top(input signed [7:0] a, input [1:0] b, output signed [7:0] r);
  assign r = ((a >>> b) >>> b) ^ (a << b);
endmodule

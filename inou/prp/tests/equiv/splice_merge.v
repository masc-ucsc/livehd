// Golden for 2f-splice RTL coverage (verified with lgcheck).
// out = (in1 + in2 + 7) * 2, computed in 16-bit unsigned arithmetic.
module \splice_merge.spl (
  input  [7:0]  in1,
  input  [7:0]  in2,
  output [15:0] out
);
  assign out = ({8'd0, in1} + {8'd0, in2} + 16'd7) << 1;
endmodule

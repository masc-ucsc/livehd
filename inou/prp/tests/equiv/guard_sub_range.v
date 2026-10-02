// Golden for guard_sub_range.prp: guarded absolute differences.
module guard_sub_range(
    input  [3:0] a,
    input  [3:0] b,
    input  [3:0] c,
    output [3:0] r,
    output [3:0] s,
    output [3:0] t
);
  assign r = (a > b) ? (a - b) : 4'd0;
  assign s = (b < a) ? (a - b) : (b - a);
  assign t = (a >= c) ? (a - c) : (c > b) ? (c - b) : (b - c);
endmodule

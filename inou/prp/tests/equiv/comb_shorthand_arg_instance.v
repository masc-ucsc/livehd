module top(
  input  [1:0] a,
  input  [1:0] b,
  input  [1:0] x,
  output [3:0] mixed,
  output [3:0] swapped
);
  assign mixed   = {x, b};
  assign swapped = {a, b};
endmodule

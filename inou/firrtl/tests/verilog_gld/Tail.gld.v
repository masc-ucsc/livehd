module Tail(
  input  [2:0] io_a,
  output       io_b
);
  assign io_b = io_a[0];
endmodule

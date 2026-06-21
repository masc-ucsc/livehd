module \ci_vars.foo (
   input      [7:0] a
  ,input      [7:0] b
  ,output     [7:0] s
);
  assign s = a + b;
endmodule

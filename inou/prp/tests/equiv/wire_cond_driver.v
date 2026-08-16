module \wire_cond_driver.wcond (input [7:0] a, input c, output [7:0] o);
  // The conditional write is the wire's ONE driver, so it holds on every path:
  // `c` does not reach `o` at all.
  assign o = a + 1;
endmodule

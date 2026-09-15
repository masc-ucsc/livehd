// A reset-token-named clock must remain the clock when the other event is
// a nonconstant asynchronous load. Demoting clk_por onto init changes domains.
module async_reset_clock_demote (
  input        clk_por,
  input        init,
  input        d,
  output reg   q
);
  always @(posedge clk_por or posedge init)
    if (init)
      q <= d;      // asynchronous load
    else
      q <= ~d;
endmodule

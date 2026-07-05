// slang async-reset-as-sync demotion — negative gate (2): a demoted reset must
// be READABLE in this module. `u_rst` is a compilation-unit-scoped net (neither
// a module input nor a module-level signal), so the synchronous body could not
// read a real driver — its guard would fold to a constant and silently kill the
// reset arm. The readability gate refuses the demotion and the reader
// hard-errors instead.
logic u_rst;
module nocheck_async_reset_unreadable (
  input        clk,
  input        d,
  output reg   q
);
  always @(posedge clk or posedge u_rst)
    if (u_rst)
      q <= 1'b0;
    else
      q <= d;
endmodule

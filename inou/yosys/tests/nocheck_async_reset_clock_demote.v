// slang async-reset-as-sync demotion — negative gate (3): a reset-token-named
// CLOCK must NOT be demoted. `clk_por` tokenizes reset-like (the `por` token)
// but is the real clock; `init` is the async reset (a non-const load, so the
// rung cannot extract). Demoting `clk_por` would move the flop onto the `init`
// domain — a wrong-clock-domain miscompile. The clock is not read as data in
// the body, so the read-in-body gate refuses the demotion and the reader
// hard-errors instead.
module nocheck_async_reset_clock_demote (
  input        clk_por,
  input        init,
  input        d,
  output reg   q
);
  always @(posedge clk_por or posedge init)
    if (init)
      q <= d;      // non-const async load: no rung extraction
    else
      q <= ~d;
endmodule

// slang async-reset-as-sync demotion — negative gate (1): a demote must NOT
// fire after a rung has already peeled. Rung 1 (arst, resets only q) extracts
// cleanly; rung 2's compound guard `soft_rst || cfg` is un-extractable. If the
// reader demoted soft_rst here, the residual body would lower synchronously
// with NO arst dependence, so `q` would update while arst is held high at a
// clock edge (a silent miscompile). The reader must instead hard-error.
module nocheck_async_reset_peel (
  input        clk,
  input        arst,
  input        soft_rst,
  input        cfg,
  input        d,
  output reg   r,
  output reg   q
);
  always @(posedge clk or posedge arst or posedge soft_rst) begin
    if (arst)
      q <= 1'b0;
    else if (soft_rst || cfg)
      r <= 1'b1;
    else begin
      r <= d;
      q <= d;
    end
  end
endmodule

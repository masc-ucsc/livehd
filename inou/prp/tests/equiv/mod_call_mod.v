module \mod_call_mod.inner (
  input            clock,
  input      [7:0] a,
  output reg [7:0] x
);

  reg [7:0] d0;
  always @(posedge clock) begin
    d0 <= a;
    x  <= d0;
  end

endmodule

module \mod_call_mod.top (
  input            clock,
  input      [7:0] a,
  output     [7:0] out
);

  \mod_call_mod.inner u_inner (.clock(clock), .a(a), .x(out));

endmodule

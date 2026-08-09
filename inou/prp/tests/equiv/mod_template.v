// Golden for mod_template.prp — the specialized `inner__u8` is a 2-cycle
// register delay; `top` feeds `a` through it and lands `out` at cycle 2.
module \mod_template.inner__u8 (
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

module \mod_template.top (
  input            clock,
  input      [7:0] a,
  output     [7:0] out
);

  \mod_template.inner__u8 u_inner (.clock(clock), .a(a), .x(out));

endmodule

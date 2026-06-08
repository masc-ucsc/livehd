// Golden for mod_template.prp — the specialized `inner__u8` is a 2-cycle
// register delay; `top` feeds `a` through it and lands `out` at cycle 2.
module \mod_template.top (
  input            clock,
  input      [7:0] a,
  output reg [7:0] out
);

  reg [7:0] d0;

  always @(posedge clock) begin
    d0  <= a;
    out <= d0;
  end

endmodule

// Regression fixture for submodule output-port inlining in the v->prp writer.
// `top` instantiates a multi-output stateful submodule and reads BOTH of its
// outputs AFTER the instance (backward-only).  The writer must drop the
// per-output extraction temps + their wires and read `u_sub.<port>` directly.
module submod (
  input            clock,
  input      [7:0] a,
  input      [7:0] b,
  output reg [7:0] s,
  output reg [7:0] d
);
  always @(posedge clock) begin
    s <= a + b;
    d <= a - b;
  end
endmodule

module top (
  input            clock,
  input      [7:0] x,
  input      [7:0] y,
  output     [7:0] o
);
  wire [7:0] ss, dd;
  submod u_sub (.clock(clock), .a(x), .b(y), .s(ss), .d(dd));
  assign o = ss ^ dd;   // both outputs used after the instance -> inline
endmodule

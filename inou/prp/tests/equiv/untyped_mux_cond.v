// Golden for untyped_mux_cond.prp — an untyped variable holding a runtime value
// used as a mux condition. Every mux below is a REAL runtime mux; before the
// upass.constprop empty-tuple guard fix each condition folded to a constant and
// the Pyrope side collapsed to the `x` arm.
module \untyped_mux_cond.top (
  input        ack,
  input        req,
  input  [3:0] x,
  input  [3:0] y,
  output [3:0] sel,
  output [3:0] sel2,
  output [3:0] sel3
);

  // sel = (ack != 0) ? x : y
  assign sel  = (ack != 1'b0)   ? x : y;

  // sel2 = (ack == req) ? x : y
  assign sel2 = (ack == req)    ? x : y;

  // sel3 = ((ack | req) != 0) ? x : y
  assign sel3 = ((ack | req) != 1'b0) ? x : y;

endmodule

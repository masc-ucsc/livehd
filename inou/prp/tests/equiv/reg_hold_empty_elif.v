// Golden for reg_hold_empty_elif: the conditional PC update with an EXPLICIT
// hold on the stall path (`pcr <= pcr`). This is an independent oracle — it
// spells out the hold the .prp leaves implicit via the empty `elif` arm, so the
// LEC fails if the empty arm is dropped (the `else` value would land on the
// stall path and the no-stall path would hold instead of taking `nx`).
module \reg_hold_empty_elif.pcunit (
   input        taken,
   input        stall,
   input  [7:0] tgt,
   input  [7:0] nx,
   output [7:0] pc,
   input        clock,
   input        reset
);
  reg [7:0] pcr;
  always @(posedge clock) begin
    if (reset)
      pcr <= 8'b0;
    else if (taken)
      pcr <= tgt;
    else if (stall)
      pcr <= pcr;     // hold while stalled (the empty `elif` arm)
    else
      pcr <= nx;
  end
  assign pc = pcr;
endmodule

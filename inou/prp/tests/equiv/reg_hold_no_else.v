// Golden for reg_hold_no_else: a register with two prioritized write enables
// and an IMPLICIT hold when neither fires (the standard register-file idiom).
// Independent oracle for the pure-write auto-hold: the LEC fails if the
// unwritten path lowers to a don't-care instead of holding the flop q.
module \reg_hold_no_else.regfile (
   input        we_a,
   input        we_b,
   input  [7:0] adata,
   input  [7:0] bdata,
   output [7:0] q,
   input        clock,
   input        reset
);
  reg [7:0] r;
  always @(posedge clock) begin
    if (reset)
      r <= 8'b0;
    else if (we_b)
      r <= bdata;
    else if (we_a)
      r <= adata;
    // neither enable: r holds its value (implicit hold)
  end
  assign q = r;
endmodule

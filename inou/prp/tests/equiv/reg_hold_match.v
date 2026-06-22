// Golden for reg_hold_match: a select-driven register that holds on any
// unselected value (the `default` arm). Independent oracle for the auto-hold of
// the `unique if`/`match` (Hotmux) lowering: the LEC fails if the none-of slot
// is a don't-care instead of the held flop q.
module \reg_hold_match.selreg (
   input  [1:0] sel,
   input  [7:0] a,
   input  [7:0] b,
   output [7:0] q,
   input        clock,
   input        reset
);
  reg [7:0] r;
  always @(posedge clock) begin
    if (reset)
      r <= 8'b0;
    else
      case (sel)
        2'd0:    r <= a;
        2'd1:    r <= b;
        default: r <= r;   // hold on any unselected value
      endcase
  end
  assign q = r;
endmodule

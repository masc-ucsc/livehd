// Golden for bitset_reg. Each registered next-state is built with explicit,
// NON-overlapping concatenation — never an overlapping `x[hi:lo] <= ...` partial
// write — so this reference does NOT share the per-range set_mask lowering the
// .prp exercises, and the LEC is a true independent oracle (it would FAIL if the
// reg partial-write chain dropped an earlier write, the bug this test guards).
module \bitset_reg.foo (
   input  [7:0] a,
   input  [7:0] b,
   input        en,
   output [11:0] o_cont,
   output [11:0] o_over,
   output [11:0] o_rev,
   output [11:0] o_hold,
   output [11:0] o_cond,
   input        clock,
   input        reset
);
  reg [11:0] x_cont;
  reg [11:0] x_over;
  reg [11:0] x_rev;
  reg [11:0] x_hold;
  reg [11:0] x_cond;

  always @(posedge clock) begin
    if (reset) begin
      x_cont <= 12'b0;
      x_over <= 12'b0;
      x_rev  <= 12'b0;
      x_hold <= 12'b0;
      x_cond <= 12'b0;
    end else begin
      x_cont <= {b[3:0], a[7:0]};                          // [0..=7]=a , [8..=11]=b
      x_over <= {b[7:0], a[3:0]};                          // [0..=7]=a then [4..=11]=b (overwrite 4..7)
      x_rev  <= {b[3:0], a[7:0]};                          // order-independent: same as cont
      x_hold <= {b[3:0], x_hold[7:4], a[3:0]};             // bits 4..7 keep the flop q
      x_cond <= en ? {b[3:0], a[7:0]} : {x_cond[11:8], a[7:0]};  // [8..=11]=b only when en, else hold q
    end
  end

  assign o_cont = x_cont;
  assign o_over = x_over;
  assign o_rev  = x_rev;
  assign o_hold = x_hold;
  assign o_cond = x_cond;
endmodule

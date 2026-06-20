// Golden for bitset_mut. Combinational `mut` bit-assembly. Each output is built
// with explicit, NON-overlapping concatenation (never an overlapping blocking
// `r[hi:lo] = ...`), so the reference does not share the per-range set_mask
// lowering the .prp exercises and the LEC is a true independent oracle.
module \bitset_mut.foo (
   input  [7:0] a,
   input  [7:0] b,
   output [11:0] o_cont,
   output [11:0] o_over,
   output [11:0] o_rev,
   output [11:0] o_gap
);
  assign o_cont = {b[3:0], a[7:0]};         // [0..=7]=a , [8..=11]=b
  assign o_over = {b[7:0], a[3:0]};         // [0..=7]=a then [4..=11]=b (overwrite 4..7)
  assign o_rev  = {b[3:0], a[7:0]};         // order-independent: same as cont
  assign o_gap  = {b[3:0], 4'b0, a[3:0]};   // [0..=3]=a , [8..=11]=b ; bits 4..7 stay 0
endmodule

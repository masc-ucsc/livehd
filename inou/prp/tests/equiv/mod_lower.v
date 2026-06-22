// Golden for mod_lower: native Verilog `%` (truncated, same as LiveHD). The
// pyrope side lowers each `%` to shift/mask (tolg lower_mod); lgcheck proves
// they are bit-exact.
module \mod_lower.foo (
  input  [7:0] a,
  input  [2:0] c,
  output [2:0] rp2,
  output [5:0] rp2b,
  output [2:0] rfit,
  output [7:0] rfit_big,
  output       rone
);
  assign rp2      = a % 8;
  assign rp2b     = a % 64;
  assign rfit     = c % 8;
  assign rfit_big = c % 100;
  assign rone     = a % 1;
endmodule

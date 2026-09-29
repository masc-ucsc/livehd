// :test: roundtrip
// :top: uarr
package uarr_pkg;
  localparam int unsigned N = 8;
endpackage
module uarr(
  input  logic       clk,
  input  logic       c,
  input  logic       d,
  input  logic [1:0] i,
  input  logic       ib,
  input  logic [1:0] j,
  input  logic       jb,
  input  logic [1:0] rp,
  output logic [7:0] o,
  output logic       any
);
  logic [(uarr_pkg::N/2)-1:0][1:0] v;
  logic [(uarr_pkg::N/2)-1:0][1:0] q;
  always_comb begin
    v = q;
    if (c) v[i][ib] = 1'b1;
    if (d) v = {uarr_pkg::N/2{2'b0}};
    v[j][jb] = 1'b0;
  end
  always_ff @(posedge clk) q <= v;
  // A runtime-indexed element read keeps `q` (and, through `v = q`, `v`)
  // array-shaped in the symbol table -- that is what makes the destination a
  // positional-array bundle and routes the const store into the scatter path.
  assign any = |q[rp];
  assign o   = v;
endmodule

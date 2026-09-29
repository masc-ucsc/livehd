// A dynamic word select over a packed word array (dino's ALU result select):
// the index is scaled into a shift amount, and the top word is constant zero,
// so satopt's odc narrows the amount (its top value selects only zeros).
module word_select(
  input  [3:0]  op,
  input  [31:0] x,
  input  [31:0] y,
  output [31:0] r
);
  wire [15:0][31:0] tbl = {
    {32'h0},
    {31'h0, x != y},
    {31'h0, x == y},
    {31'h0, x >= y},
    {31'h0, $signed(x) >= $signed(y)},
    {~(x | y)},
    {31'h0, $signed(x) < $signed(y)},
    {x - y},
    {x + y},
    {x & y},
    {x | y},
    {y - x},
    {x ^ ~y},
    {x},
    {31'h0, x < y},
    {x ^ y}};
  assign r = tbl[op];
endmodule

// Golden for ruling 26 (see bitnot_unsigned.prp). Every `~` below is written
// at the exact width the Pyrope operand's TYPE gives it, so the golden does
// not depend on Verilog's context-width extension.
module bitnot_unsigned(
  input               x,
  input        [2:0]  w,
  input        [1:0]  r,
  input signed [3:0]  s,
  input        [2:0]  a,
  input        [4:0]  b,
  input        [79:0] big,
  output              nx,
  output       [7:0]  nw8,
  output              nr,
  output       [7:0]  ncast,
  output       [5:0]  nq,
  output       [79:0] nbig,
  output signed [3:0] ns,
  output signed [4:0] nt,
  output       [4:0]  nand_o,
  output       [4:0]  nor_o,
  output       [4:0]  xnor_o,
  output signed [5:0] nmix,
  output       [2:0]  eqs,
  output              fo
);
  wire [2:0] nw = ~w;                        // 3 bits flip
  wire [4:0] a5 = {2'b00, a};
  wire [2:0] sa = s[2:0] & a;                // s & a: a is non-negative, so only 3 bits survive
  assign nx     = ~x;
  assign nw8    = {5'b0, nw};
  assign nr     = ~r[1];
  assign ncast  = ~{5'b0, w};                // 8 bits flip
  assign nq     = ~{3'b0, w};                // 6 bits flip
  assign nbig   = ~big;
  assign ns     = ~s;                        // -s - 1 (always fits s4)
  assign nt     = ~({2'b00, w} + 5'd1);      // -(w + 1) - 1 as 5-bit two's complement
  assign nand_o = ~(a5 & b);
  assign nor_o  = ~(a5 | b);
  assign xnor_o = ~(a5 ^ b);
  assign nmix   = ~{3'b000, sa};             // -(s & a) - 1 as 6-bit two's complement
  assign eqs    = {x == 1'b0, w == 3'd2, s == 4'sd2};
  assign fo     = r[0] ? ~x : x;
endmodule

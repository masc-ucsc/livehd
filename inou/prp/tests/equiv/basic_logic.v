// Canonical paired regression; original parser forms are retained below.
module basic_logic(
  input [7:0] a, b, c, d, output x, repeated, output [1:0] bits_and, output all_bits, any_bit,
  parity, nand_bits, nor_bits, xnor_bits, reduce_and, reduce_or, reduce_xor, is_zero, less,
  overwritten, shared_or, shared_xor, logic_c, logic_d, output [7:0] mask_and, mask_or,
  function_value
);
  trivial xor_bits(a[0], b[0], x);
  trivial1 repeated_term(repeated, a[0], b[0], c[0]);
  trivial_and and_bits(a[1:0], b[1:0], bits_and);
  trivial_reduce reductions(a, all_bits, any_bit, parity, nand_bits, nor_bits, xnor_bits);
  reduce reduction_aliases(a, reduce_and, reduce_or, reduce_xor, is_zero);
  compare unsigned_compare(less, a[3:0], b[3:0]);
  dce1 overwritten_assignment(a[0], b[0], overwritten);
  common_sub common_terms(shared_or, shared_xor, a[0], b[0], c[0], d[0]);
  logic_bitwise_op_gld logical_and_bitwise(a, b, logic_c, logic_d, mask_and, mask_or);
  fn_ret_bool function_bit_return(a, b, function_value);
endmodule

// Former fixture: inou/yosys/tests/trivial.v
module trivial( input a, input b, output c);
assign c = a ^ b;
endmodule


// Former fixture: inou/yosys/tests/trivial1.v
module trivial1 (
  output reg y,
  input a, b, c
);

always @(*) begin
  y = ((a & b) & c) | ((a & b) & c);
end
endmodule

// Former fixture: inou/yosys/tests/trivial_and.v

module trivial_and (input [1:0] a, input [1:0] b, output [1:0] g);

assign g = a & b;

endmodule


// Former fixture: inou/yosys/tests/trivial_reduce.v
module trivial_reduce (
input [7:0]a,
output c,
output d,
output e,
output rnand,
output rnor,
output rnxor
);

assign c = &a;
assign d = |a;
assign e = ^a;
assign rnand =  ~&a;
assign rnor = ~|a;
assign rnxor = ~^a;
endmodule

// Former fixture: inou/yosys/tests/reduce.v

module reduce(input [7:0] in, output a, output b, output c, output d);

assign a = &in;
assign b = |in;
assign c = ^in;
assign d = !in;

endmodule



// Former fixture: inou/yosys/tests/compare.v
module compare (
  output reg  y,
  input [3:0] a,
  input [3:0] b
);

always @(*) begin
  y = a < b;
end
endmodule

// Former fixture: inou/yosys/tests/dce1.v
module dce1( input a, input b, output reg c);
always @(a or b) begin
  c = a | b;

  c = a ^ b;
end
endmodule


// Former fixture: inou/yosys/tests/common_sub.v
module common_sub (
  output reg y, z,
  input a, b, c, d
);

always @ (*) begin
  y = (a & d) | c;
  z = (a & d) ^ b;
end
endmodule

// Former fixture: inou/yosys/tests/logic_bitwise_op_gld.v
module logic_bitwise_op_gld(
  input  [7:0] a,
  input  [7:0] b,
  output       c,
  output       d,
  output [7:0] c2,
  output [7:0] d2
);


assign c = a && b; //strange expression in Verilog
assign d = a && b;
// assign c = (a != 8'b0) && (b != 8'b0);
// assign d = (a != 8'b0) || (b != 8'b0);
assign c2 = a & b;
assign d2 = a | b;

endmodule


// Former fixture: inou/yosys/tests/fn_ret_bool.v
// A function whose body computes a boolean comparison, then a caller compares
// the result `!= 0`. The reader must coerce the boolean result to integer (a
// Verilog function returns a bit-vector), else upass typecheck flags the
// caller's `f(x) != 0` as a bool-vs-int comparison mismatch.
module fn_ret_bool
  ( input  [7:0]     a
  , input  [7:0]     b
  , output reg [7:0] o
  );
  function automatic is_hi(input [7:0] v);
    is_hi = v[7] == 1'b1;
  endfunction
  always @* begin
    o = 8'h0;
    if (is_hi(a) != 1'b0) o = b;
  end
endmodule

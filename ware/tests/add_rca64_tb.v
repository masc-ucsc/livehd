module add_rca_tb
(
  input [64-1:0]   a,
	input [64-1:0]   b,

	output [64-1:0]  sum,
	output reg				 carry
);

  add_rca
  #(.Bits(64))
  f (.a(a), .b(b), .sum(sum), .carry(carry));

endmodule


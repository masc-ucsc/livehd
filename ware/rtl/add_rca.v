/*verilator lint_off UNOPTFLAT*/
/*verilator lint_off UNUSED*/
module add_rca
#(parameter Bits=64)
(
  input              clk,
  input              reset,

  input [Bits-1:0]   a,
	input [Bits-1:0]   b,

	output [Bits-1:0]  sum,
	output reg				 carry
);

  reg [Bits:0] c;

  assign c[0] = 1'b0;

  genvar i;
  generate
    for(i=0; i<Bits; i = i + 1)  begin
      full_adder fa
        (
          .a(a[i]),
          .b(b[i]),
          .cin(c[i]),
          .cout(c[i+1]),
          .sum(sum[i])
        );
      end
  endgenerate

  always @(*) begin
   carry = c[Bits];
  end

/*verilator lint_on UNOPTFLAT*/
/*verilator lint_on UNUSED*/
endmodule


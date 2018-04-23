module add_csa
#(parameter Bits=64)
(
  input [Bits-1:0]  a,
  input [Bits-1:0]  b,
  input [Bits-1:0]  c,

  output [Bits:0]   sum,
  output            carry
);

 reg [Bits-1:0]   si;
 reg [Bits-1:0]   si2;
 reg [Bits:0]     ci;

 genvar i;
 generate
   for(i=0; i<Bits; i++)
    begin
      full_adder fa
      (
        .a(a[i]),
        .b(b[i]),
        .cin(c[i]),
        .cout(ci[i]),
        .sum(si[i])
      );
    end
 endgenerate

 assign sum[0] = si[0];
 si2           = si>>1;   //Shift si by 1 on right
 si2[Bits-1]   = 0;       //make top most bit os si2 as 0

 add_rca #(.Bits(Bits)) rca
 (
   .a(si),
   .b(ci),
   .sum(sum[i+1]),
   .carry(carry)
 );

 endmodule

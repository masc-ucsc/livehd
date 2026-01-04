/*verilator lint_off UNOPTFLAT*/
/*verilator lint_off UNUSED*/
`include "logfunc.h"

module mult_booth
#(parameter Bits=64,N=Bits/2)
(
  input [Bits-1:0]      a,
  input [Bits-1:0]      b,

  output [2*Bits-1:0]   ans
);


reg [2:0]        c[N-1:0];
reg [Bits:0]     p[N-1:0];
reg [2*Bits-1:0] sp[N-1:0];
reg [2*Bits-1:0] prod;
reg [Bits-1:0]   inv_a;

integer i;

always @(*) begin
  inv_a = ~a + 1'b1; // 2's complement

  c[0] = {b[1], b[0], 1'b0};
  for (i = 1; i < N; i = i + 1) begin
    c[i] = {b[2*i+1], b[2*i], b[2*i-1]};
  end

  for (i = 0; i < N; i = i + 1) begin
    case (c[i])
      3'b001: p[i] = {a[Bits-1], a};         // 1
      3'b010: p[i] = {a[Bits-1], a};         // 1
      3'b011: p[i] = {a, 1'b0};              // 2
      3'b100: p[i] = {inv_a, 1'b0};          // -2
      3'b101: p[i] = {inv_a[Bits-1], inv_a}; // -1
      3'b110: p[i] = {inv_a[Bits-1], inv_a}; // -1
      default: p[i] = { (Bits+1){1'b0} };    // 0
    endcase

    // Sign-extend to 2*Bits, then shift by 2*i.
    if (p[i][Bits] == 1'b0)
      sp[i] = {{(Bits-1){1'b0}}, p[i]};
    else
      sp[i] = {{(Bits-1){1'b1}}, p[i]};

    sp[i] = sp[i] << (2*i);
  end

  prod = sp[0];
  for (i = 1; i < N; i = i + 1) begin
    prod = prod + sp[i];
  end
end

assign ans = prod;

endmodule

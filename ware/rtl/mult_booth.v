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


reg [2:0]                 c[N-1:0];
reg [Bits:0]              p[N-1:0];
reg [2*Bits-1:0]          sp[N-1:0];
reg [2*Bits-1:0]          prod;
reg [Bits-1:0]              inv_a;
reg [Bits:0]               temp_reg_iter;

genvar i,k;
assign inv_a = ~a + 1; //2's compliment

always@(*) begin
  c[0] = {b[1],b[0],1'b0};
  for(i=1; i<N; i++)
  begin
    c[i] = {b[2*i+1],b[2*i],b[2*i-1]};
  end

  for(i=0; i<N; i++)
  begin
    case(c[i])
      3'b001:
      begin
        p[i] = {a[Bits-1],a}; // 1
      end
      3'b010:
      begin
        p[i] = {a[Bits-1],a}; // 1
      end
      3'b011:
        begin
          p[i] = {a,1'b0}; // 2
        end
      3'b100:
        begin
          p[i] = {inv_a,1'b0}; // -2
        end
      3'b101:
        begin
          p[i] = {inv_a[Bits-1],inv_a}; // -1
        end
      3'b110:
        begin
          p[i] = {inv_a[Bits-1],inv_a}; // -1
        end
      default: p[i] = 0; // 0
    endcase

    // Sign extension of partial products
    assign temp_reg_iter = p[i];
    if (temp_reg_iter[Bits] == 0)
      assign sp[i] = {63'b0,p[i]};
    else
      assign sp[i] = {(~63'b0),p[i]};

    // Shifting partial products by 2 bits
    for(k=0;k<i;k++)
    begin
      assign sp[i] = sp[i]<<2;
    end
  end

  assign prod = sp[0];
  for(i=1; i<N; i++)
  begin
    assign prod = prod + sp[i];
  end
end

assign ans = prod;

endmodule













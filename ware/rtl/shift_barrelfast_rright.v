/*verilator lint_off UNOPTFLAT*/
/*verilator lint_off UNUSED*/
`include "logfunc.h"

module shift_barrelfast_rright
#(parameter Bits=64)
(
  input [Bits-1:0]          a,
  input [`log2(Bits)-1:0]   sh,

  output reg [Bits-1:0]     b
);

 reg [Bits-1:0]             array [Bits-1:0];
 reg [(2*Bits-1):0]         a_double;

 assign a_double = {a,a};

// rotate right
 genvar i;
 generate
  for(i=0; i<Bits; i++)
   begin
    assign array[i] = a_double[Bits-1+i:i];
   end
 endgenerate

 always @(*) begin
  b = array[sh];
 end

/*verilator lint_on UNOPTFLAT*/
/*verilator lint_on UNUSED*/

// rotate right ends

endmodule

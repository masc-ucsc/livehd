/*verilator lint_off UNOPTFLAT*/
/*verilator lint_off UNUSED*/
`include "logfunc.h"

module shift_barrelfast_sleft
#(parameter Bits=64)
(
  input [Bits-1:0]          a,
  input [`log2(Bits)-1:0]   sh,

  output reg [Bits-1:0]     b
);

 reg [Bits-1:0]             array [Bits-1:0];
 reg [(2*Bits-1):0]         a_double;
 reg [Bits-1:0]             temp;

 assign temp     = 0;
 assign a_double = {a,temp};

// shift left
 genvar i;
 generate
  for(i=0; i<Bits; i++)
   begin
    assign array[i] = a_double[2*Bits-1-i:Bits-i];
   end
 endgenerate

 always @(*) begin
  b = array[sh];
 end

 // shift left ends

/*verilator lint_on UNOPTFLAT*/
/*verilator lint_on UNUSED*/

endmodule


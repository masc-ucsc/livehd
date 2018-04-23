module shift_barrelfast
#(parameter Bits=64)
(
  input [Bits-1:0]          a,
  input [`log2(Bits-1):0]   sh,

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
    array[0] = a_double[63+i:i];
   end
 endgenerate

 b = array[sh];

 // rotate right ends 

endmodule

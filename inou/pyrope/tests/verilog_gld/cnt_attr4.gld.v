module cnt_attr4(
   input signed clock
  ,input signed [1:0] reset
  ,input signed [1:0] cond
  ,input signed [16:0] inp
  ,output reg signed [16:0] out
);
reg signed [1:0] t_pin9;
reg signed [16:0] \#x ;
reg signed [16:0] \#x_next ;
reg signed [16:0] t_pin40;
reg signed [16:0] \#x_5 ;
reg signed [16:0] \#x_6 ;
reg cond_unsign;
reg [15:0] inp_unsign;
reg [15:0] \#x_6_unsign ;
always_comb begin
  t_pin9 = 2'sh1;
  t_pin40 = 17'shffff;
  cond_unsign = cond[0:0];
  inp_unsign = inp[15:0];
   if ((inp_unsign > \#x )) begin
     \#x_5  = inp;
   end else begin
     \#x_5  = ((\#x  - (t_pin9)) & t_pin40);
   end
   if (cond_unsign) begin
     \#x_6  = \#x_5 ;
   end else begin
     \#x_6  = \#x ;
   end
  \#x_6_unsign  = \#x_6 [15:0];
end
always_comb begin
  out = \#x ;
  \#x_next  = \#x_6_unsign ;
end
always @(posedge clock ) begin
\#x  <= \#x_next ;
end
endmodule

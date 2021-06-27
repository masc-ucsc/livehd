module tuple_reg2 (
   input signed reset
  ,input signed clock
  ,output reg signed [4:0] out
  ,output reg signed [6:0] out2
  ,output reg signed [6:0] out3
  ,output reg signed [6:0] out4
  ,output reg signed [6:0] out5
);
reg signed [6:0] \reg.baz ;
reg signed [6:0] \reg.baz_next ;
reg signed [6:0] \reg.foo.bar ;
reg signed [6:0] \reg.foo.bar_next ;
always_comb begin
  out4 = \reg.foo.bar ;
  out2 = \reg.foo.bar ;
  out3 = \reg.baz ;
  out = (5'shc);
  out5 = \reg.baz ;
  \reg.baz_next  = (7'sh39);
  \reg.foo.bar_next  = (7'sh38);
end
always @(posedge clock ) begin
if (reset) begin
\reg.baz  <= 'h0;
end else begin
\reg.baz  <= \reg.baz_next ;
end
end
always @(posedge clock ) begin
if (reset) begin
\reg.foo.bar  <= 'h0;
end else begin
\reg.foo.bar  <= \reg.foo.bar_next ;
end
end
endmodule

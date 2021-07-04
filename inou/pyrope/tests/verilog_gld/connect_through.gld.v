module \connect_through.sub.subsub (
   input signed reset
  ,input signed clock
  ,input signed [4:0] foo
  ,output reg signed [7:0] out1
);
reg signed [7:0] \counter.0 ;
reg signed [7:0] \___next_counter.0  ;
always_comb begin
  out1 = \counter.0 ;
  \___next_counter.0  = ((\counter.0  + foo) & (8'sh7f));
end
always @(posedge clock ) begin
if (reset) begin
\counter.0  <= 'h0;
end else begin
\counter.0  <= \___next_counter.0 ;
end
end
endmodule
module \connect_through.sub (
   input signed reset
  ,input signed clock
  ,output reg signed [7:0] \xx1.out1 
  ,output reg signed [7:0] \xx2.out1 
);
reg [6:0] t_pin31_4;
reg [6:0] t_pin32_4;
\connect_through.sub.subsub  \i___f_0:%xx2_0:connect_through.sub.subsub (
.clock(clock)
,.foo(5'sha)
,.out1(t_pin32_4)
,.reset(reset)
);
\connect_through.sub.subsub  \i___e_0:%xx1_0:connect_through.sub.subsub (
.clock(clock)
,.foo(3'sh2)
,.out1(t_pin31_4)
,.reset(reset)
);
always_comb begin
  \xx1.out1  = t_pin31_4;
  \xx2.out1  = t_pin32_4;
end
endmodule
module connect_through(
   input signed reset
  ,input signed clock
  ,output reg signed [8:0] out1
  ,output reg signed [8:0] out2
);
reg signed [7:0] t_pin45_3;
reg signed [7:0] t_pin46_4;
reg signed [7:0] t_pin47_3;
reg signed [7:0] t_pin48_4;
\connect_through.sub  \i___h_0:yy2_0:connect_through.sub (
.clock(clock)
,.reset(reset)
,.xx1.out1(t_pin47_3)
,.xx2.out1(t_pin48_4)
);
\connect_through.sub  \i___g_0:yy1_0:connect_through.sub (
.clock(clock)
,.reset(reset)
,.xx1.out1(t_pin45_3)
,.xx2.out1(t_pin46_4)
);
always_comb begin
  out1 = (t_pin45_3 + t_pin47_3);
  out2 = (t_pin46_4 + t_pin48_4);
end
endmodule

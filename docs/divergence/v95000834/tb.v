`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [32:0] i0;
  reg [30:0] i1;
  reg signed [30:0] i2;
  reg [1:0] i3;
  reg signed [16:0] i4;
  wire signed [7:0] o0;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .i3(i3), .i4(i4), .o0(o0));
  initial begin
    reset = 1; i0 = 33'd5914986311; i1 = 31'd1588276627; i2 = -31'sd621037839; i3 = 2'd0; i4 = 17'd27024;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d", o0);
    reset = 1; i0 = 33'd186192724; i1 = 31'd838795623; i2 = 31'd360743680; i3 = 2'd0; i4 = -17'sd6673;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d", o0);
    reset = 0; i0 = 33'd5116483441; i1 = 31'd689552688; i2 = -31'sd163556849; i3 = 2'd3; i4 = -17'sd15055;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d", o0);
    reset = 0; i0 = 33'd2180906130; i1 = 31'd753848105; i2 = 31'd345675845; i3 = 2'd1; i4 = -17'sd359;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d", o0);
    reset = 0; i0 = 33'd2355672279; i1 = 31'd486550714; i2 = -31'sd122750716; i3 = 2'd3; i4 = 17'd11732;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d", o0);
    reset = 0; i0 = 33'd4676018699; i1 = 31'd850971471; i2 = 31'd661950001; i3 = 2'd2; i4 = 17'd55509;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d", o0);
    reset = 0; i0 = 33'd2964804855; i1 = 31'd1748894164; i2 = -31'sd762183543; i3 = 2'd1; i4 = -17'sd20054;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d", o0);
    reset = 0; i0 = 33'd5657871031; i1 = 31'd1717400245; i2 = -31'sd258286714; i3 = 2'd3; i4 = -17'sd39419;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d", o0);
    reset = 0; i0 = 33'd4847715524; i1 = 31'd2116289717; i2 = -31'sd377546530; i3 = 2'd3; i4 = 17'd29458;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d", o0);
    reset = 0; i0 = 33'd1171269303; i1 = 31'd538508543; i2 = -31'sd564155441; i3 = 2'd2; i4 = 17'd62071;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d", o0);
    $finish;
  end
endmodule

`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg signed [2:0] i0;
  reg signed [16:0] i1;
  reg [4:0] i2;
  reg signed [4:0] i3;
  wire signed [16:0] o0;
  wire signed [16:0] o1;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .i3(i3), .o0(o0), .o1(o1));
  initial begin
    reset = 1; i0 = -3'sd4; i1 = -17'sd29922; i2 = 5'd5; i3 = -5'sd10;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d", o0, o1);
    reset = 1; i0 = 3'd0; i1 = 17'd18239; i2 = 5'd13; i3 = -5'sd7;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 3'd1; i1 = -17'sd43377; i2 = 5'd9; i3 = -5'sd5;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 3'd1; i1 = 17'd23700; i2 = 5'd14; i3 = -5'sd14;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 3'd1; i1 = -17'sd36412; i2 = 5'd18; i3 = -5'sd12;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = -3'sd3; i1 = 17'd48815; i2 = 5'd8; i3 = 5'd14;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = -3'sd4; i1 = -17'sd42238; i2 = 5'd29; i3 = -5'sd9;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = -3'sd3; i1 = 17'd32957; i2 = 5'd4; i3 = -5'sd12;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = -3'sd1; i1 = 17'd46369; i2 = 5'd4; i3 = 5'd11;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 3'd1; i1 = 17'd5191; i2 = 5'd18; i3 = 5'd13;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d", o0, o1);
    $finish;
  end
endmodule

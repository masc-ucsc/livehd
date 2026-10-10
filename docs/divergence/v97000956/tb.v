`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [11:0] i0;
  reg [11:0] i1;
  reg signed [15:0] i2;
  wire signed [11:0] o0;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .o0(o0));
  initial begin
    reset = 1; i0 = 12'd924; i1 = 12'd2478; i2 = 16'd17024;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d", o0);
    reset = 1; i0 = 12'd309; i1 = 12'd480; i2 = 16'd29078;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d", o0);
    reset = 0; i0 = 12'd913; i1 = 12'd218; i2 = 16'd21979;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d", o0);
    reset = 0; i0 = 12'd1661; i1 = 12'd3170; i2 = 16'd24553;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d", o0);
    reset = 0; i0 = 12'd4043; i1 = 12'd1905; i2 = -16'sd30959;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d", o0);
    reset = 0; i0 = 12'd554; i1 = 12'd526; i2 = 16'd16853;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d", o0);
    reset = 0; i0 = 12'd3338; i1 = 12'd3436; i2 = -16'sd6097;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d", o0);
    reset = 0; i0 = 12'd3793; i1 = 12'd2986; i2 = -16'sd32679;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d", o0);
    reset = 0; i0 = 12'd949; i1 = 12'd674; i2 = -16'sd465;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d", o0);
    reset = 0; i0 = 12'd3535; i1 = 12'd2355; i2 = 16'd26757;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d", o0);
    $finish;
  end
endmodule

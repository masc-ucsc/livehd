`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [15:0] i0;
  reg [15:0] i1;
  reg [15:0] i2;
  wire [15:0] o0;
  wire [7:0] o1;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .o0(o0), .o1(o1));
  initial begin
    reset = 1; i0 = 16'd27086; i1 = 16'd17607; i2 = 16'd57153;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d", o0, o1);
    reset = 1; i0 = 16'd48967; i1 = 16'd35873; i2 = 16'd54123;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 16'd31166; i1 = 16'd1875; i2 = 16'd20367;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 16'd10065; i1 = 16'd42587; i2 = 16'd15993;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 16'd30548; i1 = 16'd228; i2 = 16'd22380;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 16'd33111; i1 = 16'd5125; i2 = 16'd28928;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 16'd35632; i1 = 16'd54701; i2 = 16'd61963;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 16'd2652; i1 = 16'd3726; i2 = 16'd53128;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 16'd63510; i1 = 16'd40329; i2 = 16'd43731;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 16'd42038; i1 = 16'd41760; i2 = 16'd46121;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d", o0, o1);
    $finish;
  end
endmodule

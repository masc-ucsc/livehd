`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [8:0] i0;
  reg [4:0] i1;
  reg [30:0] i2;
  wire [6:0] o0;
  wire signed [16:0] o1;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .o0(o0), .o1(o1));
  initial begin
    reset = 1; i0 = 9'd487; i1 = 5'd30; i2 = 31'd1150712578;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d", o0, o1);
    reset = 1; i0 = 9'd464; i1 = 5'd23; i2 = 31'd1746788368;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 9'd346; i1 = 5'd11; i2 = 31'd1900099075;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 9'd257; i1 = 5'd29; i2 = 31'd1011038672;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 9'd248; i1 = 5'd16; i2 = 31'd2011332653;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 9'd394; i1 = 5'd25; i2 = 31'd223791579;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 9'd485; i1 = 5'd8; i2 = 31'd692152228;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 9'd17; i1 = 5'd7; i2 = 31'd1323569666;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 9'd404; i1 = 5'd8; i2 = 31'd811079920;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 9'd389; i1 = 5'd29; i2 = 31'd1087400133;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d", o0, o1);
    $finish;
  end
endmodule

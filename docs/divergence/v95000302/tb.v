`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [15:0] i0;
  reg [31:0] i1;
  wire signed [15:0] o0;
  wire o1;
  wire [8:0] o2;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .o0(o0), .o1(o1), .o2(o2));
  initial begin
    reset = 1; i0 = 16'd2291; i1 = 32'd2199214957;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 1; i0 = 16'd8158; i1 = 32'd3725365017;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 16'd6847; i1 = 32'd122809393;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 16'd19645; i1 = 32'd1050394488;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 16'd25263; i1 = 32'd439343026;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 16'd38170; i1 = 32'd604830308;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 16'd5506; i1 = 32'd2789481791;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 16'd23253; i1 = 32'd1568252159;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 16'd32549; i1 = 32'd2680585280;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 16'd15976; i1 = 32'd1820124091;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    $finish;
  end
endmodule

`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg signed [2:0] i0;
  reg [8:0] i1;
  wire [6:0] o0;
  wire [31:0] o1;
  wire o2;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .o0(o0), .o1(o1), .o2(o2));
  initial begin
    reset = 1; i0 = -3'sd4; i1 = 9'd422;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 1; i0 = -3'sd2; i1 = 9'd477;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 3'd1; i1 = 9'd334;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 3'd1; i1 = 9'd9;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 3'd2; i1 = 9'd171;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = -3'sd3; i1 = 9'd258;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = -3'sd2; i1 = 9'd153;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = -3'sd1; i1 = 9'd28;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 3'd3; i1 = 9'd330;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 3'd2; i1 = 9'd220;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    $finish;
  end
endmodule

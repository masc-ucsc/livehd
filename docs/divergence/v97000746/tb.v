`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [2:0] i0;
  reg [7:0] i1;
  reg [11:0] i2;
  reg signed [3:0] i3;
  reg [31:0] i4;
  wire signed [69:0] o0;
  wire [32:0] o1;
  wire [16:0] o2;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .i3(i3), .i4(i4), .o0(o0), .o1(o1), .o2(o2));
  initial begin
    reset = 1; i0 = 3'd1; i1 = 8'd247; i2 = 12'd1702; i3 = 4'd6; i4 = 32'd1123936927;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 1; i0 = 3'd1; i1 = 8'd204; i2 = 12'd2452; i3 = -4'sd6; i4 = 32'd2533801041;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 3'd5; i1 = 8'd26; i2 = 12'd2931; i3 = 4'd1; i4 = 32'd3270922563;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 3'd5; i1 = 8'd228; i2 = 12'd2839; i3 = -4'sd7; i4 = 32'd3470147358;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 3'd6; i1 = 8'd188; i2 = 12'd562; i3 = -4'sd2; i4 = 32'd1862197573;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 3'd0; i1 = 8'd179; i2 = 12'd412; i3 = 4'd2; i4 = 32'd3243417473;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 3'd5; i1 = 8'd227; i2 = 12'd1872; i3 = -4'sd4; i4 = 32'd2406842141;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 3'd2; i1 = 8'd235; i2 = 12'd1384; i3 = -4'sd8; i4 = 32'd963538462;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 3'd3; i1 = 8'd60; i2 = 12'd776; i3 = -4'sd6; i4 = 32'd3147550552;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 3'd3; i1 = 8'd225; i2 = 12'd2497; i3 = -4'sd2; i4 = 32'd3041503976;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    $finish;
  end
endmodule

`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [7:0] i0;
  reg signed [2:0] i1;
  reg [8:0] i2;
  reg [8:0] i3;
  reg [31:0] i4;
  wire signed [15:0] o0;
  wire [30:0] o1;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .i3(i3), .i4(i4), .o0(o0), .o1(o1));
  initial begin
    reset = 1; i0 = 8'd103; i1 = -3'sd1; i2 = 9'd175; i3 = 9'd82; i4 = 32'd2172765113;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d", o0, o1);
    reset = 1; i0 = 8'd121; i1 = -3'sd3; i2 = 9'd236; i3 = 9'd466; i4 = 32'd3274485020;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 8'd73; i1 = -3'sd4; i2 = 9'd295; i3 = 9'd445; i4 = 32'd1339665811;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 8'd163; i1 = 3'd0; i2 = 9'd430; i3 = 9'd403; i4 = 32'd1586562441;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 8'd109; i1 = -3'sd2; i2 = 9'd145; i3 = 9'd199; i4 = 32'd2208693614;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 8'd149; i1 = -3'sd3; i2 = 9'd193; i3 = 9'd122; i4 = 32'd2769412830;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 8'd26; i1 = -3'sd3; i2 = 9'd105; i3 = 9'd110; i4 = 32'd2439898853;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 8'd105; i1 = -3'sd4; i2 = 9'd341; i3 = 9'd347; i4 = 32'd3185575778;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 8'd44; i1 = 3'd2; i2 = 9'd180; i3 = 9'd434; i4 = 32'd114948195;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 8'd9; i1 = 3'd0; i2 = 9'd490; i3 = 9'd442; i4 = 32'd2328010055;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d", o0, o1);
    $finish;
  end
endmodule

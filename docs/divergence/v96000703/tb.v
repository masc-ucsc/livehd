`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg i0;
  reg [32:0] i1;
  reg signed [30:0] i2;
  reg signed i3;
  reg signed [63:0] i4;
  reg [16:0] i5;
  wire signed [31:0] o0;
  wire [63:0] o1;
  wire [31:0] o2;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .i3(i3), .i4(i4), .i5(i5), .o0(o0), .o1(o1), .o2(o2));
  initial begin
    reset = 1; i0 = 1'd0; i1 = 33'd1961580198; i2 = 31'd690981088; i3 = 1'd0; i4 = -64'sd2763091362188123198; i5 = 17'd71013;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 1; i0 = 1'd0; i1 = 33'd6316932379; i2 = 31'd304515756; i3 = 1'd1; i4 = -64'sd2118779835013526676; i5 = 17'd115941;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 1'd1; i1 = 33'd7885666418; i2 = 31'd644317304; i3 = 1'd1; i4 = -64'sd4085754189879774031; i5 = 17'd17098;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 1'd1; i1 = 33'd1265249608; i2 = 31'd487378082; i3 = 1'd0; i4 = -64'sd4675085603272389820; i5 = 17'd72077;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 1'd1; i1 = 33'd3545124421; i2 = 31'd225937101; i3 = 1'd1; i4 = 64'd1835414823881794671; i5 = 17'd6428;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 1'd1; i1 = 33'd2104702531; i2 = -31'sd948067474; i3 = 1'd1; i4 = -64'sd5088486767701503048; i5 = 17'd52248;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 1'd1; i1 = 33'd7086684895; i2 = 31'd1064062147; i3 = 1'd0; i4 = 64'd2261622762818652921; i5 = 17'd113985;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 1'd0; i1 = 33'd6648652524; i2 = 31'd640242416; i3 = 1'd0; i4 = -64'sd5301586769534287168; i5 = 17'd3904;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 1'd0; i1 = 33'd1327292000; i2 = -31'sd852320966; i3 = 1'd1; i4 = 64'd8306138957315514601; i5 = 17'd114151;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 1'd0; i1 = 33'd5903926917; i2 = 31'd911646071; i3 = 1'd0; i4 = -64'sd2373044299640025632; i5 = 17'd26971;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    $finish;
  end
endmodule

`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [11:0] i0;
  reg [2:0] i1;
  wire [16:0] o0;
  wire [1:0] o1;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .o0(o0), .o1(o1));
  initial begin
    reset = 1; i0 = 12'd3681; i1 = 3'd2;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d", o0, o1);
    reset = 1; i0 = 12'd2485; i1 = 3'd6;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 12'd865; i1 = 3'd1;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 12'd1276; i1 = 3'd6;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 12'd201; i1 = 3'd5;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 12'd3567; i1 = 3'd5;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 12'd1932; i1 = 3'd0;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 12'd2346; i1 = 3'd0;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 12'd2808; i1 = 3'd3;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 12'd1628; i1 = 3'd2;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d", o0, o1);
    $finish;
  end
endmodule

`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [11:0] i0;
  wire o0;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .o0(o0));
  initial begin
    reset = 1; i0 = 12'd210;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d", o0);
    reset = 1; i0 = 12'd1918;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d", o0);
    reset = 0; i0 = 12'd326;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d", o0);
    reset = 0; i0 = 12'd1224;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d", o0);
    reset = 0; i0 = 12'd3813;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d", o0);
    reset = 0; i0 = 12'd345;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d", o0);
    reset = 0; i0 = 12'd3458;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d", o0);
    reset = 0; i0 = 12'd270;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d", o0);
    reset = 0; i0 = 12'd3627;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d", o0);
    reset = 0; i0 = 12'd233;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d", o0);
    $finish;
  end
endmodule

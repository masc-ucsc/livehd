`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [6:0] i0;
  wire signed [7:0] o0;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .o0(o0));
  initial begin
    reset = 1; i0 = 7'd114;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d", o0);
    reset = 1; i0 = 7'd85;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d", o0);
    reset = 0; i0 = 7'd54;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d", o0);
    reset = 0; i0 = 7'd51;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d", o0);
    reset = 0; i0 = 7'd96;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d", o0);
    reset = 0; i0 = 7'd108;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d", o0);
    reset = 0; i0 = 7'd25;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d", o0);
    reset = 0; i0 = 7'd16;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d", o0);
    reset = 0; i0 = 7'd107;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d", o0);
    reset = 0; i0 = 7'd30;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d", o0);
    $finish;
  end
endmodule

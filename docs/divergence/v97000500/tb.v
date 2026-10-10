`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [8:0] i0;
  reg [31:0] i1;
  wire [31:0] o0;
  wire [15:0] o1;
  wire [2:0] o2;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .o0(o0), .o1(o1), .o2(o2));
  initial begin
    reset = 1; i0 = 9'd499; i1 = 32'd1988480284;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 1; i0 = 9'd506; i1 = 32'd595296697;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 9'd402; i1 = 32'd80628401;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 9'd414; i1 = 32'd2116522854;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 9'd409; i1 = 32'd2031319205;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 9'd341; i1 = 32'd163498521;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 9'd208; i1 = 32'd1857022076;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 9'd369; i1 = 32'd3495447159;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 9'd260; i1 = 32'd3909646340;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 9'd144; i1 = 32'd580462862;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    $finish;
  end
endmodule

`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [3:0] i0;
  reg [8:0] i1;
  reg [30:0] i2;
  wire [3:0] o0;
  wire [8:0] o1;
  wire [15:0] o2;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .o0(o0), .o1(o1), .o2(o2));
  initial begin
    reset = 1; i0 = 4'd6; i1 = 9'd454; i2 = 31'd901705863;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 1; i0 = 4'd9; i1 = 9'd220; i2 = 31'd1933837101;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 4'd5; i1 = 9'd233; i2 = 31'd745996768;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 4'd14; i1 = 9'd386; i2 = 31'd2108710051;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 4'd5; i1 = 9'd19; i2 = 31'd1151950075;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 4'd14; i1 = 9'd221; i2 = 31'd1672610743;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 4'd1; i1 = 9'd158; i2 = 31'd325864614;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 4'd13; i1 = 9'd7; i2 = 31'd2053326565;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 4'd11; i1 = 9'd137; i2 = 31'd330110455;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 4'd11; i1 = 9'd220; i2 = 31'd705449346;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    $finish;
  end
endmodule

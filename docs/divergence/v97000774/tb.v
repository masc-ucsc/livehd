`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg signed [1:0] i0;
  reg [8:0] i1;
  reg signed [1:0] i2;
  reg signed [69:0] i3;
  wire [6:0] o0;
  wire [2:0] o1;
  wire [15:0] o2;
  wire [16:0] o3;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .i3(i3), .o0(o0), .o1(o1), .o2(o2), .o3(o3));
  initial begin
    reset = 1; i0 = -2'sd2; i1 = 9'd37; i2 = 2'd1; i3 = -70'sd62044733143592699955;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 1; i0 = 2'd0; i1 = 9'd480; i2 = -2'sd2; i3 = 70'd443969683919545627877;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 2'd0; i1 = 9'd412; i2 = 2'd1; i3 = 70'd582317979868238864989;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 2'd1; i1 = 9'd419; i2 = -2'sd2; i3 = 70'd509744246036782770733;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 2'd1; i1 = 9'd369; i2 = 2'd0; i3 = -70'sd30707901611208964789;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = -2'sd2; i1 = 9'd348; i2 = -2'sd2; i3 = 70'd392843695400470557522;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 2'd1; i1 = 9'd227; i2 = -2'sd2; i3 = -70'sd486531671875802300481;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = -2'sd1; i1 = 9'd81; i2 = 2'd0; i3 = 70'd227977886036585839223;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = -2'sd2; i1 = 9'd270; i2 = 2'd1; i3 = 70'd526245803252739059118;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = -2'sd2; i1 = 9'd192; i2 = 2'd0; i3 = -70'sd200733934888778126472;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    $finish;
  end
endmodule

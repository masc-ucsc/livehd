`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [16:0] i0;
  reg signed [64:0] i1;
  reg signed [3:0] i2;
  reg i3;
  wire [8:0] o0;
  wire [64:0] o1;
  wire [64:0] o2;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .i3(i3), .o0(o0), .o1(o1), .o2(o2));
  initial begin
    reset = 1; i0 = 17'd83220; i1 = 65'd9686515833265227484; i2 = -4'sd6; i3 = 1'd0;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 1; i0 = 17'd75649; i1 = -65'sd3046472224032890440; i2 = -4'sd4; i3 = 1'd0;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 17'd115785; i1 = 65'd11215609400471050296; i2 = -4'sd1; i3 = 1'd0;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 17'd83086; i1 = -65'sd1545235231241430893; i2 = -4'sd3; i3 = 1'd0;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 17'd91205; i1 = -65'sd3774192380735036491; i2 = -4'sd5; i3 = 1'd1;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 17'd9561; i1 = -65'sd17885330337563432672; i2 = 4'd2; i3 = 1'd1;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 17'd78284; i1 = -65'sd9541308167411093183; i2 = -4'sd8; i3 = 1'd0;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 17'd15237; i1 = 65'd5164534307927404305; i2 = -4'sd6; i3 = 1'd1;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 17'd42588; i1 = 65'd6545290491575238818; i2 = 4'd5; i3 = 1'd1;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 17'd44636; i1 = 65'd11699896710259438465; i2 = -4'sd3; i3 = 1'd1;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    $finish;
  end
endmodule

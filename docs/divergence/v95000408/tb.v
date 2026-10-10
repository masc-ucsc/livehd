`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [30:0] i0;
  reg [31:0] i1;
  reg [6:0] i2;
  reg [6:0] i3;
  reg [16:0] i4;
  reg signed [7:0] i5;
  wire [31:0] o0;
  wire [11:0] o1;
  wire signed [47:0] o2;
  wire signed [1:0] o3;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .i3(i3), .i4(i4), .i5(i5), .o0(o0), .o1(o1), .o2(o2), .o3(o3));
  initial begin
    reset = 1; i0 = 31'd1662283115; i1 = 32'd1489566682; i2 = 7'd37; i3 = 7'd103; i4 = 17'd79217; i5 = -8'sd36;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 1; i0 = 31'd391918754; i1 = 32'd3025767815; i2 = 7'd84; i3 = 7'd1; i4 = 17'd124558; i5 = -8'sd28;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 31'd1914896076; i1 = 32'd2983807363; i2 = 7'd102; i3 = 7'd91; i4 = 17'd4120; i5 = 8'd15;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 31'd357665995; i1 = 32'd505693326; i2 = 7'd14; i3 = 7'd40; i4 = 17'd14961; i5 = 8'd91;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 31'd32192409; i1 = 32'd3856790203; i2 = 7'd47; i3 = 7'd106; i4 = 17'd66316; i5 = 8'd122;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 31'd2004233497; i1 = 32'd3875289427; i2 = 7'd25; i3 = 7'd56; i4 = 17'd45269; i5 = -8'sd117;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 31'd737728977; i1 = 32'd362106943; i2 = 7'd112; i3 = 7'd44; i4 = 17'd55183; i5 = 8'd59;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 31'd983992317; i1 = 32'd1463485136; i2 = 7'd120; i3 = 7'd96; i4 = 17'd91254; i5 = -8'sd21;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 31'd58038286; i1 = 32'd3633843540; i2 = 7'd14; i3 = 7'd123; i4 = 17'd52125; i5 = 8'd79;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 31'd2059150860; i1 = 32'd783468427; i2 = 7'd56; i3 = 7'd51; i4 = 17'd5886; i5 = 8'd83;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    $finish;
  end
endmodule

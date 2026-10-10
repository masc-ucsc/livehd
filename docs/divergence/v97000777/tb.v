`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [16:0] i0;
  reg signed [47:0] i1;
  reg [7:0] i2;
  reg [47:0] i3;
  reg [6:0] i4;
  wire [15:0] o0;
  wire [7:0] o1;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .i3(i3), .i4(i4), .o0(o0), .o1(o1));
  initial begin
    reset = 1; i0 = 17'd88410; i1 = 48'd119046295603229; i2 = 8'd86; i3 = 48'd166703020496192; i4 = 7'd49;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d", o0, o1);
    reset = 1; i0 = 17'd16725; i1 = -48'sd5212702817289; i2 = 8'd213; i3 = 48'd76897482246433; i4 = 7'd57;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 17'd93332; i1 = -48'sd92152818153246; i2 = 8'd229; i3 = 48'd119691278012784; i4 = 7'd67;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 17'd50398; i1 = -48'sd104716937948986; i2 = 8'd13; i3 = 48'd51392445096303; i4 = 7'd89;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 17'd103794; i1 = 48'd100349773404055; i2 = 8'd221; i3 = 48'd78740045687081; i4 = 7'd47;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 17'd112469; i1 = 48'd1886881523478; i2 = 8'd247; i3 = 48'd246682064848131; i4 = 7'd104;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 17'd96058; i1 = -48'sd38326492120193; i2 = 8'd141; i3 = 48'd84557066015391; i4 = 7'd52;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 17'd18892; i1 = -48'sd112521235343134; i2 = 8'd42; i3 = 48'd228683024756718; i4 = 7'd28;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 17'd47291; i1 = -48'sd38716948554758; i2 = 8'd29; i3 = 48'd70771813847057; i4 = 7'd29;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 17'd21509; i1 = 48'd102836122476434; i2 = 8'd4; i3 = 48'd187627403703699; i4 = 7'd124;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d", o0, o1);
    $finish;
  end
endmodule

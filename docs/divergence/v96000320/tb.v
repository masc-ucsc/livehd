`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [47:0] i0;
  reg [6:0] i1;
  reg [31:0] i2;
  reg [15:0] i3;
  wire [16:0] o0;
  wire signed [4:0] o1;
  wire [31:0] o2;
  wire [47:0] o3;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .i3(i3), .o0(o0), .o1(o1), .o2(o2), .o3(o3));
  initial begin
    reset = 1; i0 = 48'd33987835425869; i1 = 7'd116; i2 = 32'd3899306932; i3 = 16'd23381;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 1; i0 = 48'd264395113483804; i1 = 7'd80; i2 = 32'd588799357; i3 = 16'd17487;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 48'd251952712298046; i1 = 7'd99; i2 = 32'd376722209; i3 = 16'd55957;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 48'd233979945717504; i1 = 7'd22; i2 = 32'd301593975; i3 = 16'd43099;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 48'd141361140792180; i1 = 7'd54; i2 = 32'd1812137397; i3 = 16'd55325;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 48'd114348205345979; i1 = 7'd14; i2 = 32'd2660903512; i3 = 16'd29218;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 48'd183395839250723; i1 = 7'd124; i2 = 32'd595490162; i3 = 16'd2208;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 48'd231065755301906; i1 = 7'd39; i2 = 32'd2570352944; i3 = 16'd23765;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 48'd88778866466962; i1 = 7'd114; i2 = 32'd576577299; i3 = 16'd22921;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    reset = 0; i0 = 48'd148695848364543; i1 = 7'd123; i2 = 32'd949918015; i3 = 16'd37823;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d o2=%0d o3=%0d", o0, o1, o2, o3);
    $finish;
  end
endmodule

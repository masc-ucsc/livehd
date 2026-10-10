`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg signed [6:0] i0;
  reg signed [47:0] i1;
  reg [32:0] i2;
  reg i3;
  wire [32:0] o0;
  wire [4:0] o1;
  wire [3:0] o2;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .i3(i3), .o0(o0), .o1(o1), .o2(o2));
  initial begin
    reset = 1; i0 = 7'd19; i1 = -48'sd51635513939445; i2 = 33'd5399050811; i3 = 1'd1;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 1; i0 = -7'sd64; i1 = -48'sd139788836780058; i2 = 33'd7182117336; i3 = 1'd1;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 7'd44; i1 = 48'd53109987427760; i2 = 33'd3726015006; i3 = 1'd0;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 7'd16; i1 = 48'd104057358504431; i2 = 33'd7713566747; i3 = 1'd0;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = -7'sd14; i1 = 48'd7440526153236; i2 = 33'd8084126011; i3 = 1'd0;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 7'd56; i1 = 48'd139054824814314; i2 = 33'd7738522832; i3 = 1'd0;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = -7'sd63; i1 = 48'd58228136045199; i2 = 33'd836133733; i3 = 1'd1;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 7'd51; i1 = -48'sd18441770830064; i2 = 33'd1648004144; i3 = 1'd1;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = -7'sd42; i1 = -48'sd119620309889219; i2 = 33'd2187468962; i3 = 1'd0;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = -7'sd57; i1 = -48'sd3177712388851; i2 = 33'd2961069326; i3 = 1'd0;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    $finish;
  end
endmodule

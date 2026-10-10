`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [4:0] i0;
  reg signed [47:0] i1;
  reg [64:0] i2;
  reg [2:0] i3;
  reg signed [30:0] i4;
  wire [2:0] o0;
  wire [1:0] o1;
  wire [4:0] o2;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .i3(i3), .i4(i4), .o0(o0), .o1(o1), .o2(o2));
  initial begin
    reset = 1; i0 = 5'd8; i1 = 48'd137148034689380; i2 = 65'd17827764543247859224; i3 = 3'd1; i4 = -31'sd509222800;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 1; i0 = 5'd17; i1 = 48'd33270380154750; i2 = 65'd16127680642852525658; i3 = 3'd0; i4 = 31'd132253998;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 5'd8; i1 = 48'd8575013794492; i2 = 65'd21826169902874071592; i3 = 3'd3; i4 = -31'sd476528788;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 5'd26; i1 = -48'sd12139479626313; i2 = 65'd21262666925248862561; i3 = 3'd5; i4 = -31'sd263885987;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 5'd20; i1 = 48'd42659804965079; i2 = 65'd33756797479464585855; i3 = 3'd1; i4 = -31'sd129070007;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 5'd3; i1 = 48'd64866562353461; i2 = 65'd11401649572698590298; i3 = 3'd0; i4 = -31'sd109597362;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 5'd23; i1 = 48'd71950199841587; i2 = 65'd11080713618457280277; i3 = 3'd5; i4 = 31'd369692389;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 5'd30; i1 = -48'sd75938532255370; i2 = 65'd23683080047688872314; i3 = 3'd1; i4 = -31'sd873446900;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 5'd13; i1 = 48'd130561121863088; i2 = 65'd13433282196049950253; i3 = 3'd4; i4 = -31'sd439570824;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    reset = 0; i0 = 5'd10; i1 = -48'sd128997219127310; i2 = 65'd21696548386953472213; i3 = 3'd2; i4 = -31'sd647194834;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d o2=%0d", o0, o1, o2);
    $finish;
  end
endmodule

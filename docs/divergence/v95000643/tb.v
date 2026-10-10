`timescale 1ns/1ps
module tb;
  reg clock = 0;
  reg reset = 1;
  reg [69:0] i0;
  reg signed [69:0] i1;
  reg signed [15:0] i2;
  reg [4:0] i3;
  wire [69:0] o0;
  wire signed [64:0] o1;
  fz dut(.clock(clock), .reset(reset), .i0(i0), .i1(i1), .i2(i2), .i3(i3), .o0(o0), .o1(o1));
  initial begin
    reset = 1; i0 = 70'd777624895916693219776; i1 = -70'sd283455705204954868967; i2 = -16'sd2202; i3 = 5'd26;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv0 o0=%0d o1=%0d", o0, o1);
    reset = 1; i0 = 70'd318567382800420347832; i1 = -70'sd392129110576542962323; i2 = 16'd18339; i3 = 5'd12;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv1 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 70'd481227467888978414874; i1 = 70'd301804258813248149243; i2 = -16'sd9380; i3 = 5'd22;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv2 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 70'd83121519217635681489; i1 = -70'sd103177931696459986924; i2 = 16'd13540; i3 = 5'd7;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv3 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 70'd542347654563038409707; i1 = -70'sd318263817042320628494; i2 = -16'sd8544; i3 = 5'd23;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv4 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 70'd54664030876025176811; i1 = 70'd204572603405854195049; i2 = 16'd34; i3 = 5'd15;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv5 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 70'd125219408358475867791; i1 = -70'sd23800571570859589191; i2 = 16'd2667; i3 = 5'd29;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv6 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 70'd634502849283437348844; i1 = -70'sd531350769564727091630; i2 = -16'sd32735; i3 = 5'd6;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv7 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 70'd853216978017518207489; i1 = 70'd564334770301163653489; i2 = -16'sd7876; i3 = 5'd2;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv8 o0=%0d o1=%0d", o0, o1);
    reset = 0; i0 = 70'd638907744979152440342; i1 = 70'd524759927007221752576; i2 = 16'd8523; i3 = 5'd24;
    #1 clock = 1; #1 clock = 0; #1;
    $display("refv9 o0=%0d o1=%0d", o0, o1);
    $finish;
  end
endmodule

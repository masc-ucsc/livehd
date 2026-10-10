`timescale 1ns/1ps
module tb; reg clock=0, reset=1; reg [3:0] a; wire [3:0] o0; integer k;
fz dut(.clock(clock),.reset(reset),.a(a),.o0(o0));
initial begin for (k=0;k<5;k=k+1) begin reset=k<1; a=k+5; #1 clock=1; #1 clock=0; #1; $display("refv%0d o0=%0d",k,o0); end $finish; end endmodule

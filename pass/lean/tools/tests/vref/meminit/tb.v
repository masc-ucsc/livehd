`timescale 1ns/1ps
module tb;
  reg clk = 0; reg [1:0] ra = 0; reg [1:0] wa = 0; reg [7:0] wd = 0; reg we = 0;
  wire [31:0] all; wire [7:0] rd;
  meminit dut(.clk(clk), .ra(ra), .wa(wa), .wd(wd), .we(we), .all(all), .rd(rd));
  integer c;
  initial begin
    for (c = 0; c < 5; c = c + 1) begin
      ra = (c == 0) ? 0 : (c == 1) ? 1 : (c == 4) ? 3 : 2;
      we = (c == 2); wa = (c == 2) ? 2 : 0; wd = (c == 2) ? 153 : 0;
      #1; $display("STEP %0d %0d %0d", c, all, rd);
      clk = 1; #1; clk = 0; #1;
    end
    $finish;
  end
endmodule

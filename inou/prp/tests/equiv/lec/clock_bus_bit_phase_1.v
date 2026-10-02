/*
Same design, the clock bit taken through a named wire. Must PASS.
*/
module cbp_cell(input wire clk, input wire en, output wire gclk_n);
  reg held_en;
  always_latch if (clk) held_en <= en;
  assign gclk_n = clk | ~held_en;
endmodule
module cbp(input wire [1:0] clks, input wire [2:0] d, output wire [2:0] q);
  wire gn;
  wire c0 = clks[0];
  reg [2:0] early, late;
  cbp_cell u_gate(.clk(c0), .en(early[0]), .gclk_n(gn));
  always @(posedge c0) early <= d;
  always @(negedge gn) late <= d ^ 3'b111;
  assign q = late ^ early;
endmodule

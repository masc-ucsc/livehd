/*
:lec_expect: refuted
MUTATED: the gated register's data constant changed. Same clocking, so the
schedule runs and must find the difference.
*/
module cbp_cell(input wire clk, input wire en, output wire gclk_n);
  reg held_en;
  always_latch if (clk) held_en <= en;
  assign gclk_n = clk | ~held_en;
endmodule
module cbp(input wire [1:0] clks, input wire [2:0] d, output wire [2:0] q);
  wire gn;
  reg [2:0] early, late;
  cbp_cell u_gate(.clk(clks[0]), .en(early[0]), .gclk_n(gn));
  always @(posedge clks[0]) early <= d;
  always @(negedge gn) late <= d ^ 3'b101;
  assign q = late ^ early;
endmodule

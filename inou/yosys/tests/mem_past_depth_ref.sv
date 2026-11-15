// mem_past_depth.sv with every access guarded by hand: a write past the end is not made and a
// read past the end gives 0, the value the yosys reader refines the X to. Every address that
// reaches an array is inside it.
module mem_past_depth(input clk,
                      input we, input [3:0] wa, input [7:0] wd, input [3:0] ra, output [7:0] rd,
                      input we2, input [3:0] wa2, input [7:0] wd2, input [3:0] ra2, output [7:0] rd2,
                      input we3, input [1:0] wp, input [1:0] ww, input [7:0] wd3,
                      input [1:0] rp, input [1:0] rw, output [7:0] rd3);
  reg [7:0] m [0:11];
  always @(posedge clk) if (we && wa < 4'd12) m[wa] <= wd;
  assign rd = (ra < 4'd12) ? m[ra] : 8'd0;

  reg [7:0] o [3:14];
  always @(posedge clk) if (we2 && wa2 >= 4'd3 && wa2 <= 4'd14) o[wa2] <= wd2;
  assign rd2 = (ra2 >= 4'd3 && ra2 <= 4'd14) ? o[ra2] : 8'd0;

  reg [7:0] s [0:11];
  always @(posedge clk) if (we3 && wp != 2'd3) s[{wp, ww}] <= wd3;
  assign rd3 = (rp != 2'd3) ? s[{rp, rw}] : 8'd0;
endmodule

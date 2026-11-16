// mem_past_depth.sv with every access guarded by hand: a write past the end is not made and a
// read past the end gives 0, the value the yosys reader refines the X to. Every address that
// reaches an array is inside it.
module mem_past_depth(input clk,
                      input we, input [1:0] wa, input [0:0] wd, input [1:0] ra, output [0:0] rd,
                      input we2, input [2:0] wa2, input [0:0] wd2, input [2:0] ra2, output [0:0] rd2,
                      input we3, input wp, input ww, input [0:0] wd3,
                      input rp, input rw, output [0:0] rd3);
  reg [0:0] m [0:2];
  always @(posedge clk) if (we && wa < 2'd3) m[wa] <= wd;
  assign rd = (ra < 2'd3) ? m[ra] : 1'd0;

  reg [0:0] o [1:6];
  always @(posedge clk) if (we2 && wa2 >= 3'd1 && wa2 <= 3'd6) o[wa2] <= wd2;
  assign rd2 = (ra2 >= 3'd1 && ra2 <= 3'd6) ? o[ra2] : 1'd0;

  reg [0:0] s [0:2];
  always @(posedge clk) if (we3 && {wp, ww} < 2'd3) s[{wp, ww}] <= wd3;
  assign rd3 = ({rp, rw} < 2'd3) ? s[{rp, rw}] : 1'd0;
endmodule

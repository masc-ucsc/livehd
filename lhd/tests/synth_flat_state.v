// Two instances of ring_node feed each other's valid and data in a ring; a third is driven from the top inputs.
// In the ring every register keeps its reset value forever (each valid bit is constant only because the other one
// is), so nothing in the ring ever changes and its outputs read 0. No per-definition analysis sees it -- the ring's
// v_i is a module input -- and the definition is shared with `x`, whose registers are live. The whole-design state
// proof (pass/synth flat_state) gives the two ring instances their own copy with the constants folded in.
module ring_node(input clk, input rst, input v_i, input [7:0] d_i, output reg v_o, output reg [7:0] d_o);
  reg [7:0] m [0:3];  // read only on a valid cycle: in the ring nothing ever observes it
  always @(posedge clk) begin
    if (v_i) m[d_i[1:0]] <= d_i;
    if (rst) begin
      v_o <= 1'b0;
      d_o <= 8'd0;
    end else begin
      v_o <= v_i;
      if (v_i) d_o <= m[d_i[3:2]] + 8'd1;
    end
  end
endmodule

module synth_flat_state(input clk, input rst, input v_i, input [7:0] d_i, output vx, output [7:0] dx, output [7:0] dr,
                        output vr);
  wire       v0, v1;
  wire [7:0] d0, d1;
  ring_node r0(.clk(clk), .rst(rst), .v_i(v1), .d_i(d1), .v_o(v0), .d_o(d0));
  ring_node r1(.clk(clk), .rst(rst), .v_i(v0), .d_i(d0), .v_o(v1), .d_o(d1));
  ring_node x(.clk(clk), .rst(rst), .v_i(v_i), .d_i(d_i), .v_o(vx), .d_o(dx));
  assign dr = d0 ^ d1;
  assign vr = v0 | v1;
endmodule

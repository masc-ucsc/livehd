// Two instances of one pipeline stage: the parent reads both outputs of `a` but only the valid bit of `b`.
// synth.state_sweep keeps a register when ANY instance of its definition reads it, so `b`'s 16 data bits
// survive unless `b` gets its own demand-specialized copy (pass/synth split_by_demand).
module demand_stage(input clk, input rst, input v_i, input [15:0] d_i, output reg v_o, output reg [15:0] d_o);
  always @(posedge clk) begin
    if (rst) v_o <= 1'b0; else v_o <= v_i;
    d_o <= d_i + 16'd3;
  end
endmodule

module synth_demand_split(input clk, input rst, input v_i, input [15:0] d_i, output va, output [15:0] da, output vb);
  wire [15:0] db_unused;
  demand_stage a(.clk(clk), .rst(rst), .v_i(v_i), .d_i(d_i), .v_o(va), .d_o(da));
  demand_stage b(.clk(clk), .rst(rst), .v_i(va), .d_i(da), .v_o(vb), .d_o(db_unused));
endmodule

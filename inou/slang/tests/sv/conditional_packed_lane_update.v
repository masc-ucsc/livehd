module conditional_packed_lane_update (
    input  logic        reset,
    input  logic        enable,
    input  logic [15:0] base,
    input  logic        data,
    output logic [15:0] out
);
  always_comb begin
    out = base;
    if (reset) begin
      out[5] = 1'b1;
    end else if (enable) begin
      out[5] = data;
    end
  end
endmodule

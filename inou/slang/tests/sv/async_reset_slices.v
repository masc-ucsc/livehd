module async_reset_slices (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic [1:0] en_i,
  input  logic [7:0] data_i,
  output logic [7:0] q_o
);
  logic [1:0][3:0] q;

  for (genvar i = 0; i < 2; i++) begin : gen_q
    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni)
        q[i] <= '0;
      else if (en_i[i])
        q[i] <= data_i[i * 4 +: 4];
    end
  end

  assign q_o = q;
endmodule

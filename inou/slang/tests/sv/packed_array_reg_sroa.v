module packed_array_reg_sroa(
  input  logic       clk,
  input  logic       rst_n,
  input  logic       en_i,
  input  logic [2:0] idx_i,
  input  logic [7:0] data_i,
  output logic [7:0] pick_o,
  output logic [31:0] whole_o
);
  logic [3:0][7:0] lanes_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      lanes_q <= 32'h44332211;
    end else if (en_i) begin
      lanes_q[idx_i] <= data_i;
    end
  end

  assign pick_o  = lanes_q[idx_i];
  assign whole_o = lanes_q;
endmodule

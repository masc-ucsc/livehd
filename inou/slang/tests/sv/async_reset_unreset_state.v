module async_reset_unreset_state
    (input logic          clk_i,
     input logic          rst_ni,
     input logic          push_i,
     input logic [1 : 0]  idx_i,
     input logic          data_i,
     output logic [3 : 0] payload_o,
     output logic [1 : 0] ptr_o);
  logic [3 : 0] payload;
  logic [1 : 0] ptr;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      ptr <= '0;
    end else if (push_i) begin
      payload[idx_i] <= data_i;
      ptr            <= ptr + 1'b1;
    end
  end

  assign payload_o = payload;
  assign ptr_o     = ptr;
endmodule

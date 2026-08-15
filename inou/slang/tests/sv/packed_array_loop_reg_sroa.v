module packed_array_loop_reg_sroa
    (input logic                 clk,
     input logic                 rst_n,
     input logic [3 : 0]         en_i,
     input logic [3 : 0][7 : 0]  data_i,
     input logic [2 : 0]         idx_i,
     output logic [7 : 0]        pick_o,
     output logic [3 : 0][7 : 0] whole_o);
  logic [3 : 0][7 : 0] lanes_q;

  // Keep reset synchronous in this classifier regression. Async-reset loop
  // harvesting is a separate frontend contract; this test isolates whether a
  // procedural induction variable is mistaken for a runtime memory address.
  always_ff @(posedge clk) begin
    if (!rst_n) begin
      for (int i = 0; i < 4; i++) begin
        lanes_q[i] <= 8'(i + 1);
      end
    end else begin
      for (int i = 0; i < 4; i++) begin
        if (en_i[i])
          lanes_q[i] <= data_i[i];
      end
    end
  end

  assign pick_o  = lanes_q[idx_i];
  assign whole_o = lanes_q;
endmodule

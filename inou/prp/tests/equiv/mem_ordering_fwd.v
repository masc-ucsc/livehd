// Golden for ordering="fwd" (lhdsuite array_problem.md): position-blind
// transparency — every read of the written address returns the new data this
// same cycle, whether the read is textually before or after the write.
module mem_ordering_fwd(input logic clk,
                        input logic [1:0] a1,
                        input logic we,
                        input logic [1:0] a2, input logic [1:0] d2,
                        input logic [1:0] a4,
                        output logic [1:0] o1, output logic [1:0] o4);
  logic [1:0] mem [4];
  always_ff @(posedge clk) begin
    if (we) mem[a2] <= d2;
  end
  assign o1 = (we && a1 == a2) ? d2 : mem[a1];
  assign o4 = (we && a4 == a2) ? d2 : mem[a4];
endmodule

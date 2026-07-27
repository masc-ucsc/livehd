// Golden for ordering="none" (lhdsuite array_problem.md): a same-cycle
// read-during-write is undefined — encoded as x so the LEC miter treats the
// collision window as don't-care (any implementation value proves). Only the
// non-colliding behavior is pinned.
module mem_ordering_none(input logic clk,
                         input logic [1:0] a1,
                         input logic we,
                         input logic [1:0] a2, input logic [1:0] d2,
                         input logic [1:0] a4,
                         output logic [1:0] o1, output logic [1:0] o4);
  logic [1:0] mem [4];
  always_ff @(posedge clk) begin
    if (we) mem[a2] <= d2;
  end
  assign o1 = (we && a1 == a2) ? 2'bxx : mem[a1];
  assign o4 = (we && a4 == a2) ? 2'bxx : mem[a4];
endmodule

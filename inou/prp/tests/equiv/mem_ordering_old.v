// Golden for ordering="old": a plain nonblocking-written array. Both reads see
// the COMMITTED contents even on a same-cycle same-address write, and that value
// is DEFINED (no x anywhere) — the distinction from ordering="none", whose
// golden encodes the collision window as x.
module mem_ordering_old(input logic clk,
                        input logic [1:0] a1,
                        input logic we,
                        input logic [1:0] a2, input logic [1:0] d2,
                        input logic [1:0] a4,
                        output logic [1:0] o1, output logic [1:0] o4);
  logic [1:0] mem [4];
  always_ff @(posedge clk) begin
    if (we) mem[a2] <= d2;
  end
  assign o1 = mem[a1];
  assign o4 = mem[a4];
endmodule

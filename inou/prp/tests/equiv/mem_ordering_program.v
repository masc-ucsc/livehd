// Golden for ordering="program" (ruled semantics, lhdsuite array_problem.md):
// a read BEFORE the writes returns the old stored value; a read AFTER them
// sees the writes with the later one winning; on a write-write collision the
// last program write (d3) commits. Verilog non-blocking writes give the
// storage part (later statement wins) natively; the o4 bypass encodes the
// read-after-write visibility explicitly.
module mem_ordering_program(input logic clk,
                            input logic [1:0] a1,
                            input logic [1:0] a2, input logic [1:0] d2,
                            input logic [1:0] a3, input logic [1:0] d3,
                            input logic [1:0] a4,
                            output logic [1:0] o1, output logic [1:0] o4);
  logic [1:0] mem [4];
  always_ff @(posedge clk) begin
    mem[a2] <= d2;
    mem[a3] <= d3;
  end
  assign o1 = mem[a1];
  assign o4 = (a4 == a3) ? d3 : (a4 == a2) ? d2 : mem[a4];
endmodule

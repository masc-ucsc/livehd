// :test: error
// :error: single writer process
// A whole-array snapshot cannot commit over another process's writes.
module blocking_array_multiple_writers(input logic clk, en, input logic [1:0] d, output wire [1:0] q);
  logic [1:0] mem[2];
  always @(posedge clk) if (en) mem[0] = d;
  always @(posedge clk) if (!en) mem[1] = d;
  assign q = mem[0] ^ mem[1];
endmodule

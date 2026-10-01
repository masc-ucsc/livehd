// :test: error
// :error: memory .mem. is clocked by a constant
// Rulings 42/80: a memory clocked by a constant INSIDE its own module
// (`always @(posedge k)` on a tied wire) is a compile error.
module const_clock_mem_local_reject(input we, input [1:0] wa, input [7:0] wd, input [1:0] ra, output [7:0] q);
  wire k = 1'b0;
  reg [7:0] mem[0:3];
  always @(posedge k) if (we) mem[wa] <= wd;
  assign q = mem[ra];
endmodule

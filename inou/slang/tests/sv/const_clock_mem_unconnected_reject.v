// :test: error
// :error: clock input .clk. of .vmuc_sub. is unconnected.*memory .mem. of .vmuc_sub.
// Rulings 42/80: a memory port whose clock is UNCONNECTED (`.clk()`) is a
// compile error naming the memory.
module vmuc_sub(input clk, input we, input [1:0] wa, input [7:0] wd, input [1:0] ra, output [7:0] q);
  reg [7:0] mem[0:3];
  always @(posedge clk) if (we) mem[wa] <= wd;
  assign q = mem[ra];
endmodule

module const_clock_mem_unconnected_reject(input clk, input we, input [1:0] wa, input [7:0] wd, input [1:0] ra, output [7:0] q1, output [7:0] q2);
  vmuc_sub u1(.clk(), .we(we), .wa(wa), .wd(wd), .ra(ra), .q(q1));
  vmuc_sub u2(.clk(clk), .we(we), .wa(wa), .wd(wd), .ra(ra), .q(q2));
endmodule

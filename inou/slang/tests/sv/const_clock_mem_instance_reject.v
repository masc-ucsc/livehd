// :test: error
// :error: clock input .clk. of .vmcc_sub. is bound to a constant.*memory .mem. of .vmcc_sub.
// Rulings 42/80: a memory port whose clock is constant is a compile error in
// every front end (same rule as registers), naming the memory. `u1` ties the
// memory's write clock to 1'b0.
module vmcc_sub(input clk, input we, input [1:0] wa, input [7:0] wd, input [1:0] ra, output [7:0] q);
  reg [7:0] mem[0:3];
  always @(posedge clk) if (we) mem[wa] <= wd;
  assign q = mem[ra];
endmodule

module const_clock_mem_instance_reject(input clk, input we, input [1:0] wa, input [7:0] wd, input [1:0] ra, output [7:0] q1, output [7:0] q2);
  vmcc_sub u1(.clk(1'b0), .we(we), .wa(wa), .wd(wd), .ra(ra), .q(q1));
  vmcc_sub u2(.clk(clk), .we(we), .wa(wa), .wd(wd), .ra(ra), .q(q2));
endmodule

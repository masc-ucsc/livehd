/*
:lec_top: top
A multi-clock design (clk_a memory port, clk_b flop) whose memory port 1 is
clocked by a CONSTANT with its enable held at 0 -- the one dead-clock shape the
front end accepts (an idle write port, tolg check_const_clock_bind). In
pass/lec's multi-clock mode a constant-clocked port NEVER commits (encode's
`port_edge` = false; it used to be left null, i.e. commit every step), and with
a constant-0 enable it is NOT refused. Variant _1 drops the port and must PROVE;
_2 moves a live port 1 onto clk_a and must REFUTE. The live-enable refusal
needs a yosys-read lg: library: pass/lec/tests/lec_mem_dead_clock_test.sh.
*/
module ram(input logic clk0, input logic clk1, input logic we0, input logic we1,
           input logic [1:0] wa0, input logic [1:0] wa1, input logic [3:0] wd0,
           input logic [3:0] wd1, input logic [1:0] ra, output logic [3:0] o);
  logic [3:0] mem [4];
  always_ff @(posedge clk0) if (we0) mem[wa0] <= wd0;
  always_ff @(posedge clk1) if (we1) mem[wa1] <= wd1;
  assign o = mem[ra];
endmodule
module top(input logic clk_a, input logic clk_b, input logic we0, input logic we1,
           input logic [1:0] wa0, input logic [1:0] wa1, input logic [3:0] wd0,
           input logic [3:0] wd1, input logic [1:0] ra, output logic [3:0] o, output logic [3:0] q);
  ram u(.clk0(clk_a), .clk1(1'b0), .we0(we0), .we1(1'b0), .wa0(wa0), .wa1(wa1), .wd0(wd0), .wd1(wd1), .ra(ra), .o(o));
  always_ff @(posedge clk_b) q <= wd1;
endmodule

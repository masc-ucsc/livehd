// :test: error
// :error: clock input .clk. of .cmtp_sub. is bound to a constant, but it clocks memory .mem. \(port 1\) of .cmtp_sub.
// Ruling 42: a memory port whose enable the call holds at 0 is idle and its
// clock does not matter, but EVERY port on that clock must be idle: here write
// port 0 is tied off and write port 1 still writes on a constant clock.
module cmtp_sub(input clk, input we0, input we1, input [1:0] wa0, input [1:0] wa1, input [7:0] wd0,
                input [7:0] wd1, input [1:0] ra, output [7:0] q);
  reg [7:0] mem[0:3];
  always @(posedge clk) begin
    if (we0) mem[wa0] <= wd0;
    if (we1) mem[wa1] <= wd1;
  end
  assign q = mem[ra];
endmodule

module const_clock_mem_two_ports_reject(input we, input [1:0] wa0, input [1:0] wa1, input [7:0] wd0,
                                        input [7:0] wd1, input [1:0] ra, output [7:0] q);
  cmtp_sub u1(.clk(1'b0), .we0(1'b0), .we1(we), .wa0(wa0), .wa1(wa1), .wd0(wd0), .wd1(wd1), .ra(ra), .q(q));
endmodule

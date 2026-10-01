// Golden for spec_memory_cond_init: instance `xn` holds a memory with no reset,
// `xr` one that resets to 0 in one cycle (and powers on as 0), reset priority over
// the write. Hierarchical like the Pyrope side so LEC pairs the unreset `xn.m`.
module spec_memory_cond_init_n(input clock, input we, input [1:0] a, input [7:0] d,
                               input [1:0] ra, output [7:0] q);
  reg [7:0] m [0:3];
  always @(posedge clock) begin
    if (we) m[a] <= d;
  end
  assign q = m[ra];
endmodule

module spec_memory_cond_init_r(input clock, input reset, input we, input [1:0] a, input [7:0] d,
                               input [1:0] ra, output [7:0] q);
  reg [7:0] m [0:3];
  integer k;
  initial for (k = 0; k < 4; k = k + 1) m[k] = 8'd0;
  always @(posedge clock) begin
    if (reset) begin
      for (k = 0; k < 4; k = k + 1) m[k] <= 8'd0;
    end else if (we) begin
      m[a] <= d;
    end
  end
  assign q = m[ra];
endmodule

module spec_memory_cond_init(input clk, input rst, input we, input [1:0] a, input [7:0] d,
                             input [1:0] ra, output [7:0] qn, output [7:0] qr);
  spec_memory_cond_init_n xn(.clock(clk), .we(we), .a(a), .d(d), .ra(ra), .q(qn));
  spec_memory_cond_init_r xr(.clock(clk), .reset(rst), .we(we), .a(a), .d(d), .ra(ra), .q(qr));
endmodule

module issue_057(a, y);
  input [2:0] a;
  output [3:0] y;

  // this is expected to set y[3:1] = 'b100 for a = 1.
  // but verilator_3_864 sets y[3:1] = 'b000 instead.

  localparam [5:15] p = 51681708;
  assign y = p[15 + a -: 5];
endmodule

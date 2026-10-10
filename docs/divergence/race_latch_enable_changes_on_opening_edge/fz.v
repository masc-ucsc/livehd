module fz(input clock, input reset, input [3:0] a, output [3:0] o0);
  reg t1;          // posedge register: changes on the edge that opens lh
  always @(posedge clock) if (reset) t1 <= 1; else t1 <= ~t1;
  reg [3:0] lh;    // transparent while clock && t1
  always @(*) if (clock && t1) lh = a;
  assign o0 = lh;
endmodule

module selreg(input [1:0] sel, input [7:0] a, input [7:0] b,
              output [7:0] q, input clock, input reset);
  reg [7:0] r;
  always @(posedge clock)
    if (reset) r <= 8'b0;
    else case (sel)
      2'd0:    r <= a;
      2'd1:    r <= b;
      default: r <= r;
    endcase
  assign q = r;
endmodule

// Twin registers (the same clock, enable, reset and D) and the registers fed by them.
module reg_merge(input clk, input rst, input en, input [7:0] d, output [7:0] y1, output [7:0] y2, output [7:0] z1, output [7:0] z2,
                 output [7:0] w);
  reg [7:0] a, b;   // twins
  reg [7:0] c, e;   // twins once a and b have merged
  reg [7:0] f;      // not a twin: another enable
  always @(posedge clk) begin
    if (rst) begin
      a <= 0; b <= 0; c <= 0; e <= 0; f <= 0;
    end else begin
      if (en) begin a <= d; b <= d; c <= a; e <= b; end
      if (!en) f <= d;
    end
  end
  assign y1 = a;
  assign y2 = b;
  assign z1 = c;
  assign z2 = e;
  assign w  = f;
endmodule

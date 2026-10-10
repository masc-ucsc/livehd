module fz(
  input clock,
  input reset,
  input [2:0] i0,
  input [2:0] i1,
  input [11:0] i2,
  output [30:0] o1
);
  wire [4:0] t0 = (i2 % ((i0 / (i2 | 1'b1)) | 1'b1));
  wire signed t1 = (~({i0, i2} ? (i1 == t0) : t0[4]));
  reg [30:0] t2;
  always @(posedge clock) if (reset) t2 <= 15418; else t2 <= (i1[2:0] / (({3{4'd3}} <<< t0) | 1'b1));
  reg [7:0] l0;
  always @(*) if (clock && (reset || ((t0 - t1)))) l0 = reset ? 0 : ((16'b1010011010010111 & i1) % ((t1 + t2) | 1'b1));
  reg [2:0] l1;
  always_latch begin
    if (!clock && reset) l1 = 0;
  end
  reg [8:0] l2;
  always_latch begin
    if (!clock && reset) l2 = 0;
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (4'h2);
  wire gclk0 = clock & gen0;
  reg [7:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 56; else g0_0 <= ((l0 + t2) ^~ (l0 | l2));
  reg signed [16:0] m1;
  reg signed [31:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
    end
  end
  assign o1 = ((i2 - 1'sd0) ? g0_0[3:3] : (^(2'd1 >> 5)));
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output [16:0] y);
endmodule


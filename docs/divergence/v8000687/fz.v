module fz(
  input clock,
  input reset,
  input [6:0] i0,
  input [69:0] i1,
  input signed [3:0] i2,
  input [30:0] i3,
  input signed i4,
  input signed [31:0] i5,
  output [15:0] o0,
  output signed [11:0] o1,
  output signed [1:0] o2,
  output [63:0] o3
);
  wire [6:0] t0 = ((-i1) << i2);
  wire [6:0] t1 = ((({2{i5}} || 16'd26218) & (~^(3'b111 / (i5 | 1'b1)))) ? (i1 - ({i3, i1} + {i3, i4})) : i5);
  reg [7:0] t2;
  always @(posedge clock) begin
    if (reset) t2 <= 219;
    else if (((i2 ? i0[2] : (t1 >>> 0)) <<< 9)) t2 <= ((^(16'hc13c ? i3 : (i0 / (i3 | 1'b1)))) < (((i5 / (i1 | 1'b1)) + (i5 << 0)) ? i5 : t1));
  end
  reg signed [32:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if (i4) mem[i5[1:0]] <= (!12'd1496);
  end
  wire signed [32:0] mrd = mem[i3[1:0]];
  reg [63:0] m1;
  reg [8:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if ((($unsigned(i0) >= (i2 ? i2 : i4)) - 1)) begin m1 <= i5[29]; m2 <= m1; end
      else if ((((i5 >= 1'd0) + {i0[6:4], i2}) ? mrd : (i2 - (2'd3 * 8'd155)))) m2 <= i1;
      else m1 <= m2;
    end
  end
  assign o0 = ({t2, i1, 4'd8} ? i2 : (6'd22 ^ ((t2 - 1'h0) * (t2 + i1))));
  assign o1 = ((((i4 ^~ i3) ^~ {i1, mrd[16:7], i4}) / ((16'd3930 == (i4 ? 4'b1000 : i0)) | 1'b1)) <<< 2);
  assign o2 = ((((i5 | i1) < (i2 <<< i2)) * ((i2 ^ 8) % ((m1 ? i5 : i2) | 1'b1))) <= m2);
  assign o3 = (t2 >>> i4);
endmodule

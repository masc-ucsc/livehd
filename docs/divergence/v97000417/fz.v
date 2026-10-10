module fz(
  input clock,
  input reset,
  input [16:0] i0,
  input signed [64:0] i1,
  input signed [3:0] i2,
  input i3,
  output [8:0] o0,
  output [64:0] o1,
  output [64:0] o2
);
  function automatic [7:0] f0(input signed [7:0] a0, input [4:0] a1);
    f0 = a0;
  endfunction
  reg [8:0] t0;
  always @(*) begin
    t0 = (((-i1) / ((~i3) | 1'b1)) / (((~^3'sd1) * $signed(i0)) | 1'b1));
    for (int k = 0; k < 4; k = k + 1) t0[k] = t0[k] ^ (i1);
  end
  wire [32:0] t1 = ((((i3 & i2) ? (i3 ? 33'sd2298848024 : t0) : (i3 | i0)) >> i2) - i1);
  wire [47:0] t2 = ((i2 ? t0[1] : (!(i3 ? 6'd46 : i0))) ? (i3 * {i2[2:1], i1, i0[14:4]}) : i3);
  reg [31:0] l0;
  always_latch begin
    if (!clock && reset) l0 = 0;
    else if (!clock && (|((i2 != 6) / ((i2 % (i1 | 1'b1)) | 1'b1)))) l0 = {i2, i2[2:2]};
  end
  reg signed [30:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + t1[25];
  reg [7:0] m1;
  reg signed [2:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if (((t0 < (!t0)) ^ t1[7:0])) begin m1 <= i1; m2 <= m1; end
      else if ((6'd15 | ((t1 / (3 | 1'b1)) + i3))) m2 <= i3;
      else m1 <= m2;
    end
  end
  assign o0 = ((t0 % (((t1 != m1) | (fb ^ i1)) | 1'b1)) - $signed(((m1 != i1) ^~ (m1 ^~ fb))));
  assign o1 = (((i3 <<< 5) ? 3'h4 : 3'b011) >> i3);
  assign o2 = ((((i1 / (t0 | 1'b1)) ? (3'sd2 << 8) : (i0 - t0)) ? 7 : $unsigned((|m2))) >= (((fb ? m2 : 2) ** 1) ^~ 7));
endmodule

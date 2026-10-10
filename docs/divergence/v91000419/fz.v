module fz(
  input clock,
  input reset,
  input [7:0] i0,
  input signed [6:0] i1,
  input [1:0] i2,
  input signed [63:0] i3,
  output [30:0] o0,
  output signed [3:0] o1,
  output [47:0] o2
);
  function automatic [3:0] f0(input [15:0] a0, input signed [3:0] a1);
    f0 = a1;
  endfunction
  function automatic [16:0] f1(input [7:0] a0, input [2:0] a1);
    f1 = a0;
  endfunction
  wire signed [64:0] t0 = ((({8'd252, i3, i1[6:4]} ? (i0 ^ 1'd1) : 12'he17) ? ((i3 >> 8) <<< i2) : {5'd24, i1}) << 4);
  reg [63:0] t1;
  always @(*) begin
    t1 = {i2, i1, 2'd1};
    casez ({i1[0], i1[0]})
      2'b1?: t1 = ((-(i3 ? i3 : t0)) ? ((t0 % (3 | 1'b1)) % ((i2 < i2) | 1'b1)) : (~|(t0 ? 12'd10 : 8'd194)));
      2'b01: t1 = i3;
      default: ;
    endcase
  end
  localparam P0 = 6'sd7;
  reg signed [8:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + i3[61:56];
  reg [16:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if (t1[25:25]) mem[i0[1:0]] <= fb;
  end
  wire [16:0] mrd = mem[i3[1:0]];
  reg signed [2:0] m1;
  reg signed [3:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if (((2 - (i1 + t1)) + ((i1 >>> 5) ? (P0 + 33'sd3082930243) : (i1 ? t0 : 33'h1c0c51cd7)))) begin m1 <= (P0 & ({5'd11, 5'd17} != fb)); m2 <= m1; end
      else if (i0) m2 <= t0[34];
      else m1 <= m2;
    end
  end
  assign o0 = (!(m2[P0] + P0));
  assign o1 = ($signed(({1{t1}} - (fb >= i2))) * (i0 && ((i1 * m2) || (6'h2c >> i2))));
  assign o2 = P0;
endmodule

module fz(
  input clock,
  input reset,
  input signed [15:0] i0,
  input [69:0] i1,
  input i2,
  input signed [32:0] i3,
  output [2:0] o0,
  output [11:0] o1,
  output signed [4:0] o2
);
  function automatic signed [2:0] f0(input signed [4:0] a0, input [3:0] a1);
    f0 = ((a0 + (a0 - a1)) | {a0[2:0], a1[3:1]});
  endfunction
  wire [47:0] t0 = (~&(((i2 * i1) | (i2 ? i1 : 6'h38)) << i2));
  reg signed [1:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + ((6'd20 ? (i2 - t0) : i0) ? (i1[63:29] ? (t0 + 8'h2a) : $signed(i3)) : i0);
  reg [6:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if ((i2 <= fb[1:0])) mem[i3[1:0]] <= (2'd1 >>> i2);
  end
  wire [6:0] mrd = mem[i1[1:0]];
  assign o0 = (((~|(i1 & i1)) * $signed((t0 ^~ 2))) >> i2);
  assign o1 = i1[t0[5:0]];
  assign o2 = ((i1 ** 1) / ((^fb) | 1'b1));
endmodule

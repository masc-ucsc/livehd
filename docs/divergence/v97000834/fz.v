module fz(
  input clock,
  input reset,
  input i0,
  input [1:0] i1,
  output [1:0] o0,
  output [3:0] o1
);
  function automatic signed [11:0] f0(input [2:0] a0);
    f0 = ((a0 + a0) - a0);
  endfunction
  reg [1:0] t0;
  always @(*) begin
    t0 = (i0 - (i1 - i0));
    for (int k = 0; k < 2; k = k + 1) t0[k] = t0[k] ^ ((i1 - i0));
  end
  reg [2:0] t1;
  always @(posedge clock) if (reset) t1 <= 3; else t1 <= ((|(t0 >>> 3)) - ((^t0) - (t0 & t0)));
  reg [2:0] l0;
  always @(*) if (!clock && (reset || (|{2{t1}}))) l0 = reset ? 0 : ((t0 + t0) >> i1);
  reg signed [8:0] l1;
  always_latch begin
    if (!clock && reset) l1 = 0;
    else if (!clock && (|(i1 / (t1 | 1'b1)))) l1 = {i0, i0, t1};
  end
  reg [4:0] l2;
  always_latch begin
    if (!clock && reset) l2 = 0;
    else if (!clock && ((i1 ? i0 : i1))) l2 = ((8'sd62 >>> 0) + (^i0));
  end
  localparam P0 = 8;
  reg signed [15:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + l0;
  reg [8:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if ((l1 - (^fb))) mem[t1[1:0]] <= ((l0 ^~ i0) - (l0 ? 6 : l0));
  end
  wire [8:0] mrd = mem[{1'b0, P0}];
  reg [1:0] m1;
  reg [1:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if (P0) begin m1 <= {fb, i1, i0}; m2 <= m1; end
      else if (((l0 ^~ 16'sd966) + (P0 + l2))) m2 <= ((fb >>> 5) / (l1 | 1'b1));
      else m1 <= m2;
    end
  end
  assign o0 = (((fb ^~ i0) + (33'd8400772866 != t1)) ? ((t0 ? mrd : fb) || (l2 | mrd)) : ($unsigned(m1) ? fb[15 - fb[2:0] -: 6] : t0));
  assign o1 = $signed(3'h5);
endmodule

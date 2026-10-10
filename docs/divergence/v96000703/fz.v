module fz(
  input clock,
  input reset,
  input i0,
  input [32:0] i1,
  input signed [30:0] i2,
  input signed i3,
  input signed [63:0] i4,
  input [16:0] i5,
  output signed [31:0] o0,
  output [63:0] o1,
  output [31:0] o2
);
  function automatic [11:0] f0(input [2:0] a0, input [7:0] a1);
    f0 = ((a0[2] ^ a0) & ((~a1) * {3{a0}}));
  endfunction
  function automatic [6:0] f1(input [7:0] a0);
    f1 = a0;
  endfunction
  reg [11:0] t0;
  always @(*) begin
    t0 = {2{i5}};
    if ((~((i0 ^ i1) * $signed(i4)))) t0 = i5[16 - i4[3:0] -: 1];
  end
  wire [64:0] t1 = f1((i5 <= i1[23:20]));
  reg signed [16:0] t2;
  always @(*) begin
    t2 = (i3 + ((i5 & 2) - t1[54:41]));
    case (16'sd23802)
      0: t2 = i5[i2[3:0]];
      2: t2 = (((i5 >>> 7) << 4) ? ($signed(t0) ? t0 : (t0 || i4)) : i5);
      default: ;
    endcase
  end
  reg signed [30:0] l0;
  always @(*) if (!clock && (reset || (((t2 / (t2 | 1'b1)) ? (12'sd946 <<< 5) : (i0 / (5 | 1'b1)))))) l0 = reset ? 0 : (((1 * i1) + t2) ? t0 : $signed((~|12'd3948)));
  reg [3:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + ({2{l0}} < ((~&t0) ? (i4 <= i0) : i5[16:9]));
  reg signed [4:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if (i3) mem[i2[1:0]] <= {t0[4:4], i4[22:19], i4[2:2]};
  end
  wire signed [4:0] mrd = mem[t0[1:0]];
  assign o0 = ((((t0 >= 6'd8) >= {i0, t1}) ? ((2'd2 + i5) | i2) : (i3 ^ (4'sd3 >> 7))) * $unsigned({i5, i1[22:5]}));
  assign o1 = ((((9 - mrd) * $unsigned(mrd)) + ((t1 + i4) ? l0 : f0(i0, i5))) * (i5[t0[3:0]] ^ ((~^fb) ? (t0 || fb) : i3)));
  assign o2 = i2;
endmodule

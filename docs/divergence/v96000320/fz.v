module fz(
  input clock,
  input reset,
  input [47:0] i0,
  input [6:0] i1,
  input [31:0] i2,
  input [15:0] i3,
  output [16:0] o0,
  output signed [4:0] o1,
  output [31:0] o2,
  output [47:0] o3
);
  function automatic signed [32:0] f0(input signed [2:0] a0, input [16:0] a1);
    f0 = (((~a0) >> a0) | (a0 % ((a1 <<< 3) | 1'b1)));
  endfunction
  wire [6:0] t0 = (i1[2:1] ? (i1 ? ((16'd33030 ? 12'd3994 : i0) + (12'hf7d + i3)) : i3) : ($unsigned((i0 + i1)) >>> 2));
  wire signed [11:0] t1 = t0;
  reg [47:0] t2;
  always @(*) begin
    t2 = i3;
    case (t0)
      0: t2 = t0;
      default: ;
    endcase
  end
  reg signed [4:0] t3;
  always @(*) begin
    t3 = i1[6:1];
    casez ({t0[3], t0[5]})
      2'b1?: t3 = i2;
      2'b01: t3 = ((t1[8] >= (33'd7305422679 <<< 2)) + {2{i0}});
      default: ;
    endcase
  end
  reg signed [31:0] t4;
  always @(*) begin
    t4 = (16'd9081 ? $signed((i0 ? t0 : t2)) : ((i0 ? i0 : t3) % (i0 | 1'b1)));
    casez ({i3[10], t3[2]})
      2'b1?: t4 = t0[2];
      2'b01: t4 = f0({t2, i1}, ((t3 & i0) <<< t3));
      default: ;
    endcase
  end
  reg [3:0] t5;
  always @(posedge clock) if (reset) t5 <= 14; else t5 <= (i3 <= i0);
  wire [30:0] t6 = {t2[32:4], t0, t0[6:6]};
  reg l0;
  always_latch begin
    if (!clock && reset) l0 = 0;
    else if (!clock && (t6[7])) l0 = (^i1);
  end
  assign o0 = (i3[11:11] / ((t6[19:18] * ((3'h5 ? l0 : t1) == (12'd3056 ? 1'b1 : t2))) | 1'b1));
  assign o1 = $signed(t0);
  assign o2 = {3{1'd1}};
  assign o3 = (((i3[15 - i0[2:0] -: 5] + {t0[6:5], 6'd50}) ^~ 2'd2) - (t2[i3[4:0]] - t3[3:2]));
endmodule

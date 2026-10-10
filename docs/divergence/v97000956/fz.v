module fz(
  input clock,
  input reset,
  input [11:0] i0,
  input [11:0] i1,
  input signed [15:0] i2,
  output signed [11:0] o0
);
  function automatic [1:0] f0(input [3:0] a0);
    f0 = a0;
  endfunction
  function automatic signed [7:0] f1(input signed [8:0] a0, input [6:0] a1);
    f1 = a1;
  endfunction
  reg [4:0] t0;
  always @(*) begin
    t0 = i0[5];
    for (int k = 0; k < 4; k = k + 1) t0[k] = t0[k] ^ ((i2 / (i1 | 1'b1)));
  end
  wire signed [3:0] t1 = (t0[4:4] / ((i2 - t0[2]) | 1'b1));
  reg [16:0] t2;
  always @(*) begin
    t2 = i0;
    case (i2)
      1: t2 = t1;
      3: t2 = i2;
      default: ;
    endcase
  end
  wire signed [2:0] t3 = (((t0 * t0) / ($signed(t0) | 1'b1)) + i0[1]);
  reg signed [4:0] t4;
  always @(*) begin
    t4 = (t0 * $signed(6'd22));
    case (i2)
      0: t4 = ({t2, i2[15:7], i1} ? t0 : {5'd23, 8'd147, t2[16:16]});
      default: ;
    endcase
  end
  reg [11:0] l0;
  always @(*) if (!clock && (reset || (|t0))) l0 = reset ? 0 : ((t1 - 3'd4) * {i2, 7'd117, t0});
  reg [15:0] l1;
  always @(*) if (clock && (reset || (i2))) l1 = reset ? 0 : (i0[7:6] ? l0 : (i0 + t0));
  reg [3:0] l2;
  always @(*) if (clock && (reset || (|(i2 ? i2 : i1)))) l2 = reset ? 0 : ((&t0) | (t3 < i1));
  localparam [3:0] P0 = 16'sd21813;
  reg signed [16:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + l0[4:0];
  wire [16:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(i0[8:8]), .b(fb[12:7]), .y(sub_o));
  reg signed [31:0] m1;
  reg [6:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if (((l0 + t3) + l0)) begin m1 <= (!(33'sd4121502374 + 4'sd6)); m2 <= m1; end
      else if (((i1 - 8) || (0 & sub_o))) m2 <= fb[t1[3:0]];
      else m1 <= m2;
    end
  end
  assign o0 = m2;
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output [16:0] y);
  assign y = a - b;
endmodule

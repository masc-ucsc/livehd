module fz(
  input clock,
  input reset,
  input [3:0] i0,
  input signed [2:0] i1,
  output signed [31:0] o0
);
  reg signed [6:0] t0;
  always @(*) begin
    t0 = (16'sd9759 << 2);
    if ((6'd43 << 8)) t0 = i1;
  end
  reg signed [15:0] t1;
  always @(*) begin
    t1 = {i0[3:3], i0[2:0], t0};
    case (i0)
      0: t1 = (t0 ^ (t0 <<< 2));
      3: t1 = t0[3];
      default: ;
    endcase
  end
  wire [6:0] t2 = (t0 ^~ t1);
  reg [4:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + (i1 ? $signed(t1) : (i1 == i0));
  reg signed [8:0] m1;
  reg signed m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if (i1) begin m1 <= fb; m2 <= m1; end
      else if (((12'h22c ^ i0) - (t2 ? fb : t0))) m2 <= (&t1[13]);
      else m1 <= m2;
    end
  end
  assign o0 = ({t2, i1, 2'd3} * (i0[2] | m2));
endmodule

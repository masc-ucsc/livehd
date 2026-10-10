module fz(
  input clock,
  input reset,
  input signed [6:0] i0,
  input signed [15:0] i1,
  input [6:0] i2,
  output [30:0] o0,
  output [6:0] o1,
  output o2
);
  wire [4:0] t0 = i2;
  reg [16:0] t1;
  always @(*) begin
    t1 = 16'd58659;
    case (4)
      2: t1 = {1{5'd14}};
      3: t1 = (~^33'd1170928167);
      default: ;
    endcase
  end
  reg [7:0] t2;
  always @(*) begin
    t2 = {1{t1}};
    case (t0)
      0: t2 = ((i2 | i2) * 4'sd3);
      default: ;
    endcase
  end
  localparam [30:0] P0 = 3;
  reg [8:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if (i0[4]) mem[t1[1:0]] <= {i2, t0};
  end
  wire [8:0] mrd = mem[t2[1:0]];
  assign o0 = (9 ^ ((i1 <<< 2) - t1));
  assign o1 = t1;
  assign o2 = (i2[5:2] + ({mrd, i0[4:4]} ? i0 : t0));
endmodule

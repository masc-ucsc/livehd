// Golden for wrap_bitrange_dest.prp: `wrap` keeps the low lane bits, `sat`
// clamps to the lane's maximum.
module wrap_bitrange_dest (
  input  [15:0] v,
  input  [3:0]  r,
  input  [3:0]  x,
  output [15:0] z,
  output [15:0] w,
  output [15:0] s,
  output reg [15:0] d,
  output reg [15:0] t
);
  wire [3:0] r1 = r + 4'd1;
  assign z = {v[15:8], r1, v[3:0]};
  wire [3:0] x0 = x;
  wire [3:0] x1 = x + 4'd1;
  wire [3:0] x2 = x + 4'd2;
  wire [3:0] x3 = x + 4'd3;
  assign w = {x3, x2, x1, x0};
  wire [4:0] rx = r + x;
  wire [3:0] sx = (rx > 5'd15) ? 4'd15 : rx[3:0];
  assign s = {v[15:12], sx, v[7:0]};
  always @* begin
    d    = v;
    d[x] = r1[0];
    t    = v;
    t[x] = (r != 4'd0);
  end
endmodule

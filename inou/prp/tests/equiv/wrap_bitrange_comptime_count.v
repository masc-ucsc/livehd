// Golden for wrap_bitrange_comptime_count.prp: `wrap` keeps the low lane bits,
// `sat` clamps to the lane's maximum.
module wrap_bitrange_comptime_count (
  input  [15:0] v,
  input  [3:0]  r,
  input  [2:0]  x,
  output [15:0] z,
  output [15:0] s,
  output [15:0] y,
  output [15:0] w,
  output [15:0] f,
  output [15:0] g,
  output [15:0] h,
  output [15:0] m
);
  // z: a#[2..+3] = wrap(r + 1)
  wire [4:0] r1 = r + 5'd1;
  assign z = {v[15:5], r1[2:0], v[1:0]};
  // s: c#[4..=6] = sat(r + 5)
  wire [4:0] r5 = r + 5'd5;
  wire [2:0] s3 = (r5 > 5'd7) ? 3'd7 : r5[2:0];
  assign s = {v[15:7], s3, v[3:0]};
  // y: b#[3..<5] = wrap(r + r)
  wire [4:0] rr = r + r;
  assign y = {v[15:5], rr[1:0], v[2:0]};
  // w: d#[1..+4] = wrap(r + 9)
  wire [4:0] r9 = r + 5'd9;
  assign w = {v[15:5], r9[3:0], v[0]};
  // f: e#[..<2] = wrap(r + 3)
  wire [4:0] r3 = r + 5'd3;
  assign f = {v[15:2], r3[1:0]};
  // g: ga#[4..+1], ga#[8..+2], ga#[12..+3] = wrap(r + 13)
  wire [4:0] r13 = r + 5'd13;
  assign g = {v[15], r13[2:0], v[11:10], r13[1:0], v[7:5], r13[0], v[3:0]};
  // h: ha#[2..+3] = wrap(r + 6), 3 = x.[bits]
  wire [4:0] r6 = r + 5'd6;
  assign h = {v[15:5], r6[2:0], v[1:0]};
  // m: ma#[4..+1], ma#[8..+2], ma#[12..+3] = wrap(r + 11) into i bits
  wire [4:0] r11 = r + 5'd11;
  assign m = {v[15], r11[2:0], v[11:10], r11[1:0], v[7:5], r11[0], v[3:0]};
endmodule

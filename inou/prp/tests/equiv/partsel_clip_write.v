// Golden for partsel_clip_write: runtime part-select WRITES whose window runs
// past what it selects from. Verilog writes only the in-range bits of a part
// select (IEEE 1800 11.5.1), and nothing through an out-of-range element index.
//   all: `mem[j][c +: 4]` on an ascending [0:3] x 8 array; c+3 past bit 7 drops
//        the top bits (it used to spill into the neighboring element).
//   six: `arr[k][c -: 4]` on a descending [5:0] x 8 array; k = 6, 7 writes
//        nothing, c < 3 drops y's low bits.
//   w:   `v[b -: 4]` on a plain vector, below bit 0.
//   fl:  `fa[k] = y` on an ascending [0:5] x 4 array; k = 6, 7 writes nothing.
// Checked against iverilog (random differential, 0 mismatches).
module partsel_clip_write (
  input  [1:0]  j,
  input  [2:0]  c,
  input  [2:0]  k,
  input  [3:0]  b,
  input  [3:0]  y,
  input  [7:0]  d,
  input  [15:0] e,
  output [31:0] all,
  output [47:0] six,
  output [15:0] w,
  output [23:0] fl
);
  logic [7:0]  mem [0:3];
  logic [7:0]  arr [5:0];
  logic [15:0] v;
  logic [3:0]  fa  [0:5];
  always_comb begin
    for (int n = 0; n < 4; n++) mem[n] = d;
    mem[j][c +: 4] = y;
    for (int n = 0; n < 6; n++) arr[n] = d;
    arr[k][c -: 4] = y;
    v = e;
    v[b -: 4] = y;
    for (int n = 0; n < 6; n++) fa[n] = d[3:0];
    fa[k] = y;
  end
  assign all = {mem[3], mem[2], mem[1], mem[0]};
  assign six = {arr[5], arr[4], arr[3], arr[2], arr[1], arr[0]};
  assign w   = v;
  assign fl  = {fa[5], fa[4], fa[3], fa[2], fa[1], fa[0]};
endmodule

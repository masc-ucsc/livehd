// Sub-word writes into a MULTI-DIMENSIONAL COMBINATIONAL unpacked array — the
// bedrock-rtl `br_ram_flops` tiling idiom:
//
//   logic [NumWritePorts-1:0][TileWidth-1:0] tile_wr_data[DepthTiles][WidthTiles];
//   assign tile_wr_data[r][c][wport] = decoded_wr_data[r][c];
//
// The write target is one packed lane of one element of a two-dimensional
// array. Every selector is a genvar, and the array is never read on the RHS of
// its own drivers, so this unrolls to a plain sequence of element writes. The
// one-dimensional twin already worked (a 1-D combinational array flattens to a
// packed bus); the multi-dimensional one linearizes to an array instead and
// used to die on "nested non-variable assignment targets are not supported yet".
//
// Pinned here: the full-lane cover (a single write port covering the whole
// element word, which must NOT read the element back), the accumulating partial
// cover (two lanes of one element driven by two separate statements, which
// must compose in program order), a whole-element write (which needs the
// array's poison seed to exist before the first store), and a dynamic in-word
// bit position.
module array2d_partial_write (
    input  logic [31:0] din,
    input  logic  [2:0] pos,
    input  logic  [1:0] sel,
    input  logic        en,
    output logic [31:0] lane_out,
    output logic [15:0] full_out,
    output logic [15:0] whole_out,
    output logic  [7:0] dyn_out,
    output logic  [7:0] arm_out,
    output logic  [7:0] gen_out
);

  // ── two packed lanes per element, one driver each ───────────────────────
  // Element `[1:0][3:0]` = 8 bits; lane 0 and lane 1 are written by separate
  // statements, so each element takes two read-modify-write splices that must
  // accumulate rather than clobber.
  logic [1:0][3:0] lane[2][2];
  for (genvar r = 0; r < 2; r++) begin : gen_lane_r
    for (genvar c = 0; c < 2; c++) begin : gen_lane_c
      assign lane[r][c][0] = din[(r*2+c)*8+0+:4];
      assign lane[r][c][1] = din[(r*2+c)*8+4+:4];
    end
  end
  assign lane_out = {lane[1][1], lane[1][0], lane[0][1], lane[0][0]};

  // ── ONE packed lane per element (NumWritePorts == 1) ────────────────────
  // `[0:0][3:0]` makes `full[r][c][0]` cover the whole element word, which is
  // exactly the shape br_ram_flops instantiates. No read-back is needed.
  logic [0:0][3:0] full[2][2];
  for (genvar r = 0; r < 2; r++) begin : gen_full_r
    for (genvar c = 0; c < 2; c++) begin : gen_full_c
      assign full[r][c][0] = din[(r*2+c)*4+:4];
    end
  end
  assign full_out = {full[1][1], full[1][0], full[0][1], full[0][0]};

  // ── whole-element writes (no sub-word select at all) ────────────────────
  // These already lowered, but only reached tolg once the combinational array
  // carries an initializer for its packed-bus form to splice onto.
  logic [3:0] whole[2][2];
  for (genvar r = 0; r < 2; r++) begin : gen_whole_r
    for (genvar c = 0; c < 2; c++) begin : gen_whole_c
      assign whole[r][c] = din[(r*2+c)*4+:4] ^ 4'hA;
    end
  end
  assign whole_out = {whole[1][1], whole[1][0], whole[0][1], whole[0][0]};

  // ── runtime in-word bit position, constant element index ────────────────
  logic [7:0] dyn[2][1];
  always_comb begin
    dyn[0][0]      = din[7:0];
    dyn[1][0]      = din[15:8];
    dyn[1][0][pos] = din[16];
  end
  assign dyn_out = dyn[0][0] ^ dyn[1][0];

  // ── runtime ELEMENT index, first touched inside an if arm ───────────────
  // A runtime-indexed array is not eligible for the flatten branch's
  // pre-declare rule, so its declare used to land lazily at the first access —
  // here inside the `en` arm, which would leave the packed-bus seed missing on
  // the `else` path. Every multi-dimensional array is now pre-declared instead.
  logic [7:0] arm[2][2];
  always_comb begin
    if (en) begin
      arm[0][0]                = din[7:0];
      arm[0][1]                = din[7:0];
      arm[1][0]                = din[7:0];
      arm[1][1]                = din[7:0];
      arm[sel[1]][sel[0]][3:0] = 4'h5;
    end else begin
      arm[0][0] = din[15:8];
      arm[0][1] = din[15:8];
      arm[1][0] = din[15:8];
      arm[1][1] = din[15:8];
    end
  end
  assign arm_out = arm[0][0] ^ arm[0][1] ^ arm[1][0] ^ arm[1][1];

  // ── the same shape, declared INSIDE a generate block ────────────────────
  // The pre-declare walk descends into generate blocks for multi-dimensional
  // arrays specifically, so a genvar-scoped one gets its seed at module top too.
  for (genvar k = 0; k < 2; k++) begin : gen_scope
    logic [7:0] g[2][2];
    always_comb begin
      if (en) begin
        g[0][0]                = din[7:0] ^ 8'(k);
        g[0][1]                = din[7:0];
        g[1][0]                = din[7:0];
        g[1][1]                = din[7:0];
        g[sel[1]][sel[0]][7:4] = 4'h3;
      end else begin
        g[0][0] = din[15:8];
        g[0][1] = din[15:8];
        g[1][0] = din[15:8];
        g[1][1] = din[15:8];
      end
    end
  end
  assign gen_out = gen_scope[0].g[0][0] ^ gen_scope[1].g[1][1]
                 ^ gen_scope[0].g[1][0] ^ gen_scope[1].g[0][1];

endmodule

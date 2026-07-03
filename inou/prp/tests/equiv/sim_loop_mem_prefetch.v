// Golden (acyclic) form of sim_loop_mem_prefetch.prp. The Pyrope side packs
// `low` and `a<<2` into wire `io` with a Sum and reads `io[3:2]` back — a FALSE
// word-level cycle (bits 3:2 come only from `a`, and `low + (a<<2)` never
// carries because the operand footprints are disjoint), so semantically
// low == a and io == {a, a}. The array read t[low] therefore selects entry `a`
// of the per-cycle array: entry `a` was overwritten with `d` when `en`,
// otherwise it holds its comptime init {10,20,30,40}.
module sim_loop_mem_prefetch(
  input  [1:0] a,
  input        en,
  input  [7:0] d,
  output [7:0] z
);
  wire [3:0] io  = {a, a};   // Sum pack with disjoint footprints == concat
  wire [1:0] low = io[1:0];  // == a
  wire [7:0] base = (low == 2'd0) ? 8'd10
                  : (low == 2'd1) ? 8'd20
                  : (low == 2'd2) ? 8'd30
                  :                 8'd40;
  assign z = en ? d : base;  // write forwards to the same-cycle read (low == a)
endmodule

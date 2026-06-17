// HierarchicalValue — reading a signal from another generate-block instance
// through a hierarchical path.
//
// soomrv idiom (PopCnt / PriorityEncoder reduction trees): a generate-for loop
// builds one block per stage, and each stage reads the previous stage's signal
// via an indexed hierarchical reference, e.g. soomrv's
//     assign iSums[...] = tree[i-1].iSums[...] + tree[i-1].iSums[...];
// followed by a final `assign res = tree[NUM_STAGES].iSums;`.
//
// Here a 4-bit population count is computed as a running sum across the
// `stage[i]` generate instances: each `stage[i].acc` reads `stage[i-1].acc`
// (a HierarchicalValue), and the output reads `stage[3].acc`.
//
// Status: SUPPORTED. Every reader ingests this file, so the prp-v2prp test runs
// the full dual-reader LEC (native-slang Pyrope vs the .v read by yosys-slang):
//   standalone `slang` (v11)          -> OK (0 errors)
//   `yosys2 -m slang.so` (read_slang) -> OK (compiles hval_popcnt)
//   `lhd --reader yosys-slang`        -> OK (emits Verilog)
//   `lhd --reader slang` (native)     -> OK (HierarchicalValue read as a
//                                        ValueExpressionBase; see slang_expr.cpp)
module hval_popcnt (
  input  logic [3:0] in,
  output logic [2:0] cnt
);
  generate
    for (genvar i = 0; i < 4; i = i + 1) begin : stage
      logic [2:0] acc;
      if (i == 0)
        assign acc = {2'b0, in[0]};
      else
        assign acc = stage[i-1].acc + {2'b0, in[i]};  // HierarchicalValue read
    end
  endgenerate
  assign cnt = stage[3].acc;                          // HierarchicalValue read
endmodule

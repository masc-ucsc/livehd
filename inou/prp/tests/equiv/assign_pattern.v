// SimpleAssignmentPattern as an assignment target (lvalue).
//
// soomrv idiom (IntALU / StoreDataIQ / IssueQueue): a child module exposes an
// unpacked-array output port and the parent wires it with a positional '{...}
// assignment pattern, letting the port type supply the pattern's context, e.g.
//     PriorityEncoder lzc(.OUT_idx('{resLzTz}), .OUT_idxValid('{resLzTzValid}));
// The '{...} on an output-port connection is a SimpleAssignmentPattern lvalue.
//
// This file covers both the 1-element ('{res_idx}, '{res_vld}) and multi-element
// ('{res_lo, res_hi}) forms. The output-port connection is the only SV-legal
// '{...} lvalue: scalar-target forms like `'{x,y} = ...` are rejected by the
// slang FRONTEND ("assignment pattern target type cannot be deduced") because
// nothing supplies the pattern's type context.
//
// Status: SUPPORTED by the native reader; slang-only test. The native slang
// reader now lowers the '{...} lvalue (assign_to_pattern in slang_lvalue.cpp,
// decomposing the flattened child-output bus per array element). yosys-slang's
// read_slang still cannot ingest the '{...} lvalue, so no yosys reference can be
// built and the prp-v2prp harness runs this as a slang-only test (the native
// compile is the gate). Equivalence was verified at implementation time by
// LEC-ing the native-slang-emitted Verilog against a hand-written flattened
// golden (PROVEN equivalent).
//   standalone `slang` (v11)          -> OK (0 errors)  <- valid SV
//   `lhd --reader slang` (native)     -> OK (lowers the '{...} lvalue)
//   `yosys2 -m slang.so` (read_slang) -> FAIL: "unsupported assignment target expression"
//   `lhd --reader yosys-slang`        -> FAIL (same plugin lowering)
module apat_top (
  input  logic [3:0] data,
  output logic [1:0] res_idx,
  output logic       res_vld,
  output logic [1:0] res_lo,
  output logic [1:0] res_hi
);
  apat_enc enc (
    .in  (data),
    .idx ('{res_idx}),
    .vld ('{res_vld})
  );
  apat_split sp (
    .in  (data),
    .out ('{res_lo, res_hi})
  );
endmodule

// 1-element unpacked-array output ports (PriorityEncoder N=1 shape).
module apat_enc (
  input  logic [3:0] in,
  output logic [1:0] idx [0:0],
  output logic       vld [0:0]
);
  always_comb begin
    idx[0] = in[0] ? 2'd0 : in[1] ? 2'd1 : in[2] ? 2'd2 : 2'd3;
    vld[0] = |in;
  end
endmodule

// 2-element unpacked-array output port (multi-element '{lo, hi} lvalue).
module apat_split (
  input  logic [3:0] in,
  output logic [1:0] out [0:1]
);
  assign out[0] = in[1:0];
  assign out[1] = in[3:2];
endmodule

// Golden for instance_out_struct_ident — KNOWN-FAILING tracker for the
// instance-OUTPUT struct dual identity (the ExeUnitImp_4/VSetRiWvf category,
// and the root cause of the whole XiangShan fp-convert fail family: INT2FP's
// `minusExp = {.., ~_leadZerosNext_lzc_io_out.data}` reads the Lzc instance's
// struct output by FIELD while the output binding stores the FLAT name —
// CVT64 refuted by BOTH cvc5 and lgcheck, expReg 77 vs 15; FPCVT/FCVT/
// FP_INCVT/CVT32ModuleS0/ByteMaskTailGen SIM-diverge the same way).
//
// slang's lower_instance binds `.out(_lzc_out)` as a flat store; every read
// of a scalar-struct-var resolves through its per-field leaves. The in-memory
// LNAST lowers right (tolg slices the flat store; the regen .v proves vs the
// original), but the emitted .prp TEXT carries disconnected `mut x.f = 0`
// leaves + the flat store, so the RECOMPILED Pyrope reads 0-init leaves.
// Fix order (comment in lower_instance's output loop): first make tolg/cgen
// lower the instance-output wire-leaf FEEDBACK shape correctly, then re-apply
// the per-field output-binding split. Functionally: r = {in==0, ~lzc(in)}.
module icsi_lzc8(
  input  [7:0] in,
  output struct packed {logic [2:0] data; logic isZero; } out
);
  assign out = in[7] ? '{data: 3'h0, isZero: 1'h0}
             : in[6] ? '{data: 3'h1, isZero: 1'h0}
             : in[5] ? '{data: 3'h2, isZero: 1'h0}
             : '{data: 3'h7, isZero: (in == 8'h0)};
endmodule

module \instance_out_struct_ident.top (
  input  [7:0] a,
  output [3:0] r
);
  wire struct packed {logic [2:0] data; logic isZero; } _lzc_out;
  icsi_lzc8 u_lzc(.in(a), .out(_lzc_out));
  assign r = {_lzc_out.isZero, ~_lzc_out.data};
endmodule

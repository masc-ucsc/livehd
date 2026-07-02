// Golden for struct_selfref_pattern: CIRCT's `_out_output` idiom — a struct
// net whole-assigned by a '{...}' pattern whose LATER elements read EARLIER
// fields of the same net (privState from inputs; status computed FROM
// _out.privState), and the net is whole-copied to the output port. The fields
// are pairwise acyclic; only the whole-net granularity is cyclic. Lowered as a
// FLAT bus this is a false combinational loop (the Type C family: cgen.sim
// "combinational loop the single-pass sim schedule cannot order", cvc5 "no
// encodable driver" inconclusives). The slang reader must keep such a net a
// per-field bundle: the pattern splits one element per leaf, deep reads route
// through the covering leaf, and the whole copy reassembles from the leaves.
// Functionally: privState = {V:in_v, PRVM:in_prv}; status.SDT = ~V & in_sdt;
// status.MPRV = (&PRVM) & in_mprv; out = {privState, status}.
module \struct_selfref_pattern.top (
  input        in_v,
  input  [1:0] in_prv,
  input        in_sdt,
  input        in_mprv,
  output [4:0] out
);
  wire struct packed {
    struct packed {logic V; logic [1:0] PRVM; } privState;
    struct packed {logic SDT; logic MPRV; } status;
  } _out_output;
  wire isModeM = &_out_output.privState.PRVM;
  assign _out_output =
    '{privState: '{V: in_v, PRVM: in_prv},
      status: '{SDT: (~_out_output.privState.V & in_sdt),
                MPRV: (isModeM & in_mprv)}};
  assign out = _out_output;
endmodule

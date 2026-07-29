// FIXME tracker -- latch contract rule A (self-timed gate).
// `always @(d) if (d != state) state = d;` infers a level-sensitive latch whose
// ENABLE cone reads the latch's own Q, so graph/latch_contract.cpp rejects it: the
// commit-at-closing-edge model has no meaning when the gate depends on the value
// being committed. Unlike rule B above this is a REAL violation of the model's
// precondition, so the fix is a better latch model (or an explicit refusal that
// does not block the whole module), not a checker relaxation.
module latch_rule_a (
  input  wire [1:0] d,
  output wire [1:0] q
);

reg [1:0] state;

always @(d)
  if (d != state)
    state = d;

assign q = state;

endmodule

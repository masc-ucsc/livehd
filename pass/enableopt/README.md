# State-context optimization

`pass.enableopt` runs once between the first bitwidth and second cprop in the
normal compile pipeline. It owns existing flop/latch hold removal, simplification
of private data under enable conditions, reset/hold recognition, and stateful
mux sharing. It does not run cprop, a private fixed point, or width inference.
Memory-port and clock-edge transformations require separate semantic rules.

A forward sweep computes structural Boolean identities, Boolean producer facts,
and conservative state independence. Shared enable clauses are decoded once.
Private destination data regions use sparse branch facts and rollback; unknown
shared boundaries stop specialization. The remaining dependency query caches
private-region results, and unknown never means absence of feedback. Always-open
latch removal requires a proof that D cannot depend on Q.

The second cprop normalizes new expressions and deletes dead producers; the
second bitwidth re-infers annotations. Initialization, reset polarity/priority,
clock behavior and pipeline depth are preserved. Feedback-to-enable extraction
is restricted to private, single-stage flop regions.

Validation: `bazel test -c dbg //pass/enableopt:enableopt_test
//pass/cprop:cprop_test //pass/lec:query_test` (one command). The tests include
2048-deep private hold chains, shared enable clauses across 1024 registers,
pipeline restrictions, and transition equivalence.

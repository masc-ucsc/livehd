# Synthesis integration fixtures

Run `bazel test -c opt //inou/prp:integration` for the combined writer, synthesis,
and LEC fixture suites. Each `.prp`, `.v`, or `.sv` source here automatically
adds `//inou/prp:prp-synth-<stem>`; adding a design needs no runner or BUILD edit.
Small `.json` profiles reuse existing corpus inputs without copying the HDL.
The Liberty models are local test data. Synthesis tests run exclusively.

The shared `../integration.py synth FILE` flow compiles the source, synthesizes
its graph, emits a nonempty mapped netlist, generates behavioral cell models,
and compares the mapped graph with the original. A sibling `<stem>_ref.v`
provides a handwritten reference instead. The emitted mapped Verilog is re-read
and checked by a second LEC run against the same reference. Reference sidecars
(`<stem>_ref.v`) do not become separate synthesis targets.

Optional `:key: value` source-header fields (JSON profiles use the same keys):

| Key | Meaning |
| --- | --- |
| `top` | Top entity; defaults to filename stem or the Pyrope `pyrope_top` header |
| `source` | JSON profiles only: existing input relative to this directory |
| `compile_set` | Space-separated compile `key=value` options |
| `synth_set` | Space-separated synthesis options, such as `pass.abc.memory=true` |
| `libs` | Local Liberty stems; defaults to `test`, optionally `test test_qn` |
| `readers` | Compile reader names; defaults to the normal reader |
| `ref_top` | Handwritten reference top; defaults to `reference` |
| `lec_set` | LEC options, such as `formal.bound=2` or `pass.satopt=true` |
| `lec_may_timeout` | `true` downgrades both LEC runs to the sanity budget below |

Post-synthesis equivalence is the whole claim of a fixture, so both LEC runs are
strict: a 20-second internal budget, a 40-second outer watchdog, and anything
but a proof fails. A fixture that genuinely cannot be decided sets
`lec_may_timeout: true`, which makes each run a sanity check (five seconds
internally, ten for the whole process) where only an explicit internal timeout
is accepted as **inconclusive**. Refutations, unsupported operations, crashes,
missing verdicts, and external watchdog overruns always fail. The small
equivalence fixtures under `../equiv/lec/` require a definitive expected
verdict instead.

This integration deliberately replaces per-design temporary-name greps, exact
mapped gate/flop counts, and repeated equivalent representations. Dedicated
mapping-budget, cache, and QoR tests remain for those distinct behaviors.

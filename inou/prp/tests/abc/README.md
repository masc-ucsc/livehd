# Synthesis integration fixtures

Run `bazel test -c opt //inou/prp:integration` for the combined writer, synthesis,
and LEC fixture suites. Each `.prp`, `.v`, or `.sv` source here automatically
adds `//inou/prp:prp-synth-<stem>`; adding a design needs no runner or BUILD edit.
Small `.json` profiles reuse existing corpus inputs without copying the HDL.
The Liberty models are local test data. Synthesis tests run exclusively.

The shared `../integration.py synth FILE` flow compiles the source, synthesizes
its graph, emits a nonempty mapped netlist, generates behavioral cell models,
and compares the mapped graph with the original. A sibling `<stem>_ref.v`
provides a handwritten reference instead. A sibling `<stem>_tb.v` is also
compiled and executed with Icarus; missing tools and failed assertions fail.
Reference/testbench sidecars do not become separate synthesis targets.

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
| `lec_set` | LEC options, such as `formal.bound=2` |

Synthesis LEC is a sanity check: five seconds internally, ten seconds for the
whole process including setup/loading. Only an explicit internal timeout is
accepted as **inconclusive**. Refutations, unsupported operations, crashes,
missing verdicts, and external watchdog overruns fail. The small equivalence
fixtures under `../equiv/lec/` require a definitive expected verdict instead.

This integration deliberately replaces per-design temporary-name greps, exact
mapped gate/flop counts, and repeated equivalent representations. Dedicated
mapping-budget, cache, and QoR tests remain for those distinct behaviors.

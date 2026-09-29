# LEC correctness and soundness fixtures

`name.v` plus `name_1.v`, `name_2.v`, ... is a group. The same convention works
for `.prp` and `.sv`. Each numbered variant automatically adds `//inou/prp:lec-<stem>`
and joins `//inou/prp:integration`. Both sides use the same language extension.
A comment-only variant compares the base source with an independent elaboration
of itself. Use the existing `../../prplec.py` harness rather than writing a new script.

```
python3 inou/prp/tests/prplec.py inou/prp/tests/equiv/lec/gold_x.v
bazel test -c dbg //inou/prp:lec-gold_x_2
```

Header fields use the existing equivalence convention, inside `/* ... */`:

```
:lec_top: top
:lec_expect: refuted
:lec_set: formal.engine=bmc formal.bound=2
```

The base supplies the top and shared LEC options; variants add or override
options. `lec_expect` defaults to `proven`; `refuted` requires the dedicated
refutation exit code and actual final verdict. `lec_sweep` and compile `set`
headers work as in the parent corpus. `lec_collapse` names modules to collapse
in both sides, for the explicit abstraction soundness cases.

Proofs have a 20-second internal budget and a 40-second outer watchdog.
Timeout, unsupported, missing-verdict, crash, and mismatched-verdict results
fail. Progress and counterexample-artifact messages following the verdict do
not replace it. Keep both positive and negative controls for soundness changes.

Bazel reserves four CPUs per shared formal fixture to keep simultaneous
frontend/solver jobs from exhausting the wall-clock proof budget. This limits
concurrency without extending or bypassing either timeout.

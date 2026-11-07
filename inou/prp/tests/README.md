# Frontend language tests

The language corpus uses the repository-wide [frontend/backend regression
policy](../../../tools/regression.md). About 10% of important frontend tests
always run, alongside the backend smoke set. The complete language corpus is
part of the frontend selection:

```sh
bazel test -c opt --config=frontend //inou/prp:all
bazel test -c dbg --config=all //...
python3 scripts/regression.py --frontend -- -c opt
```

Language, uPass, LGraph/IR, and simulation checks belong to the frontend.
`prp-synth-*` tests belong to both groups, since the same synthesis code is
used before placement and for post-placement resynthesis. Both groups retain
important always-on cases regardless of which complete group is selected.
`tools/regression.bzl` defines the ownership and smoke selection; adding a
fixture preserves existing tests' membership.

Paired behavioral HDL belongs in `equiv/` beside its independent Pyrope model.
HDL-only parser cases live in `../../slang/tests/reader/`, retaining their
existing Slang ladder tiers. The duplicate generic Yosys compile sweep and its
driver were removed. Dedicated importer cases still compare operand extension,
packed byte writes, and mixed read latency through the shared `yvr_test.py`
harness, requiring native LEC and the independent Yosys text oracle. Solver,
clock-domain, reset, and initialization soundness tests remain in `inou/yosys`.

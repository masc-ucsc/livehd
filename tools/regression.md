# Regression groups

`bazel test --config=all -c opt //...` runs both complete test groups.
`--config=frontend` covers language readers, IR, simulation and logic passes;
`--config=backend` covers physical data, placement, timing and routing.
Synthesis and shared driver tests belong to both groups. Each selection retains
both representative smoke sets. Plain `bazel test //...` runs those smoke sets.
The wrappers in `tools/regression.bzl` preserve existing test attributes and tags,
add domain tags, and select named integration checks plus a stable 10% sample
as smoke tests. They never change test timeouts or resource requirements.

`python3 scripts/regression.py -- -c opt` selects groups from staged, unstaged
and untracked changes. CI uses `--base REV --head HEAD`. Deleted paths and both
sides of renames count. Shared/unknown paths, missing history and an empty change
list select both groups. Use `--all`, `--frontend` or `--backend` for explicit
selection; `--dry-run` prints the command without executing it.

Optional independent simulator checks require iverilog, vvp, Verilator and ninja:

```sh
bazel test --config=all -c opt --test_env=LHD_EXTERNAL_SIM=1 --test_env=PATH //...
```

Keep Bazel's CPU/memory scheduling automatic and inspect test runtimes. The
300-second timeout is a hang guard, not a performance target. See `AGENTS.md`
for coverage, contract and simulator requirements.

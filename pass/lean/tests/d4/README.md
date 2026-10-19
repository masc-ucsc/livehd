# Direction 4 runtime-certificate sweep

The sweep driver is `scripts/run_d4_runtime_sweep.sh`.  It runs
`RTL --lhd--> <mod>.dcert --CertIO.loadCert--> runChecked` per block and never
elaborates a generated `_Lgraph.lean`.  Everything resolves relative to the
worktree the script lives in; required inputs and tools are validated up front
and the run fails closed rather than reporting a short corpus.

## Running a sweep

    scripts/run_d4_runtime_sweep.sh <coreet|cva6> <module-list-file> <outdir> [jobs]

Environment (all optional, all validated when set):

| variable | meaning |
|---|---|
| `LAKE` | lake executable (default `$HOME/.elan/bin/lake`, else `PATH`) |
| `COREET_ROOT` | CORE-ET RTL corpus, external to this repository |
| `CVA6_FILELIST` | flat filelist for CVA6 (required for `cva6`) |
| `CVA6_WRAP_DIR` | `<mod>_gate.sv` wrappers (required for `cva6`) |
| `EMIT_TIMEOUT` | per-block emission timeout, seconds (default 1800) |
| `RUN_TIMEOUT` | per-block runtime timeout, seconds (default 900) |

Each block produces one machine-readable row.  Every block in the module list
gets a row, including blocks that never emitted a certificate, so a summary is
always over the full list.

## After a sweep

| script | use |
|---|---|
| `scripts/classify_d4_rows.sh <rows.tsv> [provenance.txt]` | one line per outcome, by the last stage each block reached |
| `scripts/verify_d4_run.sh <run-dir> <module-list>` | read-only reconciliation of a run against its module list, cgroup records and certificates |
| `scripts/replay_d4_certs.sh` | re-run retained certificates through the runtime path with the certificates held fixed (refuses a certificate whose SHA-256 does not match the recorded table) |

## Module lists

| list | contents |
|---|---|
| `coreet_122.list` | CORE-ET blocks, generated from `pass/lean/SWEEP_b1-b2.tsv` column 1 (`tail -n +2 pass/lean/SWEEP_b1-b2.tsv \| cut -f1 \| sort`) |
| `cva6_78.list` | the CVA6 core hierarchy reachable from module `cva6` |
| `cva6_30.list` | the canonical 30 CVA6 blocks (`pass/lean/manifests/cva6_30_frozen.tsv`) |
| `pilot_*.list`, `*_tensor_load.list`, `cva6_unpacked_bridge.list`, `coreet_emission_fanout.list` | small subsets used by focused runs |

## Recorded results

Recorded runs are kept here as `<run>_rows.tsv` (one row per block),
`<run>_provenance.txt` (inputs, binaries and digests), `<run>_gates.tsv` and
`<run>_cgroup_records.tsv`.  Re-check any of them with
`scripts/verify_d4_run.sh` or `scripts/classify_d4_rows.sh`.

## Controls

The `*_controls.sh` scripts in this directory are self-checks of the driver,
the gates and the evidence tooling; run each with `bash`.  The
`emission_fanout_test` target is registered in `BUILD`:

    bazel test //pass/lean/tests/d4:emission_fanout_test

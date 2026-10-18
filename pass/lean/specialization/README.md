# pass/lean/specialization -- the Futamura-projection (FP) simulator

Files here belong to the FP work only: specializing the hardware interpreter
against one exported DesignCert to get a checked residual simulator. The shared
certificate exporter (`pass/lean/*.cpp`), the coverage harness that every
approach uses (`pass/lean/scripts/fp_cov_run.sh`, the `fp_cov_queue_*.sh`
queues, `fp_cov_ir_check.sh`, the CORE-ET input adapters) and the Lean
semantics stay where they were.

The Lean side lives in `formal/lean/LeanSemanticPrimitives/Projection/`
(specializer, its correctness proof, the memory route, `Audit`/`AxiomGate`) and
the drivers in `formal/lean/scripts/` (`gen_probe_v2`, `gen_readiness_v2`,
`dcert2_memroute`, `dcert2_support`, `dcert2_loadcheck`, `cert_runner_any`).

## Documents

| file | content |
|---|---|
| `FUTAMURA_PROJECTIONS.md` | design report: first/second projection over the LiveHD-to-Lean flow |
| `FP_PROJECTION_GAP_AUDIT.md` | gaps against the projection branch, part B (closed by M1) and parts C/D |

## Runner and measurement scripts

| file | role |
|---|---|
| `fp_cov_dcert2.sh` | per-certificate FP runner: decode, support, generate, readiness, memroute phases; immutable attempt dirs, identity-bound reuse, memory ceiling, no wall cap |
| `fp_cov_dcert2_reuse_test.sh` | fixture check of the runner's identity/reuse/attempt rules (extracts the live functions) |
| `fp_gen_build.sh`, `fp_gen_sample.sh`, `fp_gen_measure.sh`, `fp_gen_counterbalanced.sh` | generation-latency builds and counterbalanced measurements |
| `fp_install.py` | installs the projection-branch probes onto master (`tests/fp_install_map.tsv`) |
| `fp_replay.py` | replays historical FP rows into a fresh outdir |

## tests/

Evidence ledgers (`*.tsv`, `*.jsonl`, `fp_generation_only.md`) and controls:

| control | run |
|---|---|
| `axiom_gate_controls.sh` | `bash pass/lean/specialization/tests/axiom_gate_controls.sh` |
| `fp_replay_controls.py` | `python pass/lean/specialization/tests/fp_replay_controls.py` (project venv) |
| `fp_csimp_checks.lean`, `fp_sext_controls.lean`, `fp_dcert1_admission_controls.lean` | `lean` from `formal/lean` |
| `fp_cov_dcert2_reuse_test.sh` (one level up) | `bash pass/lean/specialization/fp_cov_dcert2_reuse_test.sh` |

Historical records written before the move (`PORT_LEDGER_D3_D4_FP.md`, the
`tests/MASTER_*_PRESERVATION.json` audits) keep the old `pass/lean/scripts/fp_*`
and `pass/lean/tests/fp/` paths as they were recorded.

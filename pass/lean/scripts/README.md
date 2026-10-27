# pass/lean/scripts — the parts no single route owns

Every route's own scripts live with that route: `Legacy_Fast_Model_Simulator/`,
`Lgraph_IR_Semantics_Simulator/`, `Translation_Validation/` and
`specialization/` each keep a `scripts/` and a `tests/`. Checks on the exporter
live with the part they check, in `Exporter/Frontend/scripts/` and
`Exporter/DesignCert/scripts/`.

What is left here is what more than one of them runs. Nothing in this directory
belongs to a single route; if something ends up that way it should move out.

## Gates on an emitted file

Each takes a generated `.lean` (or an audit log) and exits non-zero on refusal,
so any route can put it in a pipeline.

| file | what it decides | called by |
|---|---|---|
| `op_census.py` | every `(op, arity)` in the file is one the bridge covers | `Exporter/DesignCert/scripts/vc_gates.py`, the IR-as-semantics CORE-ET runner, `scripts/run_cva6_alu_lean.sh`, `scripts/run_cva6_module_lean_stress.sh` |
| `const_parity.py` | every node's constants fit their declared width | the same four |
| `axiom_audit.py` | an audit log's axiom list matches an allow-list, so a green run cannot rest on `sorryAx` | `scripts/run_lean_queue.sh`, `scripts/replay_orchestrator.py`, `scripts/reaudit_*`, and the legacy corpus replay |
| `empty_cert_gate_controls.sh` | adversarial controls for `op_census.py` | run by hand |
| `axiom_audit_controls.sh` | adversarial controls for `axiom_audit.py`; takes a verified-compiler log and optionally a legacy one | run by hand |

`axiom_audit.py` stays here, rather than moving with any one caller, because
the legacy replay computes its path and the repo-level queue calls it directly.

## The 12x5 coverage harness

One cell of the coverage table is one approach's generator on one benchmark
row. The queues take the approach as an argument, so the same script drives
more than one route; `specialization/README.md` calls this "the coverage
harness shared by every approach".

| file | role |
|---|---|
| `fp_cov_run.sh` | runs one cell under a cgroup, recording sampled memory and the true producer exit |
| `fp_cov_queue_cva6.sh` | serial queue, CVA6 rows; takes `legacy` or `verified_compiler` |
| `fp_cov_queue_coreet.sh` | serial queue, CORE-ET rows; takes `legacy` or `verified_compiler` |
| `fp_cov_ir_check.sh` | the IR-as-semantics cell: elaborate the certificate, the main module, then run the launcher |
| `coreet_icache_input.py`, `coreet_icache_profile.json` | CORE-ET input adapter for the icache rows, used by the CORE-ET queue |
| `RETIRED_fp_cov_queue.sh` | refuses and names its replacements; kept so the defect it records stays findable |

**The `fp_` prefix is historical.** It reads as "Futamura projection", and the
harness did grow out of that work, but none of these scripts is specific to it:
the queues cannot run the Futamura route at all. Read `fp_cov` as "coverage
table cell".

## Translation Validation's compiler build

| file | role |
|---|---|
| `private_compiler_build.sh` | builds the fourteen compiler modules by explicit name, which a default `lake build` does not cover; step 3 of `Translation_Validation/README.md` |
| `pcb_importer_guard_test.sh` | guards the importer that build relies on; its fixture is in `specialization/tests/` |

These two are documented by the route that runs them. They sit here because
both Translation Validation and the Futamura projection reference them.

## Adding something here

Put it in your own route's folder first. Move it here only once a second route
calls it, and add a row above naming both callers — the point of this file is
that ownership can be read off it without grepping.

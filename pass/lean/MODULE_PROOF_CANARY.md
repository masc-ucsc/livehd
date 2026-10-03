# Module-proof backend: canary specification

Written BEFORE the implementation so the acceptance criteria are not adjusted
to whatever the first run happens to produce.

## Why a second backend at all

Proving every chunk fact of a large design in one process dies in the axiom
audit: `collectAxioms` forces the kernel to check what elaboration only
dispatched. Measured on `instr_queue_gate` (1,186 bindings): killed at
14,067,700 kB with the markers stopping at `audit_start`, after two separate
proof-term reductions (`simp` to `rfl`, quadratic to linear) had driven
elaboration to ~5 s without moving the wall.

Auditing an IMPORTED theorem instead costs ~7 MB, because its body was checked
when its module was built (2,027,928 kB against a 2,020,500 kB bare import
floor). So the chunk facts are built as modules and a composition imports them.
That path proved `instr_queue_gate`: gate line
`D3GATE proof=1 thm=d3_fast.correct axioms=[propext, Classical.choice, Quot.sound]`,
composition 14,707,988 kB max RSS / 13,039,784 kB cgroup charge / 10:17.48,
groups 2.27-3.03 GB each.

## What the canary must establish

Two designs, `txfma_e6` (133 bindings, small) and `instr_queue_gate` (1,186,
the one the in-process backend cannot do), through the SWEEP rather than by
hand, producing rows that are reproducible.

### Row fields -- agreement and proof stay separate

A sampled agreement and a kernel proof are different claims and must never be
collapsed into one column.

  * `agree`            unchanged: sampled fast-vs-certificate agreement
  * `requested_samples` unchanged (the schema's name; there is no
                       `samples` column)
  * `module_proof`     1 only when the gate line matched exactly, the final
                       command exited 0, and the parsed axioms were inside
                       {propext, Classical.choice, Quot.sound}
  * `module_proof_rss_kb`, `module_proof_wall_s`
  * `module_cgroup_peak_kb`   charge, NOT comparable with RSS
  * `module_groups`, `module_chunk_size`, `module_chunks`
  * `module_cert_sha256`, `module_runner_digest`
  * `module_olean_digest`     the BUILT artifacts imported: the compiled
                       Compiler library plus this design's generated oleans,
                       snapshotted BEFORE and AFTER the composition. A
                       difference aborts the credit -- `.lake` is shared, so
                       another worktree rebuilding it changes what an import
                       means while every source digest stays put.
  * `module_log_dir`   where the per-group logs and transcript live

A timed-out or killed row is `module_proof=0` and is UNDECIDED. It is never
reported as proved, and `verdict` is not raised by it.

### Acceptance

1. Both designs produce `module_proof=1` with the exact gate line.
2. Both ALSO produce their usual `agree` row from the executable stage, in the
   same run, recorded independently of the proof.
3. Re-running reproduces both rows: same digests, same `module_proof`, with
   RSS and wall allowed to vary.
4. Resume and merge refuse to mix rows whose cert sha, runner digest, olean
   digest or proof mode differ.
5. No row from before this backend existed is merged with a canary row.

### Limits

jobs=1; group builds <=10 GB cgroup; composition <=14 GB cgroup and <20 GB
process RSS; project-local TMPDIR/TMP/TEMP and out-dir, enforced by the runner.

## Scope

Nothing here is coverage. The existing in-process `--prove` backend is
unchanged and remains the default. Broad 122/30 is deferred until the canary
rows above are reproducible, and a passing canary is evidence about two
designs, not about a cohort.

## Canary result

Run through the sweep at 32 samples, jobs=1, group builds <=10 GB cgroup,
composition <=14 GB cgroup.

    design              agree  requested_samples  module_proof  rss_kb      wall_s
    txfma_e6              1          32                1         2,275,404   12.49
    instr_queue_gate      1          32                1        13,961,652  616.45

    instr_queue_gate: groups 10, chunks 38, cgroup peak 12,254,544 kB

1. Both `module_proof=1`, each from an exact gate line with rc 0, axioms inside
   the allowed set, and no olean drift.  PASS
2. Both also produced their usual `agree` row from the executable stage in the
   same run, recorded in separate fields.  PASS
3. Re-run reproduces both. Identical: agree, requested_samples, module_proof,
   module_groups, module_chunks, module_cert_sha256, module_runner_digest,
   module_olean_digest. Varying: RSS, wall, cgroup peak, log dir -- instr_queue
   13,961,652 -> 14,092,372 kB and 616.45 -> 777.46 s.  PASS
4. Two tables differing ONLY in `proof_backend` are refused, exit 2.
   `proof_backend`, `chunk_size`, `proof_segment_size`, `phase_split` and
   `phase_only` were added to SEMANTIC_KEYS, which previously carried `prove`
   and `reifier` alone.  PASS
5. Tables written before this schema lack the `module_*` columns and are
   refused by resume, so no pre-backend row can join a canary row.  PASS

What this is NOT: coverage. Two designs proved is evidence about two designs.
The in-process backend remains the default and is unchanged.

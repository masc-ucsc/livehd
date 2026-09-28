# Legacy refactor: CI scope and merge-readiness audit

PR: [#624](https://github.com/masc-ucsc/livehd/pull/624).
Base: `1af4e1492` (`b1-b2-verified-compiler`).
Last head before the unrelated functional fixes: `70ba1aed4`.

## Why unrelated code appears in these checks

The workflow runs `bazel test -c dbg //...` and repository-wide coverage. These
commands exercise all selected packages, including native compilation, simulation,
and SMT equivalence checking; they are not restricted to files changed by the PR.

The [base-branch run](https://github.com/masc-ucsc/livehd/actions/runs/36300337104)
failed to fetch the pinned `yosys-slang` archive because its checksum changed
after an upstream repository rename. The debug job ran no tests. Coverage ran
42 passing tests but could not load the frontend-dependent targets. Consequently,
that run provides no successful baseline result for the native failures below.

Fixing the archive coordinates and the cvc5/ABC linker collision allowed the
[pre-fix PR run](https://github.com/masc-ucsc/livehd/actions/runs/36398193636)
to reach the full suite: 1,570 tests passed, 17 failed, and one was skipped.
Coverage exceeded its six-hour limit. A local replay reproduced 15 of the 17
failures; the statistics and incremental-simulation cases depend on execution
conditions and were investigated separately.

## Source provenance and observed failures

The relevant native checker, compiler, and test files were byte-identical in
`1af4e1492` and `70ba1aed4`. Their Git blob IDs are recorded in
[LEGACY_CI_SOURCE_PROVENANCE.json](tests/LEGACY_CI_SOURCE_PROVENANCE.json).
The only pre-existing PR changes under `pass/lec` were the explicitly requested
static linker regression and its BUILD wiring. The `lhd_kernel_common.cpp` change
removes an unused option from Lean emission; it does not alter the native LEC path.

| Original failing targets | Observed cause |
|---|---|
| `prp-fcall6`, `prp-adv_splice_callarg`, `prp-adv_ufcs_05`, `prp-self_inline_type`, `prp-ref_self_type_receiver` | Lambda extraction assigns from a reference in a hash map into the same map. Insertion can rehash and invalidate the source reference; debugging found a destroyed-hash-table failure in `upass/func_extract`. |
| `prp-equiv-mod_varargs_csa`, `prp-equiv-mod_varargs_add`, `prp-equiv-generic_mod`, `prp-equiv-mod_template`, `prp-equiv-pipe_varargs_add`, `prp-equiv-mem_tuple_rw`, `prp-v2prp2v-comb_array_const_index_read` | Bitwidth range invalidation sends a dotted name to the scalar-only `get_bundle_for_write`, triggering its assertion. This occurs during compilation, before a solver can prove equivalence. |
| `lhd_abc_math_test` | Arithmetic-shift mapping zero-fills a spare result bit while the reference retains the sign. Native LEC reports a concrete mismatch: reference 24 versus implementation 8 for the tested five-bit output. |
| `prp-equiv-blocking_ff_state` | The fixture uses blocking assignments to edge-triggered state, which its selected Slang reader explicitly rejects. The test never reaches an SMT query. |
| `lec_stats_test` | The test requires populated cvc5 statistics even when ABC cones discharge every obligation and no cvc5 query runs. |
| `lhd_formal_budget_mine_test` | A timing assertion exceeds its ten-second limit. The original test measures frontend plus formal-engine wall time. The attempted engine-only test still exceeded the threshold in CI (10,043 ms), so that attempted change did not resolve the CI failure. |
| `lhd_sim_incremental_test` | Ninja finds work on a supposedly unchanged second build. A local relative-PATH Ninja mismatch was reproduced, but the later CI log instead identifies invalid system-header dependency paths. The local reproducer did not fully explain the CI failure. |

These observations identify defects or test assumptions in unchanged source. They
do not constitute a complete before/after runtime comparison on the unmodified
base. A controlled base replay with only the necessary dependency/link repairs is
still needed before declaring every failure unrelated to the refactor at runtime.
No native solver failure is evidence by itself of a Lean proof failure.

## Removed changes to the native checker and compiler

Commit `87ec6f102` went beyond the Lean/CI scope by attempting fixes in native
compiler and checker code and changing several test setups. Those functional
and test changes are reverted; the twelve affected files again match the PR
base exactly. Only the coverage optimization from that commit is retained,
alongside the previously requested archive, linker, trigger, cache, and filter
repairs. Test selection and expected verdicts are not weakened.

In particular, the memory-state correspondence edits in `pass/lec/encode.cpp`,
`encode.hpp`, and `query.cpp` are removed. The extra scratch-memory regression
and the statistics-test change are also removed. The original `mem_tuple_rw` CI
failure was the earlier bitwidth assertion. The memory correspondence issue was
encountered only after the attempted bitwidth repair.

The added negative test's `UNKNOWN` result was a test-design error: with
`formal.lec.decompose=true`, the existing checker deliberately returns `UNKNOWN`
when a cut does not discharge and skips the monolithic comparison. Requiring
`REFUTED` in that mode exceeded the existing behavior. The test was added in the
out-of-scope CI fix, not by the Lean refactor. Neither the checker nor its proof
strategy is changed to accommodate that test.

## Merge decision

**Not ready to merge.** Conflict-free Git history does not establish passing
checks or completed proof coverage.

- The full native suite is not green, and a controlled base runtime comparison
  has not yet established a complete inherited-failure baseline.
- The earlier [proof snapshot](LEGACY_PROOF_COVERAGE.md) records 68/90 passes.
  Twelve additional completion records and artifact hashes have now been checked
  and saved in [LEGACY_PROOF_COVERAGE_ADDITIONS.json](tests/LEGACY_PROOF_COVERAGE_ADDITIONS.json),
  bringing the recorded total to **80/90**. Ten cases still lack completion
  records: `cva6_alu_export`, `cva6_pmp_gate`, `intpipe_csr_msgs`,
  `minion_dcache_miss_handler_unit`, `minion_dcache_cache_op_unit`,
  `txfma_wallace2`, `intpipe_decode`, `txfma_wallace1`,
  `minion_dcache_tensor_load`, and `vpu_mask`. This is a result count, not a
  claim that all ten processes are currently running.
- The intentional semantic changes and the unresolved ROR constant-width and
  malformed-arity cases in [LEGACY_SEMANTIC_AUDIT.md](LEGACY_SEMANTIC_AUDIT.md)
  remain review gates. Current fast/certificate agreement is not a general proof
  of old/new semantic preservation.

The three DINO full chunked-WF and fast-bridge proofs already pass. The rollback
does not change the Lean exporter or proof-library source. The focused validation
of `//pass/lean:lean_export_tests` and `//pass/lec:cvc5_link_smoke` passes all four
targets using valid Bazel cached results. Workflow lint and `git diff --check`
also pass. These checks are not a new full-suite result.

Keep the PR as a draft; do not modify the native SMT checker to make this Lean
refactor mergeable.

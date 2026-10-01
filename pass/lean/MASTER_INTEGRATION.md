# Lean refactor integration with master

This integrates master `c54a435156a528d77afab2111bcb337403a0b42a` into refactor commit `61fd1646c1fd3106ce50d166ec02ca55ca19a478`. The shared architecture and the legacy L0–L8 work remain based on [PASS_LEAN_RESTRUCTURE_PLAN.md](PASS_LEAN_RESTRUCTURE_PLAN.md), with the implementation described in [LEGACY_REFACTOR.md](LEGACY_REFACTOR.md).

## Preservation boundary

The owned scan/IR data structures, certificate and memory lowering, verified-compiler emitter, legacy model and emitters, chunked-WF implementation, fast bridge, and `formal/lean` sources are unchanged by this integration. Native LEC and the other compiler/pass implementations come from master without local solver modifications. The previously approved CI and linker repairs remain.

The graph adapter and graph fixtures were ported to the new upstream representation:

| Upstream change | Commit | Adapter change |
| --- | --- | --- |
| New graph iteration API | `9cb2b1f7a` | Read body nodes and explicit sink drivers; retain deterministic operand ordering. |
| Constants stored on constant-pool pins; constant-node API removed | `9adce39b5` | Use `pin.is_const()` and `const_of(pin)`; constants still become owned values at the scan boundary. |
| Literal pin widths and signed-width accessor | `caa9e9fd9`, `ef46ffa4f` | Use `get_signed_bits()` where the old code used `get_bits()`; preserve intrinsic widths, negative constants, and strict width checks. |
| One driver per sink; arithmetic operand banks | `42773693a` | Translate sink slots into existing IR bank roles. Sum's even slots are adds and odd slots are subtracts; repeated operands remain separate dependencies. |
| Memory `init` renamed to `initial` | `fcf5a9d9a` | Read the new graph pin name into the existing initialization field; ROM contents, mutable-memory refusals, forwarding, and reset semantics are unchanged. |
| Global `formal.strict` removed | `b7a24e0b0` | Use `--set formal.lean.strict=true` (already the default); remove obsolete global forwarding. No compatibility alias is added. |

The exact fetched HLOP dependency sources were also compared: `Dlop::get_signed_bits`, `Blop::get_signed_bits64`, and `Blop::get_signed_bitsn` have the same bodies as their old `get_bits` counterparts after the API rename. This includes wide, multiword constants.

No new Concat/reduction support or change to reset, clock, signed comparison, or memory semantics is included. Unsupported operators remain explicit refusals. New graph databases must be generated with the matching compiler; old serialized graph databases are not portable across this upstream representation change.

## Strict mode

`formal.lean.strict` selects the Lean exporter's existing strict-width policy. At its default `true`, a constant that cannot fit its declared width and a dependency pin with zero/excessive width are rejected. Setting it to `false` relaxes those checks and can substitute a one-bit dependency width. Unsupported operators, X/Z constants, and unsupported memory policies are still rejected independently. It is an export policy, not a theorem-prover strength setting.

## Validation

The isolated GCC 14 / Bazel 9.2.0 debug build passes all three Lean test targets (24 cases) and both native LEC/linker targets (30 cases). Scanner coverage includes the existing asynchronous-reset and synchronous-ROM tests plus new checks for non-dense Sum banks, duplicate operands, and constant-pool widths.

Fresh tiny graphs were generated separately with the pre-merge and integrated binaries. Across all 38 fixtures, every acceptance/refusal result and refusal diagnostic matches. All 77 successful exports are byte-for-byte identical:

- 27 legacy fast-model/certificate exports;
- 23 legacy exports with `emit_fast_bridge=true`, `cert_wf=chunked`, and chunk size 2;
- 27 verified-compiler exports.

The 11 default-mode refusals and the 15 bridge/chunked-mode refusals are preserved. Optional proof-shape refusals are not represented as successful proofs. Machine-readable per-fixture evidence is in [MASTER_ADAPTER_PARITY.json](tests/MASTER_ADAPTER_PARITY.json).

Lean elaboration passes for all 23 accepted bridge/chunked-WF fixtures. The 27 accepted fast models pass sampled cross-version oracles in both directions (new fast model against the old verified certificate, and old fast model against the new verified certificate), using 16 seeds and every address of the tiny memories. Independent generated Lean oracles also prove the reset-priority examples and the banked Sum result of 41. No `sorryAx` was accepted. The full `lhd` executable builds. All 22 targeted integration tests pass, including all 17 tests from the historical native CI comparison, the scanner suite, option listing, and formal CLI checks. See [MASTER_NATIVE_REGRESSION.json](tests/MASTER_NATIVE_REGRESSION.json). The historical DINO/block proof evidence remains tied to its recorded pre-integration compiler/library context. These tiny-fixture checks do not establish a new full-design replay against master's frontend.

## Additional CLI findings

Master's retired-option list also hid `formal.lean.cert_chunk_size`, `formal.lean.cert_chunk_limit`, and `formal.lean.cert_wf_fallback`. They are implemented by this refactor, so the three blacklist entries are removed and CLI tests cover their availability. `normalize` stays retired. Native LEC options and implementation are unchanged.

A fresh RTL-to-Lean smoke test for an 8-bit addition followed by XOR produces a `Concat` node under master's frontend. The preserved Lean scanner rejects it explicitly. This is a frontend compatibility limitation, not evidence that an emitted proof passed for that RTL. Translating Concat into existing operations is pending the user's decision; the unchanged certificate/model/proof semantics are not being expanded implicitly.

The restored controls pass `lhd_options_test`, `lhd_list_options_test`, and `lhd_formal_verify_test`. A separate XOR-only RTL fixture exports successfully in both Lean modes, its generated Lean files elaborate, and the generated Verilog is proven equivalent to the RTL by native LEC. The legacy CLI run includes the restored chunk-size control, a full chunked-WF proof, and the fast bridge. Global `formal.strict` is confirmed rejected. The addition/Concat fixture remains a separate recorded failure.

The separate historical `prp-equiv-wire_ring` regression also passes using the unchanged master harness and default solver settings.

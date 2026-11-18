---
name: pyrope-ablation-rules
description: Apply one ASPLOS27 rule-ablation transform to a single Pyrope .prp file. Rules currently implemented — `rename` (auto-gen identifiers → semantic names), `wire-to-const` (two-step `wire` decl → `const`/`mut`), `register-removal` (delete a `reg` whose only reads are self-referential), `band-fold` (stride-N scalar `_GEN_<base+stride*i>` wire bands → `[N]uW` array + for-loop), `cascade-fold` (per-entry `_mux_<N>` + `reg_arr[i]` writeback cascade → hybrid loop + N scalar reg writes), and `flatten-extract` (inline bit-vec OR flatteners `(arr[N-1] << k*(N-1)) \| ... \| arr[0]` → `_flat_<arr>:u<N*k>` helper + loop). Invoke with the rule name as first argument, then the target .prp path. One rule per invocation.
---

# pyrope-ablation-rules

Router skill for the ASPLOS27 rule-ablation matrix. Each rule is a distinct rewrite behavior; the caller picks which one to run per invocation. Per Jose's directive (2026-08-19), all ablation rules live in this single skill — do not fork a new top-level skill for each rule.

For general Pyrope language questions, defer to the `pyrope` skill and `pyrope-lsp`. This skill only performs rule-ablation rewrites.

## Dispatch

The first argument selects the rule. Supported rule names:

| Rule | Status | What it does |
|---|---|---|
| `rename` | implemented | Rename auto-generated identifiers (`_GEN_N`, `_T_N`, `_mux_N`, `_rep_N`, `tNNN`) into semantic names. |
| `wire-to-const` | implemented | Rewrite `wire NAME:TYPE` + later `NAME = expr` into a single `const NAME:TYPE = expr` or `mut NAME:TYPE = expr`. |
| `register-removal` | implemented | Delete `reg NAME:TYPE` (and its next-value assignment) when every read of NAME is inside NAME's own next-value expression — self-referential only, no external observer. |
| `band-fold` | implemented | Fold a stride-N scalar wire band `const _GEN_<base+stride*i>__w1 = <expr(i)>` for i=0..K-1 into `mut _<arr>:[K]uW = (0,...)` + `for i { _<arr>[i] = <expr(i)> }`. Rename every downstream reference. Covers the MiniBOOM→MegaBOOM wb-port match/OR fold pattern (Rob_rob16 pyroAH..AO/AU/AV). |
| `cascade-fold` | implemented | Fold a per-entry writeback cascade `_mux_<base+stride*i>:uW = <expr(i)>` + `reg_arr[i] = <guard> & _mux_<N> & 1` for i=0..K-1 into `mut _mux_<arr>:[K]uW = (0,...)` + loop + K scalar reg writes. Hybrid pattern required by the Pyrope upass gap (looped writes to `reg [N]` arrays fail). Covers Rob_rob16 pyroAM/AP/AQ/AR/AS/AT/AW. |
| `flatten-extract` | implemented | Extract an inline bit-vec OR flattener `(arr[N-1] << k*(N-1)) \| (arr[N-2] << k*(N-2)) \| ... \| arr[0]` into a named helper `mut _flat_<arr>:u<N*k>` + for loop; replace the inline occurrence with the helper reference. Prerequisite for module-size parametrization. Covers Rob_rob16 pyroAX/AY/AZ. |
| `scalar-array-fold` | implemented | Fold K scalar `reg <prefix>_<N>_<field>:<W>` declarations (for N=0..K-1) into one `reg <prefix>_<field>:[K]<W>:[ordering="old"]` array; rewrite every reference `<prefix>_<N>_<field>` → `<prefix>_<field>[N]`. Netlist-invariant: LEC pairs the K scalars to the array under tier-2 "kind/init mismatch" and still PROVEN. The BOOM pyroA/B/… driver pattern. Covers IssueUnitCollapsing_1 pyroA (is_available 8→[8]u1), FetchBuffer pyroA–E (112→14 arrays, −14.56%), BranchKillablePipeline_2 pyroA–E (−26.84% so far). |

Rules not yet implemented in this skill (add them here when Jose lands them):
- loop-unroll-back
- sub-module extract
- I/O dot-chain cleanup
- bitvector → array-index (blocked on xiangshan snapshot — see `asplos27/experiments/bitvec_to_array_index_BLOCKER.md`)

If the caller invokes without a rule name, ask which rule to run. If the rule name is unknown, list the supported rules and stop.

## Common contract — applies to every rule

- Operate on **one** `.prp` file per invocation.
- **Emit the plan** (per-item decision, old→new mapping, or SKIP + reason) **before writing anything**.
- **Emit a unified diff** (`diff -u before after`) in chat next. Still do not write.
- **Wait for the driver to accept** before writing. The driver applies the write, then runs LEC.
- Every rule below is **netlist-invariant** — LEC must pass and flat PPA (`flat_delay`, `flat_gates`, `flat_area`, `flat_max_delay`) should be ~0 delta. If PPA moves noticeably, something changed that shouldn't have.
- Never touch module signatures, `io.*` field names, comments, or whitespace beyond what the specific rule permits.

---

## Rule: rename

Rename auto-generated identifiers in **one** Pyrope module so they describe *what they compute*. This is a **netlist-invariant** transform: the compiler lowers names to nodes/wires — same netlist means same PPA. Any change beyond identifier names is a bug.

### Contract — allowed

- Replace auto-generated identifiers with semantic names.
- Split obvious per-field container writes into individually named wires *only if* the original was clearly a scratch container (e.g. `_GEN_4.e0..e15` used as a mux vector). Prefer whole-name rename over splitting.
- Emit a rename **plan** (old → new mapping) and a **unified diff** before writing anything.

### Contract — must not

- **Do not** change operators, bit-widths, control flow, or the order of any expression.
- **Do not** change the module signature: `pub comb NAME::[...](io_...:T, ...) -> (io_...:T, ...)` stays byte-for-byte identical.
- **Do not** rename `io` struct fields — they mirror module ports, one edit breaks the whole design.
- **Do not** rename identifiers that are already semantic (e.g. `wordinst__w1`, `aluop__w1`, `operand1_32__w1`, `opcode__w1`). If a human could read the name and know what it does, it stays.
- **Do not** remove or alter the `__w1` suffix — it is a semantic tag meaning "1-bit-wide wire modifier." When renaming an auto-gen name that carries `__w1` (e.g. `_signals_T_1__w1`), rename only the auto-gen part and keep the suffix (`opcode_eq_51__w1`).
- **Do not** change `const` / `mut` / `wire` / `reg` kind keywords or type annotations.
- **Do not** add, remove, or reorder statements.
- **Do not** touch comments or whitespace beyond the identifier itself.

### Identifiers to rename

| Pattern | Meaning | Rename strategy |
|---|---|---|
| `_GEN_N` (with N a digit run, optionally followed by `__w1`) | chisel/inou-slang scratch signal | Pick a name from the RHS structure (e.g. a mux vector → `alu_muxes`, a combined predicate → `is_branch_or_jump`) |
| `_T_N`, `_signals_T_N` | chisel-emitted temp | Pick a name from what the RHS computes (e.g. `unsigned(io.opcode == 51)` → `opcode_eq_51`) |
| `_mux_N`, `_rep_N` | per-branch mux or replication result | Name after the operation the branch computes (e.g. `add_result`, `sub_result`, `sra_result`, `sll_result`, `immediate_stage_5`) |
| `tNNN` (t + digit run) | slang temporary | Name after what it holds (e.g. `result_temp`) |

### Naming rules

1. **Read the RHS of the assignment.** The name should describe the value, not the mechanism.
   - `_signals_T_1__w1 = unsigned(io.opcode == 51)` → `opcode_eq_51__w1`
   - `_mux_1 = if wordinst { add32 } else { add64 }` → `add_result`
2. **Prefer op-names over positional numbers.** In an ALU, `_mux_1..5` are `{add, sub, srl, sra, sll}_result`, not `alu_op_1..5`.
3. **When a chain of `_mux_N` selects immediates by opcode**, name each by the case (e.g. `immediate_when_opcode_23`) or by the immediate type (`imm_i`, `imm_s`, `imm_b`, `imm_u`, `imm_j` for RISC-V). If the case is not clear, `immediate_stage_N` is acceptable — still better than `_mux_N`.
4. **`_GEN_N.eK` containers**: if `_GEN_N` is a packed vector of independent per-op results (as in ALU), name the whole container after its role (`alu_muxes`, `alu_result_vec`) and leave the `.eK` indexing alone. Do not attempt to split into per-element wires unless doing so is a 1-to-1 rewrite that clearly does not change semantics.
5. **snake_case only**, ASCII, no leading digits, no shadowing an existing name in the same scope.
6. **Preserve length spirit**: don't produce absurdly long names (< 40 chars is a good target).

### Procedure

For each invocation, given a target `.prp` path:

1. **Read** the file. Enumerate every identifier defined on the LHS of `const`/`mut`/`wire`/`reg`. Ignore identifiers on `io.*` fields.
2. **Classify** each: `AUTO_GEN` (renameable), `SEMANTIC` (leave alone), `PORT_LIKE` (io/signature — never rename).
3. **Choose names** per the rules above. Do not reuse a name already in scope.
4. **Emit the plan** as a mapping (old → new), one per line, before showing any diff. If any classification is ambiguous, list it under a `-- ambiguous --` section and pick a conservative default (or leave it as-is).
5. **Emit a unified diff** (`diff -u before after`) of the proposed rewrite. Do this in the chat first — do not write the file yet.
6. **Wait for the driver to accept.** The driver applies the write, then runs LEC.

### Verification (driver's responsibility; the skill should remind)

After writing:
1. `bazel test //bench:dino_lec --test_output=summary` — must PASS. If FAIL: restore the pre-rename snapshot, flag which identifiers are "load-bearing," and retry once.
2. `bazel test //bench:dino_synth --test_output=summary` — capture flat PPA (`flat_delay`, `flat_gates`, `flat_area`, `flat_max_delay`). ΔPPA should be ~0 (netlist-invariant).
3. Save the diff to `~/asplos27/experiments/naming/diffs/<MODULE>.diff`.
4. Save the renamed file to `~/asplos27/experiments/naming/after/<MODULE>.prp`.

### Worked examples (from Dino)

**Example 1 — ALU._mux_1..5 (RISC-V RV64 op selection)**

Before:
```pyrope
mut _mux_1:u64 = if wordinst__w1 != 0 { (operand1_32__w1 + operand2_32__w1)#[...] } else { (io.inputx + io.inputy)#[...] }
mut _mux_2:u64 = if wordinst__w1 != 0 { (operand1_32__w1 - operand2_32__w1)#[...] } else { (io.inputx - io.inputy)#[...] }
mut _mux_3:u64 = if wordinst__w1 != 0 { (op1 #sext >> op2)#[...] } else { (io.inputx#sext >> io.inputy)#[...] }
mut _mux_4:u64 = if wordinst__w1 != 0 { (op1 >> op2)#[...] } else { (io.inputx >> io.inputy)#[...] }
mut _mux_5:u64 = if wordinst__w1 != 0 { (op1 << op2)#[...] } else { (io.inputx << io.inputy)#[...] }
```

Plan:
```
_mux_1 -> add_result
_mux_2 -> sub_result
_mux_3 -> sra_result   -- signed shift right, arithmetic
_mux_4 -> srl_result   -- logical shift right
_mux_5 -> sll_result   -- logical shift left
```

**Example 2 — ALU._GEN_4 (mux vector)**

Before: `wire _GEN_4:u1024` with 16 packed lanes selected by `aluop__w1`.
Plan: `_GEN_4 -> alu_muxes`. Do not split.

**Example 3 — Control._signals_T_N__w1 (opcode comparators)**

Before: `const _signals_T_1__w1 = unsigned(io.opcode == 51)`
Plan: `_signals_T_1__w1 -> opcode_eq_51__w1`. Keep the `__w1` suffix.

**Example 4 — ImmediateGenerator._mux_1..10 (RISC-V immediate select)**

The chain selects one of the RISC-V immediate types by opcode. When the mapping to `imm_i / imm_s / imm_b / imm_u / imm_j` is unambiguous, use those. Otherwise `immediate_stage_1..10` is fine.

### Failure mode: LEC fails

If LEC fails after applying the diff, the rewrite touched something more than a name. Common causes:
- Accidentally renamed an `io.<field>`.
- Reused a name that already exists in the same scope (shadowing).
- Changed a bit-width or operator by editing near the rename target.

Restore from the pre-rename snapshot, re-emit a stricter plan that keeps the offending identifier untouched, and retry once. If the second attempt also fails, log the module as "rename infeasible" and stop.

---

## Rule: wire-to-const

Convert `wire NAME:TYPE` declarations to `const` or `mut` where safe. This is a **compile-time / simulator-speed** transform — the netlist is unchanged, so PPA should be ~0 delta. The value it moves is the compiler's forward-ref bookkeeping.

### Contract — allowed

- Rewrite `wire NAME:TYPE` (separate declaration) + `NAME = expr` (later assignment) into a single `const NAME:TYPE = expr` or `mut NAME:TYPE = expr` statement.
- Delete the standalone `wire` declaration line and prefix the assignment line with `const` or `mut`.
- Emit a **plan** (per-wire decision: rewrite / skip + reason + kind chosen) and a **unified diff** before writing anything.

### Contract — must not

- **Do not** touch a `wire` whose first textual *use* is *before* its assignment (that's a genuine forward-reference; removing the `wire` will fail to compile).
- **Do not** touch **port-mirror struct wires** — any `wire NAME:(<field_list>) = nil` where the type is a struct (contains `(...)`) and the initializer is `nil`. The `io` mirror is the canonical case. Fields of these structs are read before written by design; the skill must leave them alone unconditionally.
- **Do not** touch any `wire NAME:TYPE = <initializer>` — if the wire already carries an initializer, it is not the two-step-declaration pattern this rule targets.
- **Do not** reorder statements. If a rewrite is only possible by moving code, skip.
- **Do not** convert `mut` → `const`. That is a different rule; out of scope here.
- **Do not** rename identifiers, change operators, bit-widths, port names, or module signature.
- **Do not** touch comments or whitespace beyond the two lines involved in the rewrite.

### `const` vs `mut` — the classification rule

- **`const`** if `NAME` is assigned **exactly once** in the entire scope. All the Dino chisel2prp two-step wires fall into this bucket.
- **`mut`** if `NAME` is assigned **more than once** in the scope (e.g. one initialization then a conditional overwrite). Conservative default when in doubt.
- Never emit both — pick one.

### Detection algorithm (get this wrong and LEC fails)

For each line matching `wire NAME:TYPE` in the file (where the line has **no** `=` after the type):

1. **Struct-mirror guard.** If TYPE starts with `(` — it is a struct type — skip. Also skip any `wire NAME:TYPE = anything` (has initializer).
2. **Locate assignments.** Find every subsequent line matching `^\s*NAME\s*=\s` (word-boundary on NAME to avoid substring collisions). Record their line numbers and the RHS expressions.
   - If 0 assignments → skip (dead wire; defensive; don't guess).
   - If ≥ 1 assignments → proceed.
3. **Locate reads.** Find every occurrence of NAME (word-boundary) elsewhere in the file — not the declaration line, not the LHS of an assignment. Record their line numbers.
4. **Forward-reference check.** Let `earliest_assign` = min(assignment line numbers), `earliest_read` = min(read line numbers). If `earliest_read < earliest_assign` → forward-ref, skip.
5. **Choose kind.** `const` if 1 assignment, `mut` if ≥ 2 assignments.
6. **Rewrite.**
   - Delete the `wire NAME:TYPE` line entirely.
   - On the (earliest) assignment line `<indent>NAME = <rhs>`, change to `<indent><kind> NAME:TYPE = <rhs>`. Preserve indentation exactly.
   - If `mut` and there is a **second** assignment `NAME = expr2`, leave it as-is (still legal — `mut` allows reassignment).

### Word-boundary caveat

Pyrope identifiers can contain `_` and digits. When searching for NAME, treat `[A-Za-z0-9_]` as identifier characters — a match is valid only if the characters immediately before and after are NOT identifier chars (or are start/end of line). This prevents `_GEN_4` from matching inside `_GEN_42` or `not_GEN_4_yet`.

### Procedure

For each invocation, given a target `.prp` path:

1. **Read** the file. Enumerate every `wire NAME:TYPE` declaration.
2. **Classify** each as: `SKIP_STRUCT_MIRROR`, `SKIP_HAS_INITIALIZER`, `SKIP_FORWARD_REF`, `REWRITE_CONST`, `REWRITE_MUT`, or `SKIP_DEAD`.
3. **Emit the plan** — one line per wire, with the decision and the reason. Example:
   ```
   Line 2:  wire io:(operation:u5, ...) = nil        SKIP_STRUCT_MIRROR
   Line 3:  wire _GEN_4:u1024                        REWRITE_CONST (single assignment on line 13, no earlier reads)
   ```
4. **Emit a unified diff** of the proposed rewrite. Do this in the chat first — do not write the file yet.
5. **Wait for the driver to accept.** The driver applies the write, then runs LEC.

### Verification (driver's responsibility; the skill should remind)

After writing:
1. `bazel test //bench:dino_lec` — must PASS. If FAIL: restore the pre-rewrite snapshot, mark the offending wire as `SKIP_FORWARD_REF`, retry once.
2. `bazel test //bench:dino_synth` — capture flat PPA (`flat_delay`, `flat_gates`, `flat_area`, `flat_max_delay`). Expected: **identical to before** (netlist-invariant).
3. **Measure `lhd compile` wall time** on the top-level file (`PipelinedDualIssueCPU.prp`) before and after, 5+ reps each. This is the load-bearing metric for this rule.
4. Save the diff to `~/asplos27/experiments/wire_to_const/diffs/<MODULE>.diff`.
5. Save the rewritten file to `~/asplos27/experiments/wire_to_const/after/<MODULE>.prp`.

### Worked example — ALU._GEN_4

**Before (relevant lines):**
```pyrope
  wire io:(operation:u5, inputx:u64, inputy:u64, result:u64) = nil    // line 2
  wire _GEN_4:u1024                                                    // line 3
  const wordinst__w1 = (io.operation >> 4) & 1                         // line 4
  ...
  _GEN_4 = ((unsigned(io.inputx != io.inputy) & 1) << 896) | ...       // line 13
  ...
  io.result = ((_GEN_4 << 0x40) >> ((aluop__w1 * 0x40) + 0x40))#[0..=63]  // line 17
```

**Analysis:**
- `wire io:(...)= nil` → SKIP_STRUCT_MIRROR (type is `(...)`, initializer is `nil`).
- `wire _GEN_4:u1024` → no initializer, no `(` in type. Look for assignments: exactly one, on line 13. Look for reads before that: none. → **REWRITE_CONST**.

**After:**
```pyrope
  wire io:(operation:u5, inputx:u64, inputy:u64, result:u64) = nil     // line 2 (unchanged)
  const wordinst__w1 = (io.operation >> 4) & 1                         // (was line 4, now line 3)
  ...
  const _GEN_4:u1024 = ((unsigned(io.inputx != io.inputy) & 1) << 896) | ...   // (was line 13)
  ...
  io.result = ((_GEN_4 << 0x40) >> ((aluop__w1 * 0x40) + 0x40))#[0..=63]        // unchanged
```

Net effect: file is one line shorter; `_GEN_4` becomes an inline const declaration.

### Worked example — ImmediateGenerator, Control

Both files only have `wire io:(...)=nil` as their sole wire. All wires classify as **SKIP_STRUCT_MIRROR**. The plan should say so, emit an empty diff, and the driver should record a 0/0 outcome (no changes; expected).

### Failure mode: LEC fails

If LEC fails after applying the diff, one of the following is true:
- A `NAME = ...` assignment was actually a *reassignment* inside a conditional/block and the skill missed it → should have been `mut`, not `const`.
- The forward-reference detection missed a read (e.g. a read inside a nested lambda or a use as a struct field).
- Indentation or whitespace was disturbed.

Recovery: restore the pre-rewrite snapshot. Add the offending wire to a "do-not-rewrite" list. Retry once. If the second attempt also fails, log the module as "wire→const not applicable" and stop.

### Composition with `rename`

This rule can be run either on the un-renamed originals (isolated ablation) or on top of already-renamed files (composed ablation, a separate row of the matrix). Both are valid; the caller chooses via the `before/` file state.

---

## Rule: register-removal

Delete a `reg NAME:TYPE` whose only reads are inside its own next-value expression — a self-referential state cell with no observable consumer. This is the ASPLOS27 row 7 rule. It is **netlist-changing** (removes flops), unlike rename and wire-to-const, but is **LEC-safe** on designs where the LEC gate is observation-based (which `bench:dino_lec` is — proven on `PipelinedDualIssueCPU.cycleCount` 2026-08-27, LEC PROVEN cross-language pyrope-vs-verilog in 279 ms).

### Contract — allowed

- Delete a `reg NAME:TYPE` declaration line, AND the (typically single) assignment line `NAME = if reset != 0 { RESET_EXPR } else { NEXT_EXPR }` (or any equivalent line whose only occurrences of NAME on the RHS are the self-reference for the next value).
- Emit a **plan** (per-register decision: DELETE / SKIP + reason) and a **unified diff** before writing anything.

### Contract — must not

- **Do not** delete a `reg NAME:TYPE` if NAME is read anywhere in the file *outside* NAME's own next-value expression. Any `io.<field> = NAME`, any `mut/const/wire OTHER = ... NAME ...`, any use of NAME as an index or condition is a live read — SKIP.
- **Do not** delete a `reg` that lacks a next-value assignment (defensive; that's a different pattern — either dead-code that the compiler already elides, or a broken emit — either way not what this rule targets).
- **Do not** delete a `reg` whose next-value expression references any *other* register the caller has already deleted in this session. This can cascade and is out of scope for a one-file, one-invocation rule; require re-invocation for the second-order removal.
- **Do not** rewrite the reg's next-value expression to remove reads on other names — this rule is register-scope-only.
- **Do not** rename identifiers, change operators, bit-widths, port names, or module signature.
- **Do not** touch comments or whitespace beyond the two lines involved in the deletion.

### Detection algorithm (get this wrong and LEC fails)

For each line matching `reg NAME:TYPE` in the file (where the line has **no** other purpose — pure declaration):

1. **Locate next-value assignments.** Find every subsequent line matching `^\s*NAME\s*=\s` (word-boundary on NAME to avoid substring collisions). Record the line numbers and the RHS expressions.
   - If 0 assignments → SKIP_NO_NEXT (defensive; don't guess).
   - If ≥ 2 non-reset assignments → SKIP_COMPLEX (multi-write registers may hold observer semantics that a scan-based check can't see; conservative).
2. **Locate reads.** For each occurrence of NAME (word-boundary) elsewhere in the file — not the declaration line, not the LHS of an assignment — check whether it lies *inside the RHS* of NAME's own next-value assignment.
   - If ALL reads are inside NAME's own next-value RHS → self-referential-only → **DELETE**.
   - If ANY read is outside → **SKIP_HAS_OBSERVER** with the line number of the offending read.
3. **Delete.**
   - Remove the `reg NAME:TYPE` declaration line entirely.
   - Remove the (single) next-value assignment line entirely.
   - Do not touch anything else.

### Word-boundary caveat

Same rule as `wire-to-const`: identifier chars are `[A-Za-z0-9_]`. `cycleCount` must not match `cycleCount_wrap` or `myCycleCount`.

### Procedure

For each invocation, given a target `.prp` path:

1. **Read** the file. Enumerate every `reg NAME:TYPE` declaration (line-local pattern; ignore array-of-reg or complex register-file forms — those need a separate rule).
2. **Classify** each as `DELETE`, `SKIP_HAS_OBSERVER` (with offending line), `SKIP_NO_NEXT`, or `SKIP_COMPLEX`.
3. **Emit the plan** — one line per register, with the decision and the reason. Example:
   ```
   Line 34: reg pc:u64             SKIP_HAS_OBSERVER (read at line 82: `_GEN_32.address = pc`)
   Line 35: reg cycleCount:u30     DELETE (only read is self-referential on line 86)
   ```
4. **Emit a unified diff** of the proposed deletion. Do this in the chat first — do not write the file yet.
5. **Wait for the driver to accept.** The driver applies the write, then runs LEC.

### Verification (driver's responsibility; the skill should remind)

After writing:
1. `bazel test //bench:dino_lec` — must PASS. If FAIL: the LEC gate is state-equivalent (not observation-based) or the register has an observer this scan missed. Restore the pre-deletion snapshot, add the offending register to a "do-not-delete" list, and log the module as "register-removal not applicable to this LEC gate" — do NOT attempt to loosen the gate.
2. `bazel test //bench:dino_synth` — capture flat PPA (`synth_max_delay`, `synth_sta_delay`, `synth_color_all_ge_sum`, `synthesis_elapsed_ms`). Expect tiny negative gates (30 flops ≈ 165 GE at sky130, though synth's DCE often captures some of that already, so measured Δ can be smaller than predicted).
3. **Measure sim throughput** — the LOAD-BEARING metric for this rule. `bazel test //bench:dino_sim_pyrope --runs_per_test=5 --nocache_test_results`, extract `sim_cycles_per_s` per run from `bazel-testlogs/bench/dino_sim_pyrope/run_*/test.log`. Expected direction: **positive** (fewer flops per cycle = faster tick loop). Repeat for the before snapshot; report Welch's t.
4. Save the diff to `~/asplos27/experiments/register_removal/diffs/<MODULE>.diff`.
5. Save the rewritten file to `~/asplos27/experiments/register_removal/after/<MODULE>.prp`.

### Worked example — PipelinedDualIssueCPU.cycleCount

**Before (relevant lines):**
```pyrope
  reg pc:u64
  reg cycleCount:u30
  wire _hazard_io_pcfromtaken:u1
  ...
  mut `_GEN_32.address` = pc
  ...
  cycleCount = if reset != 0 { 0 } else { (cycleCount + 1)#[0..=29] }
```

**Analysis:**
- `reg pc:u64` → grep `\bpc\b` in the file, finds many reads outside pc's next-value RHS (`_GEN_32.address = pc`, `_GEN_33.pc = pc`, etc.) → **SKIP_HAS_OBSERVER**.
- `reg cycleCount:u30` → grep `\bcycleCount\b`, all occurrences are inside `cycleCount = ...` (2 uses: the `cycleCount + 1` self-reference and the LHS). No external observer → **DELETE**.

**After:** the 2 lines vanish; every other line is byte-identical.

**Measured effect (Dino, 2026-08-27):**
LEC PROVEN (279 ms). Gates −0.06 %, delay unchanged, compile −0.97 % (n.s.), **sim throughput +3.79 % (p ≈ 3×10⁻⁸)**. Sim is the load-bearing metric.

### Failure mode: LEC fails

If LEC fails after applying the diff, one of the following is true:
- The register had an observer this scan missed (e.g. an assignment through a struct field, a use inside a nested lambda, a use as a bit-slice source).
- The LEC gate for this design is state-equivalent rather than observation-based, and removing internal state cannot be reconciled.

Recovery: restore the pre-deletion snapshot, log the register as `SKIP_HAS_OBSERVER (LEC witness)`, and mark the module as "register-removal not applicable to this LEC gate." Do NOT loosen the gate to make it green (AGENTS.md rule).

### Composition with `rename` and `wire-to-const`

Same rules as `wire-to-const`: this rule can be run on un-renamed originals or on top of already-renamed / already-wire-to-const'd files. Composition with `wire-to-const` is safe (they operate on disjoint constructs). Composition with `rename` requires that the register's identifier is not on the rename list — if it was renamed, the read/write self-reference scan still works, but the plan must show the new name.

---

## Rule: band-fold

Fold a stride-N scalar wire band — K consecutive `const _GEN_<base+stride*i>__w1 = <expr(i)>` declarations whose RHS differs only in an index literal — into one `mut _<arr>:[K]uW = (0,...)` declaration + a `for i in 0..=(K-1) { _<arr>[i] = <expr(i)> }` loop. Rename every downstream reference to `_<arr>[i]`. This is a **source-compactness / loop-recovery** transform — the netlist is unchanged (cprop collapses both forms identically), so PPA should be ~0 delta. The value it moves is LEC loop-certificate recurrence coverage (every folded band becomes a matched compact recurrence) and parametrization readiness (loops with the right bound are the prerequisite for `nRobEntries`-style parameter folds).

Covers the MiniBOOM→MegaBOOM wb-port match/OR fold pattern used in Rob_rob16 pyroAH, AI, AJ, AK, AL, AN, AO, AU, AV (9 commits).

### Contract — allowed

- Replace K ≥ 4 consecutive declarations `const _GEN_<base+stride*i>__w1 = <expr(i)>` with a single `mut _<arr>:[K]uW = (0,0,...)` + `for i in 0..=(K-1) { _<arr>[i] = <expr(i)> }` block.
- Also fold the paired alias pattern: `_mux_<N+K*i>:u1 = <mux_expr(i)>` followed by `const _GEN_<M+stride*i>__w1 = _mux_<N+K*i>` — both bands collapse into one `mut _<arr>:[K]uW` + loop, with the alias name becoming the array name (see worked example 2).
- Rename every downstream read of `_GEN_<base+stride*i>__w1` or `_mux_<N+K*i>` into `_<arr>[i]`.
- Emit a **plan** (per-band decision: FOLD / SKIP + reason, with the derived `<expr(i)>` template) and a **unified diff** before writing anything.

### Contract — must not

- **Do not** fold a band of fewer than 4 declarations — the loop overhead is not worth it, and shorter bands are often not iteration-parallel in the way this rule assumes.
- **Do not** fold a band with sequential dependencies — e.g. `_GEN_<i> = _GEN_<i-1> | brupdate_oh#[i..=15]` (prefix-OR) is NOT iteration-parallel; each iteration reads the previous. Rewriting it as `for i { ... }` with array lookups changes semantics unless the loop body is also rewritten to the sequential form, which this rule does not do.
- **Do not** fold a band whose body references `_GEN_<base+stride*(i-1)>__w1` or similar from the same band — those are inter-iteration dependencies, same reason as above.
- **Do not** fold a band whose body shape differs between entries beyond the index literal (e.g. different operators per entry).
- **Do not** touch module signatures, `io.*` field names, comments, or whitespace beyond the band and its downstream references.
- **Do not** rename any identifier *outside* the folded band and its downstream references.
- **Do not** change the LHS kind — if the originals were `const`, the array must be `mut` (needed for loop writes), but downstream references stay `const`/`mut` as they were.

### Detection algorithm (get this wrong and LEC fails)

For a candidate run of consecutive declarations:

1. **Collect a run.** Starting from any `const _<prefix>_<N>__w1 = <expr>` line, scan forward while each next non-blank non-comment line matches `const _<prefix>_<M>__w1 = <expr'>` where `M - N` is a positive constant **stride** (common strides observed: 1, 2, 3). Stop at the first mismatch. If fewer than 4 entries collected → SKIP_RUN_TOO_SHORT.
2. **Infer `<expr(i)>` template.** Compare adjacent `<expr>` strings. The only differences must be integer literals that increment by 1 per entry. If more than one literal varies, record the mapping i → (literal_a_i, literal_b_i, ...) and verify every pair progresses by 1 per step. If any non-literal character differs → SKIP_HETEROGENEOUS.
3. **Verify iteration-parallelism.** No `<expr(i)>` may reference `_<prefix>_<base+stride*j>__w1` for any `j < i` in the same band — that would be a sequential dependency. If any entry does → SKIP_SEQUENTIAL_DEP.
4. **Determine K.** Count the entries. Common case: K=16 for a 16-entry ROB; K=8 for a dispatch width; K=5 for a wb-port count. The literal that varies 0..K-1 confirms K.
5. **Handle i=K-1 anomaly (CRITICAL).** The last entry may be an INLINED combination of the "stride-shape" entry and the next entry in a paired-band fold (e.g. in a match+OR pair, i=K-1 may have the match literal and the OR expression on ONE line, not two). Build the number→index map explicitly as `{base+stride*i: i for i in 0..K-1}` first; if the actual last-entry number doesn't match `base+stride*(K-1)`, patch it with the observed value. Do NOT extrapolate the stride past K-1. See pyroAN/AO (stride-3 broke by -1 at i=15; `_GEN_173` → `_GEN_175`, skipping `_GEN_174`) and pyroAR (anomaly broke by +2; `_mux_481` → `_mux_483`, skipping `_mux_481,482`).
6. **Derive W.** Scan the RHS of entry 0 for its type width. Common case: W=1 for bool-vectors (match wires, OR wires); W=4 for small indices; W=u5/u6 for widths. If uncertain, parse the operation chain (`unsigned(... == i)` → u1).
7. **Infer helper name.** From the body: `unsigned(io_wb_resps_3_bits_uop_rob_idx == i)` → `_GEN_wb3_match_idx`. `(_GEN_wb3_match_idx[i] | _GEN_wb2_match[i]) & 1` → `_GEN_wb3_or_wb2`. `io_lsu_clr_bsy_0_valid & _mux_bsy_enq[i] ...` suggests `_mux_bsy_<something>`. If the band had an alias name upstream (e.g. `_GEN_<N> = _mux_<M>`), reuse the alias name as `_<alias>` for the array. Fall back to `_fold_<base>` if no semantic name is derivable.
8. **Rename downstream.** Enumerate every occurrence of `_<prefix>_<base+stride*i>__w1` elsewhere in the file; each must become `_<arr>[i]`. If a downstream site references the band element but with a non-literal index (`_<prefix>_<some_expr>__w1`), SKIP — the index must be a literal to be mechanically renameable.

### Paired-band fold (match + OR, or mux + alias)

When two bands have identical length K and the second band's RHS is `_<prefix1>_<base1+stride1*i>__w1` (pure alias), fold BOTH in one commit:
- The alias band becomes the array (named after the aliased signal, e.g. `_mux_wb1_unsafe`).
- The source band is deleted entirely; its original number range is renamed to `_<arr>[i]` references.
- Both must have the same i=K-1 anomaly handling (see pyroAK/AL/AN/AO).
- Line savings double vs single-band fold.

Not safe if the alias is non-trivial (`_GEN_<N> = _mux_<M> & 1` instead of `_GEN_<N> = _mux_<M>`) — that's not a pure alias.

### Procedure

For each invocation, given a target `.prp` path:

1. **Read** the file. Enumerate candidate runs of consecutive `const _<prefix>_<N>__w1 = <expr>` and `mut _mux_<N>:uW = <expr>` declarations.
2. **Classify** each run: `FOLD` (with inferred `<arr>` name, K, W, i=K-1 anomaly), `FOLD_PAIRED` (band + alias), `SKIP_RUN_TOO_SHORT`, `SKIP_HETEROGENEOUS`, `SKIP_SEQUENTIAL_DEP`, `SKIP_NON_LITERAL_DOWNSTREAM`.
3. **Emit the plan** — one entry per candidate run with the decision and reason. Example:
   ```
   Lines 221–236: _GEN_47..62 (stride 1, K=16, W=u1)
       FOLD → _GEN_brupdate_clr[16]u1
       expr(i) = io_brupdate_b2_mispredict & ((brupdate_b2_rob_clr_oh__w1 >> i) & 1) & 1
       downstream renames: 32 (2 refs per entry, in _mux_263+4i and _mux_265+4i)
   Lines 264–326: _mux_89..119 + _GEN_131..175 (paired, stride 2 + stride 3, K=16, W=u1)
       FOLD_PAIRED → _mux_wb1_bsy[16]u1
       i=15 anomaly: _GEN_175 (not _GEN_176)
       downstream renames: 48 (3 refs per entry)
   ```
4. **Emit a unified diff** of the proposed rewrite. Do this in the chat first — do not write the file yet.
5. **Wait for the driver to accept.** The driver applies the write, then runs LEC.

### Verification (driver's responsibility; the skill should remind)

After writing:
1. **LEC against the previous accepted version** (e.g. `pyroN-1` snapshot) — must PASS. Expect PROVEN with a `loop certificate: N matched compact recurrence(s)` line in the output; each folded band contributes one recurrence (bands with downstream reads in multiple cascades may lift to more).
2. If LEC verdict is REFUTED, see "Failure mode" below.
3. If LEC verdict is INCONCLUSIVE or UNKNOWN, the band fold is still accepted (NCE per `AGENTS.md`) but flag it for review.
4. Flat PPA (`flat_gates`, `flat_delay`) should be ~0 delta (netlist-invariant).
5. Save the diff to `~/asplos27/experiments/band_fold/diffs/<MODULE>.diff`.
6. Save the rewritten file to `~/asplos27/experiments/band_fold/after/<MODULE>.prp`.

### Worked example 1 — Rob_rob16 pyroAU (`_GEN_47..62` → `_GEN_brupdate_clr[16]u1`)

**Before (lines 221–236):**
```pyrope
  const _GEN_47__w1 = io_brupdate_b2_mispredict & brupdate_b2_rob_clr_oh__w1 & 1 & 1
  const _GEN_48__w1 = io_brupdate_b2_mispredict & (brupdate_b2_rob_clr_oh__w1 >> 1) & 1 & 1
  const _GEN_49__w1 = io_brupdate_b2_mispredict & (brupdate_b2_rob_clr_oh__w1 >> 2) & 1 & 1
  ...
  const _GEN_62__w1 = io_brupdate_b2_mispredict & (brupdate_b2_rob_clr_oh__w1 >> 15) & 1 & 1
```

**Plan:** stride=1, K=16, W=u1. Body of entry 0 differs from entry 1 only in `>> 0` (implicit) vs `>> 1`. No i=15 anomaly (entry 15 is `>> 15`, stride holds). No sequential dependency. 32 downstream references (2 per entry: once in `_mux_263+4i` for the will_commit branch, once for the else branch).

**After:**
```pyrope
  mut _GEN_brupdate_clr:[16]u1 = (0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)
  for i in 0..=15 {
    _GEN_brupdate_clr[i] = io_brupdate_b2_mispredict & ((brupdate_b2_rob_clr_oh__w1 >> i) & 1) & 1
  }
```

Every downstream `_GEN_<47+i>__w1` becomes `_GEN_brupdate_clr[i]`.

**Measured:** PROVEN by abc alone (71/71 cones), 12s LEC. −12 LoC.

### Worked example 2 — Rob_rob16 pyroAN (paired fold with i=K-1 anomaly)

**Before (lines 272–303):**
```pyrope
  mut _mux_89:u1 = if io_wb_resps_1_valid != 0 { (~_GEN_wb1_or_wb0[0]) & 1 & _mux_bsy_enq[0] & 1 } else { (~_GEN_wb0_match[0]) & 1 & _mux_bsy_enq[0] & 1 }
  const _GEN_131__w1 = _mux_89
  mut _mux_91:u1 = if io_wb_resps_1_valid != 0 { (~_GEN_wb1_or_wb0[1]) & 1 & _mux_bsy_enq[1] & 1 } else { (~_GEN_wb0_match[1]) & 1 & _mux_bsy_enq[1] & 1 }
  const _GEN_134__w1 = _mux_91
  ... (14 more pairs)
  mut _mux_119:u1 = if io_wb_resps_1_valid != 0 { (~_GEN_wb1_or_wb0[15]) & 1 & _mux_bsy_enq[15] & 1 } else { (~_GEN_wb0_match[15]) & 1 & _mux_bsy_enq[15] & 1 }
  const _GEN_175__w1 = _mux_119
```

**Plan:** two bands.
- Band A: `_mux_<89+2i>:u1` for i=0..15 (stride 2, K=16).
- Band B: `_GEN_<131+3i>__w1 = _mux_<89+2i>` for i=0..14, then `_GEN_175__w1 = _mux_119` for i=15 (stride 3 breaks by −1 at i=15; expected `_GEN_176` but actual `_GEN_175`).
- Both bands have identical K; band B is a pure alias of band A → FOLD_PAIRED. Use alias name: `_mux_wb1_bsy[16]u1`.

**Alias map (built explicitly to handle anomaly):**
```python
alias_nums = {131+3*i: i for i in range(15)}
alias_nums[175] = 15  # anomaly
```

**After:**
```pyrope
  mut _mux_wb1_bsy:[16]u1 = (0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)
  for i in 0..=15 {
    _mux_wb1_bsy[i] = if io_wb_resps_1_valid != 0 { (~_GEN_wb1_or_wb0[i]) & 1 & _mux_bsy_enq[i] & 1 } else { (~_GEN_wb0_match[i]) & 1 & _mux_bsy_enq[i] & 1 }
  }
```

Every downstream `_GEN_<131+3i>__w1` (and `_GEN_175__w1`) becomes `_mux_wb1_bsy[i]`.

**Measured:** PROVEN via cvc5 18.9s, 5 matched compact recurrences. −28 LoC.

### Failure mode: LEC fails

If LEC returns REFUTED after applying the diff, the rewrite is semantically different from the original. Common causes:
- **i=K-1 anomaly missed.** The actual last-entry number isn't `base+stride*(K-1)` — the delete filter removed a line that was meant to stay, or a downstream reference wasn't renamed because its number was off-by-anomaly.
- **Sequential dependency missed.** One entry's body references `_<prefix>_<base+stride*(i-1)>__w1` — this is prefix-OR shape, not iteration-parallel.
- **Downstream reference missed.** A `_<prefix>_<N>__w1` reference lies outside the file region the skill scanned (e.g. inside a nested lambda or an `elif` cascade that uses the same `[i]` subscript — see `cascade-fold` failure mode for the brace-depth state-machine lesson).

Recovery: restore the pre-fold snapshot. Re-emit the plan with the specific line/number that caused the LEC counterexample highlighted as `SKIP_<reason>`. Retry once. If the second attempt also fails, log the band as "band-fold not applicable" and stop.

### Composition with `rename`, `wire-to-const`, `register-removal`

Composable with all three. If the band identifiers (`_GEN_47..62`) have been renamed by `rename` to semantic names, the detection still works but the plan must show the new names. If a `wire-to-const` pass has already converted `wire _GEN_N:u1` + `_GEN_N = expr` to `const _GEN_N:u1 = expr`, the band-fold rule's detection pattern already matches `const` form. `register-removal` targets `reg` and doesn't interact.

---

## Rule: cascade-fold

Fold a per-entry writeback cascade — K per-entry `mut _mux_<base+stride*i>:uW = <expr(i)>` + `reg_arr[i] = <guard(i)> & _mux_<N> & 1` statements — into one `mut _mux_<arr>:[K]uW = (0,...)` + for-loop + K scalar reg writes. This is a **source-compactness / loop-recovery** transform — the netlist is unchanged (cprop collapses both forms), so PPA should be ~0 delta. The value it moves is LEC loop-certificate recurrence coverage: pyroAW on Rob_rob16 matched 22 compact recurrences by combining this rule with `band-fold` outputs.

**Hybrid pattern required.** The Pyrope `upass.tolg` has a known gap: `for i in 0..=15 { reg_arr[i] = <expr(i)> }` fails with `memory 'reg_arr' lowered 0 write sites but the pre-scan counted 1`. The workaround is the mut-array + N scalar reg writes hybrid. This rule applies that pattern unconditionally.

Covers Rob_rob16 pyroAM, AP, AQ, AR, AS, AT, AW (7 commits — the biggest line savings of the session).

### Contract — allowed

- Replace K ≥ 4 per-entry `mut _mux_<base+stride*i>:uW = <expr(i)>` + `reg_arr[i] = <guard_expr(i)> & _mux_<base+stride*i> & 1` statement pairs with:
  ```pyrope
  mut _mux_<arr>:[K]uW = (0,0,...)
  for i in 0..=(K-1) {
    _mux_<arr>[i] = <guard_expr(i)> & <expr(i)> & 1
  }
  reg_arr[0] = _mux_<arr>[0]
  reg_arr[1] = _mux_<arr>[1]
  ...
  reg_arr[K-1] = _mux_<arr>[K-1]
  ```
- **Multi-output variants.** When multiple reg arrays (`reg_a[i]`, `reg_b[i]`, ...) are written in interleaved per-entry blocks (each entry writes several registers), fold them into ONE for-loop body with multiple array updates, then emit the scalar reg writes grouped by field at the end. Example: pyroAQ bundled `rob_fflags_valid[i]` + `rob_fflags_bits[i]` (one `if _GEN_wb4_fflags[i] { ... } elif ... else { reg_arr[i] }` cascade per entry); pyroAT bundled 8 `rob_uop_<field>[i]` writes inside a shared `if _GEN_enq_fire[i]` block.
- **Hoisted-const trick.** When K identical scalar temp declarations differ only in name (e.g. 16× `mut _b2i_<N> = if rob_state == 3 { 1 } else { 0 }`), hoist them to one `const _<name> = <expr>` before the loop body (used in pyroAP for the shared `_b2i_rob_state_3` state check). Only applies when the expression is constant across all 16 entries.
- **Else-branch for hold semantics.** When a cascade uses `if ... { reg_arr[i] = X } elif ... { reg_arr[i] = Y } elif ... { reg_arr[i] = Z }` (no final `else`), the implicit "no write" branch means "hold previous value" for the reg. In the folded form, add an explicit `else { reg_arr[i] }` so the mut-array always has a value — semantically equivalent since reading the reg returns the current value, which is what "no write" means for the next cycle.
- Emit a **plan** (per-cascade decision: FOLD / SKIP + reason, with inferred arrays and output fields) and a **unified diff** before writing anything.

### Contract — must not

- **Do not** emit a pure loop writing to a `reg` array (`for i { reg_arr[i] = <expr> }`) — this hits the upass gap and FAILS at compile. The hybrid pattern is mandatory for this rule.
- **Do not** fold a cascade of fewer than 4 per-entry blocks.
- **Do not** fold a cascade whose per-entry body differs beyond the index literal.
- **Do not** fold a cascade that writes a NON-reg array (`mut _temp_arr[i]`) — this rule targets reg writebacks specifically. Non-reg loops can be a pure loop (no hybrid needed) — see band-fold.
- **Do not** touch unrelated `} elif ...` branches sharing the same `[i]` subscript. See the pyroAQ failure mode below.
- **Do not** rename identifiers outside the folded cascade, change operators, bit-widths, port names, or module signature.
- **Do not** touch comments or whitespace beyond the cascade region.

### Detection algorithm (get this wrong and LEC fails)

For a candidate cascade region:

1. **Collect per-entry blocks.** Starting from any `mut _mux_<N>:uW = <expr>` or `if <guard>[I] != 0 {` line, scan forward. Record each per-entry block as (I, body_lines, reg_arr_writes). Stop at the first mismatch. If fewer than 4 blocks collected → SKIP_RUN_TOO_SHORT.
2. **Verify `<expr(i)>` template.** Same as band-fold step 2 — adjacent entries must differ only in index literals that progress 0..K-1.
3. **Verify writes target reg arrays.** Each entry's body must write to one or more `reg_arr[i]` where `reg_arr` is declared as `reg <name>:[K]u<W>`. If any write targets a non-reg array → SKIP_NOT_REG_WRITE (use band-fold instead).
4. **Classify branch structure.**
   - **Simple:** one `mut _mux_<N>:uW = <expr>` + one `reg_arr[i] = <guard> & _mux_<N> & 1` per entry (pyroAM, AP, AS).
   - **Multi-output simple:** per entry writes multiple reg arrays, each from its own `_mux_<N>`.
   - **Nested-mux:** per entry has 2–3 chained `_mux_<N>` scalars feeding one `reg_arr[i]` (pyroAR 3-level predicated cascade).
   - **If-elif cascade:** per entry has `if <cond1> { reg_arr[i] = X } elif <cond2> { reg_arr[i] = Y } ...` with no else (pyroAQ fflags_bits, requires explicit `else { reg_arr[i] }` in the folded form).
   - **Multi-field if-block:** per entry has `if <cond> { reg_a[i] = X; reg_b[i] = Y; reg_c[i] = Z; ... }` writing 7+ reg arrays from one condition (pyroAT enq-fire).
5. **Determine K.** Same as band-fold step 4.
6. **Infer helper name.** `_mux_<arr>` where `<arr>` is the reg name (e.g. `_mux_rob_bsy` for `rob_bsy` writes).
7. **Delete filter — brace-depth state machine (CRITICAL).** Build the delete filter as a BRACE-DEPTH STATE MACHINE, not a line-prefix set. Specifically:
   - Enter DELETE mode when the first per-entry block header matches (e.g. `  if _GEN_enq_fire[I] != 0 {` or `  rob_<arr>[I] = <guard> & _mux_<N> & 1`).
   - Track `{`/`}` counts in each subsequent line; depth starts at 1 (we just consumed the opening `{`).
   - Exit DELETE mode when depth returns to 0 AND the current line is `}` — then skip that `}` too and resume normal mode.
   - On exit, auto-reset depth tracking at the NEXT per-entry header. This handles pyroAQ's interleaved `rob_fflags_valid[i]` + `rob_fflags_bits[i]` cascades without eating unrelated `} elif _GEN_enq_fire[i]` branches from `rob_uop_debug_fsrc` (which triggered pyroAQ v1 REFUTED).
   - Line-prefix delete sets are UNSAFE when the same shape appears in multiple cascades sharing per-entry `[i]`.
8. **Enumerate scalar reg writes to emit.** For each `reg_arr` written in the cascade, append K lines `reg_arr[0] = _mux_<arr>[0]`, ..., `reg_arr[K-1] = _mux_<arr>[K-1]` AFTER the for-loop.

### Procedure

For each invocation, given a target `.prp` path:

1. **Read** the file. Enumerate candidate cascade regions.
2. **Classify** each: `FOLD_SIMPLE`, `FOLD_MULTI_OUTPUT`, `FOLD_NESTED_MUX`, `FOLD_IF_ELIF`, `FOLD_MULTI_FIELD`, `SKIP_NOT_REG_WRITE`, `SKIP_HETEROGENEOUS`, `SKIP_RUN_TOO_SHORT`.
3. **Emit the plan** — one entry per candidate region. Example:
   ```
   Lines 572–585: 16× (if _GEN_enq_fire[i] { 7 field writes } + if (brupdate_match[i]) { 4 } elif _GEN_enq_fire[i] { enq } for debug_fsrc)
       FOLD_MULTI_FIELD → _mux_rob_uop_<field>[16]u<W> for 8 fields
       delete filter: brace-depth state machine starting at `  if _GEN_enq_fire[I] != 0 {`
       scalar reg writes to emit: 16 × 8 = 128
   ```
4. **Emit a unified diff**. Do this in chat first — do not write the file yet.
5. **Wait for the driver to accept.** The driver applies the write, then runs LEC.

### Verification (driver's responsibility; the skill should remind)

After writing:
1. **LEC against the previous accepted version** — must PASS. Expect PROVEN with `loop certificate: N matched compact recurrence(s)`; cascade folds often match more recurrences than band folds (13–22 observed on Rob_rob16).
2. If REFUTED, see "Failure mode" below.
3. Flat PPA (`flat_gates`, `flat_delay`) should be ~0 delta.
4. Save the diff to `~/asplos27/experiments/cascade_fold/diffs/<MODULE>.diff`.
5. Save the rewritten file to `~/asplos27/experiments/cascade_fold/after/<MODULE>.prp`.

### Worked example 1 — Rob_rob16 pyroAP (`rob_bsy[i]` with hoisted _b2i const)

**Before (lines 587–634):**
```pyrope
  mut _b2i_324 = if rob_state == 3 { 1 } else { 0 }
  mut _mux_325:u1 = if io_lsu_clr_bsy_0_valid != 0 { (~_GEN_wb4_wake[0]) & 1 & _mux_wb3_bsy[0] & 1 } else { (~_GEN_wb4_match[0]) & 1 & _mux_wb3_bsy[0] & 1 }
  rob_bsy[0] = (~_b2i_324) & 1 & _mux_325 & 1
  mut _b2i_326 = if rob_state == 3 { 1 } else { 0 }
  mut _mux_327:u1 = if io_lsu_clr_bsy_0_valid != 0 { (~_GEN_wb4_wake[1]) & 1 & _mux_wb3_bsy[1] & 1 } else { (~_GEN_wb4_match[1]) & 1 & _mux_wb3_bsy[1] & 1 }
  rob_bsy[1] = (~_b2i_326) & 1 & _mux_327 & 1
  ... (14 more entries)
```

**Plan:** K=16, W=u1. 16× identical `_b2i_<N> = if rob_state == 3 { 1 } else { 0 }` → hoist to one `const _b2i_rob_state_3`. The `_mux_<325+2i>` body differs only in `_GEN_wb4_wake[i]`, `_GEN_wb4_match[i]`, `_mux_wb3_bsy[i]`. FOLD_SIMPLE.

**After:**
```pyrope
  const _b2i_rob_state_3 = if rob_state == 3 { 1 } else { 0 }
  mut _mux_rob_bsy:[16]u1 = (0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)
  for i in 0..=15 {
    _mux_rob_bsy[i] = (~_b2i_rob_state_3) & 1 & (if io_lsu_clr_bsy_0_valid != 0 { (~_GEN_wb4_wake[i]) & 1 & _mux_wb3_bsy[i] & 1 } else { (~_GEN_wb4_match[i]) & 1 & _mux_wb3_bsy[i] & 1 }) & 1
  }
  rob_bsy[0] = _mux_rob_bsy[0]
  rob_bsy[1] = _mux_rob_bsy[1]
  ...
  rob_bsy[15] = _mux_rob_bsy[15]
```

**Measured:** PROVEN via cvc5 17.7s, 15 matched compact recurrences. −27 LoC.

### Worked example 2 — Rob_rob16 pyroAQ (multi-output interleaved cascade, brace-depth delete)

**Before:** 16× interleaved blocks, each 10 lines:
```pyrope
  rob_fflags_valid[0] = (((((_GEN_wb4_fflags[0] | (_GEN_2__w1 & _GEN_wb3_match_idx[0] & 1)) & 1) | (_GEN_1__w1 & _GEN_wb1_match_idx[0] & 1)) & 1) | ((~_GEN_enq_fire[0]) & 1 & rob_fflags_valid[0] & 1)) & 1
  if _GEN_wb4_fflags[0] != 0 {
    rob_fflags_bits[0] = io_wb_resps_4_bits_fflags_bits
  } elif (io_wb_resps_3_valid & io_wb_resps_3_bits_fflags_valid & 1 & _GEN_wb3_match_idx[0] & 1) != 0 {
    rob_fflags_bits[0] = io_wb_resps_3_bits_fflags_bits
  } elif (io_wb_resps_1_valid & io_wb_resps_1_bits_fflags_valid & 1 & _GEN_wb1_match_idx[0] & 1) != 0 {
    rob_fflags_bits[0] = io_wb_resps_1_bits_fflags_bits
  } elif _GEN_enq_fire[0] != 0 {
    rob_fflags_bits[0] = 0
  }
  (next entry starts here)
```

**Plan:** K=16, FOLD_IF_ELIF (requires explicit else-hold on fflags_bits), two output arrays (`rob_fflags_valid`, `rob_fflags_bits`). Delete filter MUST be brace-depth state machine starting at `  rob_fflags_valid[I] = (((((_GEN_wb4_fflags[I]`; otherwise it eats `} elif _GEN_enq_fire[I] != 0 {` lines from the unrelated `rob_uop_debug_fsrc` cascade.

**After:**
```pyrope
  mut _mux_rob_fflags_valid:[16]u1 = (0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)
  mut _mux_rob_fflags_bits:[16]u5 = (0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)
  for i in 0..=15 {
    _mux_rob_fflags_valid[i] = (((((_GEN_wb4_fflags[i] | (_GEN_2__w1 & _GEN_wb3_match_idx[i] & 1)) & 1) | (_GEN_1__w1 & _GEN_wb1_match_idx[i] & 1)) & 1) | ((~_GEN_enq_fire[i]) & 1 & rob_fflags_valid[i] & 1)) & 1
    _mux_rob_fflags_bits[i] = if _GEN_wb4_fflags[i] != 0 {
      io_wb_resps_4_bits_fflags_bits
    } elif (io_wb_resps_3_valid & io_wb_resps_3_bits_fflags_valid & 1 & _GEN_wb3_match_idx[i] & 1) != 0 {
      io_wb_resps_3_bits_fflags_bits
    } elif (io_wb_resps_1_valid & io_wb_resps_1_bits_fflags_valid & 1 & _GEN_wb1_match_idx[i] & 1) != 0 {
      io_wb_resps_1_bits_fflags_bits
    } elif _GEN_enq_fire[i] != 0 {
      0
    } else {
      rob_fflags_bits[i]
    }
  }
  rob_fflags_valid[0] = _mux_rob_fflags_valid[0]
  ...
  rob_fflags_bits[15] = _mux_rob_fflags_bits[15]
```

**Measured:** PROVEN via cvc5 1.6s, **17 matched compact recurrences** (strongest hit of the session), −112 LoC (−10.3%).

### Failure mode: LEC fails

If LEC returns REFUTED after applying the diff, the rewrite missed a semantic detail. Common causes:
- **Delete filter ate unrelated code.** The counterexample fires on a signal that LOOKS unrelated (e.g. `io_commit_uops_0_debug_fsrc` when the fold target was `rob_fflags_*`). The fix is the brace-depth state machine — a line-prefix filter including `  } elif <var>[I] != 0 {` matches the same shape in multiple per-entry cascades. See pyroAQ v1 → v2 for the exact fix.
- **Hold-else missing.** An if-elif cascade without a final `else { reg_arr[i] }` was folded without adding the hold branch — the mut-array ends up with 0 instead of the reg's prior value in the no-write case.
- **Non-reg write misclassified.** The target isn't actually a `reg` array; the hybrid pattern of mut + scalar reg writes is wrong for mut arrays (use band-fold instead).
- **Multi-field bundling exceeded solver patience.** With more than ~10 fields in one loop body, LEC may return INCONCLUSIVE / UNKNOWN. Split the fold into field groups.

Recovery: restore the pre-fold snapshot. Narrow the delete filter to a brace-depth state machine scoped to the specific cascade shape. Add the hold-else branch for if-elif cascades. If multi-field bundling times out, split by field. Retry once.

### Composition with `rename`, `wire-to-const`, `register-removal`, `band-fold`

Composable with all previous rules. If the reg arrays or `_mux_<N>` scalars have been renamed, the detection still works but the plan must show the new names. The usual order is: `rename` → `wire-to-const` → `band-fold` → `cascade-fold`. `band-fold` first because many `cascade-fold` bodies reference `_<arr>[i]` helpers that `band-fold` has already produced (e.g. pyroAM's body `(~_GEN_wb4_wake[i]) & 1 & _mux_wb3_unsafe[i]` depends on prior `_GEN_wb4_wake` and `_mux_wb3_unsafe` folds).

---

## Rule: flatten-extract

Extract an inline bit-vec OR flattener — a shift-and-OR chain of the form `(arr[N-1] << k*(N-1)) | (arr[N-2] << k*(N-2)) | ... | arr[0]` — into a named `mut _flat_<arr>:u<N*k>` helper + for loop; replace the inline occurrence with the helper reference. This is a **structural fold** required for module-size parametrization: inline 16-way (or N-way) ORs are the main blocker to making `nRobEntries` a module parameter. Netlist is unchanged (cprop collapses both forms); PPA ~0 delta.

Covers Rob_rob16 pyroAX (2 standalone), AY (9 embedded), AZ (3 multi-bit) — 3 commits, 14 flatteners total.

**Line count caveat.** This rule can INCREASE source line count on standalone flatteners (replaces 1-line 500-char expression with 4-line helper). It is a structural change for parametrization readiness, NOT a line-savings commit. For embedded flatteners inside larger expressions, line count is roughly net-zero (replaces a long expression with a short reference + adds the helper). Report expected line delta in the plan.

### Contract — allowed

- Extract an inline flattener matching `(arr[N-1] << k*(N-1)) | (arr[N-2] << k*(N-2)) | ... | arr[0]` where arr is a reg or mut array, N ≥ 4, k ≥ 1 (bit-width of each element), and the sequence is contiguous and ordered from highest index to lowest.
- Supported widths: k=1 (bool-vector flatten), k=3 (debug_fsrc), k=5 (fflags_bits), k=6 (pc_lob). Any k that is a positive integer works mechanically; this rule has only been exercised on these four.
- Insert the helper declaration `mut _flat_<arr>:u<N*k> = 0` followed by `for i in 0..=(N-1) { _flat_<arr> = _flat_<arr> | (arr[i] << (i * k)) }` at a point in the file AFTER arr's declaration and BEFORE the first use of the inline flattener. The anchor should be an existing landmark like a prior fold's `mut _GEN__w1:u16 = 0` line, not a magic line number.
- Replace the inline occurrence of the flattener with `_flat_<arr>` (just the identifier, no extra parens needed if the surrounding expression already parenthesized the flattener).
- Emit a **plan** (per-flattener decision: FOLD / SKIP + reason, with inferred N, k, helper name) and a **unified diff** before writing anything.

### Contract — must not

- **Do not** extract a flattener with fewer than 4 terms.
- **Do not** extract if the terms are not strictly `arr[K] << K*k` for K=0..N-1 with k constant across all terms. Expressions mixing different arrays or different shift multipliers are not foldable to one helper.
- **Do not** extract if the inline occurrence contains an unparenthesized `|` or `+` outside the flattener itself — the outer parens must be load-bearing for the flattener alone.
- **Do not** extract a flattener whose terms are `arr[K] << K*k` with HOLES (missing indices) — the loop assumes a contiguous 0..N-1 range.
- **Do not** rename arr or any downstream identifier beyond the inline replacement.
- **Do not** change the width or signedness of the helper beyond what the terms require (`u<N*k>` is the exact needed width).
- **Do not** touch module signatures, `io.*` field names, comments, or whitespace beyond the specific inline range being replaced and the helper insertion point.

### Detection algorithm (get this wrong and LEC fails)

For each candidate inline expression:

1. **Pattern-match the flattener.** Scan for substrings of the form `(arr[N-1] << M) | (arr[N-2] << M') | ... | arr[0])` where:
   - All terms reference the SAME array name `arr`.
   - Indices go from N-1 down to 0 strictly decrementing by 1.
   - Shift amounts M, M', ..., 0 satisfy M = k*(N-1), M' = k*(N-2), ..., with k = M / (N-1) a positive integer constant.
   - The final term is `arr[0]` without a shift (or `arr[0] << 0`, trimmed to `arr[0]`).
2. **Width check.** Infer k from the second-highest term's shift (e.g. `arr[14] << 84` with N=16 → k = 84/14 = 6). Verify every term matches. If any mismatch → SKIP_SHIFT_INCONSISTENT.
3. **Element width check.** Determine arr's element type (`[16]u<k>`). The helper width is `N * k`. For a mask-and-pack chain (`(arr[K] & 1) << K`), the inferred k is 1; the explicit `& 1` must appear on every term except the last (which is `arr[0] & 1`), and the helper body uses `(arr[i] & 1) << i`.
4. **Context parens check.** The flattener's outermost `(` must be balanced strictly with its outermost `)` and must contain nothing else. If the surrounding expression has `| other_stuff` inside those outer parens, the flattener is not isolatable → SKIP_NOT_ISOLATED.
5. **Deduplicate.** If the same flattener text appears multiple times in the file (same arr, same k, same N), extract ONCE and replace at all sites. Memory saves scale with duplicate count.
6. **Choose anchor.** Insert the helper declaration AFTER arr's `reg`/`mut` declaration and BEFORE the first use of the flattener. If a previous fold already placed a helper (e.g. `mut _GEN__w1:u16 = 0`), inject right before that line so related helpers group.

### Procedure

For each invocation, given a target `.prp` path:

1. **Read** the file. Enumerate candidate inline flattener expressions.
2. **Classify** each: `FOLD` (with inferred `<arr>`, N, k), `SKIP_SHIFT_INCONSISTENT`, `SKIP_NOT_ISOLATED`, `SKIP_RUN_TOO_SHORT`.
3. **Emit the plan** — one entry per flattener. Example:
   ```
   Line 61 (embedded in io_commit_uops_0_out_pc_lob__w1):
       FOLD → _flat_rob_uop_pc_lob:u96 (N=16, k=6)
       inline length: 489 chars → 20-char reference
       expected line delta: +3 (adds 4-line helper, shortens 1 line)
   Line 54 (standalone const _GEN__w1 = ...):
       FOLD → _flat_rob_val:u16 (N=16, k=1)  // standalone replaces `const _GEN__w1 = <flat>` with `mut _flat_rob_val:u16 = 0 + for`; downstream references renamed
       expected line delta: +3
   ```
4. **Emit a unified diff**. Do this in chat first — do not write the file yet.
5. **Wait for the driver to accept.** The driver applies the write, then runs LEC.

### Verification (driver's responsibility; the skill should remind)

After writing:
1. **LEC against the previous accepted version** — must PASS. Expect PROVEN. Loop certificate may or may not report new recurrences (each `_flat_<arr>` adds one potential recurrence; multi-bit flatteners added 9 recurrences in pyroAZ, singletons added 0).
2. If REFUTED, see "Failure mode" below.
3. Flat PPA (`flat_gates`, `flat_delay`) should be ~0 delta.
4. Save the diff to `~/asplos27/experiments/flatten_extract/diffs/<MODULE>.diff`.
5. Save the rewritten file to `~/asplos27/experiments/flatten_extract/after/<MODULE>.prp`.

### Worked example 1 — Rob_rob16 pyroAX (standalone single-bit, `rob_val` flatten)

**Before (line 54):**
```pyrope
  const _GEN__w1 = ((rob_val[15] & 1) << 15) | ((rob_val[14] & 1) << 14) | ((rob_val[13] & 1) << 13) | ((rob_val[12] & 1) << 12) | ((rob_val[11] & 1) << 11) | ((rob_val[10] & 1) << 10) | ((rob_val[9] & 1) << 9) | ((rob_val[8] & 1) << 8) | ((rob_val[7] & 1) << 7) | ((rob_val[6] & 1) << 6) | ((rob_val[5] & 1) << 5) | ((rob_val[4] & 1) << 4) | ((rob_val[3] & 1) << 3) | ((rob_val[2] & 1) << 2) | ((rob_val[1] & 1) << 1) | (rob_val[0] & 1)
```

**Plan:** standalone const. arr=`rob_val`, N=16, k=1. Mask-and-pack (every term has `& 1`). Replace the whole line with the helper.

**After:**
```pyrope
  mut _GEN__w1:u16 = 0
  for i in 0..=15 {
    _GEN__w1 = _GEN__w1 | ((rob_val[i] & 1) << i)
  }
```

Note: for standalone flatteners the identifier is kept (`_GEN__w1`) rather than renamed to `_flat_rob_val` — downstream references don't need to change, and the semantic shift from `const` to `mut` + loop is the only edit. This is the convention used in pyroAX.

**Measured:** PROVEN via cvc5 10.7s. +3 LoC.

### Worked example 2 — Rob_rob16 pyroAZ (multi-bit embedded, `rob_uop_pc_lob` flatten)

**Before (line 103, embedded in a larger expression):**
```pyrope
  const io_commit_uops_0_out_pc_lob__w1 = ((((rob_uop_pc_lob[15] << 90) | (rob_uop_pc_lob[14] << 84) | (rob_uop_pc_lob[13] << 78) | ... | rob_uop_pc_lob[0]) << 6) >> ((rob_head * 6) + 6))#[0..=5]
```

**Plan:** embedded multi-bit. arr=`rob_uop_pc_lob`, N=16, k=6, helper width u96. Extract to `_flat_rob_uop_pc_lob:u96`; replace the inline parenthesized flattener with the helper name.

**After (helper insertion at anchor + inline replacement):**
```pyrope
  mut _flat_rob_uop_pc_lob:u96 = 0
  for i in 0..=15 {
    _flat_rob_uop_pc_lob = _flat_rob_uop_pc_lob | (rob_uop_pc_lob[i] << (i * 6))
  }
  ... (other folds here) ...
  const io_commit_uops_0_out_pc_lob__w1 = (((_flat_rob_uop_pc_lob << 6) >> ((rob_head * 6) + 6)))#[0..=5]
```

**Measured:** PROVEN via cvc5 10.3s, 9 matched compact recurrences (one per multi-bit flatten in pyroAZ). +4 LoC per flatten (3 helpers = +12 LoC total for pyroAZ).

### Failure mode: LEC fails

If LEC returns REFUTED after applying the diff, the extraction broke a parenthesized sub-expression. Common causes:
- **Flattener not isolated by parens.** The outer `(...)` the skill matched actually contained more than the flattener (e.g. `(a | (flat) | b) & mask`). The replacement left `a | _flat_<arr> | b`, which is semantically the same ONLY if `|` is associative and commutative (it is for bitwise OR), but mis-counting parens can leave stray `)` that break the compile.
- **Shift multiplier mis-inferred.** k wrong, so the helper computes the wrong flattener (e.g. inferring k=5 when actual k=6).
- **Element width mismatch.** The arr elements are u5 but the shift pattern is `<< i*6` — mask-and-pack is required but the inline wasn't using `& 1`.

Recovery: restore the pre-extract snapshot. Re-emit the plan with the specific flattener and the corrected k / width. Retry once. If the second attempt also fails, log the flattener as "flatten-extract not applicable" and leave the inline expression as-is.

### Composition with `rename`, `wire-to-const`, `register-removal`, `band-fold`, `cascade-fold`

Composable with all previous rules. This rule is typically the LAST per-module cleanup pass before parametrization — it builds on `band-fold` and `cascade-fold` outputs (the arrays those rules produced become the targets of this rule's flatteners) and is itself a prerequisite for the parametric module header rewrite (not currently a skill — the mut-array `[16]uN = (0×16)` initializer is a Pyrope language gap, see Rob_rob16 pyroBA's open items).

---

## Rule: scalar-array-fold

Fold K scalar `reg <prefix>_<N>_<field>:<W>` declarations (for N=0..K-1) into **one** `reg <prefix>_<field>:[K]<W>:[ordering="old"]` array declaration, and rewrite every reference `<prefix>_<N>_<field>` → `<prefix>_<field>[N]`. **Netlist-invariant**: the LEC tier-2 state-pairing matcher collapses the K scalars to the array under "kind/init mismatch" or "no full match" notices, and still returns PROVEN. This is the BOOM pyroA/B/… driver pattern — the primary scalar-compaction rule for machine-emitted hierarchical designs.

**Validated (2026-10-27):**
- `IssueUnitCollapsing_1` pyroA: `is_available_0..7:u1` (8 scalars) → `[8]u1`. −7 LOC, PROVEN vs .sv (388/388 cones, 112 ms, commit `053d071`).
- `FetchBuffer` pyroA–E: 112 `fb_uop_ram_<0..7>_<field>` scalars across 14 field names → 14 `[8]uN` arrays. −98 LOC (−14.56 %), PROVEN pairwise + PROVEN vs .sv end-of-chain (132/132 cones, commit `67d94f0`).
- `BranchKillablePipeline_2` pyroA–E in flight: 61 of 112 `uops_<0..3>_bits_uop_<field>` scalars folded, −273 LOC (−26.84 %), PROVEN pairwise.

### Contract — allowed

- Replace K scalar `reg <prefix>_<N>_<field>:<W>` declarations (N=0..K-1) with **one** `reg <prefix>_<field>:[K]<W>:[ordering="old"]` array declaration placed in a stable position (after existing related array decls of the same prefix, or just after the `pub mod` line if none).
- Rewrite every reference `<prefix>_<N>_<field>` → `<prefix>_<field>[N]` using **word-boundary** matching (identifier chars `[A-Za-z0-9_]`; both EOL and prefix/suffix boundaries count as word breaks).
- **Batch multiple fields** in one invocation only when all share the same K and the same `<prefix>` naming scheme. Each field becomes an independent array; decls are inserted sequentially; refs are rewritten per field.
- Emit a **plan** (per-field decision + K + W + refcount; or SKIP + reason) and a **unified diff** before writing anything.

### Contract — must not

- **Do not drop the `[ordering="old"]` attribute.** Without it, the array lowers to a `mem:(K)xW` on the impl side, and the matcher cannot pair against the ref's K scalar flops → UNKNOWN. (Reference: `reference_pyrope_reg_array_semantic_diff.md`.)
- **Do not use `wire` for the array type.** `upass.tolg` rejects it: *"array `wire` is not supported — declare an array as `mut`/`reg`; a `wire` is a scalar net"*. If the family uses `wire` + forward-ref (reads before writes in source), SKIP it — the fold is blocked at the Pyrope language level.
- **Do not use `mut` without an explicit initializer.** `mut arr:[K]uW` without `= 0` fails compile: *"array ... is written before it has an initializer"*. If the fold must use `mut`, add `= 0`. For `reg` arrays, no initializer is needed (the `[ordering="old"]` attribute carries the semantic).
- **Do not use plain regex without word boundary.** sed `s/<prefix>_([0-9]+)_<field>/<prefix>_<field>[\1]/g` MISSES end-of-line identifiers like `uops_3_valid` at `io_resp_3_valid = uops_3_valid` (no trailing char). Use `sed -E "s/\<<prefix>_([0-9]+)_<field>\>/<prefix>_<field>[\1]/g"` with GNU-sed `\<...\>` word boundaries.
- **Do not batch fields with DIFFERENT K across one invocation.** Each K requires its own array decl; mixing them in one plan increases review surface without benefit.
- **Do not fold forward-referenced wires.** If a field's references include reads that appear *before* any write in source order, the family must stay as `wire` scalars (the array-wire gap applies).
- **Do not change module signatures, `io.*` field names, comments, or whitespace** beyond the decls+refs being folded.
- **Do not use this rule to fold scalar wires** declared with `wire ... :<W>` (no initializer, forward-refs allowed). Those are the `wire`-array-gap class — fold is blocked until Pyrope gains array-wire support.
- **Do not fold families with per-index reset init.** If any decl in the group carries an `:[initial=…]` or `:[reset_pin=…]` attribute with a non-zero or index-specific value (e.g. `reg state_0:u1:[initial=1,…]` while `state_1..5` initialize to 0; or BOOM CSRFile's `reg_pmp_3_addr=268435456` + `reg_pmp_7_addr=536870912` baked PMP region constants in the firtool-emitted ref), SKIP. Collapsing the group into a single `reg …:[K]uW:[ordering="old"]` array drops the per-index init and the LEC solver finds an initial-state counter-example — verdict UNKNOWN after a long BMC timeout. The matching Rocket CSRFile pyroA (committed as NCE, not PROVEN) is the published precedent; this skill guarantees PROVEN only, so the family must be SKIP'd here.
- **Do not fold families with sparse (non-dense-from-0) indices.** Patterns like `arb_uop_bits_fu_code_1`, `arb_uop_bits_fu_code_2` (MemExeUnit — indices start at 1, no `_0`) or RenameBusyTable's `wakeups_wu_valid_REG` + `wakeups_wu_valid_REG_2` + `wakeups_wu_valid_REG_4` (indices {implicit 0, 2, 4}) are legitimate domain-indexed scalars where the index encodes semantics (functional-unit lane number, pipeline stage ID). Folding them into `[K]uW` would misalign the index with the semantics and produce either a compile fail or a REFUTED LEC. SKIP.
- **Do not fold families with mixed-prefix naming.** When the group alternates between `<base>` (no numeric infix) and `<base>_<N>` (with infix) — RenameBusyTable's `wakeups_wu_bits_REG_uop_pdst` vs `wakeups_wu_bits_REG_1_uop_pdst` vs `wakeups_wu_bits_REG_2_uop_pdst` — the implicit "index 0" cannot be matched by the ref-rewrite sed without a bespoke pre-pass to normalize the names. SKIP unless the caller first runs `rename` to unify the naming.

### Detection algorithm

Given a target `.prp` file and (optionally) a `<prefix>` + `<field>` + `<K>` triple:

1. **Enumerate candidate families.** Grep every decl matching `^\s*reg\s+<prefix>_[0-9]+_<field>:<W>$`. Record (prefix, field, N, W) per decl.
2. **Group by (prefix, field, W).** For each group, let `S` = the sorted set of N values and `K` = max(S)+1. Require `S = {0, 1, …, K-1}` (dense, zero-indexed, no gaps) and `K ≥ 2`.
3. **Verify uniform width.** All K decls must have the same `W`. If widths disagree, the group is NOT a fold candidate — mark `SKIP_MIXED_WIDTH`.
4. **Per-index-init check.** Grep each decl for `:[initial=…]` or `:[reset_pin=…]`. If ANY decl in the group has a non-zero or index-specific init expression — `SKIP_PER_INDEX_INIT`. Also check the matching ref `.sv` for `initial begin … regX = …` blocks or Chisel `RegInit(<nonzero>)` emissions (counter-example PMP addresses in BOOM CSRFile). The ref-side init is the authoritative check because the ref is what LEC compares against; a pyroA `reg …:[K]uW:[ordering="old"]` has free random init that can't match a specific per-index init.
5. **Mixed-prefix check.** Enumerate every decl of form `<prefix>_<?suffix>` where `?suffix` is either empty or `_<N>_<field>`. If the group contains both the no-index form and `_<N>_` forms — `SKIP_MIXED_PREFIX`.
6. **Enumerate references.** For each `<prefix>_<N>_<field>` with N ∈ S, grep every occurrence that is NOT the decl line (word-boundary). Verify none are inside a `wire ... :<W>` declaration or a port list.
7. **Forward-ref check.** For each ref line L and the matching decl's would-be write line Lw: if `wire` ever introduced the field earlier, SKIP. For `reg` decls (the common case), this is a non-issue because reg values persist across cycles.
8. **Form the plan.** Per field: `FOLD` (K, W, refcount), `SKIP_WIRE_FAMILY` (array-wire gap), `SKIP_MIXED_WIDTH`, `SKIP_SPARSE_N` (gaps in S), `SKIP_TOO_FEW` (K < 2), `SKIP_PER_INDEX_INIT` (ref has baked per-index reset values), or `SKIP_MIXED_PREFIX` (implicit-index-0 names coexist with `_<N>_` forms).

### Rewrite procedure

For each `FOLD` field in order:

1. Delete all K scalar decls:
   ```sh
   sed -i -E "/^\s*reg\s+<prefix>_[0-9]+_<field>:<W>$/d" "$PRP"
   ```
2. Insert one array decl at a stable position. Default: after the last existing `reg <prefix>_<other_field>:[K]<W2>:[ordering="old"]` line of the same prefix. If none exists yet, insert directly after the `pub mod` header line.
   ```sh
   sed -i -E "/^<ANCHOR_PATTERN>$/a\\  reg <prefix>_<field>:[K]<W>:[ordering=\"old\"]" "$PRP"
   ```
3. Rewrite references:
   ```sh
   sed -i -E "s/\<<prefix>_([0-9]+)_<field>\>/<prefix>_<field>[\1]/g" "$PRP"
   ```
4. Verify no scalar refs remain:
   ```sh
   grep -cE "\<<prefix>_[0-9]+_<field>\>" "$PRP"   # must print 0
   ```

### Procedure

For each invocation, given a target `.prp` path and optional `<prefix>_<field>_<K>_<W>` tuples:

1. **Read** the file. If the caller supplied a specific field, scope the detection to that field; otherwise enumerate all fold candidates by prefix.
2. **Classify** each candidate family: `FOLD`, `SKIP_WIRE_FAMILY`, `SKIP_MIXED_WIDTH`, `SKIP_SPARSE_N`, `SKIP_TOO_FEW`, `SKIP_PER_INDEX_INIT`, or `SKIP_MIXED_PREFIX`.
3. **Emit the plan** — one line per field, with the decision and the reason. Example:
   ```
   FOLD      fb_uop_ram _ _inst          K=8 W=u32 refs=48   (anchor: pub mod line)
   FOLD      fb_uop_ram _ _debug_inst    K=8 W=u32 refs=48   (anchor: fb_uop_ram_inst array)
   SKIP      uops _ _valid               -- K=4 but forward-ref via wire would be needed
   ```
4. **Emit a unified diff** of the proposed rewrite. Do this in the chat first — do not write the file yet.
5. **Wait for the driver to accept.** The driver applies the write, then runs `lhd compile` + `lhd lec --impl <new_lg> --ref <previous_lg>` (incremental) or vs the SV ref (end-of-chain audit).

### Verification (driver's responsibility; the skill should remind)

After writing:
1. `lhd compile <PRP> --emit-dir lg:<new_lg> --workdir <wd>` — must PASS. If FAIL (common: array-wire gap, missing `= 0` on `mut`), restore the pre-fold snapshot and reclassify the field as SKIP.
2. `lhd lec --impl lg:<new_lg> --ref lg:<previous_lg> --top <MODULE>` (incremental) — must return PROVEN. Expected tier-2 notice: *"'<MODULE>' tier-2 unpaired state: ref{<prefix>_0_<field> ... <prefix>_<K-1>_<field>} impl{<prefix>_<field>}"* — this is the matcher reporting the group-to-array pairing, not a failure.
3. Record LOC delta: expect `K - 1` LOC saved per field folded (K scalars deleted, 1 array inserted).
4. End-of-chain audit after the last fold: `lhd lec --impl lg:<final_lg> --ref lg:<sv_ref_lg> --top <MODULE>` — must return PROVEN. SV ref must be compiled with `-DSYNTHESIS` to strip firtool `$error` system tasks.

### Worked example — FetchBuffer pyroA (one field, K=8)

**Before:**
```pyrope
pub mod FetchBuffer::[timecheck=false](clock:u1, reset:u1, ...) -> (...) {
  reg fb_uop_ram_0_inst:u32
  reg fb_uop_ram_0_debug_inst:u32
  ...
  reg fb_uop_ram_1_inst:u32
  ...
  reg fb_uop_ram_7_inst:u32
  ...
  if reset != 0 { ... } else {
    fb_uop_ram_0_inst = io_enq_bits_exp_insts_0
    ...
  }
  mut _mux_23:u32 = if (head & 1) != 0 { fb_uop_ram_0_inst } else { 0 }
  ...
}
```

**Plan:**
```
FOLD  fb_uop_ram _ _inst  K=8 W=u32 refs=48  (anchor: pub mod line)
```

**After:**
```pyrope
pub mod FetchBuffer::[timecheck=false](clock:u1, reset:u1, ...) -> (...) {
  reg fb_uop_ram_inst:[8]u32:[ordering="old"]
  reg fb_uop_ram_0_debug_inst:u32   // (not folded in this invocation)
  ...
  if reset != 0 { ... } else {
    fb_uop_ram_inst[0] = io_enq_bits_exp_insts_0
    ...
  }
  mut _mux_23:u32 = if (head & 1) != 0 { fb_uop_ram_inst[0] } else { 0 }
  ...
}
```

Net effect: 7 lines removed (8 decls − 1 array decl); 48 refs rewritten; semantics unchanged.

### Worked example — FetchBuffer pyroC (3 fields batched)

Same shape, batched. Input: three fields `is_rvc:u1`, `debug_pc:u40`, `is_sfb:u1` all with K=8 and anchor `fb_uop_ram_debug_inst`. The plan lists 3 FOLD lines; the diff inserts 3 array decls contiguously and rewrites 3×48=144 refs. One compile + one LEC pass covers the batch. Line delta: 3 × 7 = 21 lines removed.

### Failure mode: compile fails with "array wire is not supported"

The caller tried to fold a `wire`-family (forward-reference signals). Pyrope doesn't support array `wire` yet. Recovery: SKIP the family and leave it as scalars. Flag the module as "contains wire families; wait for Pyrope array-wire support before full fold."

### Failure mode: compile fails with "written before it has an initializer"

The caller used `mut` instead of `reg` on the array decl without `= 0`. Recovery: either switch to `reg …:[ordering="old"]` (correct for state), or add `= 0` to the `mut` decl (correct for combinational hoist).

### Failure mode: LEC returns UNKNOWN with "'<impl>_<field>' no full match"

Possible causes:
- Forgot `[ordering="old"]`: the array lowered to `mem:(K)xW` instead of K retained flops. Recovery: add the attribute, recompile, re-LEC.
- Ref SV side has reset-init (RegInit) that the pyroA side didn't preserve. Recovery: check `reference_boom_lec_conventions.md` for the init rules; the fold itself is correct but the module's reset handling may need a separate pyroX step.

### Composition with other rules

- Composable with every prior rule. Typically the FIRST pass on a machine-emitted BOOM module: fold scalar families, then apply `rename`, `wire-to-const`, `band-fold`, `cascade-fold`, `flatten-extract` in whatever order the module needs.
- `scalar-array-fold` output arrays are candidates for later `flatten-extract` if their contents are read in bit-vec OR flatteners.
- A module that has completed scalar-array-fold on all its reg families is ready for the parametric module-size rewrite (Rob_rob16 pyroBA pattern).

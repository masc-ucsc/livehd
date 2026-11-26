---
name: pyrope-ablation-rules
description: Apply one isolated Pyrope rewrite to one .prp file for a controlled comparison. Select rename, wire-to-const, register-removal, band-fold, cascade-fold, flatten-extract, or scalar-array-fold.
---

# Isolated Pyrope rewrites

Use this skill when a caller wants to measure one named change. For a translation or redesign that can combine changes across modules, use the [Pyrope cleanup guide](../pyrope/cleanup.md).

## Running a comparison

1. Take the rule name and target file from the caller. If the rule is missing, ask which of the seven rules to apply. Record whether the input is the original or a previously accepted revision.
2. Identify candidates, their declarations and all reads and writes. Use identifier boundaries and scope. For nested branches, match complete syntax blocks. A line-prefix match can capture the closing branch of a different write.
3. Show the plan and unified diff to the experiment driver. Include the old-to-new mapping, the reason for each skipped candidate, and any exceptional lane. The driver accepts the patch before applying it. Keep the module signature, public fields, unrelated statements and other rules out of the diff.
4. Compile both snapshots with the same top, parameters and settings. Run LEC on the common observable interface. Record the verdict and any proof bound. A counterexample needs investigation. An inconclusive result leaves equivalence open. Preserve all assertions and compare reset, hold and active cycles in simulation.
5. Report the source-line delta and measure the outcome under study. For compile time, distinguish cold and warm runs. For simulation, report cycles per second on the same stimulus after setup. For mapped area or delay, use identical libraries and options. A proof of equal behavior establishes neither equal mapped structure nor equal speed.

## rename

Rename generated temporaries by what their expressions compute. For example, an equality test on a response tag might become `tag_matches`. Preserve emitter suffixes when consumers use them. Check for collisions and update all uses of the symbol, including uses at end of line. Keep existing meaningful names, operation order, types and public ports.

## wire-to-const

Fold a two-step scalar wire declaration and its unconditional driver into a typed `const` at the driver site when every read follows it in the same scope. For example, `wire selected:Bool = nil` followed by `selected = a == b` becomes `const selected:Bool = a == b`. Preserve the declared type and the expression's evaluation point.

Keep a wire if anything reads it before the driver, if a conditional driver relies on wire semantics, or if the replacement would hide the name in a nested scope. Skip partial field drives and bundle mirrors for this rule. A wire has one driver. Multiple apparent drivers warrant checking the source and diagnostics before changing its declaration kind.

## register-removal

Remove a register declaration and its next-value assignment only when its sole reads feed that same assignment and eliminating the assignment has no other effects. Check guards, indices, assertions, test and hierarchical observations, and every output. A free-running counter that also feeds an assertion is observable. Restrict each pass to the original candidates to keep cascading removals attributable. Removing unobserved state may change resource usage even when outputs are equivalent.

## band-fold

Fold four or more parallel scalar expressions into a typed lane array filled by a bounded `for`. Build a table of actual identifier, lane and expression for every entry. Verify operator, widths, sign and lane-dependent constants across the entire table. Emit the array and loop at a point where all inputs are available and all consumers can still read the result.

Rewrite each downstream use through that table. A numbering stride suggests candidates, but the last identifier can break the stride. A second band of pure aliases can share the same array if the lane mappings agree. Preserve a mask, conversion or permutation in an alias. Skip heterogeneous bodies and loop-carried dependencies such as a prefix scan for this parallel rule.

## cascade-fold

Identify four or more entries with the same guarded writeback and priority order. Capture each complete `if/elif` chain, including nested branches, holds and multi-field updates. Check for reads between writes and determine whether each read sees the old value or a forwarded update before moving any assignment.

For the isolated hybrid transform, compute next values per lane in a loop and retain the original scalar writeback sites. Share a pure expression outside the loop only when its operands have the same values at every entry. Preserve write enables whenever the presence of a write matters. Skip candidates whose read/write order or priority would change.

In a general translation, Pyrope also supports writing a register array directly in a loop. The compiler unrolls such a loop. For example, the disabled lanes here receive no write:

```pyrope
for i in 0..<4 {
  if en { entries[i] = d ^ i }
}
```

## flatten-extract

Extract a shift-and-OR expression covering four or more contiguous lanes of one array. Verify the exact set of indices, lane width, masks, signedness and shifts. For four unsigned bytes with lane zero at the least significant end, the packing loop can be written as:

```pyrope
mut packed_word:U32 = 0
for i in 0..<4 {
  packed_word#[(8*i)..+8] = bytes[i]
}
```

Place the helper after the array has its intended value and before its first use. Reuse it only at sites that observe the same array version. Replace precisely the grouped expression, retaining neighboring operators. Skip gaps, overlapping lanes, sign extension into adjacent lanes or ambiguous grouping.

## scalar-array-fold

Fold two or more same-width scalar registers with dense, zero-based indices into a register array. Build an explicit mapping from declarations to indices, including every read and write. Skip sparse indices, ambiguous implicit lane zero, mixed widths and wire families. Batch fields only when their indexing agrees.

Match the original clocks, reset values, enables and same-cycle read semantics. Use `ordering="old"` when all reads must see committed state, including a read after a source write. The initializer independently defines reset behavior. Preserve a missing reset with `= nil`. Skip a family with per-entry resets in the isolated rule. In a general translation, a compile-time array of reset values can express that family. Check read/write collisions and reset in simulation, then inspect the actual proof verdict for the new state representation.

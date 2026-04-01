# Pyrope Test Catalogue — Spring 2026

## Summary

Catalogued pyrope examples through the upass pipeline to identify failing cases.

### Results

| Category | Status | Count | Notes |
|----------|--------|-------|-------|
| **LGraph IR** | ✓ Pass | 3/3 | `fold_neutral` pass converges correctly |
| **LNAST IR** | ✗ Crash | 0/3 | `constprop` pass crashes on tuple/function structures |

---

## Detailed Results

### ✓ Passing: LGraph fold_neutral

Tests through pipeline: `pyrope → lnast_tolg → upass(lgraph, fold_neutral)`

| File | Result | Notes |
|------|--------|-------|
| `pass/upass/tests/lgraph_neutral.prp` | ✓ Converge | Neutral simplification rewrites |
| `pass/upass/tests/lgraph_sum_const.prp` | ✓ Converge | Sum fold with compile-time constants |
| `pass/upass/tests/lgraph_dce.prp` | ✓ Converge | Dead code elimination |

**Implication:** LGraph IR passes work correctly. The test suite for lgraph fold operations is solid.

---

### ✗ Failing: LNAST constprop

Tests through pipeline: `pyrope → upass(lnast, constprop)`

| File | Crash Type | Symptoms |
|------|-----------|----------|
| `inou/pyrope/tests/funcall.prp` | Assertion | `symbol_table.cpp:24` `assertion bundle failed` |
| `inou/pyrope/tests/simple_tuple.prp` | Assertion | `symbol_table.cpp:24` `assertion bundle failed` |
| `inou/pyrope/tests/hier_tuple_io.prp` | Assertion | `symbol_table.cpp:24` `assertion bundle failed` |

**Root Cause:** Symbol_table::set() receives a null Bundle pointer when constprop attempts to store results from tuple or function-call nodes in the LNAST.

**Pyrope → LNAST Translation Issue:**
- Pyrope constructs like tuples `(x, foo = y, bar = 4)` and function calls are translated to LNAST tuple/struct nodes
- Constprop's process_assign() does not handle tuple_get, tuple_set, or func_call operations correctly
- When constprop encounters these node types, current_prim_value() may return invalid or attempt to create a null Bundle
- St.set(var, nullptr) triggers the assertion

---

## Files Involved

### Test Pyrope Files
- **LGraph tests:** `pass/upass/tests/*.prp` (6 files, all work with fold_neutral)
- **General pyrope:** `inou/pyrope/tests/*.prp` (40+ files, sampled 3 with constprop)

### Failing Code Paths
- **Crash:** `upass/constprop/upass_constprop.cpp` — `process_assign()` → `current_prim_value()` on unhandled node type
- **Symbol table:** `lnast/symbol_table.cpp:24` — Assertion `I(bundle)` in `set(std::string_view, std::shared_ptr<Bundle>)`

---

## Next Steps

### P1: Fix LNAST constprop crashes (blocking)
1. Debug why tuple/function-call nodes cause null Bundle pointers
2. Implement missing process_tuple_get, process_tuple_set, process_func_call handlers
3. Add guards in process_assign() to skip or defer unhandled node types
4. Regression test: all 3 failing pyrope files should pass through upass(lnast, constprop)

### P2: Expand LNAST test coverage
- Add constprop GTest cases for tuple and function-call structures
- Ensure Symbol_table never receives null Bundle pointers

### P3: Cross-IR compatibility
- Verify that LGraph and LNAST pipelines produce equivalent constant propagation results
- Possibly consolidate logic via `upass_shared.hpp` (like fold_sum_const_shared)

---

## Testing Commands

**Test LNAST constprop on pyrope file:**
```sh
printf 'inou.pyrope files:inou/pyrope/tests/funcall.prp |> pass.upass ir:lnast order:constprop max_iters:3\nquit\n' \
  | ./bazel-bin/main/lgshell
```

**Test LGraph fold_neutral on pyrope file:**
```sh
printf 'inou.pyrope files:pass/upass/tests/lgraph_neutral.prp |> pass.lnast_tolg |> pass.upass ir:lgraph order:fold_neutral max_iters:3\nquit\n' \
  | ./bazel-bin/main/lgshell
```

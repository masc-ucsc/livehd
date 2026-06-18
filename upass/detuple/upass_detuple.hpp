//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>

#include "lnast.hpp"

// Standalone LNAST->LNAST pass that LOWERS tuple-typed body declarations into
// per-field scalar leaves, BEFORE the lambda split (so a file-scope named type
// and the module that uses it are still in one tree, and the type's field
// layout can be resolved by a purely local, structural read of its definition —
// there is no symbol table this early).
//
// LGraph (the lowering target) has no tuples, but LNAST memories/arrays may be
// declared with any tuple element type. This pass rewrites, for every tuple-
// typed declaration whose fields resolve to concrete scalar types:
//
//   declare(mem, comp_type_array(q_type, '[N]'), mode)   (q_type = (a:u32,b:u1))
//     -> declare(mem.a, comp_type_array(u32, '[N]'), mode)
//        declare(mem.b, comp_type_array(u1,  '[N]'), mode)
//   store(mem, idx, 'a', v)        -> store(mem.a, idx, v)         (field write)
//   store(mem, nil)                -> store(mem.a, nil) ; store(mem.b, nil)
//   tuple_get(t, mem, idx)         (index step, dropped — fused below)
//   tuple_get(d, t, 'a')           -> tuple_get(d, mem.a, idx)     (fused read)
//
// Scope is MEMORIES ONLY. A scalar tuple variable (`mut v:Named`) is left
// alone: constprop already tracks and resolves it for the comptime case, and
// splitting it this early breaks whole-tuple assignment, field defaults, and
// the unknown-field error path. (Lowering a runtime-valued scalar tuple is a
// separate, pre-existing gap, not addressed here.)
//
// Anything it cannot fully resolve (a non-concrete field type, a nested tuple
// field, a multi-dim tuple array, a whole-tuple element write, an index temp
// with a non-field consumer) is left verbatim, so downstream behavior is
// unchanged for those — no regression, just no lowering yet.
class uPass_detuple {
public:
  // Run the pass on `lnast` in place. A no-op when the tree has no tuple-typed
  // declaration this pass can lower (the common case stays zero-cost).
  static void run(const std::shared_ptr<Lnast>& lnast);
};

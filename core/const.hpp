// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "hlop/dlop.hpp"
#include "hlop/spool_ptr.hpp"

// Short alias used throughout LiveHD for the live-debug constant value type.
// `Dlop` is now a full value type (hlop @ 437dfb26): copy/move constructible,
// inline storage for size<=1, pool storage for larger values, and assignment
// from `spool_ptr<Dlop>` (the type returned by all op factories). Storage is
// inline so containers `absl::flat_hash_map<K, Const>` stay single-indirection.
//
// Method-call convention:
//   Const x; x.method()         — direct call on value
//   auto y = a.add_op(b);       — y is spool_ptr<Dlop>; use y->method()
//   Const z = a.add_op(b);      — assigns via operator=(spool_ptr&&); z.method()
//
// Migration notes from prior Lconst:
//   Lconst x                   -> Const x                   // value-initialized
//   Lconst::from_pyrope(s)     -> Dlop::from_pyrope(s)      // returns spool_ptr<Dlop>
//   Lconst::invalid()          -> Dlop::invalid()
//   Lconst::nil()              -> Dlop::nil()
//   Lconst(int64_t v)          -> Dlop::create_integer(v)
using Const = Dlop;

// Cross-type structural-equality helpers — Dlop's `operator==`/`!=` were
// removed because they implicitly conflated tri-state outcomes (e.g.
// `0sb1?` vs `0sb11` could plausibly be true or false) into a hard bool.
// For unknown-aware logical equality use `is_known_eq` (false if either
// side has unknowns); for "structurally identical bit pattern incl.
// unknowns" use `same_repr`. These helpers route through `same_repr` so
// gtest EXPECT_EQ-style call sites that want bit-identity still compile.
inline bool dlop_same_repr(const Dlop& a, const Dlop& b) { return a.same_repr(b); }
inline bool dlop_same_repr(const Dlop& a, const spool_ptr<Dlop>& b) { return a.same_repr(*b); }
inline bool dlop_same_repr(const spool_ptr<Dlop>& a, const Dlop& b) { return a->same_repr(b); }
inline bool dlop_same_repr(const spool_ptr<Dlop>& a, const spool_ptr<Dlop>& b) { return a->same_repr(*b); }

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "hlop/dlop.hpp"
#include "hlop/spool_ptr.hpp"

// Short alias used throughout LiveHD for the live-debug constant value type.
// Backed by hlop::Dlop in a thread-local pool; ref-counted via spool_ptr.
//
// Replaces the prior Lconst by-value type. Migration notes:
//   Lconst x                   -> Const x                   // null spool_ptr default
//   Lconst::from_pyrope(s)     -> Dlop::from_pyrope(s)      // returns Const
//   Lconst::invalid()          -> Dlop::invalid()
//   Lconst::nil()              -> Dlop::nil()
//   Lconst(int64_t v)          -> Dlop::create_integer(v)
//   a.add_op(b)                -> a->add_op(b)
//   a == b   (value compare)   -> *a == *b
using Const = spool_ptr<Dlop>;

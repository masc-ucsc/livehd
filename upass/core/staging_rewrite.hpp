//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The post-walk staging rewrite seam (todo 2a-treeaeg). uPass_runner::run()
// hands the complete staging tree to an injected step after the source walk and
// before the post-walk DCE: the coalescer and detuple have flushed, every pass
// of the walk has already judged the source form, and the DCE that follows
// sweeps every definition the step orphans. The cycle checks are the exception:
// upass/timecheck and tolg (its `@[N]` lowering and LG checker) judge the
// post-step body, so the step leaves what they read as written (`timed`). The
// step lives in its own library (upass/aeg links pass/aegraph); the runner only
// sees this function pointer, so it keeps no dependency on the e-graph engine.

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "lnast.hpp"

namespace upass {

struct Staging_rewrite_stats {
  uint64_t units         = 0;  // units the step ran on
  uint64_t stmts         = 0;  // statements visited
  uint64_t imported      = 0;  // value statements imported into the e-graph
  uint64_t forwarded     = 0;  // statements whose readers now read an existing value (CSE, copy, fold)
  uint64_t renamed       = 0;  // reader operands renamed to another name holding the same value
  uint64_t consts        = 0;  // reader operands replaced by the constant they hold
  uint64_t respelled     = 0;  // statements rewritten in place to a rule's result
  uint64_t refused_arity = 0;  // the result has fewer operands than the statement (children are never deleted)
  uint64_t refused_sum   = 0;  // a Sum of more than two operands (upass.tolg sizes every Sum as max+1)
  uint64_t refused_kind  = 0;  // the result's kind (bool, integer) differs from the destination's
  uint64_t refused_class = 0;  // the result leaves an op class tolg keys on (partial-write window, output slice chain)
  uint64_t refused_spell = 0;  // the result needs a node no statement can spell (Not, Mux, ...) or an unnamed operand
  uint64_t budget_hit    = 0;  // units whose rule budget ran out
  uint64_t ns            = 0;  // wall time
  // Per aegraph Rule_id (slot 0 unused): tried, matched, applied.
  std::vector<std::array<uint64_t, 3>> rules;

  Staging_rewrite_stats& operator+=(const Staging_rewrite_stats& o) {
    units         += o.units;
    stmts         += o.stmts;
    imported      += o.imported;
    forwarded     += o.forwarded;
    renamed       += o.renamed;
    consts        += o.consts;
    respelled     += o.respelled;
    refused_arity += o.refused_arity;
    refused_sum   += o.refused_sum;
    refused_kind  += o.refused_kind;
    refused_class += o.refused_class;
    refused_spell += o.refused_spell;
    budget_hit    += o.budget_hit;
    ns            += o.ns;
    if (rules.size() < o.rules.size()) {
      rules.resize(o.rules.size(), {0, 0, 0});
    }
    for (size_t i = 0; i < o.rules.size(); ++i) {
      for (size_t k = 0; k < 3; ++k) {
        rules[i][k] += o.rules[i][k];
      }
    }
    return *this;
  }
};

struct Staging_rewrite_ctx {
  Lnast&               staging;  // edited in place: statements are retyped and operands renamed, never added or removed
  const Lnast_tree_io& io;       // the unit's ports (the staging tree carries none)
  std::string_view     unit;
  // The unit's cycle checks run on the post-step body (a mod or pipe without
  // `::[timecheck=false]`): upass/timecheck after the queue, tolg's `@[N]`
  // lowering and its LG checker. They read dependencies, not values, so the
  // step keeps what they judge as written.
  bool                 timed = false;
  bool                 pipe  = false;  // a pipe: its plain regs are feedforward stages
};

using Staging_rewrite_fn = Staging_rewrite_stats (*)(const Staging_rewrite_ctx&);

}  // namespace upass

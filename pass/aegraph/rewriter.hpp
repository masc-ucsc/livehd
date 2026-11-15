//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The rule-application core of the e-graph engine, free of the LGraph: it folds,
// hash-conses and rewrites e-nodes with the registered rules (runtime/match.hpp).
// The LGraph engine (aegraph.cpp) imports a graph body into the e-graph and
// elaborates the result back; a tree front end (an LNAST block) drives the same
// core, so a rule is written once and runs on both IRs.

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "aegraph.hpp"
#include "runtime/match.hpp"

namespace livehd::aegraph {

namespace detail {
class Checker;  // check=unions|full value evidence (aegraph.cpp)
}

class Rewriter : public rt::Builder {
public:
  Rewriter(const Options& options, std::string name);
  ~Rewriter() override;
  Rewriter(const Rewriter&)            = delete;
  Rewriter& operator=(const Rewriter&) = delete;

  // ---- rt::Builder: what the rules may do ----
  Class_id           make(Ntype_op op, std::span<const Operand> ops) override;
  Class_id           konst(const Dlop& value) override { return eg_.add_const(value); }
  [[nodiscard]] bool single_use(Class_id cls) const override { return cls < readers_.size() && readers_[cls] == 1; }
  [[nodiscard]] bool chain_inner(Class_id cls) const override { return cls < chain_inner_.size() && chain_inner_[cls] != 0; }
  [[nodiscard]] bool guard_scope() const override { return opt_.guard_scope && opt_.unstamped_ok && !opt_.preflight; }

  // The rewrite rules of one new e-node (depth = rule-built nesting, 0 for an
  // original node). Returns the class that stands for the node afterwards.
  Class_id rewrite(Class_id cls, uint32_t depth);

  [[nodiscard]] Egraph&       egraph() { return eg_; }
  [[nodiscard]] const Egraph& egraph() const { return eg_; }
  [[nodiscard]] const Stats&  stats() const { return stats_; }
  [[nodiscard]] std::string   describe(Class_id cls) const;

protected:
  // Import one ORIGINAL e-node (a statement of the front end's IR, not a rule
  // result): constant analysis, normalization (operands sorted by bank then class)
  // and hash-consing with `attrs`, with NO readers bump and NO rules. The front end
  // then sets readers_[raw] / chain_inner_[raw] from what it knows about the
  // node's readers and, for a created node, calls rewrite(cls, 0) with
  // cur_attrs_ = attrs, current_rule_ = kNoRule and depth_ = 0: chain_inner must be
  // in place before the rules run, which make() cannot do (flatten absorbs a whole
  // chain at its top only when the inner links say so, keeping the pool linear).
  // folded: a forward or a fully known constant; `cls` is then the operand class or
  // the constant and no e-node exists.
  struct Original {
    Class_id cls     = kNoClass;
    Class_id raw     = kNoClass;  // the hash-consed class before any alias
    bool     created = false;     // a new e-node (false: a hash-cons hit, or folded)
    bool     folded  = false;
  };
  Original add_original(Ntype_op op, std::span<const Operand> ops, const Enode_attrs& attrs);

  // Per-class side tables grow with the e-graph; a front end with tables of its
  // own extends this.
  virtual void ensure(Class_id cls);
  void         check_pair(Class_id a, Class_id b, rt::Rule_id rule);

  Options                          opt_;
  std::string                      name_;
  Egraph                           eg_;
  std::unique_ptr<detail::Checker> union_checker_;
  Stats                            stats_;

  std::vector<uint8_t>  enabled_;  // per Rule_id
  bool                  rules_active_ = false;
  rt::Rule_id           current_rule_ = rt::kNoRule;  // the rule whose body is running
  Enode_attrs           cur_attrs_;                   // attrs the rule's new e-nodes inherit
  uint32_t              depth_       = 0;
  uint64_t              rewrites_    = 0;
  bool                  cutoff_      = false;  // max_rewrites reached
  uint64_t              work_used_   = 0;
  uint64_t              work_budget_ = 0;
  bool                  budget_hit_  = false;
  uint64_t              check_work_  = 0;  // class evaluations spent by check_pair (bounded)
  std::vector<uint32_t> readers_;          // per class: readers in the graph being built
  std::vector<uint32_t> rule_readers_;     // per class: the part of readers_ that are rule-built e-nodes (an alt may lose)
  std::vector<uint8_t>  chain_inner_;      // per class: its one reader is a same-op, same-attrs cell (absorbs it)
};

}  // namespace livehd::aegraph

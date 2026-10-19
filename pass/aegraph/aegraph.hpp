//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// pass/aegraph -- a Cranelift-style acyclic e-graph ("aegraph") constant
// propagation engine for LGraph, the alternative to pass/cprop behind
// `--set compile.cprop.engine=aegraph` (todo/livehd/2a-aegraph.html).
//
// The engine (milestones B, D):
//
//   1. pre-pass   comb SCC (false-cycle) detection; members are opaque leaves
//   2. build      forward walk; every pure node is normalized (commutative
//                 banks sorted, multiplicity kept) and hash-consed on
//                 op + operand classes + color + synth_policy + proven; the
//                 constant analysis folds a node ONLY to a fully known value
//                 (the P7 contract: a '?' literal is never split). A node that
//                 is NEW in the e-graph then meets the rewrite rules of its root
//                 op (runtime/match.hpp, rules/*.cpp): a `simplify` rule replaces
//                 the class (subsume), an `alt` rule unions its result with it
//   3. extract    the cheapest member of each union class under the cost profile
//                 (cost/synth.cost or cost/sim.cost: width- and sharing-aware, ties keep
//                 the original); a class without unions is its first member
//   4. elaborate  diff against the original graph: matching nodes are reused
//                 (nids, names and srcids survive), duplicates are retired,
//                 nodes only a rule built are created, the loop-carry guard is
//                 kept, width stamps are carried and dead pure nodes are deleted
//
// The post steps (pack canonicalization, vector-mux narrowing, muxctx, flop
// enable folding) are the classic implementations (Cprop::do_post); loop_hoist
// is the library-wide classic pass. pass.aegraph (pass_aegraph.cpp) wires the
// stages together.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "cell.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"

namespace livehd::aegraph {

using Class_id = uint32_t;
using Enode_id = uint32_t;
inline constexpr Class_id kNoClass = UINT32_MAX;
inline constexpr Enode_id kNoEnode = UINT32_MAX;

namespace rt {
struct Analysis_cache;  // runtime/analyses.hpp: the lazy facts and lane layouts of the classes
}

// Per-rule counters (thread-local to one engine run, merged after the workers
// join). `tried` = the rule body ran; `matched` = it produced at least one result;
// `applied` = a result was committed (a subsume or a union); `subsumed` = the
// committed results that replaced the class; `limit_rejected` = results dropped
// by a limit (eclass_limit, matches_limit, max_rewrites); `extracted` = e-nodes
// the rule built that the extraction kept and the elaborator created.
struct Rule_counters {
  uint64_t tried          = 0;
  uint64_t matched        = 0;
  uint64_t applied        = 0;
  uint64_t subsumed       = 0;
  uint64_t limit_rejected = 0;
  uint64_t extracted      = 0;

  Rule_counters& operator+=(const Rule_counters& o) {
    tried          += o.tried;
    matched        += o.matched;
    applied        += o.applied;
    subsumed       += o.subsumed;
    limit_rejected += o.limit_rejected;
    extracted      += o.extracted;
    return *this;
  }
};

// The engine knobs (`compile.aegraph.*`). The defaults are the contract: the
// Cranelift limits (rewrite_depth, matches_limit, eclass_limit, extractor_fuel),
// the rules all on, no bisection.
struct Options {
  enum class Check : uint8_t { off, unions, full };
  enum class Cost : uint8_t { synth, sim };

  std::string rules_disable;           // `+`-separated rule names or `group:NAME`
  std::string rules_only;              // same syntax; empty = every rule
  uint64_t    max_rewrites   = 0;      // 0 = unlimited; else stop applying rules after N (per graph, forward-walk order)
  std::string bisect_graph;            // limit the max_rewrites cutoff to this graph (empty = every graph)
  std::string explain;                 // print the rule chain behind each extracted node built by this rule
  Check       check          =
#ifdef NDEBUG
      Check::off;
#else
      Check::unions;
#endif
  uint32_t    rewrite_depth  = 5;      // nested rule-built e-nodes that still meet rules
  uint32_t    matches_limit  = 5;      // alt results kept per e-node
  uint32_t    eclass_limit   = 5;      // members of one class (a union's size)
  uint32_t    extractor_fuel = 500;    // tree-cost saturation of the extractor
  uint32_t    edge_budget    = 32;     // rule work (operands scanned) allowed per input edge of the graph
  Cost        cost           = Cost::synth;  // the extraction cost profile (cost/<name>.cost)
  bool        choices        = false;  // `?` choice rules (off until they ship)
  // The LEC preflight (formal.lec.preflight_engine=aegraph): no pass.bitwidth follows, so every cell the
  // elaborator creates has to carry a width stamp -- the contract of classic Cprop{false}. A new interior cell
  // is stamped from the range analysis; when a cell cannot be stamped the engine REFUSES the graph (leaves
  // it as the pre-pass produced it, Stats::refused = 1) and the caller falls back to classic.
  bool        preflight      = false;
  // classic scalar_mux's accumulation guard (`mux(s, a, a OP x)` -> `a OP mux(s, id, x)`) is a rolled-loop rule: it
  // runs only inside a lifted loop body (guard_scope) and only where bitwidth follows and may size the new Mux
  // (unstamped_ok, the pre-bitwidth slot: Cprop's low_lanes).
  bool        guard_scope    = false;
  bool        unstamped_ok   = false;
};

// The registered name of a cost profile (the file cost/<name>.cost).
[[nodiscard]] constexpr const char* profile_name(Options::Cost cost) { return cost == Options::Cost::sim ? "sim" : "synth"; }

// What one engine run did to one graph body.
struct Stats {
  size_t nodes_in       = 0;  // nodes in the body before the engine ran
  size_t nodes_out      = 0;  // nodes in the body after the engine ran
  size_t pure           = 0;  // nodes that entered the e-graph as pure e-nodes
  size_t skeleton       = 0;  // nodes kept as skeleton leaves (IO, state, Sub, ...)
  size_t opaque         = 0;  // pure-looking nodes held out because they sit on a (false) cycle
  size_t classes        = 0;  // e-classes at the end of the build
  size_t cse_merged     = 0;  // duplicates replaced by their class representative
  size_t const_folded   = 0;  // nodes replaced by a fully known constant
  size_t forwarded      = 0;  // nodes replaced by an existing operand (const-selector Mux, identity folds)
  size_t kept_twin      = 0;  // merges declined: both twins are named, or a loop-carry/parallel-edge guard
  size_t dead_removed   = 0;  // dead pure nodes deleted
  size_t consts_created = 0;  // constants the elaborator had to create
  size_t rules_fired    = 0;  // rewrite-rule firings (applied results)
  size_t aeg_bytes      = 0;  // bytes in use by the e-graph tables (Egraph::bytes), the peak
  size_t aeg_cap_bytes  = 0;  // the same tables at capacity (Egraph::capacity_bytes)
  size_t rewritten      = 0;  // original nodes rebuilt or replaced by a rule's result
  size_t unions         = 0;  // union classes made by alt rules
  size_t depth_limited  = 0;  // e-nodes that skipped the rules at rewrite_depth
  size_t budget_hit     = 0;  // bodies whose edge_budget ran out
  size_t checks         = 0;  // check=: vector comparisons done
  size_t check_skipped  = 0;  // check=: comparisons skipped (a '?' or an unevaluable cone)
  size_t refused        = 0;  // preflight: graphs left unelaborated because a new cell could not be stamped
  size_t stamped        = 0;  // preflight: new interior cells stamped from the range analysis
  std::vector<Rule_counters> rule_counts;  // indexed by Rule_id (slot 0 unused)

  Stats& operator+=(const Stats& o) {
    nodes_in       += o.nodes_in;
    nodes_out      += o.nodes_out;
    pure           += o.pure;
    skeleton       += o.skeleton;
    opaque         += o.opaque;
    classes        += o.classes;
    cse_merged     += o.cse_merged;
    const_folded   += o.const_folded;
    forwarded      += o.forwarded;
    kept_twin      += o.kept_twin;
    dead_removed   += o.dead_removed;
    consts_created += o.consts_created;
    rules_fired    += o.rules_fired;
    aeg_bytes      += o.aeg_bytes;
    aeg_cap_bytes  += o.aeg_cap_bytes;
    rewritten      += o.rewritten;
    unions         += o.unions;
    depth_limited  += o.depth_limited;
    budget_hit     += o.budget_hit;
    checks         += o.checks;
    check_skipped  += o.check_skipped;
    refused        += o.refused;
    stamped        += o.stamped;
    if (rule_counts.size() < o.rule_counts.size()) {
      rule_counts.resize(o.rule_counts.size());
    }
    for (size_t i = 0; i < o.rule_counts.size(); ++i) {
      rule_counts[i] += o.rule_counts[i];
    }
    return *this;
  }
};

// One operand of an e-node: the sink BANK it occupies (Ntype::sink_bank: the
// bank for a commutative cell, the port id for a positional one) and the class
// that feeds it.
struct Operand {
  uint32_t bank = 0;
  Class_id cls  = kNoClass;

  friend bool operator==(const Operand&, const Operand&) = default;
  friend auto operator<=>(const Operand&, const Operand&) = default;
};

// The hash-cons key minus its operands. `policy` indexes Egraph::policies()
// (0 = no synth_policy attribute).
struct Enode_attrs {
  bool     has_color  = false;
  bool     has_proven = false;
  int32_t  color      = 0;
  uint32_t proven     = 0;
  uint32_t policy     = 0;

  friend bool operator==(const Enode_attrs&, const Enode_attrs&) = default;
};

// 20 bytes: the attrs are an index into Egraph::attrs() (almost every e-node
// has the default ones) and the hash-cons table is a flat array of Enode_id.
struct Enode {
  uint32_t first_operand = 0;  // offset into the operand pool
  uint32_t n_operands    = 0;
  Class_id cls           = kNoClass;
  uint32_t attr          = 0;  // Egraph::attrs() index
  Ntype_op op            = Ntype_op::Invalid;
  uint16_t rule          = 0;  // creating Rule_id; 0 = the original node
};

// Pure data: no LGraph handles. Classes are created in forward-walk order, so
// class ids are deterministic.
class Egraph {
public:
  Egraph();
  ~Egraph();
  Egraph(const Egraph&)            = delete;
  Egraph& operator=(const Egraph&) = delete;
  Egraph(Egraph&&)                 = delete;
  Egraph& operator=(Egraph&&)      = delete;

  enum class Kind : uint8_t {
    leaf,     // a skeleton driver pin, or any value the engine cannot see through
    literal,  // a '?'-bearing constant: ONE const node (Q5), never merged by value
    konst,    // a fully known constant, interned by value
    node,     // a hash-consed pure e-node
    uni,      // a union of two classes (an `alt` rule's result with the class it extends)
  };

  // 20 bytes. `first` (node) and `alt_a` (uni) never coexist; the three-valued
  // analysis value lives in Egraph::values_ (absent = a non-constant class).
  struct Eclass {
    union {
      Enode_id first = kNoEnode;  // the e-node of a node class
      Class_id alt_a;             // uni: the extended class (the original side)
    };
    Class_id alt_b = kNoClass;    // uni: the rule's result
    Class_id alias = kNoClass;    // set once a rewrite replaced or extended this class
    uint32_t value = UINT32_MAX;  // index into Egraph::values_
    uint16_t members = 1;         // e-nodes the class stands for (a union sums its sides)
    Kind     kind    = Kind::leaf;
  };

  // A fresh class that is never merged with anything.
  Class_id add_leaf();
  // A leaf that is the output of a state element (Flop, Latch, Memory, Sub, ...): rules that move logic around a
  // held value must not touch it (the latch contract, enableopt's hold regions).
  void               mark_state(Class_id cls) { state_.insert(cls); }
  [[nodiscard]] bool is_state(Class_id cls) const { return state_.contains(cls); }
  // A '?'-bearing constant: a fresh class per const node.
  Class_id add_literal(const Dlop& value);
  // A fully known constant, interned by value (same_repr).
  Class_id add_const(const Dlop& value);
  // Hash-cons a pure node. `created` tells whether a new class was minted. A hit
  // returns the class a rewrite left in its place (the alias chain's end); `raw`,
  // when given, receives the class the e-node itself belongs to.
  Class_id intern(Ntype_op op, const Enode_attrs& attrs, std::span<const Operand> operands, bool& created,
                  Class_id* raw = nullptr);
  // Attach the analysis value of a node class (three-valued, may hold '?').
  void     set_value(Class_id cls, Dlop value);
  // A union of `a` (the class being extended) and `b` (a rule's alternative);
  // the analysis value is a's. Neither side changes: an aegraph never rebuilds.
  Class_id add_union(Class_id a, Class_id b);
  // Every later reference to `from` (a hash-cons hit, a rule match) reads `to`:
  // `to` replaced `from` (subsume) or extends it (union). `to` is always newer.
  void     set_alias(Class_id from, Class_id to) { classes_[from].alias = to; }
  [[nodiscard]] Class_id resolve(Class_id id) const {
    while (classes_[id].alias != kNoClass) {
      id = classes_[id].alias;
    }
    return id;
  }
  // The non-union classes under `id` (the side `a` first), at most `limit` of
  // them; stops early when `fn` returns true. Returns whether it stopped.
  template <typename Fn>
  bool for_each_member(Class_id id, size_t limit, Fn&& fn) const {
    Class_id stack[64];
    size_t   top   = 0;
    size_t   count = 0;
    stack[top++]   = id;
    while (top != 0) {
      const Class_id cur = stack[--top];
      if (classes_[cur].kind == Kind::uni && top + 2 <= 64) {
        stack[top++] = classes_[cur].alt_b;  // popped last: a is visited first
        stack[top++] = classes_[cur].alt_a;
        continue;
      }
      if (count++ >= limit) {
        return false;
      }
      if (fn(cur)) {
        return true;
      }
    }
    return false;
  }
  // The e-node of a node class (null for a leaf, a constant or a union).
  [[nodiscard]] const Enode* enode_of(Class_id id) const {
    return classes_[id].kind == Kind::node ? &enodes_[classes_[id].first] : nullptr;
  }
  void set_rule(Class_id cls, uint16_t rule) {
    if (classes_[cls].kind == Kind::node) {
      enodes_[classes_[cls].first].rule = rule;
    }
  }
  uint32_t policy_id(std::string_view policy);
  // The attrs of an e-node, and the (deduplicated) index a new e-node stores.
  [[nodiscard]] const Enode_attrs& attrs_of(const Enode& n) const { return attrs_[n.attr]; }
  // Bytes in use by the tables (classes, e-nodes, operand pool, hash-cons and
  // constant tables): sizes, not capacities. Tables only grow, so the value
  // after the build is the peak.
  [[nodiscard]] size_t bytes() const;
  // The same tables at their CAPACITY: what the allocator handed out (a vector
  // doubles on growth, so this is up to 2x bytes()). The resident bound.
  [[nodiscard]] size_t capacity_bytes() const;

  [[nodiscard]] const Eclass&             cls(Class_id id) const { return classes_[id]; }
  [[nodiscard]] size_t                    num_classes() const { return classes_.size(); }
  [[nodiscard]] size_t                    num_enodes() const { return enodes_.size(); }
  [[nodiscard]] const Enode&              enode(Enode_id id) const { return enodes_[id]; }
  [[nodiscard]] std::span<const Operand>  operands(const Enode& n) const { return {pool_.data() + n.first_operand, n.n_operands}; }
  [[nodiscard]] const std::vector<std::string>& policies() const { return policies_; }
  // The lazily filled analysis facts (runtime/analyses.cpp). A fact about a class stays true when the
  // class later gains union members (they are equal to it), so the cache is never invalidated.
  [[nodiscard]] rt::Analysis_cache& analysis_cache() const;
  [[nodiscard]] const Dlop*               value_of(Class_id id) const {
    const auto idx = classes_[id].value;
    return idx == UINT32_MAX ? nullptr : &values_[idx];
  }

private:
  absl::flat_hash_set<Class_id> state_;
  static uint64_t hash_of(Ntype_op op, uint32_t attr, std::span<const Operand> operands);
  uint32_t        attr_id(const Enode_attrs& attrs);
  void            grow_table();

  std::vector<Eclass>                    classes_;
  std::vector<Enode>                     enodes_;
  std::vector<Operand>                   pool_;
  std::vector<Dlop>                      values_;         // analysis values, shared by index
  std::vector<Enode_id>                  table_;          // hash-cons: open addressing over enodes_, kNoEnode = empty
  std::vector<Enode_attrs>               attrs_{Enode_attrs{}};
  absl::flat_hash_map<std::array<uint32_t, 5>, uint32_t> attr_ids_;
  absl::flat_hash_map<uint64_t, absl::InlinedVector<Class_id, 1>> consts_;  // Dlop::hash -> known-constant classes
  std::vector<std::string>               policies_{std::string{}};
  absl::flat_hash_map<std::string, uint32_t> policy_ids_;
  mutable std::unique_ptr<rt::Analysis_cache> analysis_;
};

// One operand of eval_op: the sink port it occupies, its bank, and its value.
struct Eval_arg {
  hhds::Port_id port = 0;
  uint32_t      bank = 0;
  Dlop          value;
};

// The constant analysis' folding table as a function: the three-valued value of a pure node
// whose operands all have `args` values (Dlop semantics, unlimited precision), or nullopt where
// the engine would keep the node (a non-numeric operand, an illegal shift, an unsupported op). One
// source of truth for the engine's folds, the check= modes and the generated rule tests.
[[nodiscard]] std::optional<Dlop> eval_op(Ntype_op op, std::span<const Eval_arg> args);

// The pre-pass hook: called once with the members of every combinational SCC
// (word-level cycles that may be false at bit level), in forward order, before
// the build. pass.aegraph supplies the classic scalar rewrites that cut the false
// cycles they can cut (Cprop::do_scalar); the body is re-examined afterwards, and
// what stays cyclic is opaque. May delete and create nodes.
using Cycle_cutter = std::function<void(const std::vector<hhds::Node_class>&)>;

// Run the engine on one graph body: pre-pass, build (with the rewrite rules),
// extract, elaborate. The graph is rewritten in place. Thread-safe across
// DIFFERENT graphs (the Dlop pool is thread-local; the rule registry is
// read-only after startup; nothing else is shared).
Stats optimize(const std::shared_ptr<hhds::Graph>& graph, const Cycle_cutter& cut_cycles = {}, const Options& options = {});

// Parse `compile.aegraph.*` label strings into Options (fatal diagnostic on a
// bad value or an unknown rule name). Empty strings keep the defaults.
Options make_options(std::string_view rules_disable, std::string_view rules_only, std::string_view max_rewrites,
                     std::string_view bisect_graph, std::string_view explain, std::string_view check,
                     std::string_view rewrite_depth, std::string_view matches_limit, std::string_view eclass_limit,
                     std::string_view extractor_fuel, std::string_view edge_budget, std::string_view cost,
                     std::string_view choices);

}  // namespace livehd::aegraph

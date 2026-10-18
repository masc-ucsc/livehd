//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// pass/aegraph -- a Cranelift-style acyclic e-graph ("aegraph") constant
// propagation engine for LGraph, the alternative to pass/cprop behind
// `--set compile.cprop.engine=aegraph` (todo/livehd/2a-aegraph.html).
//
// This file is the engine skeleton of milestone B. There are no rewrite rules
// yet, so the engine does exactly what hash-consing and the constant analysis
// give for free:
//
//   1. pre-pass   comb SCC (false-cycle) detection; members are opaque leaves
//   2. build      forward walk; every pure node is normalized (commutative
//                 banks sorted, multiplicity kept) and hash-consed on
//                 op + operand classes + color + synth_policy + proven; the
//                 constant analysis folds a node ONLY to a fully known value
//                 (the P7 contract: a '?' literal is never split)
//   3. extract    cost-free: the first member of a class (the original node)
//   4. elaborate  diff against the original graph: matching nodes are reused
//                 (nids, names and srcids survive), duplicates are retired,
//                 the loop-carry guard is kept, width stamps are carried and
//                 dead pure nodes are deleted
//
// The post steps (pack canonicalization, vector-mux narrowing, muxctx, flop
// enable folding) are the classic implementations (Cprop::do_post); loop_hoist
// is the library-wide classic pass. pass.aegraph (pass_aegraph.cpp) wires the
// stages together.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "cell.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"

namespace livehd::aegraph {

using Class_id = uint32_t;
using Enode_id = uint32_t;
inline constexpr Class_id kNoClass = UINT32_MAX;
inline constexpr Enode_id kNoEnode = UINT32_MAX;

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

struct Enode {
  Ntype_op    op = Ntype_op::Invalid;
  Enode_attrs attrs;
  uint32_t    first_operand = 0;  // offset into the operand pool
  uint32_t    n_operands    = 0;
  Class_id    cls           = kNoClass;
  uint64_t    hash          = 0;
  Enode_id    chain         = kNoEnode;  // hash-cons bucket chain
  Enode_id    union_next    = kNoEnode;  // next member of the class (no rule unions yet)
  uint16_t    rule          = 0;         // creating rule id; 0 = the original node
};

// Pure data: no LGraph handles. Classes are created in forward-walk order, so
// class ids are deterministic.
class Egraph {
public:
  enum class Kind : uint8_t {
    leaf,     // a skeleton driver pin, or any value the engine cannot see through
    literal,  // a '?'-bearing constant: ONE const node (Q5), never merged by value
    konst,    // a fully known constant, interned by value
    node,     // a hash-consed pure e-node
  };

  struct Eclass {
    Kind                 kind       = Kind::leaf;
    Enode_id             first      = kNoEnode;  // the original (first) member of a node class
    std::optional<Dlop>  value;                  // three-valued; absent = no value (non-constant)
  };

  // A fresh class that is never merged with anything.
  Class_id add_leaf();
  // A '?'-bearing constant: a fresh class per const node.
  Class_id add_literal(const Dlop& value);
  // A fully known constant, interned by value (same_repr).
  Class_id add_const(const Dlop& value);
  // Hash-cons a pure node. `created` tells whether a new class was minted.
  Class_id intern(Ntype_op op, const Enode_attrs& attrs, std::span<const Operand> operands, bool& created);
  // Attach the analysis value of a node class (three-valued, may hold '?').
  void     set_value(Class_id cls, Dlop value);
  uint32_t policy_id(std::string_view policy);

  [[nodiscard]] const Eclass&             cls(Class_id id) const { return classes_[id]; }
  [[nodiscard]] size_t                    num_classes() const { return classes_.size(); }
  [[nodiscard]] size_t                    num_enodes() const { return enodes_.size(); }
  [[nodiscard]] const Enode&              enode(Enode_id id) const { return enodes_[id]; }
  [[nodiscard]] std::span<const Operand>  operands(const Enode& n) const { return {pool_.data() + n.first_operand, n.n_operands}; }
  [[nodiscard]] const std::vector<std::string>& policies() const { return policies_; }
  [[nodiscard]] const Dlop*               value_of(Class_id id) const {
    return classes_[id].value ? &*classes_[id].value : nullptr;
  }

private:
  static uint64_t hash_of(Ntype_op op, const Enode_attrs& attrs, std::span<const Operand> operands);

  std::vector<Eclass>                    classes_;
  std::vector<Enode>                     enodes_;
  std::vector<Operand>                   pool_;
  absl::flat_hash_map<uint64_t, Enode_id> buckets_;       // hash -> first enode of the chain
  absl::flat_hash_map<uint64_t, std::vector<Class_id>> consts_;  // Dlop::hash -> known-constant classes
  std::vector<std::string>               policies_{std::string{}};
  absl::flat_hash_map<std::string, uint32_t> policy_ids_;
};

// The pre-pass hook: called once with the members of every combinational SCC
// (word-level cycles that may be false at bit level), in forward order, before
// the build. pass.aegraph supplies the classic scalar rewrites that cut the false
// cycles they can cut (Cprop::do_scalar); the body is re-examined afterwards, and
// what stays cyclic is opaque. May delete and create nodes.
using Cycle_cutter = std::function<void(const std::vector<hhds::Node_class>&)>;

// Run the engine on one graph body: pre-pass, build, extract, elaborate. The
// graph is rewritten in place. Thread-safe across DIFFERENT graphs (the Dlop
// pool is thread-local; nothing else is shared).
Stats optimize(const std::shared_ptr<hhds::Graph>& graph, const Cycle_cutter& cut_cycles = {});

}  // namespace livehd::aegraph

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "cvc5/cvc5.h"

namespace livehd::lec {

// Concrete simulation of an SMT miter (the sampled-effort engine, effort.cpp).
//
// build() linearizes the cone of the roots -- through the DEFINITIONAL
// assertions `EQUAL(symbol, term)` the BMC unrolling uses to keep each cycle
// shallow -- into a tape. Every other assertion (a design assume, a monitor
// assumption, `false`) is a CONSTRAINT root. A free leaf is a symbol no
// assertion defines: a primary input of some cycle, a power-on state symbol, a
// free initial memory array, a fresh X value.
//
// eval() computes the whole tape on concrete leaf values with SMT-LIB
// semantics (division by zero, shifts past the width, ...): one sampled input
// vector costs one linear pass instead of an SMT query. A free ARRAY leaf
// reads pseudo-random content seeded per leaf (a concrete power-on memory).
//
// reduce() is the partial evaluator behind the search queries: given the
// leaves that stay SYMBOLIC, every node whose value no symbolic leaf reaches
// folds to its constant, an ITE with a concrete condition takes its branch,
// and only the symbolic cone is rebuilt as cvc5 terms (a defined symbol keeps
// its symbol plus one EQUAL, exactly as the unrolling wrote it, so the terms
// stay shallow). A concrete free memory keeps its simulated content: reads at
// concrete indices fold, and one a symbolic index still reaches stays a
// symbol pinned at every index the evaluation read (eval(true) records them).
//
// Supported: Boolean connectives, EQUAL/DISTINCT/ITE, the bit-vector
// operators of the SMT-LIB QF_BV logic and SELECT/STORE/CONST_ARRAY. Anything
// else (APPLY_UF, integers, array equality) makes build() fail; the caller
// then keeps its solver-only path.
class Term_sim {
public:
  struct Leaf {
    cvc5::Term term;
    uint32_t   node        = 0;
    uint32_t   width       = 0;  // BV width (Bool: 1); array leaf: element width
    uint32_t   index_width = 0;  // array leaf only
    bool       is_bool     = false;
    bool       is_array    = false;
  };

  explicit Term_sim(cvc5::TermManager& tm) : tm_(tm) {}

  // `roots` are the terms the caller reads back (root(i) / reduce ids, in this
  // order). Returns false, with `why`, on an unsupported term.
  bool build(const std::vector<cvc5::Term>& assertions, const std::vector<cvc5::Term>& roots, std::string& why);

  [[nodiscard]] size_t                   nodes() const { return nodes_.size(); }
  [[nodiscard]] size_t                   constraints() const { return constraint_nodes_.size(); }
  [[nodiscard]] bool                     has_arrays() const { return has_arrays_; }
  [[nodiscard]] const std::vector<Leaf>& leaves() const { return leaves_; }
  // Leaf index of a free symbol of the cone, -1 when `t` is none.
  [[nodiscard]] int                      leaf_index(const cvc5::Term& t) const;

  // Leaf values. A BV/Bool leaf owns (width+63)/64 words (LSB first; bits
  // above the width are ignored). An array leaf takes a content seed.
  [[nodiscard]] uint64_t* leaf_words(size_t leaf);
  [[nodiscard]] uint32_t  leaf_nwords(size_t leaf) const;
  void                    set_array_seed(size_t leaf, uint64_t seed);
  // Set a BV/Bool leaf from a cvc5 value (a model's getValue).
  void                    set_leaf_value(size_t leaf, const cvc5::Term& value);

  // Evaluate the whole tape. True when every constraint holds. With
  // `record_reads` the free-array reads are kept for pins().
  bool                     eval(bool record_reads = false);
  [[nodiscard]] bool       root(size_t r) const;
  // Root r's value (Bool or BV) as a cvc5 constant.
  [[nodiscard]] cvc5::Term root_value(size_t r) const;

  // The last evaluation as assertions: `symbol == value` for every BV/Bool
  // leaf and defined symbol, plus `select(array, i) == v` for each
  // free-array read recorded.
  [[nodiscard]] std::vector<cvc5::Term> pins(bool with_reads = true) const;
  // The last evaluation as ONE substitution, for confirming a trace with a
  // single substitute + rewrite: every BV/Bool symbol (leaf or defined) ->
  // its value, and every defined memory -> its definition with those values
  // and the earlier memories inlined (a store chain over the free memories).
  // `reads` maps select(free memory, i) -> v for the reads eval(true) kept.
  struct Substitution {
    std::vector<cvc5::Term> from, to, read_from, read_to;
  };
  [[nodiscard]] Substitution substitution() const;
  // One BV/Bool leaf's current value as a cvc5 constant.
  [[nodiscard]] cvc5::Term   leaf_value(size_t leaf) const;

  // The design's input-decoding constants: every EQUAL / DISTINCT /
  // BITVECTOR_COMP of a constant (up to 64 bits) with a contiguous bit field
  // that is, through pure copies (aliases -- registers --, extracts, concats,
  // extensions, mask And/Or, ITE arms that agree), a field of ONE input port
  // (`leaf_port[leaf]`, -1 for a leaf that is no input). A random input
  // biased toward these values decodes as a real opcode with probability
  // ~1/#opcodes instead of 2^-width.
  struct Field {
    int                   port  = -1;
    uint32_t              lo    = 0;
    uint32_t              width = 0;
    std::vector<uint64_t> values;
  };
  // Runs one eval() (constant subterms need their values).
  [[nodiscard]] std::vector<Field> fields(const std::vector<int>& leaf_port, size_t max_values = 64);

  struct Reduced {
    std::vector<cvc5::Term> assertions;           // symbolic definitions + symbolic constraints
    std::vector<cvc5::Term> roots;                // per requested root: its reduced term (a constant when folded)
    bool                    vacuous     = false;  // a concrete constraint is false: no trace is admitted
    bool                    has_arrays  = false;
    bool                    constrained = false;  // a constraint survived (depends on symbolic leaves)
    size_t                  terms       = 0;      // rebuilt nodes
  };
  // Requires a prior eval(true): pinned leaves keep the values it used.
  [[nodiscard]] Reduced reduce(const std::vector<bool>& symbolic_leaf, const std::vector<size_t>& root_ids);

private:
  enum class Op : uint8_t {
    Leaf,
    Alias,
    Const,
    ConstArray,
    Not,
    And,
    Or,
    Xor,
    Implies,
    Eq,
    Distinct,
    Ite,
    BvNot,
    BvAnd,
    BvOr,
    BvXor,
    BvNand,
    BvNor,
    BvXnor,
    BvComp,
    BvNeg,
    BvAdd,
    BvSub,
    BvMul,
    BvUdiv,
    BvUrem,
    BvSdiv,
    BvSrem,
    BvSmod,
    BvShl,
    BvLshr,
    BvAshr,
    BvUlt,
    BvUle,
    BvUgt,
    BvUge,
    BvSlt,
    BvSle,
    BvSgt,
    BvSge,
    BvUltbv,
    BvSltbv,
    BvRedor,
    BvRedand,
    Extract,
    Concat,
    Zext,
    Sext,
    Repeat,
    Rotl,
    Rotr,
    Select,
    Store,
  };
  enum Sort : uint8_t { kBool = 0, kBv = 1, kArray = 2 };
  struct Node {
    Op       op    = Op::Leaf;
    uint8_t  sort  = kBv;
    uint32_t width = 0;  // Bool: 1; BV: width; array: element width
    uint32_t nw    = 0;  // value words (arrays: 0)
    uint32_t off   = 0;  // first value word (arrays: array slot)
    uint32_t c0    = 0;  // first child in kids_
    uint32_t nc    = 0;
    uint32_t p0    = 0;  // extract hi / extend amount / repeat count / rotate amount / array index width
    uint32_t p1    = 0;  // extract lo
    int32_t  leaf  = -1;
  };
  // A per-evaluation array value: a free leaf, a constant array, or a store.
  struct Arr {
    uint8_t  kind   = 0;  // 0 free leaf, 1 constant, 2 store
    uint32_t parent = 0;
    uint32_t leaf   = 0;
    uint32_t idx    = 0;  // words in arr_words_
    uint32_t val    = 0;  // words in arr_words_
  };
  struct Read {
    uint32_t              leaf;
    std::vector<uint64_t> idx, val;
  };

  bool                     add_node(const cvc5::Term& t, std::string& why);
  void                     eval_node(uint32_t n);
  void                     select(const Node& n, uint32_t arr, const uint64_t* idx, uint64_t* out);
  [[nodiscard]] cvc5::Term const_of(uint32_t n) const;

  cvc5::TermManager&                        tm_;
  std::vector<Node>                         nodes_;
  std::vector<cvc5::Term>                   orig_;  // per node: the term it evaluates
  std::vector<uint32_t>                     kids_;
  std::vector<uint64_t>                     words_;
  std::vector<Leaf>                         leaves_;
  std::vector<uint64_t>                     leaf_seed_;
  absl::flat_hash_map<uint64_t, uint32_t>   node_of_;  // term id -> node
  absl::flat_hash_map<uint64_t, int>        leaf_of_;  // term id -> leaf
  absl::flat_hash_map<uint64_t, cvc5::Term> defs_;     // symbol id -> defining term
  std::vector<uint32_t>                     root_nodes_;
  std::vector<uint32_t>                     constraint_nodes_;
  bool                                      has_arrays_ = false;
  uint32_t                                  arr_slots_  = 0;
  // Per evaluation.
  std::vector<uint32_t>                     arr_ref_;  // array slot -> arrs_ index
  std::vector<Arr>                          arrs_;
  std::vector<uint64_t>                     arr_words_;
  bool                                      record_ = false;
  std::vector<Read>                         reads_;
  std::vector<uint64_t>                     scratch_;
  // reduce() memo
  std::vector<cvc5::Term>                   built_;
  std::vector<uint32_t>                     built_gen_;
  uint32_t                                  gen_ = 0;
};

}  // namespace livehd::lec

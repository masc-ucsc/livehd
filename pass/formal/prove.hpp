// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cvc5/cvc5.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "encode.hpp"  // livehd::lec::Val + exported helpers (fit_to/flop_state_key)
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "query.hpp"  // livehd::lec::Verdict

namespace livehd::formal {

// pass.formal answers SINGLE-design, sub-cone questions about one LGraph (where
// pass.lec is relational across two). The Prover demand-encodes only the cone of
// each queried pin into cvc5 bit-vectors (reusing pass/lec's Encoder helpers and
// Val), memoizes per pin so overlapping queries collapse shared state, and runs
// one fresh QF_BV solver per query under a deterministic, cone-scaled resource
// budget. Unsupported nodes OUTSIDE a property's cone are never visited, so they
// cannot poison its verdict; an unsupported node INSIDE the cone yields Unknown
// (sound: defer to runtime, never a wrong answer). See todo/livehd/2f-verify.
//
// With `descend_subs` the cone is VIRTUALLY FLAT: a Sub output continues at the
// driver inside its definition, and that definition's inputs continue at the
// instance's drivers. Every instance is its own scope, so state inside two
// instances of one definition stays two independent symbols. A Sub without a
// body, and a compact loop Sub (one body standing for N iterations), stay
// unsupported. A caller that caches verdicts must then key them on every
// definition in `descended()` too.

// One step of that descent, for any walker of the same virtual-flat view: the
// driver inside the definition behind a Sub output, and the instance's driver
// behind one of that definition's inputs. Invalid when the Sub cannot be
// descended (no body, compact loop) or the port is unconnected.
hhds::Pin_class sub_body_driver(const hhds::Pin_class& sub_output);
hhds::Pin_class sub_input_driver(const hhds::Node_class& inst, const hhds::Pin_class& def_input);

using Verdict = livehd::lec::Verdict;  // Proven | Refuted | Unknown

struct Prove_options {
  // Deterministic budget: the per-query cvc5 resource limit (rlimit) is
  // budget_k * (#driver pins in the property cone). cvc5's rlimit is a
  // machine-/wall-clock-independent internal counter, so the same config yields
  // the same verdict everywhere. 0 disables the limit.
  int  budget_k                 = 256;
  // Floor of that rlimit. A tiny cone's budget_k * pins can be too small for
  // the solver's fixed start-up cost, so the verdict would depend on what an
  // earlier query already cached. 0 = no floor.
  long long min_rlimit          = 0;
  // Pre-solve gate: a cone larger than this skips the solver entirely -> Unknown.
  int  cone_max                 = 50000;
  // Wall-clock cap for ONE checkSat, in ms (cvc5 `tlimit-per`). 0 = none, and
  // the deterministic `budget_k` rlimit alone bounds a query, so the same config
  // yields the same verdict on every machine and build mode. A non-zero cap
  // trades that reproducibility for a BOUNDED compile (pass.formal.timeout draws
  // it down as its total budget drains). Degrading is sound either way: a
  // time-out is `unknown` -> Verdict::Unknown -> the obligation stays a runtime
  // check, never a wrong verdict.
  int  timeout_ms               = 0;
  // Synthesis queries cut each memory dout to an independent free word.
  bool memory_as_symbols        = false;
  bool reject_unknown_constants = false;
  // Encode through Sub instances (virtual flat) instead of stopping at them.
  bool descend_subs             = false;
  // A driver pin with no stamped width (bits 0) makes its cone Unknown instead
  // of being read as one bit: a rewrite proven about a guessed width would be
  // proven about a different circuit (satopt).
  bool reject_unstamped         = false;
  // Fill Query_out::model on a Refuted query (satopt feeds it back into its
  // simulation). Off by default: reading every leaf's value costs a solver
  // call each.
  bool produce_model            = false;
};

// One free symbol of a refuted query -- a primary input, or a state or memory
// output cut to a symbol -- in the instance `path` names from the prover's
// graph (empty = the graph itself), with the value the counterexample gives
// it: the leaf's bits, read unsigned.
struct Model_leaf {
  std::vector<hhds::Node_class> path;
  hhds::Pin_class               pin;
  Dlop                          value;
};
using Model = std::vector<Model_leaf>;

struct Query_out {
  Verdict     verdict  = Verdict::Unknown;
  bool        stateful = false;  // cone cut a Flop/Memory (a Refuted witness may be unreachable)
  std::string witness;           // input assignment, when Refuted
  Model       model;             // every leaf of the refuted formula, when Refuted and produce_model
};

class Prover {
public:
  explicit Prover(hhds::Graph* g, const Prove_options& opts = {});

  // cond is treated as "true" iff non-zero (matches assert / bool semantics).
  Query_out is_true(const hhds::Pin_class& cond);
  Query_out is_false(const hhds::Pin_class& cond);
  // a == b across their common (max) width.
  Query_out equal(const hhds::Pin_class& a, const hhds::Pin_class& b);
  // at-most-one-bit-set ((sel & (sel-1)) == 0): the Hotmux selector obligation.
  Query_out is_onehot0(const hhds::Pin_class& sel);
  Query_out are_exclusive(const std::vector<hhds::Pin_class>& controls);
  // exactly-one-bit-set (onehot0 AND sel != 0).
  Query_out is_onehot(const hhds::Pin_class& sel);
  Query_out equal_when(const hhds::Pin_class& a, const hhds::Pin_class& b, const std::vector<hhds::Pin_class>& enables,
                       int address_bits = 0);
  Query_out never_collide(const hhds::Pin_class& a, const hhds::Pin_class& b, const std::vector<hhds::Pin_class>& enables,
                          int address_bits = 0);
  Query_out constant_bit(const hhds::Pin_class& pin, int bit, bool value);
  // Word-level masked facts over the low `width` bits (each value fitted to
  // `width` by its own signedness); `mask` and `value` are read as unsigned
  // `width`-bit patterns. masked_const: the masked bits always carry `value`.
  // masked_relation: the masked bits of `a` always equal those of `b`
  // (complement: always their complement).
  Query_out masked_const(const hhds::Pin_class& pin, int width, const Dlop& mask, const Dlop& value);
  Query_out masked_relation(const hhds::Pin_class& a, const hhds::Pin_class& b, int width, const Dlop& mask, bool complement);
  // Bit 0 of `t` always equals `op` (And, Or or Xor) over bit 0 of `a` and of
  // `b` (complemented when `invert_b`): a gate proven before it is built.
  Query_out bit_gate(const hhds::Pin_class& t, Ntype_op op, const hhds::Pin_class& a, const hhds::Pin_class& b, bool invert_b);
  // Observability: with `target`'s low `width` bits in `mask` replaced by
  // those of `value` (read with target's sign), does every exit keep its
  // value? `window` lists the cells between target and the exits in
  // topological order; each exit is an output of one of them, and every other
  // input of a window cell keeps its value (the caller makes every edge that
  // leaves the window an exit, so reconvergence outside is covered).
  Query_out unchanged_under(const hhds::Pin_class& target, int width, const Dlop& mask, const Dlop& value,
                            const std::vector<hhds::Node_class>& window, const std::vector<hhds::Pin_class>& exits);

  // Register a hypothesis (an assume condition's driver pin): every later query
  // assumes cond != 0. Returns false if the assume cone is unsupported.
  bool assume(const hhds::Pin_class& cond);

  // Are the registered hypotheses jointly satisfiable? A contradictory set makes
  // EVERY later query vacuously Proven, so an obligation is "discharged" by an
  // impossible environment. Only PROVEN assumes are sound by construction;
  // unchecked ones (assume_nocheck / formal.assume_check=false) need this probe.
  // Conservative: Unknown / solver-error answers true (never fail a build on a
  // budget-out), so it only ever reports a CONFIRMED contradiction.
  bool assumes_consistent();

  // Drop every hypothesis (used after a confirmed contradiction: proving under an
  // impossible environment is worse than proving nothing).
  void clear_assumes() { assumes_.clear(); }

  // Re-arm the per-query wall cap (Prove_options::timeout_ms) between queries, so
  // a caller holding a TOTAL budget can hand each query only what is left of it.
  void set_timeout_ms(int ms) { opts_.timeout_ms = ms; }

  // Solver-free: does cond's cone cut a Flop/Memory? The same deterministic
  // cone walk that classifies a query's `Query_out::stateful`, without
  // encoding or solving, for a caller that skipped the query (e.g. out of
  // budget) but still needs the classification. Conservative: a cone the
  // encoder cannot handle (an undescended Sub, Fflop, Latch) answers true,
  // since a Sub may hide state.
  bool stateful_cone(const hhds::Pin_class& cond);

  // Every definition a query so far descended into (descend_subs), once each.
  std::vector<hhds::Graph*> descended() const;

  // Deterministic effort so far: the cone pins every query walked (the same
  // count its rlimit scales with). A caller holding a work budget charges it.
  [[nodiscard]] uint64_t work() const { return work_; }

private:
  // A pin inside one instance: scope 0 is g_ itself, every other scope one
  // descended Sub instance (see scopes_).
  using Key = std::pair<uint32_t, hhds::Class_index>;
  struct Scope {
    hhds::Graph*     graph  = nullptr;
    uint32_t         parent = 0;
    hhds::Node_class inst;    // the Sub in `parent` (invalid for scope 0)
    std::string      prefix;  // keeps state symbols apart per instance
    int              depth = 0;
  };
  // The scope of `inst`'s body inside `scope`; nullopt past the depth cap.
  std::optional<uint32_t> child_scope(uint32_t scope, const hhds::Node_class& inst);
  // Where a pin of `scope` continues across an instance boundary: a Sub
  // output into its body, a definition input up to the instance's driver.
  // nullopt when the pin is not a boundary or cannot be crossed.
  std::optional<std::pair<uint32_t, hhds::Pin_class>> cross(uint32_t scope, const hhds::Pin_class& pin);

  // Demand-encode dpin's cone to a Val; nullopt if unsupported / over budget.
  std::optional<livehd::lec::Val> val_of(const hhds::Pin_class& dpin) { return val_of(0, dpin); }
  std::optional<livehd::lec::Val> val_of(uint32_t scope, const hhds::Pin_class& dpin);
  std::optional<livehd::lec::Val> encode_comb(uint32_t scope, const hhds::Node_class& node, const hhds::Pin_class& dpin);

  // Deterministic cone walk used for the budget + pre-gate + state detection.
  // Counts unique driver pins reachable backward from `pin`; sets `stateful`
  // (cone cuts a Flop/Memory) and `unsupported` (cone hits an op the encoder
  // cannot handle: Memory/undescended Sub/Fflop/Latch).
  int  cone_info(const hhds::Pin_class& pin, bool& stateful, bool& unsupported);
  void cone_walk(uint32_t scope, const hhds::Pin_class& pin, absl::flat_hash_set<Key>& seen, int& n, bool& stateful,
                 bool& unsupported);

  Query_out  address_relation(const hhds::Pin_class& a, const hhds::Pin_class& b, const std::vector<hhds::Pin_class>& enables,
                              bool equal, int address_bits);
  cvc5::Term bv_const(int width, uint64_t val);
  Query_out  masked(const std::vector<hhds::Pin_class>& pins, const std::function<std::optional<cvc5::Term>(const std::vector<cvc5::Term>&)>& refute_of, int width);
  cvc5::Term bv_of(int width, const Dlop& v);  // the low `width` bits of v
  cvc5::Term bv_extract(const cvc5::Term& t, int hi, int lo);
  cvc5::Term pred_to_bv(const cvc5::Term& b);

  // Assert `refute` (+ all assumes + memory side-eqs) under an rlimit derived
  // from `cone_nodes`; map UNSAT->Proven, SAT->Refuted, else Unknown.
  Query_out solve(const cvc5::Term& refute, int cone_nodes, bool stateful);

  cvc5::TermManager tm_;
  hhds::Graph*      g_;
  Prove_options     opts_;
  uint64_t          work_ = 0;

  std::vector<Scope>                                       scopes_;        // [0] = g_
  absl::flat_hash_map<Key, uint32_t>                       child_scopes_;  // (parent scope, Sub) -> scope
  absl::flat_hash_map<Key, livehd::lec::Val>               memo_;          // driver pin -> Val (shared across queries)
  absl::flat_hash_set<Key>                                 on_stack_;      // combinational-cycle guard
  std::vector<std::pair<cvc5::Term, cvc5::Term>>           side_eqs_;  // memory read ties (asserted every query)
  std::vector<cvc5::Term>                                  assumes_;   // hypotheses (cond != 0)
  std::vector<std::pair<std::string, cvc5::Term>>          inputs_;    // seeded input symbols, for witnesses
  // Every free symbol that stands for a pin (input, flop Q, memory output):
  // how a model names its leaves.
  std::unordered_map<cvc5::Term, std::pair<uint32_t, hhds::Pin_class>> leaves_;
  void                                                     leaf(const cvc5::Term& t, uint32_t scope, const hhds::Pin_class& pin);
  Model                                                    model(cvc5::Solver& solver, const cvc5::Term& refute);

  bool enc_unsupported_ = false;  // set by val_of when the cone hit an unsupported op
  bool enc_stateful_    = false;  // set by val_of when the cone cut a Flop/Memory
};

}  // namespace livehd::formal

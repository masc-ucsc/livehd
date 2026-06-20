// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cvc5/cvc5.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "encode.hpp"  // livehd::lec::Val + exported helpers (fit_to/real_width/flop_state_key)
#include "hhds/graph.hpp"
#include "query.hpp"  // livehd::lec::Verdict

namespace livehd::formal {

// pass.formal answers SINGLE-design, sub-cone questions about one LGraph (where
// pass.lec is relational across two). The Prover demand-encodes only the cone of
// each queried pin into cvc5 bit-vectors (reusing pass/lec's Encoder helpers and
// Val), memoizes per pin so overlapping queries collapse shared state, and runs
// one fresh QF_BV solver per query under a deterministic, cone-scaled resource
// budget. Unsupported nodes OUTSIDE a property's cone are never visited, so they
// cannot poison its verdict; an unsupported node INSIDE the cone yields Unknown
// (sound: defer to runtime, never a wrong answer). See todo/livehd/2f-formal.

using Verdict = livehd::lec::Verdict;  // Proven | Refuted | Unknown

struct Prove_options {
  // Deterministic budget: the per-query cvc5 resource limit (rlimit) is
  // budget_k * (#driver pins in the property cone). cvc5's rlimit is a
  // machine-/wall-clock-independent internal counter, so the same config yields
  // the same verdict everywhere. 0 disables the limit.
  int budget_k = 256;
  // Pre-solve gate: a cone larger than this skips the solver entirely -> Unknown.
  int cone_max = 50000;
};

struct Query_out {
  Verdict     verdict  = Verdict::Unknown;
  bool        stateful = false;  // cone cut a Flop/Memory (a Refuted witness may be unreachable)
  std::string witness;           // input assignment, when Refuted
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
  // exactly-one-bit-set (onehot0 AND sel != 0).
  Query_out is_onehot(const hhds::Pin_class& sel);

  // Register a hypothesis (an assume condition's driver pin): every later query
  // assumes cond != 0. Returns false if the assume cone is unsupported.
  bool assume(const hhds::Pin_class& cond);

private:
  // Demand-encode dpin's cone to a Val; nullopt if unsupported / over budget.
  std::optional<livehd::lec::Val> val_of(const hhds::Pin_class& dpin);
  std::optional<livehd::lec::Val> encode_comb(const hhds::Node_class& node, const hhds::Pin_class& dpin);

  // Deterministic cone walk used for the budget + pre-gate + state detection.
  // Counts unique driver pins reachable backward from `pin`; sets `stateful`
  // (cone cuts a Flop/Memory) and `unsupported` (cone hits an op the encoder
  // cannot handle: Memory/Sub/Fflop/Latch).
  int cone_info(const hhds::Pin_class& pin, bool& stateful, bool& unsupported);
  void cone_walk(const hhds::Pin_class& pin, absl::flat_hash_set<hhds::Class_index>& seen, int& n, bool& stateful,
                 bool& unsupported);

  cvc5::Term bv_const(int width, uint64_t val);
  cvc5::Term bv_extract(const cvc5::Term& t, int hi, int lo);
  cvc5::Term pred_to_bv(const cvc5::Term& b);

  // Assert `refute` (+ all assumes + memory side-eqs) under an rlimit derived
  // from `cone_nodes`; map UNSAT->Proven, SAT->Refuted, else Unknown.
  Query_out solve(const cvc5::Term& refute, int cone_nodes, bool stateful);

  cvc5::TermManager tm_;
  hhds::Graph*      g_;
  Prove_options     opts_;

  absl::flat_hash_map<hhds::Class_index, livehd::lec::Val> memo_;      // driver pin -> Val (shared across queries)
  absl::flat_hash_set<hhds::Class_index>                   on_stack_;  // combinational-cycle guard
  std::vector<std::pair<cvc5::Term, cvc5::Term>>           side_eqs_;  // memory read ties (asserted every query)
  std::vector<cvc5::Term>                                  assumes_;   // hypotheses (cond != 0)
  std::vector<std::pair<std::string, cvc5::Term>>          inputs_;    // seeded input symbols, for witnesses

  bool enc_unsupported_ = false;  // set by val_of when the cone hit an unsupported op
  bool enc_stateful_    = false;  // set by val_of when the cone cut a Flop/Memory
};

}  // namespace livehd::formal

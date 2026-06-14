// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "query.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "core/fts.h"
#include "core/prop.h"
#include "core/proverresult.h"
#include "encode.hpp"
#include "engines/prover.h"
#include "node_util.hpp"
#include "options/options.h"
#include "smt/available_solvers.h"
#include "utils/make_provers.h"

namespace livehd::lec {

using namespace smt;

namespace {

pono::Engine to_engine(const std::string& s, std::string& err) {
  if (s == "bmc") {
    return pono::BMC;
  }
  if (s == "bmc-sp") {
    return pono::BMC_SP;
  }
  if (s == "ind" || s == "kind") {
    return pono::KIND;
  }
  if (s == "ic3" || s == "ic3bits") {
    return pono::IC3_BITS;
  }
  err = "unknown lec.engine '" + s + "' (expected bmc|ind|ic3)";
  return pono::KIND;
}

}  // namespace

Query_result prove_equal(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts) {
  Query_result res;
  res.detail = "engine=" + opts.engine + " solver=" + opts.solver + " bound=" + std::to_string(opts.bound);

  if (opts.solver != "cvc5") {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; solver backend '" + opts.solver + "' not built (M1 = cvc5 only)";
    return res;
  }
  std::string  eng_err;
  pono::Engine engine = to_engine(opts.engine, eng_err);
  if (!eng_err.empty()) {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; " + eng_err;
    return res;
  }

  SmtSolver                        solver = pono::create_solver(CVC5, false, true, true);
  pono::FunctionalTransitionSystem fts(solver);

  // Shared primary inputs: one FTS input var per input name (union of both
  // designs), so the unroller treats them as free-per-cycle. Both encodings
  // reuse these terms -> assume_equal(inputs) is structural.
  absl::flat_hash_map<std::string, Val> shared;
  auto                                  add_inputs = [&](hhds::Graph* g) {
    auto gio = g->get_io();
    for (const auto& d : gio->get_input_pin_decls()) {
      if (shared.count(d.name)) {
        continue;
      }
      auto pin = g->get_input_pin(d.name);
      int  w   = real_width_io(pin, *gio, d.name);
      if (w == 0) {
        w = 1;
      }
      bool sgn       = pin.is_invalid() ? !gio->is_unsign(d.name) : !graph_util::is_unsign(pin);
      Term t         = fts.make_inputvar(d.name, solver->make_sort(BV, w));
      shared[d.name] = Val{t, w, sgn};
    }
  };
  add_inputs(ref);
  add_inputs(impl);

  Encoder enc(solver);
  Encoded re = enc.encode(ref, &shared);
  if (!re.ok) {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; ref encode failed: " + re.error;
    return res;
  }
  Encoded ie = enc.encode(impl, &shared);
  if (!ie.ok) {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; impl encode failed: " + ie.error;
    return res;
  }

  // Miter over common outputs: bad = Or_i (ref_out_i != impl_out_i).
  Term bad;
  for (const auto& [name, rv] : re.outputs) {
    auto it = ie.outputs.find(name);
    if (it == ie.outputs.end()) {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; output '" + name + "' present in ref but not impl";
      return res;
    }
    int  w    = std::max(rv.width, it->second.width);
    Term a    = fit_to(solver, rv, w);
    Term b    = fit_to(solver, it->second, w);
    Term diff = solver->make_term(Distinct, a, b);
    bad       = bad == nullptr ? diff : solver->make_term(Or, bad, diff);
  }
  // also flag impl-only outputs
  for (const auto& [name, iv] : ie.outputs) {
    if (!re.outputs.count(name)) {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; output '" + name + "' present in impl but not ref";
      return res;
    }
  }
  if (bad == nullptr) {
    res.verdict  = Verdict::Proven;  // no outputs to compare -> vacuously equal
    res.detail  += "; no comparable outputs";
    return res;
  }

  // Latch the combinational miter into a 1-bit state var so the SafetyProperty
  // references only current state (a pure-combinational bad makes BMC return
  // UNKNOWN). bad_state' = bad, init bad_state = 0, property = Not(bad_state).
  Term bad_state = fts.make_statevar("__lec_bad", solver->make_sort(BOOL));
  fts.set_init(solver->make_term(Equal, bad_state, solver->make_term(false)));
  fts.assign_next(bad_state, bad);

  pono::SafetyProperty prop(solver, solver->make_term(Not, bad_state));
  pono::PonoOptions    pono_opts;
  auto                 prover = pono::make_prover(engine, prop, fts, solver, pono_opts);
  prover->initialize();

  pono::ProverResult r = prover->check_until(std::max(1, opts.bound));

  if (r == pono::TRUE) {
    res.verdict = Verdict::Proven;
  } else if (r == pono::FALSE) {
    res.verdict = Verdict::Refuted;
    if (opts.witness) {
      std::vector<smt::UnorderedTermMap> wit;
      if (prover->witness(wit) && !wit.empty()) {
        std::string w;
        for (const auto& [name, v] : shared) {
          auto it = wit[0].find(v.term);
          if (it != wit[0].end()) {
            if (!w.empty()) {
              w += ", ";
            }
            w += name + "=" + it->second->to_string();
          }
        }
        res.witness = w;
      }
    }
  } else {
    res.verdict = Verdict::Unknown;
  }
  return res;
}

}  // namespace livehd::lec

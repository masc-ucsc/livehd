// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "query.hpp"

#include <cvc5/cvc5.h>

#include <algorithm>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "encode.hpp"
#include "node_util.hpp"

namespace livehd::lec {

Query_result prove_equal(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts) {
  Query_result res;
  res.detail = "solver=" + opts.solver + " (cvc5 direct, flop-cut inductive miter)";

  if (opts.solver != "cvc5") {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; solver backend '" + opts.solver + "' not built (cvc5 only)";
    return res;
  }

  cvc5::TermManager tm;
  cvc5::Solver      solver(tm);
  solver.setLogic("QF_BV");
  if (opts.witness) {
    solver.setOption("produce-models", "true");
  }

  // Shared primary inputs: one symbolic BV per input name (union of both
  // designs). Both encodings reuse these terms, so "equal inputs" is structural
  // (the miter constrains nothing on the inputs -> they range freely).
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
      cvc5::Term t   = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(w)), d.name);
      shared[d.name] = Val{t, w, sgn};
    }
  };
  add_inputs(ref);
  add_inputs(impl);

  // Shared current-state symbols: one symbolic BV per corresponding flop, keyed
  // by its cross-design state key (source span preferred). Both encodings reuse
  // these, so the miter assumes equal current state and proves equal next state
  // + outputs (the M2 register-correspondence inductive step).
  auto add_flops = [&](hhds::Graph* g) {
    for (auto node : g->forward_class()) {
      if (graph_util::type_op_of(node) != Ntype_op::Flop) {
        continue;
      }
      auto q = node.get_driver_pin(0);
      if (q.is_invalid()) {
        continue;
      }
      std::string key = flop_state_key(*g, node);
      if (shared.count(key)) {
        continue;
      }
      int w = real_width(q);
      if (w == 0) {
        w = 1;
      }
      bool       sgn = !graph_util::is_unsign(q);
      cvc5::Term t   = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(w)), key);
      shared[key]    = Val{t, w, sgn};
    }
  };
  add_flops(ref);
  add_flops(impl);

  Encoder enc(tm);
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
  cvc5::Term bad;
  for (const auto& [name, rv] : re.outputs) {
    auto it = ie.outputs.find(name);
    if (it == ie.outputs.end()) {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; output '" + name + "' present in ref but not impl";
      return res;
    }
    int        w    = std::max(rv.width, it->second.width);
    cvc5::Term a    = fit_to(tm, rv, w);
    cvc5::Term b    = fit_to(tm, it->second, w);
    cvc5::Term diff = tm.mkTerm(cvc5::Kind::DISTINCT, {a, b});
    bad             = bad.isNull() ? diff : tm.mkTerm(cvc5::Kind::OR, {bad, diff});
  }
  // also flag impl-only outputs
  for (const auto& [name, iv] : ie.outputs) {
    if (!re.outputs.count(name)) {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; output '" + name + "' present in impl but not ref";
      return res;
    }
  }
  if (bad.isNull()) {
    res.verdict  = Verdict::Proven;  // no outputs to compare -> vacuously equal
    res.detail  += "; no comparable outputs";
    return res;
  }

  // Combinational equivalence: the miter is UNSAT iff the designs agree on every
  // common output for all inputs. No state, no unrolling -- a single SMT query.
  // (opts.engine / opts.bound are sequential-model-checking knobs, unused here.)
  solver.assertFormula(bad);
  cvc5::Result r = solver.checkSat();

  if (r.isUnsat()) {
    res.verdict = Verdict::Proven;
  } else if (r.isSat()) {
    res.verdict = Verdict::Refuted;
    if (opts.witness) {
      std::string w;
      for (const auto& [name, v] : shared) {
        cvc5::Term val = solver.getValue(v.term);
        if (val.isNull()) {
          continue;
        }
        if (!w.empty()) {
          w += ", ";
        }
        w += name + "=" + val.getBitVectorValue(10);
      }
      res.witness = w;
    }
  } else {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; checkSat returned unknown";
  }
  return res;
}

}  // namespace livehd::lec

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// pass.satopt: bounded, proof-backed simplification of the graphs it is given,
// committed in place (todo/livehd/2s-satopt A). Every rewrite is proven on the
// definition with free inputs, so it holds for every instance, and keeps every
// observable check (satopt_stages.hpp, Profile::shared). The optimized graphs
// are what later emits, simulation, LEC and synthesis read.
#include <fstream>
#include <print>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "bitwidth.hpp"
#include "cprop.hpp"
#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "pass.hpp"
#include "satopt_stages.hpp"

namespace {
class Pass_satopt : public Pass {
public:
  explicit Pass_satopt(const Eprp_var& var) : Pass("pass.satopt", var) {}

  static void work(Eprp_var& var) {
    std::string error;
    const auto  stages = livehd::satopt::parse_stages(var.get("stages", ""), livehd::satopt::Profile::shared, &error);
    if (!stages) {
      livehd::diag::err("pass.satopt", "bad-stages", "syntax").msg("pass.satopt.stages: {}", error).fatal();
      return;
    }
    // The graphs to optimize: every given graph, or --top and the definitions
    // it reaches. Each is optimized once, as a definition.
    std::vector<std::shared_ptr<hhds::Graph>> graphs;
    absl::flat_hash_set<std::string>          seen;
    const auto                                top = std::string{var.get("top", "")};
    for (const auto& g : var.graphs) {
      if (!g || (!top.empty() && g->get_name() != top)) {
        continue;
      }
      if (top.empty()) {
        if (seen.insert(std::string(g->get_name())).second) {
          graphs.push_back(g);
        }
        continue;
      }
      for (const auto& def : g->definitions().graphs()) {
        if (def && seen.insert(std::string(def->get_name())).second) {
          graphs.push_back(def);
        }
      }
    }
    if (!top.empty() && graphs.empty()) {
      livehd::diag::err("pass.satopt", "top-not-found", "name").msg("definition '{}' was not found", top).fatal();
      return;
    }

    livehd::satopt::Options opts;
    opts.stages    = *stages;
    opts.profile   = livehd::satopt::Profile::shared;
    opts.cache_dir = std::string{var.get("cache_dir", "")};
    for (const auto key : livehd::satopt::kBudgetKeys) {
      if (const auto value = var.get(key, ""); !value.empty() && !livehd::satopt::set_budget(opts.budget, key, value, &error)) {
        livehd::diag::err("pass.satopt", "bad-budget", "syntax").msg("{}", error).fatal();
        return;
      }
    }
    std::vector<uint64_t> before;
    for (const auto& g : graphs) {
      before.push_back(g->body_epoch());
    }
    const auto report = livehd::satopt::run(graphs, opts);

    // Bounded cleanup of what changed: constant folding, dead logic and
    // widths recomputed from the rewritten operands.
    if (var.get("cleanup", "true") != "false") {
      for (size_t i = 0; i < graphs.size(); ++i) {
        if (graphs[i]->body_epoch() == before[i]) {
          continue;
        }
        Cprop cprop;
        cprop.do_trans(graphs[i]);
        Bitwidth bitwidth(3);
        bitwidth.do_trans(graphs[i]);
      }
    }
    std::print("[pass.satopt] stages {}: {} graph(s), {} changed, {:.1f} ms\n",
               opts.stages.text(),
               report.graphs,
               report.changed_graphs,
               report.ms);
    std::print("[pass.satopt] report {}\n", report.json());
    if (const auto path = std::string{var.get("report", "")}; !path.empty()) {
      // The run's report for the caller's result JSON (lhd: the "satopt" member).
      std::ofstream out(path);
      out << report.json() << '\n';
    }

    const auto out = std::string{var.get("out", "")};
    if (!out.empty()) {
      // The rewrite happened in place in the input library: emitting is a
      // cross-library copy of every given module and the modules it calls,
      // body-less declarations included (the caller refused out == in).
      auto& outlib = livehd::Hhds_graph_library::instance(out);
      for (const auto& g : var.graphs) {
        auto* srclib = g && g->get_io() ? g->get_io()->get_library() : nullptr;
        if (srclib == nullptr || !livehd::copy_with_callees(outlib, *srclib, g->get_name())) {
          livehd::diag::err("pass.satopt", "copy-failed", "internal")
              .msg("pass.satopt: could not copy module '{}' into {}", g ? g->get_name() : "?", out)
              .fatal();
          return;
        }
      }
    }
  }

  static void setup() {
    Eprp_method method("pass.satopt",
                       "Bounded, proof-backed logic simplification of the graphs, committed in place: selectors and values "
                       "proven constant, values proven equal or complementary to earlier ones, unobserved values, mux arms "
                       "proven constant or equal when selected, provably exclusive unique-if collapse, exact memory-port "
                       "edits and small resubstitutions -- each a selectable stage under one deterministic budget",
                       &work);
    method.add_label_optional("top", "Optimize this definition and the definitions it reaches (default: every graph)", "");
    method.add_label_optional("stages",
                              "none, default, or a comma-separated list of: constants, equiv, complement, odc, hotmux, memory, "
                              "resub (always run in that order)",
                              "");
    method.add_label_optional("cleanup", "true: constant propagation and bitwidth on every changed graph afterwards", "true");
    method.add_label_optional("work",
                              "Deterministic effort limit for the whole run (simulation, graph walks, solver cones); out of "
                              "budget, a stage keeps what it proved and reports itself exhausted",
                              "200000000");
    method.add_label_optional("queries", "Solver query limit for the whole run", "200000");
    method.add_label_optional("time_ms", "Wall-clock backstop for the whole run in ms, 0 = none (not deterministic)", "0");
    method.add_label_optional("budget_k", "Per-query solver resource factor (the rlimit per cone pin)", "256");
    method.add_label_optional("cone_max", "Per-query cone size above which the solver is skipped", "50000");
    method.add_label_optional("samples", "Simulation patterns per value", "64");
    method.add_label_optional("report", "INTERNAL file the run's stage report (JSON) is written to", "");
    method.add_label_optional("out", "INTERNAL output graph_library directory (the --emit-dir lg: slot)", "");
    method.add_label_optional("cache_dir", "INTERNAL persistent proof cache under the named workdir", "");
    register_pass(method);
  }
};
Pass_plugin plugin("pass_satopt", Pass_satopt::setup);
}  // namespace

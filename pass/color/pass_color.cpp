// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_color.hpp"

#include <format>
#include <string>
#include <string_view>

#include "color_acyclic.hpp"
#include "color_common.hpp"
#include "color_mincut.hpp"
#include "color_path.hpp"
#include "color_synth.hpp"
#include "diag.hpp"
#include "str_tools.hpp"

using namespace livehd::color;

static Pass_plugin sample("pass_color", Pass_color::setup);

Pass_color::Pass_color(const Eprp_var& var) : Pass("pass.color", var) {}

void Pass_color::setup() {
  Eprp_method m("pass.color", "Hierarchical node coloring (acyclic|synth|path|mincut|clear)", &Pass_color::color);
  m.add_label_optional("alg", "algorithm: acyclic|synth|path|mincut|clear", "acyclic");
  m.add_label_optional("top", "top module: anchors the coloring metadata + hierarchy walk", "");
  m.add_label_optional("hier", "color every unique def in the hierarchy (else only the top)", "true");
  m.add_label_optional("verbose", "print per-graph coloring statistics", "false");
  m.add_label_optional("compact", "write the flat per-def color (default; false = per-instance hier color)", "true");
  m.add_label_optional("continuous", "split each color into one id per connected region", "false");
  m.add_label_optional("keep_colored", "preserve pre-existing colors on nodes the algorithm leaves uncolored", "false");
  m.add_label_optional("cutoff", "acyclic: small-partition node-count merge cutoff", "1");
  m.add_label_optional("merge", "acyclic: enable same/one-parent partition merging", "false");
  m.add_label_optional("seed", "mincut: RNG seed", "0");
  m.add_label_optional("iters", "mincut: how many times to run the cut", "1");
  m.add_label_optional("mincut_alg", "mincut: VieCut algorithm (vc, cactus, ...)", "vc");
  m.add_label_optional("synth_alg", "synth: pipe|synth boundary mode", "synth");
  m.add_label_optional("instance", "path: comma-separated seed instance names (forward-only)", "");
  register_pass(m);
}

namespace {

bool parse_bool(std::string_view v) { return v != "false" && v != "0" && !v.empty(); }

// JSON object string of the algorithm parameters (for the metadata blob).
std::string params_json(std::string_view alg, const Color_opts& opts, const Eprp_var& var) {
  std::string s = "{";
  s += std::format("\"hier\":{},", opts.hier);
  s += std::format("\"compact\":{},", opts.compact);
  s += std::format("\"continuous\":{},", opts.continuous);
  s += std::format("\"keep_colored\":{}", opts.keep_colored);
  if (alg == "acyclic") {
    s += std::format(",\"cutoff\":{},\"merge\":{}", var.get("cutoff", "1"), parse_bool(var.get("merge", "false")));
  } else if (alg == "synth") {
    s += std::format(",\"synth_alg\":\"{}\"", var.get("synth_alg", "synth"));
  } else if (alg == "mincut") {
    s += std::format(",\"iters\":{},\"seed\":{},\"mincut_alg\":\"{}\"",
                     var.get("iters", "1"),
                     var.get("seed", "0"),
                     var.get("mincut_alg", "vc"));
  } else if (alg == "path") {
    s += std::format(",\"instance\":\"{}\"", var.get("instance", ""));
  }
  s += "}";
  return s;
}

void run_one(std::string_view alg, hhds::Graph* g, const Color_opts& opts, const Eprp_var& var) {
  if (alg == "acyclic") {
    Color_acyclic c(opts, str_tools::to_i(var.get("cutoff", "1")), parse_bool(var.get("merge", "false")));
    c.label(g);
  } else if (alg == "synth") {
    Color_synth c(opts, var.get("synth_alg", "synth"));
    c.label(g);
  } else if (alg == "path") {
    Color_path c(opts, var.get("instance", ""));
    c.label(g);
  } else if (alg == "mincut") {
    Color_mincut c(opts, str_tools::to_i(var.get("iters", "1")), str_tools::to_i(var.get("seed", "0")), var.get("mincut_alg", "vc"));
    c.label(g);
  }
}

}  // namespace

void Pass_color::color(Eprp_var& var) {
  auto alg = std::string{var.get("alg", "acyclic")};
  auto top = std::string{var.get("top", "")};

  if (alg == "clear") {
    for (const auto& g : var.graphs) {
      if (g) {
        clear_coloring(g.get());
      }
    }
    return;
  }

  if (alg != "acyclic" && alg != "synth" && alg != "path" && alg != "mincut") {
    livehd::diag::err("pass.color", "bad-alg", "unsupported")
        .msg("unknown algorithm '{}' (expected acyclic|synth|path|mincut|clear)", alg)
        .fatal();
  }

  Color_opts opts;
  opts.hier         = parse_bool(var.get("hier", "true"));
  opts.verbose      = parse_bool(var.get("verbose", "false"));
  opts.compact      = parse_bool(var.get("compact", "true"));
  opts.continuous   = parse_bool(var.get("continuous", "false"));
  opts.keep_colored = parse_bool(var.get("keep_colored", "false"));

  // gid -> graph, so the hierarchy walk can resolve sub-def bodies.
  absl::flat_hash_map<hhds::Gid, hhds::Graph*> gid2graph;
  hhds::Graph*                                 top_g = nullptr;
  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    gid2graph[g->get_gid()] = g.get();
    if (top_g == nullptr && (top.empty() || g->get_name() == top)) {
      top_g = g.get();
    }
  }

  if (top_g != nullptr && opts.hier) {
    // Top-driven hierarchical walk: color the top plus every unique sub-def
    // reachable through the instance hierarchy (hhds hier_range yields one
    // Hier_instance per subnode at every depth). Each unique def is colored
    // once via the per-def algorithm.
    absl::flat_hash_set<hhds::Gid> todo;
    todo.insert(top_g->get_gid());
    for (auto inst : top_g->hier_range()) {
      todo.insert(inst.get_target_gid());
    }
    for (auto gid : todo) {
      auto it = gid2graph.find(gid);
      if (it != gid2graph.end()) {
        run_one(alg, it->second, opts, var);
      }
    }
  } else if (top_g != nullptr) {
    run_one(alg, top_g, opts, var);  // single-level (hier=false)
  } else {
    for (const auto& g : var.graphs) {
      if (g) {
        run_one(alg, g.get(), opts, var);
      }
    }
  }

  if (top_g != nullptr) {
    set_coloring_info(top_g, build_coloring_info_json(top_g, top.empty() ? top_g->get_name() : top, alg, params_json(alg, opts, var)));
  }
}

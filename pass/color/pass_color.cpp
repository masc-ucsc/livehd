// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_color.hpp"

#include <format>
#include <string>
#include <string_view>

#include "color_acyclic.hpp"
#include "color_cgen.hpp"
#include "color_common.hpp"
#include "color_flat.hpp"
#include "color_mincut.hpp"
#include "color_path.hpp"
#include "color_stats.hpp"
#include "color_synth.hpp"
#include "diag.hpp"
#include "node_util.hpp"
#include "str_tools.hpp"

using namespace livehd::color;

static Pass_plugin sample("pass_color", Pass_color::setup);

Pass_color::Pass_color(const Eprp_var& var) : Pass("pass.color", var) {}

void Pass_color::setup() {
  Eprp_method m("pass.color", "Hierarchical node coloring (acyclic|cgen|synth|path|mincut|flat|clear)", &Pass_color::color);
  m.add_label_optional("alg", "algorithm: acyclic|cgen|synth|path|mincut|flat|clear", "acyclic");
  // The top module is the shared kernel `--top` flag (lhd plumbs it into the
  // `top` label), not a per-pass --set option.
  m.add_label_optional("hier", "color every unique def in the hierarchy (else only the top)", "true");
  m.add_label_optional("verbose", "print per-graph coloring statistics", "false");
  m.add_label_optional("stats",
                       "print the partition report (count, max/min/avg/median size, singletons, uncolored) to stderr. "
                       "A partition is one (def, color) -- the unit pass.partition emits as `<def>__c<id>`. "
                       "`--stats` is the CLI sugar for the same knob; verbose adds the per-def table",
                       "false");
  m.add_label_optional("compact", "write the flat per-def color (default; false = per-instance hier color)", "true");
  m.add_label_optional("continuous", "split each color into one id per connected region", "false");
  m.add_label_optional("keep_colored", "preserve pre-existing colors on nodes the algorithm leaves uncolored", "false");
  m.add_label_optional("cutoff", "acyclic: small-partition node-count merge cutoff", "1");
  m.add_label_optional("merge", "acyclic: enable same/one-parent partition merging", "false");
  // The mincut RNG seed is the shared kernel `lhd.seed` (lhd plumbs it into the
  // `seed` label), not a per-pass --set option.
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

// Each algorithm hands apply_coloring its own Node2Id, so the per-def sizes are
// collected there. Color_opts carries the sink so every algorithm reports
// without each one growing a stats parameter.
void run_one(std::string_view alg, hhds::Graph* g, const Color_opts& opts, const Eprp_var& var) {
  if (alg == "acyclic") {
    Color_acyclic c(opts, str_tools::to_i(var.get("cutoff", "1")), parse_bool(var.get("merge", "false")));
    c.label(g);
  } else if (alg == "cgen") {
    Color_cgen c(opts);
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
  } else if (alg == "flat") {
    Color_flat c(opts);
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

  if (alg != "acyclic" && alg != "cgen" && alg != "synth" && alg != "path" && alg != "mincut" && alg != "flat") {
    livehd::diag::err("pass.color", "bad-alg", "unsupported")
        .msg("unknown algorithm '{}' (expected acyclic|cgen|synth|path|mincut|flat|clear)", alg)
        .fatal();
  }

  // A silent fallback here is a wrong ANSWER, not a wrong flag: `synth_alg=pipe`
  // and `synth_alg=synth` cut at different boundaries, and a typo used to mean
  // "synth" -- so it colored, exited 0, and reported nothing.
  if (auto synth_alg = std::string{var.get("synth_alg", "synth")}; alg == "synth" && synth_alg != "synth" && synth_alg != "pipe") {
    livehd::diag::err("pass.color", "bad-synth-alg", "unsupported")
        .msg("unknown synth_alg '{}' (expected synth|pipe)", synth_alg)
        .hint("synth: cut at state AND large arithmetic (Mult/Div, Sum wider than 8)")
        .hint("pipe: cut at state only -- one region per pipeline stage")
        .fatal();
  }

  Color_opts opts;
  opts.hier         = parse_bool(var.get("hier", "true"));
  opts.verbose      = parse_bool(var.get("verbose", "false"));
  const bool stats  = parse_bool(var.get("stats", "false"));
  opts.compact      = parse_bool(var.get("compact", "true"));
  opts.continuous   = parse_bool(var.get("continuous", "false"));
  opts.keep_colored = parse_bool(var.get("keep_colored", "false"));

  // `flat` is "one color for everything"; the per-region continuous split would
  // undo that. Force it off up front so both the run and the recorded params
  // (params_json below) reflect the actual single-color behavior.
  if (alg == "flat") {
    opts.continuous = false;
  }

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

  // Size warning: coloring itself is fine at any size, but a flattened design
  // this large is a landmine for a downstream ABC/LEC run (which is exactly why
  // one usually colors before synthesizing). Warn once, up front, with the real
  // count. This is a plain lazy walk of each unique def body -- no O(flat-nodes)
  // materialization (see flat_node_count). Skipped entirely when the gate is
  // disabled, so it never adds a walk to a run that did not ask for it.
  if (const uint64_t threshold = livehd::graph_util::large_design_node_threshold();
      top_g != nullptr && threshold != UINT64_MAX) {
    const uint64_t nodes = livehd::graph_util::flat_node_count(top_g, [&](hhds::Gid gid) -> hhds::Graph* {
      auto it = gid2graph.find(gid);
      return it == gid2graph.end() ? nullptr : it->second;
    });
    if (nodes > threshold) {
      livehd::diag::warn("pass.color", "large-design", "unsupported")
          .msg("very large flattened design: {} nodes (over {})", nodes, threshold)
          .hint("synthesis (pass.abc) or formal (pass.lec) on a design this size may exhaust host memory")
          .hint("color into smaller regions (pass.color synth) and check them individually")
          .emit();
    }
  }

  // One def at a time: the algorithms are per-def, so `stats` folds each def's
  // outcome into the cross-def aggregate here. Without --stats the sink stays
  // null and apply_coloring skips the tallying entirely.
  // How many times each def appears under --top. hier_range walks the structure
  // tree alone (never node_table), so this is proportional to the hierarchy
  // size, not the node count. Only `flat` reads it -- see Color_stats::report.
  absl::flat_hash_map<hhds::Gid, uint64_t> inst_cnt;
  if (stats && top_g != nullptr) {
    inst_cnt[top_g->get_gid()] = 1;  // the top is instantiated once, by definition
    for (auto inst : top_g->hier_range()) {
      ++inst_cnt[inst.get_target_gid()];
    }
  }

  Color_stats stats_acc;
  auto        color_def = [&](hhds::Graph* g) {
    Def_color_sizes sizes;
    Color_opts      o = opts;
    o.sizes           = stats ? &sizes : nullptr;
    run_one(alg, g, o, var);
    if (stats) {
      auto it = inst_cnt.find(g->get_gid());
      stats_acc.add(g->get_name(), sizes, it == inst_cnt.end() ? 1 : it->second);
    }
  };

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
        color_def(it->second);
      }
    }
  } else if (top_g != nullptr) {
    color_def(top_g);  // single-level (hier=false)
  } else {
    for (const auto& g : var.graphs) {
      if (g) {
        color_def(g.get());
      }
    }
  }

  if (stats) {
    stats_acc.report(alg, opts.verbose);
  }

  if (top_g != nullptr) {
    // preserve_seeded_info keeps the block-attribute members ("seeded",
    // "region_opts") alive across this rebuild (2opt-freq B).
    set_coloring_info(
        top_g,
        preserve_seeded_info(top_g,
                             build_coloring_info_json(top_g, top.empty() ? top_g->get_name() : top, alg, params_json(alg, opts, var))));
  }
}

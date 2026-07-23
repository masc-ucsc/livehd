// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_color.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <print>
#include <string>
#include <string_view>
#include <system_error>

#include "color_absorb.hpp"
#include "color_acyclic.hpp"
#include "color_cgen.hpp"
#include "color_common.hpp"
#include "color_flat.hpp"
#include "color_mincut.hpp"
#include "color_path.hpp"
#include "color_reduce.hpp"
#include "color_stats.hpp"
#include "color_synth.hpp"
#include "diag.hpp"
#include "node_util.hpp"
#include "str_tools.hpp"

using namespace livehd::color;

static Pass_plugin sample("pass_color", Pass_color::setup);

Pass_color::Pass_color(const Eprp_var& var) : Pass("pass.color", var) {}

void Pass_color::setup() {
  Eprp_method m("pass.color", "Hierarchical node coloring (acyclic|cgen|synth|path|mincut|flat|reduce|clear)", &Pass_color::color);
  m.add_label_optional("alg", "algorithm: acyclic|cgen|synth|path|mincut|flat|reduce|clear", "acyclic");
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
  // The size window. MAPPABLE gate equivalents (graph_util::mappable_ge_weight:
  // Sub instances count ~1 -- their logic is weighed in their own def), not
  // nodes: ABC's memory scales with the BIT-BLASTED gate count, so a 200k-node
  // region of wide datapath passes any node gate and still exhausts the host.
  m.add_label_optional("min_ge",
                       "synth: merge a region below this many gate-equivalents into its best-connected "
                       "neighbour (0 => no lower bound). Kills singleton regions",
                       "1000");
  // 30M GE ~= 1 GB of expected ABC peak, calibrated at ~30 bytes/GE from the
  // one hard data point there is: a flat XSCore run reached 221 GB against
  // 7.43e9 boundary GE. The window now counts MAPPABLE GE (Sub ports excluded),
  // which is smaller, so the same cap is if anything more conservative. Raise it
  // on a bigger host; lower it if admission fires.
  m.add_label_optional("max_ge",
                       "synth: split a region above this many MAPPABLE gate-equivalents (0 => no upper bound). "
                       "Default ~1 GB of expected ABC peak per region (~30 bytes/GE); raise it on a bigger host",
                       "30000000");
  m.add_label_optional("absorb",
                       "synth: STRUCTURALLY INLINE every def below `min_ge` into its parents before coloring, so its "
                       "logic can cluster with its neighbours (a Sub is a blackbox to ABC, so nothing less merges "
                       "it). Rewrites the design; needs min_ge>0 and hier=true. false leaves tiny defs their own regions",
                       "true");
  m.add_label_optional("name_weight",
                       "synth: in the size window, bind a region N x tighter across an ANONYMOUS crossing (one that "
                       "pass.partition would name `<op>_<nid>` -- a Mult/Div/mask intermediate) so the merge swallows it "
                       "and surviving boundaries land on STABLE names the incremental cache can reuse. 1 = off. QoR-"
                       "neutral at the default",
                       "4");
  m.add_label_optional("instance", "path: comma-separated seed instance names (forward-only)", "");
  m.add_label_optional("min_count",
                       "reduce: occurrences a repeated subgraph needs before it is extracted as a shared def",
                       "3");
  m.add_label_optional("min_nodes", "reduce: smallest cone (in nodes) worth extracting", "3");
  m.add_label_optional("min_win",
                       "reduce: required PER-SITE Verilog line win (estimated lines saved minus the instance's "
                       "ports+2). 0 disables the guard and extracts on node count alone",
                       "1");
  register_pass(m);
}

namespace {

bool parse_bool(std::string_view v) { return v != "false" && v != "0" && !v.empty(); }

// A size-window bound. A silent fallback here is a wrong ANSWER rather than a
// wrong flag: `max=200O00` (letter O) parsed leniently would read as 200, and
// pass.color would happily shred the design into thousands of tiny regions and
// report success. Same shape as pass.abc's memory_budget_mb.
uint64_t parse_ge_bound(const Eprp_var& var, std::string_view label, std::string_view dflt) {
  const auto s = std::string{var.get(label, std::string{dflt})};
  uint64_t   v = 0;
  auto*      b = s.data();
  auto*      e = s.data() + s.size();
  if (auto [p, ec] = std::from_chars(b, e, v); ec != std::errc{} || p != e) {
    livehd::diag::err("pass.color", "bad-size-window", "io")
        .msg("pass.color: {} must be a non-negative integer of gate-equivalents, got '{}'", label, s)
        .hint("0 disables that half of the window")
        .fatal();
  }
  return v;
}

// A plain non-negative integer label (counts). Same no-silent-fallback rule as
// parse_ge_bound: a mis-typed `min_count=3O` must not quietly become something.
uint64_t parse_count(const Eprp_var& var, std::string_view label, std::string_view dflt) {
  const auto s = std::string{var.get(label, std::string{dflt})};
  uint64_t   v = 0;
  auto*      b = s.data();
  auto*      e = s.data() + s.size();
  if (auto [p, ec] = std::from_chars(b, e, v); ec != std::errc{} || p != e) {
    livehd::diag::err("pass.color", "bad-count", "io")
        .msg("pass.color: {} must be a non-negative integer, got '{}'", label, s)
        .fatal();
  }
  return v;
}

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
    // The window is recorded only for the algorithm that honors it -- printing
    // min/max under `acyclic` would claim a bound nothing enforced.
    s += std::format(",\"synth_alg\":\"{}\",\"min_ge\":{},\"max_ge\":{},\"name_weight\":{}", var.get("synth_alg", "synth"),
                     opts.min_ge, opts.max_ge, opts.name_weight);
    if (opts.min_ge != 0) {
      // The window bin-packs isolated under-min leftovers, so a color id MAY
      // span several disconnected clouds. pass.partition keys its same-color
      // anchor union off this flag; without it the component split would
      // silently shred the bins back into per-cloud modules.
      s += ",\"packed\":true";
    }
  } else if (alg == "mincut") {
    s += std::format(",\"iters\":{},\"seed\":{},\"mincut_alg\":\"{}\"",
                     var.get("iters", "1"),
                     var.get("seed", "0"),
                     var.get("mincut_alg", "vc"));
  } else if (alg == "path") {
    s += std::format(",\"instance\":\"{}\"", var.get("instance", ""));
  }
  // `reduce` never reaches here: it neither writes colors nor coloring_info.
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

  if (alg != "acyclic" && alg != "cgen" && alg != "synth" && alg != "path" && alg != "mincut" && alg != "flat"
      && alg != "reduce") {
    livehd::diag::err("pass.color", "bad-alg", "unsupported")
        .msg("unknown algorithm '{}' (expected acyclic|cgen|synth|path|mincut|flat|reduce|clear)", alg)
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
  opts.min_ge       = parse_ge_bound(var, "min_ge", "1000");
  opts.max_ge       = parse_ge_bound(var, "max_ge", "30000000");
  opts.name_weight  = std::max(1, std::atoi(std::string{var.get("name_weight", "4")}.c_str()));

  if (opts.min_ge != 0 && opts.max_ge != 0 && opts.min_ge > opts.max_ge) {
    livehd::diag::err("pass.color", "bad-size-window", "io")
        .msg("pass.color: min ({}) is above max ({}): no region size satisfies the window", opts.min_ge, opts.max_ge)
        .fatal();
    return;
  }

  // `flat` is "one color for everything"; the per-region continuous split would
  // undo that, and so would the size window -- a flat region is a deliberate
  // whole-design ABC request and is never subdivided (2opt-incr ruling). Force
  // both off up front so the run and the recorded params (params_json below)
  // reflect the actual single-color behavior.
  if (alg == "flat") {
    opts.continuous = false;
    opts.min_ge     = 0;
    opts.max_ge     = 0;
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

  // `reduce` is not a per-def labeling: it mines repeated cones ACROSS defs,
  // then rewrites the library in place (shared pattern defs + instance
  // splices). It owns its own orchestration and reporting, so it branches off
  // before the per-def color_def machinery.
  if (alg == "reduce") {
    std::vector<hhds::Graph*> defs;
    if (top_g != nullptr && opts.hier) {
      absl::flat_hash_set<hhds::Gid> todo;
      todo.insert(top_g->get_gid());
      for (auto inst : top_g->hier_range()) {
        todo.insert(inst.get_target_gid());
      }
      for (auto gid : todo) {
        if (auto it = gid2graph.find(gid); it != gid2graph.end()) {
          defs.push_back(it->second);
        }
      }
    } else if (top_g != nullptr) {
      defs.push_back(top_g);
    } else {
      for (const auto& g : var.graphs) {
        if (g) {
          defs.push_back(g.get());
        }
      }
    }
    // Deterministic def order => deterministic representative selection.
    std::sort(defs.begin(), defs.end(), [](hhds::Graph* a, hhds::Graph* b) { return a->get_name() < b->get_name(); });

    Reduce_opts ropts;
    ropts.min_count = parse_count(var, "min_count", "3");
    ropts.min_nodes = parse_count(var, "min_nodes", "3");
    ropts.min_win   = parse_count(var, "min_win", "1");
    ropts.verbose   = opts.verbose;
    if (ropts.min_count < 2) {
      livehd::diag::err("pass.color", "bad-count", "io")
          .msg("pass.color: reduce min_count must be at least 2 (got {}): a pattern seen once has nothing to share",
               ropts.min_count)
          .fatal();
      return;
    }
    if (ropts.min_nodes < 1) {
      livehd::diag::err("pass.color", "bad-count", "io")
          .msg("pass.color: reduce min_nodes must be at least 1 (got 0)")
          .fatal();
      return;
    }

    Reduce_stats rst;
    if (!color_reduce(defs, ropts, &rst)) {
      return;  // diag already emitted; the library may be half-transformed
    }
    if (stats || opts.verbose) {
      // stderr on purpose: run_step dup2's fd 1 into the log file.
      std::print(stderr,
                 "[color.reduce] defs {} ({} seeded skipped); {} cones >= {} nodes; {} patterns x {} sites "
                 "({} const params); nodes -{} +{} (net {:+}); dropped: {} verify, {} port-heavy, {} dup-edge, "
                 "{} reuse-fragile\n",
                 rst.defs_scanned,
                 rst.defs_skipped_seeded,
                 rst.cones,
                 ropts.min_nodes,
                 rst.patterns,
                 rst.occurrences,
                 rst.promoted_consts,
                 rst.nodes_deleted,
                 rst.nodes_created,
                 static_cast<int64_t>(rst.nodes_created) - static_cast<int64_t>(rst.nodes_deleted),
                 rst.verify_dropped,
                 rst.port_heavy_skipped,
                 rst.dup_edge_skipped,
                 rst.reuse_refused);
    }
    // NO coloring_info rewrite: reduce neither reads nor writes node colors,
    // and clobbering the descriptor of a prior coloring (e.g. synth's
    // "packed":true, which pass.partition keys its anchor union off) would
    // corrupt a downstream partition run. The library rewrite IS the output.
    return;
  }

  // Absorb runs BEFORE anything is colored and BEFORE the instance counts below
  // are taken: it removes defs from the hierarchy, so a count taken first would
  // describe a design that no longer exists.
  //
  // Only `synth` honors the window, so only `synth` absorbs -- and only with a
  // floor to measure against. This is the one thing pass.color does that rewrites
  // the design rather than annotating it, which is why it has its own off switch.
  Absorb_stats absorbed;
  if (alg == "synth" && opts.hier && opts.min_ge != 0 && parse_bool(var.get("absorb", "true")) && top_g != nullptr) {
    if (!absorb_small_defs(top_g, gid2graph, opts.min_ge, &absorbed)) {
      return;  // diag already emitted; the library is half-transformed
    }
    if (opts.verbose && absorbed.defs_absorbed != 0) {
      std::print(stderr,
                 "[color.absorb] {} def(s) below {} GE inlined at {} site(s); {} GE duplicated\n",
                 absorbed.defs_absorbed,
                 opts.min_ge,
                 absorbed.sites_inlined,
                 absorbed.ge_duplicated);
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
    stats_acc.set_absorbed_defs(absorbed.defs_absorbed);
    stats_acc.report(alg, opts.verbose, opts.min_ge, opts.max_ge);
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

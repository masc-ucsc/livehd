//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Standalone graph-pass command plumbing, including semdiff.

#include "lhd_kernel_internal.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>

#include "color_common.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"
#include "pass.hpp"
#include "semdiff.hpp"

namespace lhd {

// ---- semdiff (structural diff/match: stamp the `match` attribute) -----------

// `lhd pass semdiff --ref lg:A --impl lg:B [--top m]`: structural LEC. Mirrors
// lec_command (two sides, per-side top pick) but instead of an SMT proof it
// calls semdiff::structural_match to stamp corresponding nodes/pins of both
// libraries with a shared `match` id (0 = no counterpart), then saves both lg:
// back in place — so the diff is greppable (`lhd tool grep match=0 lg:B`) and
// visualizable (`lhd tool diff lg:A lg:B --match`). v1 marks lg: libraries in
// place, so both sides must be lg: (compile sources to lg: first); the
// structural_match API itself is kind-agnostic (pass.semdiff / a future lec).
// Reached only through pass_command (`lhd pass semdiff`), not a top-level word.
void semdiff_command(Options& opts, Result& res) {
  setup_diag(opts, "semdiff");
  if (opts.impl_path.empty() || opts.ref_path.empty()) {
    throw Lhd_error{"usage",
                    "pass semdiff requires --ref lg:DIR and --impl lg:DIR",
                    "e.g. `lhd pass semdiff --ref lg:gold --impl lg:opt --top adder`"};
  }
  if (opts.ref_kind != "lg" || opts.impl_kind != "lg") {
    throw Lhd_error{"usage",
                    "v1 semdiff marks lg: libraries in place, so both --ref and --impl must be lg:DIR",
                    "compile sources to lg: first (e.g. `lhd compile a.v --emit-dir lg:gold`)"};
  }
  if (fs::weakly_canonical(opts.ref_path) == fs::weakly_canonical(opts.impl_path)) {
    throw Lhd_error{"usage", "pass semdiff --ref and --impl must be different lg: libraries", ""};
  }

  // Each side loads from its OWN library instance, so the two graphs keep
  // independent gids and attr stores (the cross-library trap) — hold the two
  // Graph* directly.
  Eprp_var ref_var;
  Eprp_var impl_var;
  load_side_graphs(opts, res, opts.ref_kind, opts.ref_path, "ref", ref_var);
  load_side_graphs(opts, res, opts.impl_kind, opts.impl_path, "impl", impl_var);

  Eprp_var::Eprp_dict labels;
  merge_sets(opts, "pass.semdiff", labels);
  auto label = [&](std::string_view k, std::string_view def) -> std::string {
    auto it = labels.find(std::string{k});
    return it == labels.end() ? std::string{def} : it->second;
  };
  auto truthy = [&](std::string_view k, std::string_view def) {
    auto v = label(k, def);
    if (v.empty()) {
      v = def;  // a registry-injected empty default means "unset"
    }
    return v != "false" && v != "0";
  };
  // `stats` is a PRESET, not just a report toggle: "how well do these two
  // designs correspond?" needs the state tiers on, and a single-def node count
  // answers nothing. An explicit --set of any implied key still wins — label()
  // returns the registry default only when the key is absent, so
  // `truthy(k, stats?…)` is overridden by --set. `--stats` is the CLI sugar for
  // the same knob; either form turns it on. hier is on by default (like every
  // hier option): sweep the whole def DAG; hier=false compares one top pair.
  const bool stats = opts.stats || truthy("stats", "0");
  const bool hier  = truthy("hier", "1");

  livehd::semdiff::Semdiff_options o;
  o.alg            = label("alg", "structural");
  o.matching_names = truthy("matching_names", stats ? "true" : "false");
  o.state_pairing  = truthy("state_pairing", stats ? "true" : "false");
  o.dump_state     = truthy("dump_state", "false");
  o.id_granularity = label("id_granularity", "pair");
  o.verbose        = false;  // the command prints its own summary below
  if (o.id_granularity != "pair" && o.id_granularity != "region") {
    throw Lhd_error{"usage", std::format("--set pass.semdiff.id_granularity expects pair|region, got '{}'", o.id_granularity), ""};
  }
  if (auto v = label("name_noise", "0"); !v.empty() && v != "0") {
    char*  end  = nullptr;
    double frac = std::strtod(v.c_str(), &end);
    if (end == v.c_str() || frac < 0.0 || frac > 1.0) {
      throw Lhd_error{"usage", std::format("--set pass.semdiff.name_noise expects a fraction in [0,1], got '{}'", v), ""};
    }
    o.name_noise = frac;
  }
  if (auto v = label("noise_seed", "1"); !v.empty()) {
    o.noise_seed = std::strtoull(v.c_str(), nullptr, 10);
  }
  if (auto v = label("synalign_maxiter", "64"); !v.empty()) {
    o.synalign_maxiter = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
    if (o.synalign_maxiter == 0) {
      throw Lhd_error{"usage", "--set pass.semdiff.synalign_maxiter expects a round count >= 1", ""};
    }
  }
  if (auto v = label("explain_noise", "0"); !v.empty()) {
    o.explain_noise = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
  }
  // Stats sweeps are the iterate-on-the-matcher loop; re-saving two whole
  // libraries per iteration is pure drag, so stats defaults save OFF. Plain
  // runs save, keeping the mark-in-place `match` workflow (tool grep/diff).
  const bool save = truthy("save", stats ? "0" : "1");

  res.recipe_steps.emplace_back(std::format("pass.semdiff alg:{} matching_names:{} state_pairing:{} hier:{} id_granularity:{}",
                                            o.alg,
                                            o.matching_names,
                                            o.state_pairing,
                                            hier,
                                            o.id_granularity));

  // The state-correspondence stats line — the per-def and aggregate instrument
  // for iterating on the tier-2 matcher (2f-lec two-tier correspondence).
  auto print_state = [](std::string_view tag, const livehd::semdiff::State_stats& st) {
    std::print(
        "semdiff[state]{}: state ref/impl {}/{} (mems {}/{}), name-paired {} (grouped {}/{}), full-paired {} ({} rounds), "
        "unpaired ref {} (ambiguous {}) impl {} (ambiguous {})\n",
        tag,
        st.a_total,
        st.b_total,
        st.a_mems,
        st.b_mems,
        st.name_pairs,
        st.a_name_grouped,
        st.b_name_grouped,
        st.full_pairs,
        st.rounds,
        st.a_unpaired,
        st.a_ambiguous,
        st.b_unpaired,
        st.b_ambiguous);
    if (st.noised != 0) {
      std::print("semdiff[noise]{}: noised {} recovered {} (correct {}, wrong {})\n",
                 tag,
                 st.noised,
                 st.noised_recovered,
                 st.noised_correct,
                 st.noised_recovered - st.noised_correct);
    }
  };

  // The `stats` report: the one-screen "do these two designs correspond?" answer.
  // Deliberately reports BOTH sides — a ref-side-only view hides impl-side extra
  // logic (the flatten/inline asymmetry that makes a diff look clean from one
  // end). `pct` guards div-by-zero on empty designs.
  auto print_stats = [](uint32_t def_pairs, uint32_t ref_only, uint64_t a_matched, uint64_t a_total, uint64_t b_matched,
                        uint64_t b_total, uint64_t regions, const livehd::semdiff::State_stats& st) {
    auto pct = [](uint64_t n, uint64_t d) { return d == 0 ? 100.0 : 100.0 * static_cast<double>(n) / static_cast<double>(d); };
    // Registers = every state cell that is not a Memory; the pair counts carry
    // their own Memory subset so both rows are exact rather than inferred.
    const uint32_t a_regs = st.a_total - st.a_mems;
    const uint32_t b_regs = st.b_total - st.b_mems;
    const uint32_t paired_mem  = st.name_pairs_mem + st.full_pairs_mem;
    const uint32_t paired_regs = (st.name_pairs + st.full_pairs) - paired_mem;
    std::print("semdiff[stats]: defs      {} paired, {} ref-only\n", def_pairs, ref_only);
    std::print("semdiff[stats]: nodes     ref {}/{} matched ({:.1f}%), impl {}/{} matched ({:.1f}%), {} region(s)\n",
               a_matched, a_total, pct(a_matched, a_total), b_matched, b_total, pct(b_matched, b_total), regions);
    std::print("semdiff[stats]: registers ref {}/{} paired ({:.1f}%), impl {}/{} — by name {}, by structure {}\n",
               paired_regs, a_regs, pct(paired_regs, a_regs), paired_regs, b_regs,
               st.name_pairs - st.name_pairs_mem, st.full_pairs - st.full_pairs_mem);
    std::print("semdiff[stats]: memories  ref {}/{} paired ({:.1f}%), impl {}/{} — by name {}, by structure {}\n",
               paired_mem, st.a_mems, pct(paired_mem, st.a_mems), paired_mem, st.b_mems, st.name_pairs_mem,
               st.full_pairs_mem);
    std::print("semdiff[stats]: UNMATCHED ref {} node(s) / {} state ({} ambiguous), impl {} node(s) / {} state ({} ambiguous)\n",
               a_total - a_matched, st.a_unpaired, st.a_ambiguous, b_total - b_matched, st.b_unpaired, st.b_ambiguous);
    const bool clean = a_matched == a_total && b_matched == b_total && st.a_unpaired == 0 && st.b_unpaired == 0 && ref_only == 0;
    std::print("semdiff[stats]: => {}\n",
               clean ? "designs fully correspond"
                     : "DIFFERENCES present (grep the `match=0` nodes: `lhd tool grep match=0 lg:<impl>`)");
  };

  if (hier) {
    // ---- hierarchical stats sweep: every def pair, entity-canonicalized ------
    // Mirrors lec_hierarchical's correspondence: defs pair by ENTITY (post-'.'
    // tail) when the entity is side-unique, else full name; scoped to --top's
    // transitive ref-side subtree when a top is given, else every shared def.
    namespace gu = livehd::graph_util;
    auto entity_of = [](std::string_view n) -> std::string {
      auto d = n.rfind('.');
      return std::string(d == std::string_view::npos ? n : n.substr(d + 1));
    };
    absl::flat_hash_map<std::string, int> ref_ent_cnt, impl_ent_cnt;
    for (auto& g : ref_var.graphs) {
      if (g) {
        ref_ent_cnt[entity_of(g->get_name())]++;
      }
    }
    for (auto& g : impl_var.graphs) {
      if (g) {
        impl_ent_cnt[entity_of(g->get_name())]++;
      }
    }
    auto canon_ref = [&](std::string_view full) -> std::string {
      auto e  = entity_of(full);
      auto it = ref_ent_cnt.find(e);
      return it != ref_ent_cnt.end() && it->second == 1 ? e : std::string(full);
    };
    auto canon_impl = [&](std::string_view full) -> std::string {
      auto e  = entity_of(full);
      auto it = impl_ent_cnt.find(e);
      return it != impl_ent_cnt.end() && it->second == 1 ? e : std::string(full);
    };
    absl::flat_hash_map<std::string, hhds::Graph*> ref_by_name, impl_by_name;
    for (auto& g : ref_var.graphs) {
      if (g) {
        ref_by_name[canon_ref(g->get_name())] = g.get();
      }
    }
    for (auto& g : impl_var.graphs) {
      if (g) {
        impl_by_name[canon_impl(g->get_name())] = g.get();
      }
    }

    // Scope: --top's ref-side transitive subtree (over ALL ref defs, so
    // one-sided children are still visited and reported), else every ref def.
    std::vector<std::string>                                    scope;  // ref canon keys, leaves-first
    absl::flat_hash_map<std::string, std::vector<std::string>> children;
    for (auto& [name, g] : ref_by_name) {
      absl::flat_hash_set<std::string> seen;
      for (auto node : g->forward_class()) {
        if (gu::type_op_of(node) != Ntype_op::Sub) {
          continue;
        }
        auto        sio = node.get_subnode_io();
        std::string cn  = canon_ref(sio->get_name());
        if (ref_by_name.contains(cn) && seen.insert(cn).second) {
          children[name].push_back(cn);
        }
      }
    }
    std::string want_top = !opts.ref_top.empty() ? opts.ref_top : opts.top;
    if (want_top.empty() && !opts.impl_top.empty()) {
      // --impl-top names the ref top's renamed counterpart; anchor the pair on
      // the sole ref def when no --ref-top/--top disambiguates.
      if (ref_by_name.size() == 1) {
        want_top = ref_by_name.begin()->first;
      } else {
        throw Lhd_error{"usage",
                        "pass semdiff: --impl-top needs --ref-top (or --top) when the ref library holds several defs",
                        ""};
      }
    }
    if (!want_top.empty()) {
      // Accept the top as full name or unambiguous entity (lec's rule, via
      // the shared resolver — it warns when the fallback substitutes).
      std::vector<std::string> ref_names;
      ref_names.reserve(ref_var.graphs.size());
      for (const auto& g : ref_var.graphs) {
        if (g) {
          ref_names.emplace_back(g->get_name());
        }
      }
      const std::string resolved = resolve_top_name(ref_names, want_top, "pass.semdiff");
      if (resolved.empty()) {
        throw Lhd_error{"config", std::format("pass semdiff: ref top '{}' not found", want_top), ""};
      }
      const std::string top_key = canon_ref(resolved);
      if (!opts.impl_top.empty()) {
        // Renamed-top compare: the sweep pairs defs by canonical name, which
        // can never find a differently-named counterpart — seed the explicit
        // top pair under the ref key (children still pair by name).
        std::vector<std::string> impl_names;
        impl_names.reserve(impl_var.graphs.size());
        for (const auto& g : impl_var.graphs) {
          if (g) {
            impl_names.emplace_back(g->get_name());
          }
        }
        const std::string iresolved = resolve_top_name(impl_names, opts.impl_top, "pass.semdiff");
        if (iresolved.empty()) {
          throw Lhd_error{"config", std::format("pass semdiff: impl top '{}' not found", opts.impl_top), ""};
        }
        for (const auto& g : impl_var.graphs) {
          if (g && g->get_name() == iresolved) {
            impl_by_name[top_key] = g.get();
            break;
          }
        }
      }
      absl::flat_hash_map<std::string, int>   mark;
      std::function<void(const std::string&)> dfs = [&](const std::string& n) {
        int& m = mark[n];
        if (m != 0) {
          return;
        }
        m = 1;
        if (auto it = children.find(n); it != children.end()) {
          for (const auto& c : it->second) {
            dfs(c);
          }
        }
        m = 2;
        scope.push_back(n);
      };
      dfs(top_key);
    } else {
      for (auto& [name, g] : ref_by_name) {
        scope.push_back(name);
      }
      std::sort(scope.begin(), scope.end());
    }

    livehd::semdiff::State_stats agg;
    uint32_t                     def_pairs = 0, ref_only = 0;
    // NODE totals across the sweep (the state agg above covers state cells only).
    // A ref-only def's nodes are unmatched by construction and are counted below,
    // so `nodes ref X/Y` stays honest about one-sided defs.
    uint64_t agg_a_matched = 0, agg_a_total = 0, agg_b_matched = 0, agg_b_total = 0, agg_regions = 0;
    uint32_t                     explain_left = o.explain_noise;  // budget shared across defs
    auto count_state = [](hhds::Graph* g, uint32_t& total, uint32_t& mems) {
      for (auto node : g->forward_class()) {
        auto op = gu::type_op_of(node);
        if (op == Ntype_op::Flop || op == Ntype_op::Latch || op == Ntype_op::Fflop || op == Ntype_op::Memory) {
          ++total;
          mems += op == Ntype_op::Memory ? 1 : 0;
        }
      }
    };
    for (const auto& name : scope) {
      auto rit = ref_by_name.find(name);
      auto iit = impl_by_name.find(name);
      if (iit == impl_by_name.end()) {
        // One-sided def: its state is unpairable by construction — count it so
        // the aggregate totals stay honest.
        ++ref_only;
        uint32_t t = 0, m = 0;
        count_state(rit->second, t, m);
        for (auto node : rit->second->forward_class()) {
          (void)node;
          ++agg_a_total;  // one-sided def: every node is unmatched by construction
        }
        agg.a_total += t;
        agg.a_mems += m;
        agg.a_unpaired += t;
        if (!opts.quiet && t != 0) {
          std::print("semdiff[hier]: '{}' REF-ONLY, {} state cell(s) unpairable\n", name, t);
        }
        continue;
      }
      ++def_pairs;
      auto o2          = o;
      o2.explain_noise = explain_left;
      auto r           = livehd::semdiff::structural_match(rit->second, iit->second, o2);
      explain_left -= std::min(explain_left, r.state.explained);
      agg += r.state;
      agg_a_matched += r.a_matched;
      agg_a_total += r.a_matched + r.a_unmatched;
      agg_b_matched += r.b_matched;
      agg_b_total += r.b_matched + r.b_unmatched;
      agg_regions += r.regions;
      const auto& st = r.state;
      if (!opts.quiet && (st.a_unpaired != 0 || st.b_unpaired != 0 || st.full_pairs != 0)) {
        print_state(std::format(" '{}'", name), st);
      }
    }
    if (def_pairs == 0) {
      // A sweep that paired NOTHING compared nothing — never a silent "pass"
      // (differently-named tops land here; the sweep pairs defs by name).
      throw Lhd_error{"config",
                      std::format("pass semdiff: 0 def pairs ({} ref-only def(s)) — no impl def pairs any ref def by name",
                                  ref_only),
                      "tops named differently? pass --ref-top/--impl-top for the renamed top pair, or "
                      "--set pass.semdiff.hier=0 for a single top-pair compare"};
    }
    if (!opts.quiet) {
      std::print("semdiff[hier]: {} def pair(s), {} ref-only def(s){}\n",
                 def_pairs,
                 ref_only,
                 want_top.empty() ? std::string{} : std::format(", scope top '{}'", want_top));
      print_state("[total]", agg);
      if (stats) {
        print_stats(def_pairs, ref_only, agg_a_matched, agg_a_total, agg_b_matched, agg_b_total, agg_regions, agg);
      }
    }
    res.recipe_steps.emplace_back(std::format("pass.semdiff hier defs:{} state:{}/{} name:{} full:{} unpaired:{}/{}{}",
                                              def_pairs,
                                              agg.a_total,
                                              agg.b_total,
                                              agg.name_pairs,
                                              agg.full_pairs,
                                              agg.a_unpaired,
                                              agg.b_unpaired,
                                              // Node totals ride the machine-readable line only under stats (they cost
                                              // an extra aggregate the plain hier sweep does not promise).
                                              stats ? std::format(" nodes:{}/{}/{}/{}", agg_a_matched, agg_a_total,
                                                                  agg_b_matched, agg_b_total)
                                                    : std::string{}));
    if (save) {
      livehd::Hhds_graph_library::save(opts.ref_path);
      livehd::Hhds_graph_library::save(opts.impl_path);
      res.outputs.push_back(opts.ref_path);
      res.outputs.push_back(opts.impl_path);
    }
    return;
  }

  // Pick the top module on each side (same rule as lec): explicit
  // --{ref,impl}-top, else --top, else the sole module (pick_top_graph: exact
  // name or unambiguous entity fallback with a diag warning).
  auto ref_g  = pick_top_graph(ref_var, opts.ref_top, opts.top, "ref", "pass semdiff", "pass.semdiff");
  auto impl_g = pick_top_graph(impl_var, opts.impl_top, opts.top, "impl", "pass semdiff", "pass.semdiff");
  auto r      = livehd::semdiff::structural_match(ref_g.get(), impl_g.get(), o);

  if (!opts.quiet) {
    std::print("semdiff: ref '{}' {}/{} matched, impl '{}' {}/{} matched, {} regions, similarity {:.3f}\n",
               ref_g->get_name(),
               r.a_matched,
               r.a_matched + r.a_unmatched,
               impl_g->get_name(),
               r.b_matched,
               r.b_matched + r.b_unmatched,
               r.regions,
               r.similarity);
    if (o.matching_names || o.state_pairing) {
      print_state("", r.state);
    }
    if (stats) {
      // Single-pair: this def only. hier defaults on, so reaching here means
      // the user asked for hier=false explicitly — report the one pair honestly
      // (0 ref-only: a single pair has no one-sided defs by construction).
      print_stats(1, 0, r.a_matched, r.a_matched + r.a_unmatched, r.b_matched, r.b_matched + r.b_unmatched, r.regions,
                  r.state);
    }
    std::print("  inspect: `lhd tool diff lg:{} lg:{} --match`  |  `lhd tool grep match=0 lg:{}`\n",
               opts.ref_path,
               opts.impl_path,
               opts.impl_path);
  }

  // v1 persistence: mark-in-place. Save both libraries back so the `match`
  // attribute survives for the greppable / visualizable workflow
  // (--set pass.semdiff.save=0 skips it — the stats-iteration loop).
  if (save) {
    livehd::Hhds_graph_library::save(opts.ref_path);
    livehd::Hhds_graph_library::save(opts.impl_path);
    res.outputs.push_back(opts.ref_path);
    res.outputs.push_back(opts.impl_path);
  }
}

// ---- pass (graph-pass plumbing: color / partition) --------------------------

// Load every graph of an lg: library into `var` (mirrors synth's lg branch).
void load_lg_into_var(const std::string& lib_path, Eprp_var& var) {
  auto& lib = livehd::Hhds_graph_library::instance(lib_path);
  for (const hhds::Gid id : lib.all_gids()) {  // gids are sparse name-hashes
    auto g = lib.get_graph(id);
    if (g) {
      var.add(g);
    }
  }
}

// Stamp labels["top"] with --top resolved once against the loaded library, so
// every pass consumer gets the full internal name (they match g->get_name()
// exactly); an unresolvable name passes through unchanged (each pass keeps its
// own not-found policy).
static void set_top_label(const Options& opts, const Eprp_var& var, Eprp_var::Eprp_dict& labels,
                          std::string_view diag_pass) {
  if (opts.top.empty()) {
    return;
  }
  std::vector<std::string> names;
  names.reserve(var.graphs.size());
  for (const auto& g : var.graphs) {
    if (g) {
      names.emplace_back(g->get_name());
    }
  }
  const std::string resolved = resolve_top_name(names, opts.top, diag_pass);
  labels["top"]              = resolved.empty() ? opts.top : resolved;
}

// Slurp a pass's "qor" sidecar label into the envelope's "qor" member so an
// agent loop reads its score straight from --result-json (2opt-freq A/D).
static void embed_qor_sidecar(const Eprp_var::Eprp_dict& labels, Result& res) {
  auto qit = labels.find("qor");
  if (qit == labels.end() || qit->second.empty() || !fs::exists(qit->second)) {
    return;
  }
  std::ifstream ifs(qit->second, std::ios::binary);
  std::string   j((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  while (!j.empty() && (j.back() == '\n' || j.back() == '\r' || j.back() == ' ')) {
    j.pop_back();  // RawValue must embed exactly one JSON value
  }
  if (!j.empty()) {
    res.qor_json = std::move(j);
    res.outputs.push_back(qit->second);
  }
}

void pass_command(Options& opts, Result& res) {
  setup_diag(opts, "pass");
  if (opts.files.empty()) {
    throw Lhd_error{"usage",
                    "pass requires a subcommand: color <alg> | partition | abc | opentimer | liberty gensim | semdiff",
                    "e.g. `lhd pass color acyclic --top m lg:dir` or `lhd pass abc --top m lg:dir --emit-dir lg:net`"};
  }
  const std::string sub = opts.files[0];

  // `pass semdiff --ref lg:A --impl lg:B`: the structural diff/match pass takes
  // two lg: libraries via --ref/--impl (not a positional lg: input) and marks
  // the `match` attribute on both in place — so handle it before the single
  // lg:-input requirement below. semdiff_command holds the side-loading logic
  // (it mirrors lec_command), just like `pass liberty` is handled specially.
  if (sub == "semdiff") {
    // (--emit-dir lg:/verilog:/... is already rejected up front for `pass semdiff`
    // by reject_emit_kind in lhd_kernel_compile.cpp -- it marks match in place.)
    semdiff_command(opts, res);
    return;
  }

  // `pass liberty gensim <file.lib> --emit-dir lg:DIR` takes a Liberty FILE, not
  // an lg: input — handle it before the lg: requirement below.
  if (sub == "liberty") {
    std::string subsub = opts.files.size() > 1 ? opts.files[1] : std::string{};
    if (subsub != "gensim") {
      throw Lhd_error{"usage", "pass liberty supports: gensim <file.lib> --emit-dir lg:DIR", ""};
    }
    if (opts.files.size() < 3) {
      throw Lhd_error{"usage", "pass liberty gensim needs a Liberty .lib file argument", ""};
    }
    const std::string lib_file = opts.files[2];
    check_inputs_exist({lib_file});
    const auto* lg_out = find_slot(opts.emit_dirs, "lg");
    if (lg_out == nullptr) {
      throw Lhd_error{"usage", "pass liberty gensim needs --emit-dir lg:DIR for the model library", ""};
    }
    std::error_code ec;
    fs::remove_all(lg_out->path, ec);
    ensure_dir(lg_out->path);
    res.inputs.push_back(lib_file);
    Eprp_var            var;
    Eprp_var::Eprp_dict labels{
        {"files", lib_file},
        {  "out", lg_out->path}
    };
    merge_sets(opts, "pass.liberty", labels);
    run_step("pass.liberty", var, labels, opts, res);
    livehd::Hhds_graph_library::save(lg_out->path);
    res.outputs.push_back(lg_out->path);
    return;
  }

  auto ir = gather_ir_inputs(opts, "pass");
  if (ir.lg_dirs.empty()) {
    throw Lhd_error{"usage", "pass requires an lg:DIR input", "e.g. `lhd pass color acyclic --top m lg:dir`"};
  }
  if (ir.lg_dirs.size() > 1) {
    throw Lhd_error{"unsupported", "multiple lg: inputs are not supported (gids are library-scoped)", ""};
  }
  const auto& lg_in = ir.lg_dirs.front();
  if (!fs::is_directory(lg_in)) {
    throw Lhd_error{"missing_file", std::format("lg: input not found: {}", lg_in), "an lg: input is a GraphLibrary directory"};
  }
  check_ir_body_magic(lg_in, "graph_", kHhdsGraphBodyMagic, "lg:");
  res.inputs.push_back(lg_in);

  if (sub == "color") {
    std::string alg = opts.files.size() > 1 ? opts.files[1] : std::string{"acyclic"};
    if (alg == "reduce" && find_slot(opts.emit_dirs, "lg") != nullptr) {
      // reduce is not a region labeling: it rewrites lg_in in place (shared
      // pattern defs + instance splices), so fusing pass.partition over its
      // leftover colors would shred the reduced defs into modules. Refuse
      // BEFORE the pass runs -- after it, the library is already rewritten.
      throw Lhd_error{"usage",
                      "color reduce rewrites the input lg: in place; --emit-dir lg: is not supported",
                      "copy the lg: dir first if you need the original; run `lhd pass partition` separately for region modules"};
    }
    Eprp_var var;
    load_lg_into_var(lg_in, var);
    if (var.graphs.empty()) {
      throw Lhd_error{"config", std::format("lg: input {} holds no graphs", lg_in), ""};
    }
    Eprp_var::Eprp_dict labels;
    labels["alg"]  = alg;
    labels["seed"] = opts.seed;  // the shared `lhd.seed` (mincut RNG); no per-pass seed option
    set_top_label(opts, var, labels, "pass.color");
    merge_sets(opts, "pass.color", labels);
    if (opts.stats) {
      labels["stats"] = "true";  // CLI sugar for pass.color.stats; either form turns it on
    }
    run_step("pass.color", var, labels, opts, res);
    livehd::Hhds_graph_library::save(lg_in);  // in-place coloring
    res.outputs.push_back(lg_in);

    // `--emit-dir lg:OUT` fuses pass.partition: emit the per-(def,color) module
    // library straight from this run, so a coloring that produces regions (synth,
    // acyclic, ...) does not need a separate `pass partition` afterward. It runs
    // on the just-colored graphs in memory. `flat` colors everything one id, so
    // partition flattens the hierarchy into a single module -- the same result as
    // running the two passes by hand (never a silent no-op).
    if (const auto* lg_out = find_slot(opts.emit_dirs, "lg"); lg_out != nullptr) {
      if (fs::weakly_canonical(lg_out->path) == fs::weakly_canonical(lg_in)) {
        throw Lhd_error{"usage", "color --emit-dir lg: must differ from the input lg:", ""};
      }
      std::error_code ec;
      fs::remove_all(lg_out->path, ec);
      ensure_dir(lg_out->path);
      Eprp_var::Eprp_dict plabels;
      set_top_label(opts, var, plabels, "pass.partition");
      plabels["out"] = lg_out->path;
      merge_sets(opts, "pass.partition", plabels);
      run_step("pass.partition", var, plabels, opts, res);
      livehd::Hhds_graph_library::save(lg_out->path);
      res.outputs.push_back(lg_out->path);
    }
  } else if (sub == "partition") {
    Eprp_var var;
    load_lg_into_var(lg_in, var);
    if (var.graphs.empty()) {
      throw Lhd_error{"config", std::format("lg: input {} holds no graphs", lg_in), ""};
    }
    const auto*         lg_out = find_slot(opts.emit_dirs, "lg");
    Eprp_var::Eprp_dict labels;
    set_top_label(opts, var, labels, "pass.partition");
    if (lg_out != nullptr) {
      if (fs::weakly_canonical(lg_out->path) == fs::weakly_canonical(lg_in)) {
        throw Lhd_error{"usage", "partition --emit-dir lg: must differ from the input lg:", ""};
      }
      std::error_code ec;
      fs::remove_all(lg_out->path, ec);
      ensure_dir(lg_out->path);
      labels["out"] = lg_out->path;
    }
    merge_sets(opts, "pass.partition", labels);
    run_step("pass.partition", var, labels, opts, res);
    if (lg_out != nullptr) {
      livehd::Hhds_graph_library::save(lg_out->path);
      res.outputs.push_back(lg_out->path);
    }
  } else if (sub == "abc") {
    Eprp_var var;
    load_lg_into_var(lg_in, var);
    if (var.graphs.empty()) {
      throw Lhd_error{"config", std::format("lg: input {} holds no graphs", lg_in), ""};
    }
    const auto*         lg_out = find_slot(opts.emit_dirs, "lg");
    Eprp_var::Eprp_dict labels;
    set_top_label(opts, var, labels, "pass.abc");
    if (lg_out != nullptr) {
      if (fs::weakly_canonical(lg_out->path) == fs::weakly_canonical(lg_in)) {
        throw Lhd_error{"usage", "abc --emit-dir lg: must differ from the input lg:", ""};
      }
      std::error_code ec;
      fs::remove_all(lg_out->path, ec);
      ensure_dir(lg_out->path);
      labels["out"] = lg_out->path;
    }
    // QoR sidecar (2opt-freq A): default under --workdir; merge_sets below runs
    // after, so an explicit `--set pass.abc.qor=FILE` overrides the default.
    if (!opts.workdir.empty()) {
      labels["qor"] = (fs::path(opts.workdir) / "qor.json").string();
    }
    merge_sets(opts, "pass.abc", labels);
    // Incremental region cache (2opt-incr): lives under the USER's workdir,
    // exactly like lec's formal_cache.json -- a scratch workdir would make
    // every run cold, so no --workdir means no cache. The location is kernel
    // policy, not a user knob (set AFTER merge_sets on purpose); the user
    // switch is `pass.abc.cache=true|false`.
    if (!opts.workdir.empty()) {
      labels["cache_dir"] = (fs::path(opts.workdir) / "abc_cache").string();
    }
    run_step("pass.abc", var, labels, opts, res);
    if (lg_out != nullptr) {
      livehd::Hhds_graph_library::save(lg_out->path);
      res.outputs.push_back(lg_out->path);
    }
    embed_qor_sidecar(labels, res);
  } else if (sub == "opentimer") {
    // `lhd pass opentimer --top <module> lg:net cells.lib [file.sdc file.spef]`
    // (2opt-freq D): STA on ONE tech-mapped module. Timing files are the bare
    // positional args after the subcommand (like `pass liberty gensim`);
    // `files` is a kernel-managed label, so the kernel builds it here.
    if (const auto* lg_out = find_slot(opts.emit_dirs, "lg"); lg_out != nullptr) {
      throw Lhd_error{"usage",
                      std::format("pass opentimer does not emit an lg: library; --emit-dir lg:{} is unused", lg_out->path),
                      "opentimer reports timing (see --workdir/timing.json), it does not transform the graph"};
    }
    std::vector<std::string> tfiles(opts.files.begin() + 1, opts.files.end());
    if (tfiles.empty()) {
      throw Lhd_error{"usage",
                      "pass opentimer needs a Liberty .lib file argument (+ optional .sdc/.spef)",
                      "e.g. `lhd pass opentimer --top 'mod__c0' lg:net cells.lib`"};
    }
    check_inputs_exist(tfiles);
    Eprp_var var;
    load_lg_into_var(lg_in, var);
    if (var.graphs.empty()) {
      throw Lhd_error{"config", std::format("lg: input {} holds no graphs", lg_in), ""};
    }
    std::string joined;
    for (const auto& f : tfiles) {
      if (!joined.empty()) {
        joined += ",";
      }
      joined += f;
      res.inputs.push_back(f);
    }
    Eprp_var::Eprp_dict labels{
        {"files", joined}
    };
    set_top_label(opts, var, labels, "pass.opentimer");
    // Timing sidecar: always defaulted — workdir() mints the same ephemeral
    // scratch dir run_step's logs land in when --workdir is absent — so the
    // pretty/jsonl report always has timing data to render. merge_sets runs
    // after, so an explicit `--set pass.opentimer.qor=FILE` still overrides.
    const std::string ephemeral_qor = opts.workdir.empty() ? (fs::path(workdir(opts)) / "timing.json").string() : std::string{};
    labels["qor"] = ephemeral_qor.empty() ? (fs::path(opts.workdir) / "timing.json").string() : ephemeral_qor;
    merge_sets(opts, "pass.opentimer", labels);
    run_step("pass.opentimer", var, labels, opts, res);
    embed_qor_sidecar(labels, res);
    if (!ephemeral_qor.empty() && labels["qor"] == ephemeral_qor) {
      // The scratch-dir sidecar only exists to feed the report renderer: keep
      // the embedded qor JSON but do not advertise the ephemeral path as an
      // output (`outputs` lists only user-visible artifacts).
      std::erase(res.outputs, ephemeral_qor);
    }
  } else if (sub == "formal") {
    if (const auto* lg_out = find_slot(opts.emit_dirs, "lg"); lg_out != nullptr) {
      throw Lhd_error{"usage",
                      std::format("pass formal does not emit an lg: library; --emit-dir lg:{} is unused", lg_out->path),
                      "formal produces a verdict (and marks proven/runtime_check in place), not a graph library"};
    }
    Eprp_var var;
    load_lg_into_var(lg_in, var);
    if (var.graphs.empty()) {
      throw Lhd_error{"config", std::format("lg: input {} holds no graphs", lg_in), ""};
    }
    Eprp_var::Eprp_dict labels;
    set_top_label(opts, var, labels, "pass.formal");
    merge_sets(opts, "pass.formal", labels);
    run_step("pass.formal", var, labels, opts, res);  // marks proven/runtime_check in place; errors on a real violation
  } else {
    throw Lhd_error{"usage",
                    std::format("unknown pass subcommand '{}'", sub),
                    "use: color <alg> | partition | abc | opentimer | formal | liberty gensim | semdiff"};
  }
}

}  // namespace lhd

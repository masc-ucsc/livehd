// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// A/B of the transistor count pass.usyn assigns to a domino gate (its exact
// minimum SOP, then `factor_literals`) against pass/domino's minimum-transistor
// factoring (Sp_factorer), on the LUTs of real covers.
//
//   usyn_ab lg:DIR top cells.lib [support=6] [literals=16] [series=4]
//
// Runs the pass.abc region pipeline with a region hook that, like pass.usyn,
// STRASHes each region and covers it with lut_cover, then costs every chosen
// LUT both ways and leaves the region to the ABC flow unchanged. Nothing in
// pass/usyn is modified; the numbers say whether swapping the factorer would
// change which functions are admitted as domino gates.
//
// Upstream reads every input in both polarities for free, so a binate
// function (an XOR, a MUX) is a domino gate too: its network is over the
// distinct LITERALS of the minimum SOP. Such a function is costed here as a
// positive unate function of its literals (at most 8 distinct literals; wider
// ones are counted and skipped).

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <print>
#include <string>
#include <vector>

#include "abc_map.hpp"  // livehd::abc::Map_options
#include "domino_gate.hpp"
#include "eprp_var.hpp"
#include "graph_library_singleton.hpp"
#include "lnet.hpp"
#include "lut_cover.hpp"
#include "pass.hpp"
#include "pass_abc.hpp"
#include "region_backend.hpp"
#include "unate.hpp"

// inou/yosys/inou_yosys_api.cpp: registers the yosys reader methods (declared
// for lhd in lhd_kernel_internal.hpp).
void setup_inou_yosys();

namespace {

namespace fs = std::filesystem;

struct Stats {
  uint64_t                 regions = 0, regions_refused = 0;
  uint64_t                 luts = 0, small = 0, theirs_infeasible = 0, costed = 0;
  uint64_t                 unate = 0, via_literals = 0, wide_literals = 0;
  uint64_t                 mine_less = 0, mine_equal = 0, mine_more = 0, mine_unknown = 0;
  uint64_t                 theirs_sum = 0, mine_sum = 0;  // where both counts are known
  uint64_t                 theirs_domino = 0;             // their classification under the recipe (fn.domino)
  uint64_t                 flip_gain16 = 0, flip_loss16 = 0, flip_gain12 = 0;
  std::array<uint64_t, 4>  theirs_bins{}, mine_bins{};  // <=8, 9..16, 17..24, >24
  std::array<uint64_t, 9>  by_inputs{};
  std::array<uint64_t, 9>  less_by_inputs{};
  double                   mine_ms = 0;
  std::vector<std::string> examples;
};

int bin_of(uint32_t t) { return t <= 8 ? 0 : t <= 16 ? 1 : t <= 24 ? 2 : 3; }

// The minimum SOP's cubes as a positive unate function of its distinct
// literals (literal 2j = x_j, 2j+1 = !x_j). Returns the literal count, or
// > kMaxVars when the table would be too wide.
int literal_function(const livehd::usyn::Form& form, uint32_t n, livehd::domino::Truth& out) {
  std::array<int, 16> index{};
  index.fill(-1);
  int m = 0;
  for (const auto& c : form.cubes) {
    for (uint32_t j = 0; j < n; ++j) {
      if (((c.care >> j) & 1u) == 0) {
        continue;
      }
      const size_t k = j * 2 + (((c.ones >> j) & 1u) != 0 ? 1 : 0);
      if (index[k] < 0) {
        if (m >= livehd::domino::kMaxVars) {
          return livehd::domino::kMaxVars + 1;
        }
        index[k] = m++;
      }
    }
  }
  for (uint32_t y = 0; y < (1u << m); ++y) {
    bool on = false;
    for (const auto& c : form.cubes) {
      bool all = true;
      for (uint32_t j = 0; j < n && all; ++j) {
        if (((c.care >> j) & 1u) == 0) {
          continue;
        }
        const size_t k = j * 2 + (((c.ones >> j) & 1u) != 0 ? 1 : 0);
        all            = ((y >> index[k]) & 1u) != 0;
      }
      if (all) {
        on = true;
        break;
      }
    }
    out.set(static_cast<int>(y), on);
  }
  return m;
}

void compare(const livehd::usyn::Cover_lut& lut, uint32_t series, livehd::domino::Sp_factorer& F, Stats& st) {
  using livehd::domino::Rail;
  const uint32_t n = lut.table.inputs;
  ++st.luts;
  if (n < 2) {
    ++st.small;
    return;
  }
  ++st.by_inputs[n];
  // Theirs: the cheaper polarity, literals unbounded, the same series limit.
  livehd::usyn::Budget budget;
  budget.remaining          = uint64_t{1} << 40;
  uint32_t           theirs = UINT32_MAX;
  livehd::usyn::Form best_form;
  const auto         comp = lut.table.complement();
  for (int sign = 0; sign < 2; ++sign) {
    auto form = livehd::usyn::exact_form(sign ? comp : lut.table, 4096, series, true, budget);
    if (form.status == livehd::usyn::Status::feasible && form.factored < theirs) {
      theirs    = form.factored;
      best_form = form;
    }
  }
  if (theirs == UINT32_MAX) {
    ++st.theirs_infeasible;  // no polarity has an SOP within the series limit
    return;
  }
  ++st.costed;
  if (lut.fn.domino) {
    ++st.theirs_domino;
  }
  // Mine: the same question, bounded by theirs + 8 so a worse answer is
  // measured rather than searched for.
  livehd::domino::Truth t;
  for (uint32_t x = 0; x < (1u << n); ++x) {
    t.set(static_cast<int>(x), lut.table.get(x));
  }
  const int                   probe = static_cast<int>(theirs) + 8;
  livehd::domino::Gate_limits lim{.k = 8, .stack = static_cast<int>(series), .budget = probe};
  const auto                  t0     = std::chrono::steady_clock::now();
  int                         mine   = livehd::domino::kInfCost;
  bool                        binate = true;
  std::string                 formula;
  for (Rail rail : {Rail::pos, Rail::neg}) {
    auto g = F.fit(t, static_cast<int>(n), lim, rail);
    if (g.reason != livehd::domino::Fit_reason::binate) {
      binate = false;
    }
    if (g.ok() && g.formula->transistors < mine) {
      mine    = g.formula->transistors;
      formula = g.formula->to_string() + (rail == Rail::neg ? " (complement)" : "");
    }
  }
  if (!binate) {
    ++st.unate;
  } else {
    livehd::domino::Truth F_lit;
    const int             m = literal_function(best_form, n, F_lit);
    if (m > livehd::domino::kMaxVars) {
      ++st.wide_literals;
      st.mine_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      return;
    }
    ++st.via_literals;
    if (auto r = F.factor(F_lit, m, static_cast<int>(series), probe)) {
      mine    = r->transistors;
      formula = r->to_string() + " (over literals)";
    }
  }
  st.mine_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (mine >= livehd::domino::kInfCost) {
    ++st.mine_unknown;  // more than theirs + 8
    if (st.examples.size() < 16) {
      st.examples.push_back(std::format("UNKNOWN {} inputs: theirs {} (SOP {} cubes, {} literals) mine none within {}",
                                        n,
                                        theirs,
                                        best_form.cubes.size(),
                                        best_form.literals,
                                        probe));
    }
    return;
  }
  ++st.theirs_bins[static_cast<size_t>(bin_of(theirs))];
  ++st.mine_bins[static_cast<size_t>(bin_of(static_cast<uint32_t>(mine)))];
  st.theirs_sum += theirs;
  st.mine_sum   += static_cast<uint64_t>(mine);
  if (static_cast<uint32_t>(mine) < theirs) {
    ++st.mine_less;
    ++st.less_by_inputs[n];
    if (st.examples.size() < 16) {
      st.examples.push_back(std::format("{} inputs: theirs {} mine {}  {}", n, theirs, mine, formula));
    }
  } else if (static_cast<uint32_t>(mine) == theirs) {
    ++st.mine_equal;
  } else {
    ++st.mine_more;
    if (st.examples.size() < 16) {
      st.examples.push_back(std::format("WORSE {} inputs: theirs {} mine {}  {}", n, theirs, mine, formula));
    }
  }
  if (theirs > 16 && mine <= 16) {
    ++st.flip_gain16;
  }
  if (theirs <= 16 && mine > 16) {
    ++st.flip_loss16;
  }
  if (theirs > 12 && mine <= 12) {
    ++st.flip_gain12;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::println(stderr, "usage: usyn_ab lg:DIR top cells.lib [support=6] [literals=16] [series=4]");
    return 2;
  }
  std::string lgdir = argv[1];
  if (lgdir.starts_with("lg:")) {
    lgdir = lgdir.substr(3);
  }
  const std::string            top     = argv[2];
  const std::string            liberty = argv[3];
  livehd::usyn::Search_options search;
  search.recipe.support  = argc > 4 ? static_cast<uint32_t>(std::atoi(argv[4])) : 6;
  search.recipe.literals = argc > 5 ? static_cast<uint32_t>(std::atoi(argv[5])) : 16;
  search.recipe.series   = argc > 6 ? static_cast<uint32_t>(std::atoi(argv[6])) : 4;
  // pass.usyn's CLI defaults (they differ from the struct's).
  search.duplicate       = false;
  search.depth_slack     = 0;
  search.cover_memories  = false;

  // What lhd's init_engine does: run every pass/inou plugin's setup so the
  // methods the region pipeline calls by name (memory RTL through cgen and
  // the yosys reader) are registered in this process.
  for (const auto& it : Pass_plugin::get_registry()) {
    it.second();
  }
  setup_inou_yosys();

  auto&    lib = livehd::Hhds_graph_library::instance(lgdir);
  Eprp_var var;
  for (const hhds::Gid id : lib.all_gids()) {
    if (auto g = lib.get_graph(id)) {
      var.graphs.push_back(g);
    }
  }
  if (var.graphs.empty()) {
    std::println(stderr, "no graphs in {}", lgdir);
    return 1;
  }
  auto pattern = (fs::temp_directory_path() / "livehd-usyn-ab-XXXXXX").string();
  if (!mkdtemp(pattern.data())) {
    std::println(stderr, "cannot create a scratch directory");
    return 1;
  }
  const std::string                           out = pattern + "/out";
  livehd::Hhds_graph_library::Scoped_instance published_scope(out);
  var.dict["library"] = liberty;
  var.dict["top"]     = top;
  var.dict["out"]     = out;
  var.dict["qor"]     = pattern + "/qor.json";
  var.set_stage_labels(var.dict);

  Stats                       st;
  livehd::domino::Sp_factorer F;
  Pass_abc::work_with(var, [&](livehd::abc::Map_options& opts) {
    opts.threads            = 1;
    opts.region_hook_recipe = "domino-ab";
    opts.region_hook        = [&](const livehd::synth::Lnet& net, const livehd::synth::Region_ctx&) {
      livehd::synth::Region_rewrite none;
      ++st.regions;
      livehd::synth::Strash_options so;
      so.xor_nodes = search.recipe.support >= 2 && search.recipe.literals >= 4 && search.recipe.series >= 2;
      auto source  = livehd::synth::strash(net, so);
      if (!source) {
        ++st.regions_refused;
        return none;
      }
      auto cover = livehd::usyn::lut_cover(*source, search);
      if (cover.status != livehd::usyn::Status::feasible) {
        ++st.regions_refused;
        return none;
      }
      for (const auto& lut : cover.luts) {
        compare(lut, search.recipe.series, F, st);
      }
      return none;  // the ABC flow maps the original region
    };
  });

  const uint64_t known = st.mine_less + st.mine_equal + st.mine_more;
  std::println("usyn_ab top={} support={} literals={} series={}",
               top,
               search.recipe.support,
               search.recipe.literals,
               search.recipe.series);
  std::println(
      "regions {} (refused {}), cover LUTs {} (1-input/const {}), theirs infeasible {}, costed {}: unate {}, binate over "
      "literals {}, skipped >8 literals {}",
      st.regions,
      st.regions_refused,
      st.luts,
      st.small,
      st.theirs_infeasible,
      st.costed,
      st.unate,
      st.via_literals,
      st.wide_literals);
  std::println("their domino gates under the recipe: {}", st.theirs_domino);
  std::println("transistors, LUTs with both counts known ({}): theirs {} mine {}", known, st.theirs_sum, st.mine_sum);
  std::println("  mine < theirs: {}   equal: {}   mine > theirs: {}   mine > theirs+8 (unknown): {}",
               st.mine_less,
               st.mine_equal,
               st.mine_more,
               st.mine_unknown);
  std::println("  bins <=8 / 9-16 / 17-24 / >24   theirs {} {} {} {}   mine {} {} {} {}",
               st.theirs_bins[0],
               st.theirs_bins[1],
               st.theirs_bins[2],
               st.theirs_bins[3],
               st.mine_bins[0],
               st.mine_bins[1],
               st.mine_bins[2],
               st.mine_bins[3]);
  std::println("  admission flips at 16: gained {} lost {}; at 12: gained {}", st.flip_gain16, st.flip_loss16, st.flip_gain12);
  std::print("  LUTs by inputs 2..8:");
  for (int n = 2; n <= 8; ++n) {
    std::print(" {}:{}(-{})", n, st.by_inputs[static_cast<size_t>(n)], st.less_by_inputs[static_cast<size_t>(n)]);
  }
  std::println("");
  std::println("  factorer time {:.1f} ms, memo {}", st.mine_ms, F.memo_size());
  for (const auto& e : st.examples) {
    std::println("  e.g. {}", e);
  }
  std::error_code ec;
  fs::remove_all(pattern, ec);
  return 0;
}

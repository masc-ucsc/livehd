//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_runner_lgraph.hpp"

#include <format>
#include <functional>
#include <optional>
#include <print>
#include <string>
#include <unordered_map>
#include <vector>

#include "lgraph.hpp"
#include "lgedgeiter.hpp"
#include "upass_runner_common.hpp"
#include "upass_shared.hpp"

namespace {

struct Lgraph_pass_visit final : public upass::uPass_lgraph {
  using upass::uPass_lgraph::uPass_lgraph;

  void run_once() override {
    if (!gm || !gm->ref_lgraph()) {
      std::print("uPass(lgraph) - no graph available\n");
      return;
    }

    std::size_t visited = 0;
    for (const auto &node : gm->ref_lgraph()->fast()) {
      std::print("uPass(lgraph) - visit {}\n", node.get_type_name());
      ++visited;
    }
    std::print("uPass(lgraph) - visited {} nodes in {}\n", visited, gm->ref_lgraph()->get_name());
  }
};

struct Lgraph_pass_fold_scan final : public upass::uPass_lgraph {
  using upass::uPass_lgraph::uPass_lgraph;

  void run_once() override {
    if (!gm || !gm->ref_lgraph()) {
      std::print("uPass(lgraph) - no graph available\n");
      return;
    }
    const auto s = gm->scan_fold_candidates();
    std::print("uPass(lgraph) - scan visited:{} const:{} arithmetic:{} fold_candidates:{}\n",
               s.visited_nodes,
               s.const_nodes,
               s.arithmetic_nodes,
               s.fold_candidate_nodes);
  }
};

struct Lgraph_pass_fold_tag final : public upass::uPass_lgraph {
  using upass::uPass_lgraph::uPass_lgraph;

  void run_once() override {
    if (!gm || !gm->ref_lgraph()) {
      std::print("uPass(lgraph) - no graph available\n");
      return;
    }
    const auto s = gm->tag_fold_candidates();
    std::print("uPass(lgraph) - tag fold_candidates:{}\n", s.tagged_nodes);
    if (s.tagged_nodes > 0) {
      mark_changed();
    }
  }
};

struct Lgraph_pass_fold_sum_const final : public upass::uPass_lgraph {
  using upass::uPass_lgraph::uPass_lgraph;
  explicit Lgraph_pass_fold_sum_const(std::shared_ptr<upass::Lgraph_manager> &gm, bool _dry_run = false)
      : upass::uPass_lgraph(gm), dry_run(_dry_run) {}

  void run_once() override {
    if (!gm || !gm->ref_lgraph()) {
      std::print("uPass(lgraph) - no graph available\n");
      return;
    }
    auto &      adapter = static_cast<upass::IR_adapter &>(*gm);
    const auto  s       = upass::run_fold_sum_const_shared(adapter, "uPass(lgraph)", dry_run);
    std::print("uPass(lgraph) - sum_const_folded:{} rewired:{} new_consts:{} deleted:{} dry_run:{}\n",
               s.folded_nodes,
               s.rewired_edges,
               s.new_const_nodes,
               s.deleted_nodes,
               dry_run ? "true" : "false");
    if (!dry_run && s.folded_nodes > 0) {
      mark_changed();
    }
  }

private:
  bool dry_run;
};

struct Lgraph_pass_fold_neutral final : public upass::uPass_lgraph {
  using upass::uPass_lgraph::uPass_lgraph;
  explicit Lgraph_pass_fold_neutral(std::shared_ptr<upass::Lgraph_manager> &gm, bool _dry_run = false)
      : upass::uPass_lgraph(gm), dry_run(_dry_run) {}

  void run_once() override {
    if (!gm || !gm->ref_lgraph()) {
      std::print("uPass(lgraph) - no graph available\n");
      return;
    }
    const auto s = gm->fold_neutral_const(false, dry_run);
    const auto total = s.simplified_to_driver + s.simplified_to_const_zero + s.simplified_to_const_one;
    std::print("uPass(lgraph) - neutral_simplified:{} to_driver:{} to_const0:{} to_const1:{} rewired:{} new_consts:{} deleted:{} dry_run:{}\n",
               total,
               s.simplified_to_driver,
               s.simplified_to_const_zero,
               s.simplified_to_const_one,
               s.rewired_edges,
               s.new_const_nodes,
               s.deleted_nodes,
               dry_run ? "true" : "false");
    if (!dry_run && total > 0) {
      mark_changed();
    }
  }

private:
  bool dry_run;
};

struct Lgraph_pass_fold_shift_div final : public upass::uPass_lgraph {
  using upass::uPass_lgraph::uPass_lgraph;
  explicit Lgraph_pass_fold_shift_div(std::shared_ptr<upass::Lgraph_manager> &gm, bool _dry_run = false)
      : upass::uPass_lgraph(gm), dry_run(_dry_run) {}

  void run_once() override {
    if (!gm || !gm->ref_lgraph()) {
      std::print("uPass(lgraph) - no graph available\n");
      return;
    }
    const auto s = gm->fold_shift_div_const(false, dry_run);
    const auto total = s.simplified_to_driver + s.simplified_to_const_zero + s.simplified_to_const_one
                       + s.simplified_to_const_other;
    std::print("uPass(lgraph) - shiftdiv_simplified:{} to_driver:{} to_const0:{} to_const1:{} to_const_other:{} rewired:{} new_consts:{} deleted:{} dry_run:{}\n",
               total,
               s.simplified_to_driver,
               s.simplified_to_const_zero,
               s.simplified_to_const_one,
               s.simplified_to_const_other,
               s.rewired_edges,
               s.new_const_nodes,
               s.deleted_nodes,
               dry_run ? "true" : "false");
    if (!dry_run && total > 0) {
      mark_changed();
    }
  }

private:
  bool dry_run;
};

struct Lgraph_pass_fold_sub_const final : public upass::uPass_lgraph {
  using upass::uPass_lgraph::uPass_lgraph;
  explicit Lgraph_pass_fold_sub_const(std::shared_ptr<upass::Lgraph_manager> &gm, bool _dry_run = false)
      : upass::uPass_lgraph(gm), dry_run(_dry_run) {}

  void run_once() override {
    if (!gm || !gm->ref_lgraph()) {
      std::print("uPass(lgraph) - no graph available\n");
      return;
    }
    const auto s     = gm->fold_sub_const(false, dry_run);
    const auto total = s.sub_zero_simplified + s.sub_self_simplified + s.const_sub_folded;
    std::print(
        "uPass(lgraph) - sub_folded:{} sub_zero:{} sub_self:{} const_sub:{} rewired:{} new_consts:{} deleted:{} dry_run:{}\n",
        total,
        s.sub_zero_simplified,
        s.sub_self_simplified,
        s.const_sub_folded,
        s.rewired_edges,
        s.new_const_nodes,
        s.deleted_nodes,
        dry_run ? "true" : "false");
    if (!dry_run && total > 0) {
      mark_changed();
    }
  }

private:
  bool dry_run;
};

struct Lgraph_pass_fold_mult_const final : public upass::uPass_lgraph {
  using upass::uPass_lgraph::uPass_lgraph;
  explicit Lgraph_pass_fold_mult_const(std::shared_ptr<upass::Lgraph_manager> &gm, bool _dry_run = false)
      : upass::uPass_lgraph(gm), dry_run(_dry_run) {}

  void run_once() override {
    if (!gm || !gm->ref_lgraph()) {
      std::print("uPass(lgraph) - no graph available\n");
      return;
    }
    const auto s = gm->fold_mult_const(false, dry_run);
    std::print(
        "uPass(lgraph) - mult_folded:{} rewired:{} new_consts:{} deleted:{} dry_run:{}\n",
        s.const_mult_folded,
        s.rewired_edges,
        s.new_const_nodes,
        s.deleted_nodes,
        dry_run ? "true" : "false");
    if (!dry_run && s.const_mult_folded > 0) {
      mark_changed();
    }
  }

private:
  bool dry_run;
};

struct Lgraph_pass_noop_shared final : public upass::uPass_lgraph {
  using upass::uPass_lgraph::uPass_lgraph;

  void run_once() override {
    if (!gm) {
      std::print("uPass(lgraph) - no graph available\n");
      return;
    }
    upass::run_noop_shared(*gm, "uPass(lgraph)");
  }
};

struct Lgraph_pass_scan_shared final : public upass::uPass_lgraph {
  using upass::uPass_lgraph::uPass_lgraph;

  void run_once() override {
    if (!gm) {
      std::print("uPass(lgraph) - no graph available\n");
      return;
    }
    const auto rep = upass::run_scan_shared(*gm, "uPass(lgraph)");
    std::print("uPass(lgraph) - shared scan summary nodes:{} const:{} arith:{}\n",
               rep.node_count,
               rep.const_count,
               rep.arithmetic_count);
  }
};

struct Lgraph_pass_decide_shared final : public upass::uPass_lgraph {
  using upass::uPass_lgraph::uPass_lgraph;

  void run_once() override {
    if (!gm) {
      std::print("uPass(lgraph) - no graph available\n");
      return;
    }
    const auto rep = upass::run_decide_shared(*gm, "uPass(lgraph)");
    std::print("uPass(lgraph) - shared decide summary fold_candidates:{} has_fold_candidates:{}\n",
               rep.fold_candidate_count,
               rep.has_fold_candidates ? "true" : "false");
  }
};

static upass::uPass_lgraph_plugin plugin_lgraph_visit("visit", upass::uPass_lgraph_wrapper<Lgraph_pass_visit>::get_upass);
static upass::uPass_lgraph_plugin plugin_lgraph_fold_scan(
    "fold_scan",
    upass::uPass_lgraph_wrapper<Lgraph_pass_fold_scan>::get_upass,
    {"visit"});
static upass::uPass_lgraph_plugin plugin_lgraph_fold_tag(
    "fold_tag",
    upass::uPass_lgraph_wrapper<Lgraph_pass_fold_tag>::get_upass,
    {"fold_scan"});
static upass::uPass_lgraph_plugin plugin_lgraph_fold_sum_const(
    "fold_sum_const",
    upass::uPass_lgraph_wrapper<Lgraph_pass_fold_sum_const>::get_upass,
    {"fold_scan"});
static upass::uPass_lgraph_plugin plugin_lgraph_fold_neutral(
    "fold_neutral",
    upass::uPass_lgraph_wrapper<Lgraph_pass_fold_neutral>::get_upass,
    {"fold_scan"});
static upass::uPass_lgraph_plugin plugin_lgraph_fold_shift_div(
    "fold_shift_div",
    upass::uPass_lgraph_wrapper<Lgraph_pass_fold_shift_div>::get_upass,
    {"fold_scan"});
static upass::uPass_lgraph_plugin plugin_lgraph_fold_sub_const(
    "fold_sub_const",
    upass::uPass_lgraph_wrapper<Lgraph_pass_fold_sub_const>::get_upass,
    {"fold_scan"});
static upass::uPass_lgraph_plugin plugin_lgraph_fold_mult_const(
    "fold_mult_const",
    upass::uPass_lgraph_wrapper<Lgraph_pass_fold_mult_const>::get_upass,
    {"fold_scan"});
static upass::uPass_lgraph_plugin plugin_lgraph_noop_shared(
    "noop_shared",
    upass::uPass_lgraph_wrapper<Lgraph_pass_noop_shared>::get_upass);
static upass::uPass_lgraph_plugin plugin_lgraph_scan_shared(
    "scan_shared",
    upass::uPass_lgraph_wrapper<Lgraph_pass_scan_shared>::get_upass);
static upass::uPass_lgraph_plugin plugin_lgraph_decide_shared(
    "decide_shared",
    upass::uPass_lgraph_wrapper<Lgraph_pass_decide_shared>::get_upass);
}  // namespace

uPass_runner_lgraph::uPass_runner_lgraph(
    std::shared_ptr<upass::Lgraph_manager> _gm, const std::vector<std::string> &upass_names, bool _dry_run)
    : gm(std::move(_gm)), dry_run(_dry_run) {
  auto requested = upass_names;
  if (requested.empty()) {
    requested = {"visit", "fold_scan"};
  }

  std::string order_error;
  auto        resolved = resolve_order(requested, &order_error);
  if (!order_error.empty()) {
    configuration_error     = true;
    configuration_error_msg = order_error;
  }
  if (!resolved.empty()) {
    std::print("uPass(lgraph) - resolved order:");
    for (const auto &name : resolved) {
      std::print(" {}", name);
    }
    std::print("\n");
  }

  const auto &registry = upass::uPass_lgraph_plugin::get_registry();
  for (const auto &name : resolved) {
    const auto it = registry.find(name);
    if (it == registry.end()) {
      std::print("{} is not defined.\n", name);
      continue;
    }

    std::print("uPass(lgraph) - add {}\n", name);
    if (name == "fold_sum_const") {
      upasses.emplace_back(Pass_entry{.name = name, .pass = std::make_unique<Lgraph_pass_fold_sum_const>(gm, dry_run)});
      continue;
    }
    if (name == "fold_neutral") {
      upasses.emplace_back(Pass_entry{.name = name, .pass = std::make_unique<Lgraph_pass_fold_neutral>(gm, dry_run)});
      continue;
    }
    if (name == "fold_shift_div") {
      upasses.emplace_back(Pass_entry{.name = name, .pass = std::make_unique<Lgraph_pass_fold_shift_div>(gm, dry_run)});
      continue;
    }
    if (name == "fold_sub_const") {
      upasses.emplace_back(Pass_entry{.name = name, .pass = std::make_unique<Lgraph_pass_fold_sub_const>(gm, dry_run)});
      continue;
    }
    if (name == "fold_mult_const") {
      upasses.emplace_back(Pass_entry{.name = name, .pass = std::make_unique<Lgraph_pass_fold_mult_const>(gm, dry_run)});
      continue;
    }
    upasses.emplace_back(Pass_entry{.name = name, .pass = it->second.setup_fn(gm)});
  }
}

std::vector<std::string> uPass_runner_lgraph::resolve_order(const std::vector<std::string> &requested_names, std::string *error_msg) const {
  const auto &registry = upass::uPass_lgraph_plugin::get_registry();

  enum class Mark { kUnseen, kVisiting, kDone };
  std::unordered_map<std::string, Mark> marks;
  std::vector<std::string>              ordered;

  std::function<bool(const std::string &)> dfs = [&](const std::string &name) {
    const auto it = registry.find(name);
    if (it == registry.end()) {
      std::print("{} is not defined.\n", name);
      if (error_msg && error_msg->empty()) {
        *error_msg = std::format("unknown pass '{}'", name);
      }
      return false;
    }

    const auto mit = marks.find(name);
    if (mit != marks.end()) {
      if (mit->second == Mark::kVisiting) {
        upass::error("uPass(lgraph) dependency cycle detected at {}\n", name);
        if (error_msg && error_msg->empty()) {
          *error_msg = std::format("dependency cycle detected at '{}'", name);
        }
        return false;
      }
      return mit->second == Mark::kDone;
    }

    marks.emplace(name, Mark::kVisiting);
    for (const auto &dep : it->second.depends_on) {
      if (!dfs(dep)) {
        upass::error("uPass(lgraph) dependency chain for {} is invalid\n", name);
        if (error_msg && error_msg->empty()) {
          *error_msg = std::format("dependency chain for '{}' is invalid", name);
        }
        marks[name] = Mark::kDone;
        return false;
      }
    }
    marks[name] = Mark::kDone;
    ordered.emplace_back(name);
    return true;
  };

  for (const auto &name : requested_names) {
    dfs(name);
  }

  return ordered;
}

std::vector<std::string> uPass_runner_lgraph::changed_passes() const {
  std::vector<std::string> changed;
  for (const auto &entry : upasses) {
    if (entry.pass->has_changed()) {
      changed.emplace_back(entry.name);
    }
  }
  return changed;
}

void uPass_runner_lgraph::execute_passes() {
  // fold_tag, fold_sum_const, and fold_mult_const are gated by the general
  // "all-inputs-const" scan (they only fire when every input is a constant).
  auto is_general_guarded_fold = [](const std::string &name) {
    return name == "fold_tag" || name == "fold_sum_const" || name == "fold_mult_const";
  };

  std::optional<upass::Shared_decision_report> decide_cache;
  auto get_decision = [&]() -> upass::Shared_decision_report {
    if (!decide_cache.has_value()) {
      if (!gm) {
        decide_cache = upass::Shared_decision_report{};
      } else {
        decide_cache = upass::compute_decide_shared(*gm);
      }
    }
    return *decide_cache;
  };

  for (auto &entry : upasses) {
    if (entry.name == "decide_shared") {
      (void)get_decision();
      entry.pass->run_once();
      continue;
    }

    if (is_general_guarded_fold(entry.name)) {
      const auto rep = get_decision();
      if (!rep.has_fold_candidates) {
        std::print("uPass(lgraph) - skip {} (no fold candidates)\n", entry.name);
        continue;
      }
    }

    // fold_sub_const uses its own sub-pattern guard: it can fold a-0 and a-a
    // which have non-const inputs and would be missed by the general candidate count.
    if (entry.name == "fold_sub_const") {
      if (!gm || gm->count_sub_candidates() == 0) {
        std::print("uPass(lgraph) - skip fold_sub_const (no fold candidates)\n");
        continue;
      }
    }

    entry.pass->run_once();
  }
}

void uPass_runner_lgraph::run(std::size_t max_iters) {
  if (configuration_error) {
    std::print("uPass(lgraph) - invalid configuration: {}\n", configuration_error_msg);
    return;
  }

  if (upasses.empty()) {
    std::print("uPass(lgraph) - no passes configured\n");
    return;
  }

  if (!gm || !gm->ref_lgraph()) {
    std::print("uPass(lgraph) - no graph available\n");
    return;
  }

  upass::Runner_fixed_point::run(
      "uPass(lgraph)",
      max_iters,
      [this]() {
        for (auto &entry : upasses) {
          entry.pass->begin_iteration();
        }
      },
      [this]() {
        execute_passes();
        last_visited_count = collect_type_names().size();
        last_scan_summary = gm->scan_fold_candidates();
      },
      [this]() { return changed_passes(); });
}

std::size_t uPass_runner_lgraph::visit_fast(bool visit_sub) const {
  if (!gm || !gm->ref_lgraph()) {
    std::print("uPass(lgraph) - no graph available\n");
    return 0;
  }

  std::size_t visited = 0;
  for (const auto &node : gm->ref_lgraph()->fast(visit_sub)) {
    std::print("uPass(lgraph) - visit {}\n", node.get_type_name());
    ++visited;
  }

  return visited;
}

std::vector<std::string> uPass_runner_lgraph::collect_type_names(bool visit_sub) const {
  std::vector<std::string> type_names;
  if (!gm || !gm->ref_lgraph()) {
    return type_names;
  }

  for (const auto &node : gm->ref_lgraph()->fast(visit_sub)) {
    type_names.emplace_back(node.get_type_name());
  }

  return type_names;
}

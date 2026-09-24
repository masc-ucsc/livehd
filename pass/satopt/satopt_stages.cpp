// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_stages.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <type_traits>

#include "node_util.hpp"
#include "rapidjson/document.h"
#include "satopt.hpp"
#include "satopt_memory.hpp"
#include "satopt_mux.hpp"
#include "satopt_sweep.hpp"

namespace livehd::satopt {
namespace {
namespace gu = livehd::graph_util;

int64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double ms_since(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// One O(nodes) scan tells which stages have anything to look at.
struct Applicability {
  bool selects = false;  // a Mux/Hotmux control or a one-bit Flop enable
  bool muxes   = false;
  bool memory  = false;
  bool values  = false;  // an operation output the value sweep may replace
};
Applicability scan(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  Applicability a;
  for (const auto& g : graphs) {
    if (!g) {
      continue;
    }
    for (const auto n : g->body().nodes()) {
      const auto op = gu::type_op_of(n);
      if (op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
        a.selects = a.muxes = true;
      } else if (op == Ntype_op::Flop) {
        const auto en = gu::get_driver_of_sink_name(n, "enable");
        a.selects |= !en.is_invalid() && gu::bits_of(en) == 1;
      } else if (op == Ntype_op::Memory) {
        a.memory = true;
      }
      a.values |= sweep_target(op);
    }
  }
  return a;
}
}  // namespace

std::string_view stage_name(Stage s) {
  switch (s) {
    case Stage::constants: return "constants";
    case Stage::equiv: return "equiv";
    case Stage::complement: return "complement";
    case Stage::odc: return "odc";
    case Stage::hotmux: return "hotmux";
    case Stage::memory: return "memory";
    case Stage::resub: return "resub";
  }
  return "?";
}

std::string_view state_name(Stage_state s) {
  switch (s) {
    case Stage_state::disabled: return "disabled";
    case Stage_state::inapplicable: return "inapplicable";
    case Stage_state::exhausted: return "exhausted";
    case Stage_state::completed: return "completed";
  }
  return "?";
}

bool set_budget(Budget& budget, std::string_view key, std::string_view value, std::string* error) {
  uint64_t v = 0;
  const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), v);
  const auto fail      = [&](std::string_view why) {
    if (error) {
      *error = std::format("pass.satopt.{}: {} (got '{}')", key, why, value);
    }
    return false;
  };
  if (value.empty() || ec != std::errc{} || end != value.data() + value.size()) {
    return fail("expects a non-negative decimal integer");
  }
  const auto small = [&](int& out, uint64_t lo, uint64_t hi) {
    if (v < lo || v > hi) {
      return fail(std::format("expects {}..{}", lo, hi));
    }
    out = static_cast<int>(v);
    return true;
  };
  if (key == "work") {
    budget.work = v;
  } else if (key == "queries") {
    budget.queries = v;
  } else if (key == "time_ms") {
    budget.time_ms = v;
  } else if (key == "budget_k") {
    return small(budget.budget_k, 1, 1'000'000);
  } else if (key == "cone_max") {
    return small(budget.cone_max, 1, 100'000'000);
  } else if (key == "samples") {
    if (v < 1 || v > 4096) {
      return fail("expects 1..4096");
    }
    budget.samples = static_cast<uint32_t>(v);
  } else {
    if (error) {
      *error = std::format("unknown budget knob '{}'", key);
    }
    return false;
  }
  return true;
}

std::optional<Budget> parse_budget(std::string_view text, std::string* error) {
  Budget budget;
  while (!text.empty()) {
    const auto comma = text.find(',');
    const auto item  = text.substr(0, comma);
    text             = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    const auto eq    = item.find('=');
    if (eq == std::string_view::npos || !set_budget(budget, item.substr(0, eq), item.substr(eq + 1), error)) {
      if (error && eq == std::string_view::npos) {
        *error = std::format("budget item '{}' is not name=value", item);
      }
      return std::nullopt;
    }
  }
  return budget;
}

std::string Budget::proof_key() const { return std::format("k{}:c{}:s{}", budget_k, cone_max, samples); }

Budget Budget::unlimited() {
  Budget b;
  b.work    = UINT64_MAX;
  b.queries = UINT64_MAX;
  b.time_ms = 0;
  return b;
}

Meter::Meter(const Budget& budget) : budget_(budget), start_ns_(now_ns()) {
  stage_work_cap_  = budget_.work;
  stage_query_cap_ = budget_.queries;
}

void Meter::begin_stage(int waiting) {
  const auto share = [&](uint64_t total, uint64_t used) {
    const uint64_t left = total > used ? total - used : 0;
    return waiting > 0 ? left / 2 : left;
  };
  stage_work_      = 0;
  stage_queries_   = 0;
  stage_work_cap_  = share(budget_.work, work_);
  stage_query_cap_ = share(budget_.queries, queries_);
  exhausted_       = timed_out_;
}

bool Meter::over(bool clock) {
  if (stage_work_ > stage_work_cap_ || stage_queries_ > stage_query_cap_) {
    exhausted_ = true;
  }
  // The wall-clock backstop is read on every query and every 256th work
  // charge; once hit, it holds for the rest of the run.
  if (!exhausted_ && budget_.time_ms != 0 && (clock || (++charges_ & 255) == 0)
      && static_cast<uint64_t>(now_ns() - start_ns_) / 1'000'000 >= budget_.time_ms) {
    exhausted_ = timed_out_ = true;
  }
  return exhausted_;
}

bool Meter::work(uint64_t units) {
  // Work already done always counts, so the report is the true cost.
  work_       += units;
  stage_work_ += units;
  return !over(false);
}

bool Meter::replay(uint64_t work, uint64_t queries) {
  if (over(true) || work > stage_work_cap_ - stage_work_ || queries > stage_query_cap_ - stage_queries_) {
    return false;
  }
  work_          += work;
  stage_work_    += work;
  queries_       += queries;
  stage_queries_ += queries;
  return true;
}

bool Meter::query() {
  if (over(true)) {
    return false;
  }
  if (stage_queries_ >= stage_query_cap_) {
    exhausted_ = true;
    return false;
  }
  ++queries_;
  ++stage_queries_;
  return true;
}

std::string Stage_set::text() const {
  std::string out;
  for (auto s : kStageOrder) {
    if (has(s)) {
      out += out.empty() ? "" : ",";
      out += stage_name(s);
    }
  }
  return out.empty() ? std::string{"none"} : out;
}

Stage_set default_stages(Profile p) {
  // The stages whose safety audit (B) is done. Synthesis also simplifies
  // memory ports; experimental searches are never on by default.
  (void)p;
  return {Stage::constants, Stage::hotmux, Stage::memory};
}

std::optional<Stage_set> parse_stages(std::string_view text, Profile profile, std::string* error) {
  const auto trim = [](std::string_view s) {
    while (!s.empty() && s.front() == ' ') {
      s.remove_prefix(1);
    }
    while (!s.empty() && s.back() == ' ') {
      s.remove_suffix(1);
    }
    return s;
  };
  text = trim(text);
  if (text == "none") {
    return Stage_set{};
  }
  if (text.empty() || text == "default") {
    return default_stages(profile);
  }
  Stage_set set;
  while (!text.empty()) {
    const auto comma = text.find(',');
    const auto name  = trim(text.substr(0, comma));
    text             = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    bool found       = false;
    for (auto s : kStageOrder) {
      if (name == stage_name(s)) {
        if (set.has(s)) {
          if (error) {
            *error = std::format("stage '{}' is listed twice", name);
          }
          return std::nullopt;
        }
        set.add(s);
        found = true;
      }
    }
    if (!found) {
      if (error) {
        *error = std::format("unknown stage '{}' (expected none, default, or a list of: {})",
                             name,
                             Stage_set{Stage::constants,
                                       Stage::equiv,
                                       Stage::complement,
                                       Stage::odc,
                                       Stage::hotmux,
                                       Stage::memory,
                                       Stage::resub}
                                 .text());
      }
      return std::nullopt;
    }
  }
  return set;
}

std::string Report::json() const {
  std::string out = std::format(R"({{"graphs":{},"changed_graphs":{},"ms":{:.1f},"stages":{{)", graphs, changed_graphs, ms);
  bool        first = true;
  for (auto s : kStageOrder) {
    const auto& r  = at(s);
    out           += std::format(
        R"({}"{}":{{"state":"{}","ms":{:.1f},"work":{},"candidates":{},"sim_rejects":{},"queries":{},"proven":{},"refuted":{},)"
        R"("unknown":{},"reused":{},"applied":{},"bits":{},"nodes_removed":{},"budget_skips":{}}})",
        first ? "" : ",",
        stage_name(s),
        state_name(r.state),
        r.ms,
        r.work,
        r.candidates,
        r.sim_rejects,
        r.queries,
        r.proven,
        r.refuted,
        r.unknown,
        r.reused,
        r.applied,
        r.bits,
        r.nodes_removed,
        r.budget_skips);
    first = false;
  }
  return out + "}}";
}

std::optional<Report> Report::parse(std::string_view text) {
  rapidjson::Document d;
  d.Parse(text.data(), text.size());
  if (d.HasParseError() || !d.IsObject() || !d.HasMember("stages") || !d["stages"].IsObject()) {
    return std::nullopt;
  }
  const auto num = [](const rapidjson::Value& v, const char* key, auto& out) {
    const auto it = v.FindMember(key);
    if (it == v.MemberEnd() || !it->value.IsNumber()) {
      return false;
    }
    out = static_cast<std::remove_reference_t<decltype(out)>>(it->value.GetDouble());
    return true;
  };
  Report r;
  if (!num(d, "graphs", r.graphs) || !num(d, "changed_graphs", r.changed_graphs) || !num(d, "ms", r.ms)) {
    return std::nullopt;
  }
  for (auto stage : kStageOrder) {
    const auto it = d["stages"].FindMember(std::string(stage_name(stage)).c_str());
    if (it == d["stages"].MemberEnd() || !it->value.IsObject() || !it->value.HasMember("state")
        || !it->value["state"].IsString()) {
      return std::nullopt;
    }
    auto&      st    = r.at(stage);
    const auto state = std::string_view{it->value["state"].GetString()};
    bool       known = false;
    for (auto s : {Stage_state::disabled, Stage_state::inapplicable, Stage_state::exhausted, Stage_state::completed}) {
      if (state == state_name(s)) {
        st.state = s;
        known    = true;
      }
    }
    const auto& v = it->value;
    if (!known || !num(v, "ms", st.ms) || !num(v, "work", st.work) || !num(v, "candidates", st.candidates)
        || !num(v, "sim_rejects", st.sim_rejects) || !num(v, "queries", st.queries) || !num(v, "proven", st.proven)
        || !num(v, "refuted", st.refuted) || !num(v, "unknown", st.unknown) || !num(v, "reused", st.reused)
        || !num(v, "applied", st.applied) || !num(v, "bits", st.bits) || !num(v, "nodes_removed", st.nodes_removed)
        || !num(v, "budget_skips", st.budget_skips)) {
      return std::nullopt;
    }
  }
  return r;
}

void Report::merge(const Report& other) {
  const auto rank = [](Stage_state s) {
    switch (s) {
      case Stage_state::disabled: return 0;
      case Stage_state::inapplicable: return 1;
      case Stage_state::completed: return 2;
      case Stage_state::exhausted: return 3;
    }
    return 0;
  };
  for (size_t i = 0; i < stages.size(); ++i) {
    auto&       a = stages[i];
    const auto& b = other.stages[i];
    if (rank(b.state) > rank(a.state)) {
      a.state = b.state;
    }
    a.ms            += b.ms;
    a.work          += b.work;
    a.candidates    += b.candidates;
    a.sim_rejects   += b.sim_rejects;
    a.queries       += b.queries;
    a.proven        += b.proven;
    a.refuted       += b.refuted;
    a.unknown       += b.unknown;
    a.reused        += b.reused;
    a.applied       += b.applied;
    a.bits          += b.bits;
    a.nodes_removed += b.nodes_removed;
    a.budget_skips  += b.budget_skips;
  }
  graphs         += other.graphs;
  changed_graphs += other.changed_graphs;
  ms             += other.ms;
}

Report run(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, const Options& opts, Meter* shared_meter) {
  const auto            start = std::chrono::steady_clock::now();
  Report                report;
  std::vector<uint64_t> epochs;
  for (const auto& g : graphs) {
    epochs.push_back(g ? g->body_epoch() : 0);
    report.graphs += g != nullptr;
  }
  if (opts.stages.empty()) {
    report.ms = ms_since(start);
    return report;
  }
  const auto  applicable = scan(graphs);
  const auto* prover     = opts.mux_prover ? opts.mux_prover : registered_mux_prover();
  const auto  runs       = [&](Stage s) {
    if (!opts.stages.has(s)) {
      return false;
    }
    switch (s) {
      case Stage::constants: return applicable.selects || applicable.values;
      case Stage::equiv:
      case Stage::complement:
      case Stage::odc:
      case Stage::resub: return applicable.values;
      case Stage::hotmux    : return applicable.muxes;
      case Stage::memory: return applicable.memory;
    }
    return false;
  };
  Meter  own(opts.budget);
  Meter& meter = shared_meter ? *shared_meter : own;
  for (size_t i = 0; i < kStageOrder.size(); ++i) {
    const auto stage = kStageOrder[i];
    auto&      r     = report.at(stage);
    if (!opts.stages.has(stage)) {
      r.state = Stage_state::disabled;
      continue;
    }
    if (!runs(stage)) {
      r.state = Stage_state::inapplicable;
      continue;
    }
    int waiting = opts.later_stages;
    for (size_t j = i + 1; j < kStageOrder.size(); ++j) {
      waiting += runs(kStageOrder[j]);
    }
    meter.begin_stage(waiting);
    const auto stage_start = std::chrono::steady_clock::now();
    bool       partial     = false;
    switch (stage) {
      case Stage::constants:
        // Selectors first: a tied select folds whole cones the value sweep
        // then need not simulate.
        if (applicable.selects) {
          optimize_selects(graphs, opts.cache_dir, opts.profile, &r, &meter);
        }
        if (applicable.values) {
          sweep_values(graphs, Sweep::constants, opts.profile, opts.cache_dir, r, meter);
        }
        break;
      case Stage::equiv: sweep_values(graphs, Sweep::equiv, opts.profile, opts.cache_dir, r, meter); break;
      case Stage::complement: sweep_values(graphs, Sweep::complement, opts.profile, opts.cache_dir, r, meter); break;
      case Stage::odc: sweep_values(graphs, Sweep::odc, opts.profile, opts.cache_dir, r, meter); break;
      case Stage::resub: sweep_values(graphs, Sweep::resub, opts.profile, opts.cache_dir, r, meter); break;
      case Stage::hotmux:
        for (const auto& g : graphs) {
          if (!g) {
            continue;
          }
          // Only arm-bit proofs need the optional bit-level prover. The
          // word-level exclusivity/collapse step also works without ABC.
          const auto stats
              = prover ? optimize_muxes(g.get(), *prover, opts.cache_dir, opts.all_regions, opts.profile, &meter) : Mux_satopt{};
          r.candidates     += stats.candidates;
          r.sim_rejects    += stats.candidates - stats.survivors;
          r.queries        += stats.queries;
          r.proven         += stats.proven;
          r.reused         += stats.reused ? stats.proven : 0;
          r.applied        += stats.arms;
          r.bits           += stats.bits;
          r.nodes_removed  += stats.nodes_removed;
          r.budget_skips   += stats.budget_skips;
          partial          |= !stats.complete;
          // I: prove the remaining `unique if` controls exclusive, then
          // collapse the selections that absorbs.
          const auto collapse  = collapse_hotmuxes(g.get(), opts.profile, &meter);
          r.candidates        += collapse.candidates;
          r.queries           += collapse.queries;
          r.proven            += collapse.proven;
          r.refuted           += collapse.refuted;
          r.unknown           += collapse.unknown;
          r.applied           += collapse.proven;
          r.nodes_removed     += collapse.nodes_removed;
          r.budget_skips      += collapse.budget_skips;
        }
        break;
      case Stage::memory: {
        const auto stats = optimize_memories(graphs, opts.cache_dir, opts.profile, &meter);
        r.queries        = stats.queries;
        r.proven         = stats.proven;
        r.reused         = stats.reused;
        r.applied        = stats.merged_writes + stats.merged_reads + stats.dead_ports + stats.address_bits;
        r.bits           = stats.collision_bits;
        r.budget_skips   = stats.budget_skips;
        break;
      }
    }
    r.work  = meter.stage_work();
    r.state = meter.exhausted() || partial || r.budget_skips != 0 ? Stage_state::exhausted : Stage_state::completed;
    r.ms    = ms_since(stage_start);
  }
  for (size_t i = 0; i < graphs.size(); ++i) {
    report.changed_graphs += graphs[i] && graphs[i]->body_epoch() != epochs[i];
  }
  report.ms = ms_since(start);
  return report;
}

Report run(hhds::Graph* graph, const Options& opts, Meter* meter) {
  // A non-owning handle: the caller owns the graph.
  return run(std::vector<std::shared_ptr<hhds::Graph>>{std::shared_ptr<hhds::Graph>(std::shared_ptr<void>{}, graph)}, opts, meter);
}

}  // namespace livehd::satopt

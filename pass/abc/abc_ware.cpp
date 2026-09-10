// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <print>

#include "abc_incr.hpp"
#include "abc_map.hpp"
#include "diag.hpp"
#include "node_util.hpp"
#include "synthesis_cost.hpp"

namespace livehd::abc {
namespace gu = livehd::graph_util;

float ware_delay_target(std::string_view value) {
  const std::string text{value};
  char*             end    = nullptr;
  const float       target = std::strtof(text.c_str(), &end);
  return end != text.c_str() && *end == '\0' && std::isfinite(target) && target > 0 ? target : 0.0f;
}

bool ware_qor_better(const Ware_qor& baseline, const Ware_qor& candidate, bool timing) {
  if (!std::isfinite(baseline.area) || !std::isfinite(candidate.area) || candidate.area < 0.0) {
    return false;
  }
  if (timing) {
    if (baseline.delays.empty() || baseline.delays.size() != candidate.delays.size()) {
      return false;
    }
    for (size_t i = 0; i < baseline.delays.size(); ++i) {
      if (!std::isfinite(baseline.delays[i]) || !std::isfinite(candidate.delays[i])) {
        return false;
      }
    }
    for (size_t i = 0; i < baseline.delays.size(); ++i) {
      if (std::abs(candidate.delays[i] - baseline.delays[i]) > 0.001f) {
        return candidate.delays[i] < baseline.delays[i];
      }
    }
  }
  return candidate.area < baseline.area - 1e-6;
}

// `options` are the REGION's resolved options (map_region's per-region overlay),
// which is not this mapper's opts_ when a parallel worker records into the
// coordinator -- pass them explicitly rather than swapping a shared member.
void Mapper::remember_ware(const livehd::partition::Region_body& rb, const Map_options& options) {
  if (!options.ware || rb.nodes.empty()) {
    return;
  }
  Ware_region w;
  uint64_t    ge = 0;
  for (auto n : rb.nodes) {
    ge      += gu::synthesis_ge_weight(n);
    auto op  = gu::type_op_of(n);
    w.add   |= options.auto_adder
               && ((options.ware_arith && op == Ntype_op::Sum) || (options.ware_cmp && (op == Ntype_op::LT || op == Ntype_op::GT)));
    w.mult  |= options.ware_arith && options.auto_multiplier && op == Ntype_op::Mult;
    w.barrel |= options.ware_shift && options.auto_barrel && (op == Ntype_op::SHL || op == Ntype_op::SRA);
  }
  if ((!w.add && !w.mult && !w.barrel) || (options.large_ge && ge >= options.large_ge)) {
    return;
  }
  w.rb          = rb;
  // These are callback-lifetime views, not owned snapshots.
  w.rb.pre_body = nullptr;
  w.rb.pre_lib  = nullptr;
  w.rb.pre_name.clear();
  w.nodes.assign(rb.nodes.begin(), rb.nodes.end());
  w.source = rb.src->get_io()->get_graph();
  // A whole-design flat source may be a temporary in the OUTPUT library,
  // deleted by the partitioner on return. Preserve only that exceptional case.
  if (rb.src->get_io()->get_library() == outlib_) {
    const std::string name{rb.src->get_name()};
    if (!ware_sources_.find_io(name) && !ware_sources_.copy_from(*outlib_, name)) {
      return;
    }
    w.source = ware_sources_.find_io(name)->get_graph();
    for (auto& n : w.nodes) {
      n = w.source->get_node(n.get_class_index());
    }
    for (auto& p : w.rb.inputs) {
      p.src_driver = w.source->get_pin(p.src_driver.get_class_index());
    }
    for (auto& p : w.rb.outputs) {
      p.src_driver = w.source->get_pin(p.src_driver.get_class_index());
    }
    w.rb.src = w.source.get();
  }
  // Small extracted primitives retain their original pre-map snapshot for
  // candidate reuse. Inlined regions keep the bounded baseline-only policy.
  if (rb.src->get_input_node().attr(attrs::ware_module).has() && rb.pre_body && rb.pre_lib
      && ware_pre_.copy_from(*rb.pre_lib, rb.pre_name)) {
    w.rb.pre_name = rb.pre_name;
    w.rb.pre_lib  = &ware_pre_;
    w.rb.pre_body = ware_pre_.find_io(rb.pre_name)->get_graph().get();
  }
  w.options = options;
  if (!ware_shells_.copy_from(*outlib_, rb.module_name)) {
    return;
  }
  // The partitioner stamps preservation metadata after its callback returns.
  // Trials restore this earlier shell, so carry the marker into it explicitly.
  if (auto a = rb.src->get_input_node().attr(attrs::ware_module); a.has()) {
    ware_shells_.find_io(rb.module_name)->get_graph()->get_input_node().attr(attrs::ware_module).set(a.get());
  }
  ware_regions_.push_back(std::move(w));
}

void Mapper::optimize_ware(hhds::GraphLibrary& outlib, std::string_view top) {
  if (ware_regions_.empty()) {
    return;
  }
  const float global_target = ware_delay_target(startup_opts_.delay);
  auto        score         = score_ware(outlib, top);
  if (!score.valid) {
    std::print("[pass.abc] ware: stitched QoR unavailable; retaining baseline implementations\n");
    return;
  }
  std::sort(ware_regions_.begin(), ware_regions_.end(), [](const auto& a, const auto& b) {
    return a.rb.module_name < b.rb.module_name;
  });
  auto  saved_options = opts_;
  auto* saved_cache   = incr_;
  incr_               = nullptr;  // trials must never overwrite an independent baseline cache
  ware_trial_         = true;
  struct Restore {
    Mapper&     m;
    Map_options options;
    Incr_cache* cache;
    ~Restore() {
      m.opts_       = std::move(options);
      m.incr_       = cache;
      m.ware_trial_ = false;
    }
  } restore{*this, saved_options, saved_cache};
  absl::flat_hash_set<std::string> visited;
  while (true) {
    auto next = std::find_if(ware_regions_.begin(), ware_regions_.end(), [&](const auto& w) {
      if (visited.contains(w.rb.module_name)) {
        return false;
      }
      const float target = ware_delay_target(w.options.delay);
      if (target <= 0) {
        return true;
      }
      const auto it = score.region_path_delay.find(w.rb.module_name);
      return it != score.region_path_delay.end() && !score.delays.empty()
             && it->second >= std::min(target, score.delays.front()) - 0.001f;
    });
    if (next == ware_regions_.end()) {
      break;
    }
    auto&       w      = *next;
    const auto& name   = w.rb.module_name;
    const float target = ware_delay_target(w.options.delay);
    const bool  timing = target > 0;
    visited.insert(name);
    auto row_it = std::find_if(qor_.begin(), qor_.end(), [&](const auto& q) { return q.module == name; });
    if (row_it == qor_.end()) {
      continue;
    }
    const size_t row = static_cast<size_t>(row_it - qor_.begin());
    struct Candidate {
      Map_options options;
      std::string label;
    };
    // Enumerate the small local selector product (at most 3 * 2 * 2).
    // Otherwise a later barrel/multiplier trial could discard a faster adder,
    // or miss a combination that only pays off with a different adder.
    std::vector<Candidate> candidates{
        {w.options, ""}
    };
    if (w.add || (w.mult && w.options.auto_adder)) {
      for (auto kind : {arith::Adder_kind::cla, arith::Adder_kind::cska}) {
        auto o  = w.options;
        o.adder = kind;
        candidates.push_back({o, kind == arith::Adder_kind::cla ? "adder=cla" : "adder=cska"});
      }
    }
    const auto append = [](std::string label, std::string_view selector) {
      if (!label.empty()) {
        label += ",";
      }
      label += selector;
      return label;
    };
    if (w.mult) {
      const auto count = candidates.size();
      for (size_t i = 0; i < count; ++i) {
        auto o       = candidates[i].options;
        o.multiplier = arith::Mult_kind::tree;
        candidates.push_back({o, append(candidates[i].label, "multiplier=tree")});
      }
    }
    if (w.barrel) {
      const auto count = candidates.size();
      for (size_t i = 0; i < count; ++i) {
        auto o           = candidates[i].options;
        o.reverse_barrel = !o.reverse_barrel;
        candidates.push_back({o, append(candidates[i].label, o.reverse_barrel ? "barrel=reverse" : "barrel=log")});
      }
    }
    candidates.erase(candidates.begin());  // baseline already measured
    for (const auto& candidate : candidates) {
      hhds::GraphLibrary backup;
      if (!backup.copy_from(outlib, name)) {
        break;
      }
      auto  previous = qor_[row];
      auto* shell    = ware_shells_.find_io(name)->get_graph().get();
      // Preserve primary-input/constant passthroughs installed by the
      // partitioner AFTER the original callback returned.
      auto  old_body = outlib.find_io(name)->get_graph();
      for (const auto& od : old_body->get_io()->get_output_pin_decls()) {
        if (std::any_of(w.rb.outputs.begin(), w.rb.outputs.end(), [&](const auto& p) { return p.name == od.name; })) {
          continue;
        }
        auto out = shell->get_output_pin(od.name);
        if (!out.inp_edges().empty()) {
          continue;
        }
        for (auto e : old_body->get_output_pin(od.name).inp_edges()) {
          if (e.driver.is_const()) {
            auto c = gu::create_const(*shell, gu::const_of(e.driver));
            c.connect_sink(out);
          } else if (gu::is_graph_input_pin(e.driver)) {
            shell->get_input_pin(gu::pin_name_of(e.driver)).connect_sink(out);
          }
        }
      }
      if (!outlib.replace_body_from(name, *shell)) {
        break;
      }
      opts_              = candidate.options;
      w.rb.nodes         = w.nodes;
      const size_t count = qor_.size();
      incr_              = nullptr;
      std::unique_ptr<Incr_cache> candidate_cache;
      if (saved_cache && w.rb.pre_body) {
        const auto& o   = candidate.options;
        const auto  key = std::format("a{}_b{}_m{}_s{}",
                                      static_cast<int>(o.adder),
                                      o.block_size,
                                      static_cast<int>(o.multiplier),
                                      o.reverse_barrel);
        candidate_cache = std::make_unique<Incr_cache>(saved_cache->dir() + "/ware/" + name + "/" + key, saved_cache->salt(), true);
        incr_           = candidate_cache.get();
      }
      map_region(w.rb);
      // Persist the independent mapped candidate before selection/rollback.
      // Criticality is re-scored against the current assembled design on every
      // run; the cache never stores the context-dependent winning decision.
      if (incr_) {
        incr_->save();
      }
      const bool mapped  = qor_.size() == count + 1 && refusal_.empty() && time_refusal_.empty();
      auto       trial_q = mapped ? qor_.back() : previous;
      if (qor_.size() > count) {
        qor_.resize(count);
      }
      auto       trial_score      = mapped ? score_ware(outlib, top) : Ware_score{};
      const bool keep             = trial_score.valid
                                    && ware_qor_better(score, trial_score, timing)
                                    // Area-only sections must not degrade another section's
                                    // constrained stitched paths.
                                    && (timing || score.delays.empty() || !ware_qor_better(trial_score, score, true));
      trial_q.ware_trials         = previous.ware_trials + 1;
      const bool   candidate_hit  = mapped && !trial_q.resynth;
      const double trial_ms       = mapped ? trial_q.ms : 0.0;
      trial_q.ms                 += previous.ms;
      if (keep) {
        trial_q.ware_selected = candidate.label;
        qor_[row]             = std::move(trial_q);
      } else {
        (void)outlib.replace_body_from(name, *backup.find_io(name)->get_graph());
        qor_[row] = previous;
        ++qor_[row].ware_trials;
        qor_[row].ms += trial_ms;
      }
      std::print("[pass.abc] ware region='{}' {} objective={} delay_ps={:.3f}->{:.3f} area={:.6f}->{:.6f} {}\n",
                 name,
                 candidate.label,
                 timing ? "timing" : "area",
                 score.delays.empty() ? 0.0f : score.delays.front(),
                 trial_score.delays.empty() ? -1.0f : trial_score.delays.front(),
                 score.area,
                 trial_score.valid ? trial_score.area : -1.0,
                 keep ? "keep" : "reject");
      if (incr_) {
        std::print("[pass.abc] ware candidate cache {}\n", candidate_hit ? "hit" : "miss");
      }
      if (keep) {
        score = std::move(trial_score);
      }
      incr_ = nullptr;  // candidate_cache releases its libraries at the end of this iteration
      if (!refusal_.empty() || !time_refusal_.empty()) {
        std::print("[pass.abc] ware: trial budget reached; retained previous module: {}{}\n", refusal_, time_refusal_);
        refusal_.clear();
        time_refusal_.clear();
        break;
      }
    }
  }
  if (global_target > 0 && !score.delays.empty()) {
    std::print("[pass.abc] ware: selected stitched delay={:.3f} ps target={:.3f} ps {}\n",
               score.delays.front(),
               global_target,
               score.delays.front() <= global_target ? "met" : "missed (fastest measured retained)");
  }
  if (global_target <= 0) {
    for (const auto& w : ware_regions_) {
      const float target = ware_delay_target(w.options.delay);
      const auto  it     = score.region_path_delay.find(w.rb.module_name);
      if (target > 0 && it != score.region_path_delay.end()) {
        std::print("[pass.abc] ware region='{}': selected stitched path={:.3f} ps target={:.3f} ps {}\n",
                   w.rb.module_name,
                   it->second,
                   target,
                   it->second <= target ? "met" : "missed (fastest measured retained)");
      }
    }
  }
}
}  // namespace livehd::abc

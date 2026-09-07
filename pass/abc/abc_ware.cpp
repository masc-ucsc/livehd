// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <format>
#include <print>

#include "abc_incr.hpp"
#include "abc_map.hpp"
#include "diag.hpp"
#include "node_util.hpp"
#include "synthesis_cost.hpp"

namespace livehd::abc {
namespace gu = livehd::graph_util;

bool ware_depth_better(const std::vector<int>& baseline, const std::vector<int>& candidate) {
  if (baseline.empty() || baseline.size() != candidate.size()) {
    return false;
  }
  const int old_depth = *std::max_element(baseline.begin(), baseline.end());
  const int new_depth = *std::max_element(candidate.begin(), candidate.end());
  return new_depth < old_depth
         || (new_depth == old_depth
             && std::count(candidate.begin(), candidate.end(), new_depth)
                    < std::count(baseline.begin(), baseline.end(), old_depth));
}

void Mapper::remember_ware(const livehd::partition::Region_body& rb) {
  if (!opts_.ware || rb.nodes.empty()) {
    return;
  }
  Ware_region w;
  uint64_t    ge = 0;
  for (auto n : rb.nodes) {
    ge       += gu::synthesis_ge_weight(n);
    auto op   = gu::type_op_of(n);
    w.add    |= opts_.auto_adder && (op == Ntype_op::Sum || op == Ntype_op::LT || op == Ntype_op::GT);
    w.mult   |= opts_.auto_multiplier && op == Ntype_op::Mult;
    w.barrel |= opts_.auto_barrel && (op == Ntype_op::SHL || op == Ntype_op::SRA);
  }
  if ((!w.add && !w.mult && !w.barrel) || (opts_.large_ge && ge >= opts_.large_ge)) {
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
  w.options = opts_;
  if (!ware_shells_.copy_from(*outlib_, rb.module_name)) {
    return;
  }
  ware_regions_.push_back(std::move(w));
}

void Mapper::optimize_ware(hhds::GraphLibrary& outlib, std::string_view top) {
  if (ware_regions_.empty()) {
    return;
  }
  auto score = score_ware(outlib, top);
  if (!score.valid) {
    std::print("[pass.abc] ware: stitched depth unavailable; retaining baseline implementations\n");
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
      return !visited.contains(w.rb.module_name) && score.critical_regions.contains(w.rb.module_name);
    });
    if (next == ware_regions_.end()) {
      break;
    }
    auto&       w    = *next;
    const auto& name = w.rb.module_name;
    visited.insert(name);
    auto row_it = std::find_if(qor_.begin(), qor_.end(), [&](const auto& q) { return q.module == name; });
    if (row_it == qor_.end()) {
      continue;
    }
    const size_t row      = static_cast<size_t>(row_it - qor_.begin());
    auto         selected = w.options;
    struct Candidate {
      Map_options options;
      std::string label;
    };
    std::vector<Candidate> candidates;
    if (w.add || (w.mult && w.options.auto_adder)) {
      for (auto kind : {arith::Adder_kind::cla, arith::Adder_kind::cska}) {
        auto o  = selected;
        o.adder = kind;
        candidates.push_back({o, kind == arith::Adder_kind::cla ? "adder=cla" : "adder=cska"});
      }
    }
    if (w.mult) {
      auto o       = selected;
      o.multiplier = arith::Mult_kind::tree;
      candidates.push_back({o, "multiplier=tree"});
      if (w.options.auto_adder) {
        o.adder = arith::Adder_kind::cla;
        candidates.push_back({o, "multiplier=tree,adder=cla"});
      }
    }
    if (w.barrel) {
      auto o           = selected;
      o.reverse_barrel = !o.reverse_barrel;
      candidates.push_back({o, o.reverse_barrel ? "barrel=reverse" : "barrel=log"});
    }
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
      map_region(w.rb);
      const bool mapped  = qor_.size() == count + 1 && refusal_.empty() && time_refusal_.empty();
      auto       trial_q = mapped ? qor_.back() : previous;
      if (qor_.size() > count) {
        qor_.resize(count);
      }
      auto       trial_score  = mapped ? score_ware(outlib, top) : Ware_score{};
      const bool keep         = trial_score.valid && ware_depth_better(score.endpoints, trial_score.endpoints);
      trial_q.ware_trials     = previous.ware_trials + 1;
      const double trial_ms   = mapped ? trial_q.ms : 0.0;
      trial_q.ms             += previous.ms;
      if (keep) {
        trial_q.ware_selected = candidate.label;
        qor_[row]             = std::move(trial_q);
        selected              = candidate.options;
      } else {
        (void)outlib.replace_body_from(name, *backup.find_io(name)->get_graph());
        qor_[row] = previous;
        ++qor_[row].ware_trials;
        qor_[row].ms += trial_ms;
      }
      std::print("[pass.abc] ware region='{}' {} stitched_depth={} -> {} {}\n",
                 name,
                 candidate.label,
                 score.endpoints.empty() ? 0 : score.endpoints.front(),
                 trial_score.endpoints.empty() ? -1 : trial_score.endpoints.front(),
                 keep ? "keep" : "reject");
      if (keep) {
        score = std::move(trial_score);
      }
      if (!refusal_.empty() || !time_refusal_.empty()) {
        std::print("[pass.abc] ware: trial budget reached; retained previous module: {}{}\n", refusal_, time_refusal_);
        refusal_.clear();
        time_refusal_.clear();
        break;
      }
    }
  }
}
}  // namespace livehd::abc

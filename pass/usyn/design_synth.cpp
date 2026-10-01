// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_synth.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <unordered_map>

#include "design_prepare.hpp"
#include "node_util.hpp"

namespace livehd::usyn {
namespace {
namespace gu = graph_util;

// The user-visible names a region binds: top-level design IO (what an
// equivalence check matches, with state names) and native barrier names.
// Definition regions have no design IO; their port and net names, like every
// other boundary-net name, stay out of the cache identity.
Identity_names identity_names(const partition::Region_body& rb, const synth::Region_blast& blast, std::string_view top,
                              Budget& io) {
  Identity_names names;
  const auto     source = rb.src ? rb.src->get_name() : std::string_view{};
  // A whole-design flatten partitions a scratch copy that keeps top's IO.
  if (source == top || source == partition::flatten_scratch_name(top)) {
    std::vector<std::string> inputs, outputs;
    for (const auto& port : rb.inputs) {
      if (!io.spend()) {
        return names;
      }
      inputs.emplace_back(gu::is_graph_input_pin(port.src_driver) ? gu::pin_name_of(port.src_driver) : std::string_view{});
    }
    for (const auto& port : rb.outputs) {
      std::vector<std::string_view> driven;
      if (gu::is_graph_input_pin(port.src_driver)) {
        driven.push_back(gu::pin_name_of(port.src_driver));
      }
      for (const auto& edge : port.src_driver.out_edges()) {
        if (!io.spend()) {
          return names;
        }
        if (gu::is_graph_output_pin(edge.sink)) {
          driven.push_back(gu::pin_name_of(edge.sink));
        }
      }
      std::sort(driven.begin(), driven.end());
      std::string joined;
      for (const auto name : driven) {
        joined += joined.empty() ? "" : ",";
        joined += name;
      }
      outputs.push_back(std::move(joined));
    }
    std::vector<bool> positioned(inputs.size() + outputs.size());
    names.inputs.resize(blast.lnet.inputs().size());
    for (size_t i = 0; i < blast.all_pi_order.size() && i < names.inputs.size(); ++i) {
      const auto& origin = blast.all_pi_order[i];
      if (origin.kind == synth::Pi_kind::region_input && origin.index < blast.pi_order.size()) {
        const auto [port, lane] = blast.pi_order[origin.index];
        if (port < inputs.size()) {
          positioned[port] = true;
          if (!inputs[port].empty()) {
            names.inputs[i] = std::format("{}[{}]", inputs[port], lane);
          }
        }
      }
    }
    names.outputs.resize(blast.lnet.outputs().size());
    for (size_t j = 0; j < blast.po_order.size() && j < names.outputs.size(); ++j) {
      const auto [port, lane] = blast.po_order[j];
      if (port < outputs.size()) {
        positioned[inputs.size() + port] = true;
        if (!outputs[port].empty()) {
          names.outputs[j] = std::format("{}[{}]", outputs[port], lane);
        }
      }
    }
    // Design IO on ports without a logical position (an unused input, a direct
    // native output). Port order follows boundary signatures that see logic
    // outside the region, so these are a sorted set, not a sequence.
    for (size_t k = 0; k < positioned.size(); ++k) {
      const auto& name = k < inputs.size() ? inputs[k] : outputs[k - inputs.size()];
      if (!positioned[k] && !name.empty()) {
        names.ports.push_back((k < inputs.size() ? "input " : "output ") + name);
      }
    }
    std::sort(names.ports.begin(), names.ports.end());
  }
  // A memory, instance or clock-cell row is named by the node itself; its
  // snapshot spelling also embeds a node number and a nearby wire.
  const auto& rows = blast.source_state->sources;
  if (std::any_of(rows.begin(), rows.end(), [](const auto& row) { return !row.q.present; })) {
    std::unordered_map<uint64_t, std::string_view> nodes;
    for (const auto& node : rb.nodes) {
      if (!io.spend()) {
        return names;
      }
      nodes.emplace(node.get_debug_nid(), gu::node_name_of(node));
    }
    names.barriers.reserve(rows.size());
    for (const auto& row : rows) {
      const auto it = row.q.present ? nodes.end() : nodes.find(row.node);
      names.barriers.emplace_back(it == nodes.end() ? std::string_view{} : it->second);
    }
  }
  return names;
}
}  // namespace

Design_result synthesize_cmos_design(const std::shared_ptr<hhds::Graph>& top, const Design_options& options, Budget& work) {
  Design_result result;
  if (!top || !top->get_io() || !top->get_io()->get_library()) {
    result.reason = "missing source design";
    return result;
  }
  // Two deterministic ledgers under the one invocation limit. `work` is the
  // structural allowance: preparation, partition/region admission, translation,
  // each region's mandatory synthesis steps (semantic import, logical
  // admission and identity baseline, freezing) and emission. Region searches
  // draw only on a search-only remainder: region i starts with the limit minus
  // the search work of earlier regions (a hit replays its recorded search
  // work), so structural work never changes a search, and a region starved of
  // search credits publishes its minimal (identity) selection instead of
  // failing the design. A search observes its credits, so a cached decision
  // records its credit floor and is reused only under credits that reproduce
  // it; a hit also replays the region's recorded structural work, so warm
  // structural totals equal cold ones. Both ledgers share process/time
  // admission; any refusal is sticky.
  uint64_t   search_left = work.remaining;
  const auto exhausted   = [&] {
    result.status = Status::search_exhausted;
    result.reason = "design preparation budget";
  };
  if (!work.spend()) {
    exhausted();
    return result;
  }
  synth::Preparation_budget preparation;
  preparation.max_source_nodes = options.max_source_nodes;
  preparation.admission        = [&](std::string_view, uint64_t amount) {
    if (!amount) {
      work.checkpoint_work = 0;  // force process/time admission around bulk copies
    }
    return work.spend(amount);
  };
  const std::array roots{top};
  auto             prepared = synth::prepare_design(roots, false, "pass.usyn", &preparation);
  if (!prepared) {
    if (preparation.refused) {
      exhausted();
    } else {
      result.reason = "private design preparation failed";
    }
    return result;
  }
  auto output                        = std::make_unique<Logical_design>();
  auto cache_options                 = options.cache;
  cache_options.limits.nodes         = options.logical.max_nodes;
  cache_options.limits.formula_nodes = options.logical.endpoint.functions.max_formula_nodes;
  output->cache.enabled              = !cache_options.directory.empty();
  bool       failed                  = false;
  const auto fail                    = [&](Status status, std::string reason) {
    failed        = true;
    result.status = status;
    result.reason = std::move(reason);
  };
  const auto build = [&](const partition::Region_body& rb) {
    if (failed) {
      return;
    }
    if (!work.spend(rb.nodes.size() + 1)) {
      fail(Status::search_exhausted, "region admission budget");
      return;
    }
    synth::Blast_options blast_options;
    blast_options.logical_state     = true;
    blast_options.state_target      = synth::State_target::cmos;
    blast_options.logical_max_nodes = options.logical.max_nodes;
    synth::Blast_hooks hooks;
    hooks.over_budget = [&](uint64_t, size_t, size_t, size_t) {
      // Force process admission at a blaster checkpoint; do not charge the
      // cumulative blasted count repeatedly as deterministic work.
      work.checkpoint_work = 0;
      return !work.spend();
    };
    auto blast = synth::blast_region(rb, blast_options, hooks);
    if (blast.status != synth::Region_blast::Status::blasted || !blast.source_state) {
      fail(blast.status == synth::Region_blast::Status::over_budget ? Status::search_exhausted : Status::invalid,
           "logical translation failed for " + rb.module_name);
      return;
    }
    // Validate fresh source semantics before any reuse. Cached metadata cannot
    // authorize a target that the current source does not support.
    if (!synth::validate_state_target(*blast.source_state, synth::State_target::cmos)) {
      fail(Status::invalid, "source target validation failed for " + rb.module_name);
      return;
    }
    const uint64_t credits = search_left;
    Budget         search{credits};
    search.admission          = work.admission;
    search.admission_interval = work.admission_interval;
    Budget io{cache_options.entry_work};
    io.admission          = work.admission;
    io.admission_interval = work.admission_interval;
    Logical_cache_probe cached;
    if (output->cache.enabled) {
      const auto names = identity_names(rb, blast, top->get_name(), io);
      cached           = probe_logical_cache(cache_options,
                                             rb.module_name,
                                             blast.lnet,
                                             *blast.source_state,
                                             names,
                                             options.logical,
                                             credits,
                                             io);
    }
    if (io.resource_exhausted) {
      work.resource_exhausted = work.exhausted = true;
      fail(Status::search_exhausted, "logical cache process admission");
      return;
    }
    Stateful_result selected;
    Credit_floor    credit;
    uint64_t        structural = 0;
    const bool      reused     = cached.record.has_value();
    if (reused) {
      // The record reproduces under these credits (the probe checked its
      // floor): it stands for the cold search, including its consumed work,
      // and for the cold structural steps, whose plain spends the replay
      // charges at once (refused exactly when the cold run would refuse).
      credit     = cached.record->credit;
      structural = cached.record->structural;
      if (!work.spend(structural)) {
        fail(Status::search_exhausted,
             rb.module_name + (work.resource_exhausted ? ": region process admission" : ": region structural budget"));
        return;
      }
      output->cache.replayed_search_work     += credit.work;
      output->cache.replayed_structural_work += structural;
      ++output->cache.reused;
      selected.status = Status::feasible;
      selected.region = std::move(cached.record->selected);
      selected.report = std::move(cached.record->report);
    } else {
      if (output->cache.enabled) {
        ++output->cache.misses;
        output->cache.invalid       += cached.outcome == Cache_lookup::invalid;
        output->cache.refused       += cached.outcome == Cache_lookup::refused;
        output->cache.credit_misses += cached.outcome == Cache_lookup::credit_miss;
      }
      const auto before = work.consumed;
      selected
          = synthesize_stateful_region(blast.lnet, *blast.source_state, synth::State_target::cmos, options.logical, work, search);
      structural = work.consumed - before;
      if (search.resource_exhausted || work.resource_exhausted) {
        // A process/time refusal is sticky for the design; its result is not a
        // reproducible function of the credits and is never published or cached.
        work.resource_exhausted = work.exhausted = true;
        fail(Status::search_exhausted, rb.module_name + ": region search process admission");
        return;
      }
      credit = search.credit_floor();
    }
    // Later regions draw on what this search left, whatever its outcome. A
    // search short of credits keeps its complete incumbent, at worst the
    // identity baseline (logical_region); no region is published only when the
    // structural allowance refused it (or its input is invalid).
    search_left -= std::min(search_left, credit.work);
    if (!selected.region) {
      fail(selected.status, rb.module_name + ": " + selected.reason);
      return;
    }
    auto emitted = emit_logical_region(rb, blast, *selected.region, work, options.logical.max_nodes);
    if (emitted.status != Status::feasible) {
      fail(emitted.status, rb.module_name + ": " + emitted.reason);
      return;
    }
    if (output->cache.enabled && !reused && !cached.key.empty()) {
      if (!replaces_stored_record(cached, credit)) {
        ++output->cache.kept;  // the missed unbound record is the more general one
      } else if (store_logical_cache(cache_options,
                                     cached.key,
                                     rb.module_name,
                                     *selected.region,
                                     selected.report,
                                     credit,
                                     structural,
                                     io)) {
        ++output->cache.stored;
      } else {
        ++output->cache.store_failures;
      }
    }
    if (io.resource_exhausted) {
      work.resource_exhausted = work.exhausted = true;
      fail(Status::search_exhausted, "logical cache publication admission");
      return;
    }
    output->cache.io_work += io.consumed;
    output->regions.push_back({rb.module_name,
                               std::move(*selected.region),
                               std::move(selected.report),
                               reused,
                               std::move(cached.key),
                               credits,
                               credit,
                               structural});
  };
  const auto partition_admission = [&](std::string_view, uint64_t amount) {
    if (failed) {
      return false;  // stop reconstruction immediately after a failed builder
    }
    if (!amount) {
      work.checkpoint_work = 0;
    }
    if (!work.spend(amount)) {
      fail(Status::search_exhausted, "design partition budget");
      return false;
    }
    return true;
  };
  const bool partitioned = Pass_partition::build_decomposition(prepared->resolve_graphs,
                                                               &output->library,
                                                               top->get_name(),
                                                               false,
                                                               build,
                                                               options.flatten,
                                                               false,
                                                               {},
                                                               1,
                                                               prepared->loops.preserved_defs,
                                                               {},
                                                               partition_admission);
  if (failed) {
    return result;
  }
  if (!partitioned) {
    result.reason = "design partition failed";
    return result;
  }
  const auto io = output->library.find_io(top->get_name());
  output->top   = io ? io->get_graph() : nullptr;
  if (!output->top) {
    result.reason = "partition produced no top definition";
    return result;
  }
  result.status = Status::feasible;
  result.design = std::move(output);
  return result;
}

}  // namespace livehd::usyn

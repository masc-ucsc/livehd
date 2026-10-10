// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_synth.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <print>
#include <unordered_map>

#include "design_prepare.hpp"
#include "diag.hpp"
#include "hash_util.hpp"
#include "literal_stats.hpp"
#include "node_util.hpp"
#include "synth_policy.hpp"
#include "usyn_salt.hpp"

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
  auto             prepared = synth::prepare_design(roots, false, "pass.usyn", &preparation, options.specialize);
  if (!prepared) {
    if (preparation.refused) {
      exhausted();
    } else {
      result.reason = "private design preparation failed";
    }
    return result;
  }
  auto output                         = std::make_unique<Logical_design>();
  auto cache_options                  = options.cache;
  cache_options.context              += options.mux_tree ? "/mux-tree" : "/mux-decode";
  cache_options.context              += options.eq_balance ? "/eq-balanced" : "/eq-shared";
  cache_options.limits.nodes          = options.logical.max_nodes;
  cache_options.limits.formula_nodes  = options.logical.endpoint.functions.max_formula_nodes;
  output->cache.enabled               = !cache_options.directory.empty();
  bool       failed                   = false;
  const auto fail                     = [&](Status status, std::string reason) {
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
    blast_options.usyn           = true;
    blast_options.reverse_barrel = options.reverse_barrel;
    blast_options.adder          = options.adder;
    blast_options.block_size     = options.adder_block;
    blast_options.multiplier     = options.multiplier;
    blast_options.mux_tree       = options.mux_tree;
    blast_options.eq_balance     = options.eq_balance;
    bool auto_adder = options.auto_sum_adder, auto_multiplier = options.auto_multiplier, auto_barrel = options.auto_barrel;
    bool allow_tune = true;
    synth_attr::Policy mapping_policy;
    for (auto n : rb.nodes) {
      auto a = n.attr(attrs::synth_policy);
      if (!a.has()) {
        continue;
      }
      const auto p = synth_attr::read(a.get());
      if (p.contains("abc")) {
        fail(Status::invalid, "synth.abc cannot be honored by USYN in " + rb.module_name);
        return;
      }
      for (const auto* key : {"delay", "ware"}) {
        if (auto hint = p.find(key); hint != p.end()) {
          auto old = mapping_policy.find(key);
          if (old != mapping_policy.end() && old->second.rank == hint->second.rank && old->second.value != hint->second.value) {
            fail(Status::invalid, "conflicting source synthesis policy in " + rb.module_name);
            return;
          }
          if (old == mapping_policy.end() || old->second.rank <= hint->second.rank) {
            mapping_policy[key] = hint->second;
          }
        }
      }
      if (synth_attr::get(p, "ware") == "false") {
        allow_tune = false;
      }
      if (!auto_adder && synth_attr::get(p, "adder") == "auto") {
        blast_options.inherited_adder = blast_options.adder;
        blast_options.adder           = synth::arith::Adder_kind::rca;
        auto_adder                    = true;
      }
      if (!auto_multiplier && synth_attr::get(p, "multiplier") == "auto") {
        blast_options.inherited_multiplier = blast_options.multiplier;
        blast_options.multiplier           = synth::arith::Mult_kind::csa;
        auto_multiplier                    = true;
      }
      if (!auto_barrel && synth_attr::get(p, "barrel") == "auto") {
        blast_options.inherited_barrel = blast_options.reverse_barrel;
        blast_options.reverse_barrel   = false;
        auto_barrel                    = true;
      }
    }
    if (auto_adder) {
      // The native mapper has no subsequent Boolean restructuring to remove
      // a ripple carry chain. Keep narrow sums and divider internals compact;
      // wide sums and the final multiplier addition use logarithmic carries.
      blast_options.sum_adder                  = synth::arith::Adder_kind::prefix;
      blast_options.sum_adder_min_width        = 16;
      blast_options.multiplier_adder           = synth::arith::Adder_kind::prefix;
      blast_options.comparator_adder           = synth::arith::Adder_kind::prefix;
      blast_options.comparator_adder_min_width = 8;
    }
    if (auto it = options.region_opts.find(rb.color); it != options.region_opts.end()) {
      const auto& ro = it->second;
      if (ro.flow) {
        fail(Status::invalid, "synth.abc cannot be honored by USYN");
        return;
      }
      if (ro.ware) {
        allow_tune = *ro.ware;
      }
      if (ro.adder) {
        blast_options.source_adder = false;
        blast_options.inherited_adder.reset();
        auto_adder              = false;
        blast_options.adder     = *ro.adder;
        blast_options.sum_adder = blast_options.comparator_adder = blast_options.multiplier_adder = *ro.adder;
        blast_options.sum_adder_min_width = blast_options.comparator_adder_min_width = 0;
      }
      if (ro.multiplier) {
        blast_options.source_multiplier = false;
        blast_options.inherited_multiplier.reset();
        blast_options.multiplier = *ro.multiplier;
        auto_multiplier          = false;
      }
      if (ro.reverse_barrel) {
        blast_options.source_barrel = false;
        blast_options.inherited_barrel.reset();
        blast_options.reverse_barrel = *ro.reverse_barrel;
        auto_barrel                  = false;
      }
      if (ro.block_size) {
        blast_options.source_block = false;
        blast_options.block_size   = *ro.block_size;
      }
    }
    blast_options.logical_state     = true;
    blast_options.state_target      = synth::State_target::cmos;
    blast_options.logical_max_nodes = options.logical.max_nodes;
    const auto encoded              = [](const synth::Blast_options& o) {
      const char* adder = o.adder == synth::arith::Adder_kind::rca      ? "rca"
                                       : o.adder == synth::arith::Adder_kind::cla    ? "cla"
                                       : o.adder == synth::arith::Adder_kind::cska   ? "cska"
                                       : o.adder == synth::arith::Adder_kind::prefix ? "prefix"
                                                                                     : "brent";
      const char* mult  = o.multiplier == synth::arith::Mult_kind::array  ? "array"
                                       : o.multiplier == synth::arith::Mult_kind::tree ? "tree"
                                       : o.multiplier == synth::arith::Mult_kind::csa  ? "csa"
                                                                                       : "sn";
      return std::format("{{\"adder\":\"{}\",\"multiplier\":\"{}\",\"barrel\":\"{}\",\"block_size\":{},\"heuristic\":{}}}",
                         adder,
                         mult,
                         o.reverse_barrel ? "reverse" : "log",
                         o.block_size,
                         o.sum_adder_min_width > 0 || o.comparator_adder_min_width > 0);
    };
    synth::Blast_hooks hooks;
    hooks.over_budget = [&](uint64_t, size_t, size_t, size_t) {
      // Force process admission at a blaster checkpoint; do not charge the
      // cumulative blasted count repeatedly as deterministic work.
      work.checkpoint_work = 0;
      return !work.spend();
    };
    auto blast = synth::blast_region(rb, blast_options, hooks);
    if (blast.status == synth::Region_blast::Status::over_budget) {
      // Name the limit: a silent "translation failed" on a large region reads
      // as a source bug, while the fix is a larger pass.usyn.max_nodes.
      fail(Status::search_exhausted,
           "logical translation of " + rb.module_name + " exceeds the node/memory admission limit (pass.usyn.max_nodes="
               + std::to_string(options.logical.max_nodes) + ")");
      return;
    }
    if (blast.status != synth::Region_blast::Status::blasted || !blast.source_state) {
      fail(Status::invalid, "logical translation failed for " + rb.module_name);
      return;
    }
    std::string tune_key;
    bool        replay = false;
    if (options.tune_store && options.tune_profile != "off") {
      Budget identity_work{options.cache.entry_work};
      identity_work.admission = work.admission;
      const auto  names       = identity_names(rb, blast, top->get_name(), identity_work);
      auto        identity    = serialize_logical_identity(blast.lnet,
                                                 *blast.source_state,
                                                 names,
                                                 options.logical,
                                                 kUsynSrcSalt,
                                                 options.cache.context,
                                                 identity_work,
                                                 cache_options.limits);
      std::string recipe
          = identity.bytes + encoded(blast_options) + options.tune_context
            + std::format("|inherited={}/{}/{}",
                          blast_options.inherited_adder ? static_cast<int>(*blast_options.inherited_adder) : -1,
                          blast_options.inherited_multiplier ? static_cast<int>(*blast_options.inherited_multiplier) : -1,
                          blast_options.inherited_barrel ? static_cast<int>(*blast_options.inherited_barrel) : -1);
      for (auto n : rb.nodes) {
        if (auto a = n.attr(attrs::synth_policy); a.has()) {
          recipe += std::string(a.get());
        }
      }
      if (identity.status == Status::feasible) {
        tune_key = "usyn-" + std::format("{:016x}", livehd::hash_util::fnv1a64(recipe));
      }
      if (!tune_key.empty()) {
        const auto stored = options.tune_store->find(tune_key);
        if (!stored.empty()) {
          rapidjson::Document doc;
          doc.Parse(stored.c_str());
          const auto& o = doc["options"];
          if (auto_adder && (!o.HasMember("heuristic") || !o["heuristic"].GetBool())) {
            blast_options.sum_adder_min_width = blast_options.comparator_adder_min_width = 0;
            blast_options.adder     = *synth::arith::parse_adder_kind(o["adder"].GetString());
            blast_options.sum_adder = blast_options.comparator_adder = blast_options.multiplier_adder = blast_options.adder;
          }
          if (auto_multiplier) {
            blast_options.multiplier = *synth::arith::parse_mult_kind(o["multiplier"].GetString());
          }
          if (auto_barrel) {
            blast_options.reverse_barrel = std::string_view(o["barrel"].GetString()) == "reverse";
          }
          blast  = synth::blast_region(rb, blast_options, hooks);
          replay = true;
          if (blast.status != synth::Region_blast::Status::blasted || !blast.source_state) {
            fail(Status::invalid, "tuning replay translation failed");
            return;
          }
        }
      }
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
    if (options.tune_store) {
      options.tune_store->applied(rb.module_name,
                                  std::format("{{\"key\":{},\"options\":{},\"origin\":\"{}\",\"score_kind\":\"native_logic\"}}",
                                              synth_attr::quote(tune_key),
                                              encoded(blast_options),
                                              replay ? "stored" : "source/default"));
    }
    if (options.tune_store && options.tune_profile == "on" && allow_tune && !tune_key.empty()) {
      bool add = false, mult = false, barrel = false;
      for (auto n : rb.nodes) {
        const auto op     = graph_util::type_op_of(n);
        auto       a      = n.attr(attrs::synth_policy);
        auto       p      = a.has() ? synth_attr::read(a.get()) : synth_attr::Policy{};
        const auto pinned = [&](std::string_view key) {
          const auto v = synth_attr::get(p, key);
          return !v.empty() && v != "auto";
        };
        add    |= auto_adder && !pinned("adder") && (op == Ntype_op::Sum || op == Ntype_op::LT || op == Ntype_op::GT);
        mult   |= auto_multiplier && !pinned("multiplier") && op == Ntype_op::Mult;
        barrel |= auto_barrel && !pinned("barrel") && (op == Ntype_op::SHL || op == Ntype_op::SRA);
      }
      std::vector<synth::Blast_options> candidates;
      if (mult) {
        for (auto kind : {synth::arith::Mult_kind::sn,
                          synth::arith::Mult_kind::csa,
                          synth::arith::Mult_kind::tree,
                          synth::arith::Mult_kind::array}) {
          auto o       = blast_options;
          o.multiplier = kind;
          candidates.push_back(o);
        }
      }
      if (add) {
        for (auto kind : {synth::arith::Adder_kind::rca,
                          synth::arith::Adder_kind::cska,
                          synth::arith::Adder_kind::cla,
                          synth::arith::Adder_kind::prefix,
                          synth::arith::Adder_kind::brent}) {
          auto o      = blast_options;
          o.adder     = kind;
          o.sum_adder = o.multiplier_adder = o.comparator_adder = kind;
          o.sum_adder_min_width = o.comparator_adder_min_width = 0;
          candidates.push_back(o);
        }
      }
      if (barrel) {
        auto o           = blast_options;
        o.reverse_barrel = !o.reverse_barrel;
        candidates.push_back(o);
      }
      const auto start    = std::chrono::steady_clock::now();
      uint32_t   attempts = 0;
      for (const auto& candidate : candidates) {
        if (attempts++ >= options.tune_attempts
            || (options.tune_time_ms
                && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()
                       >= static_cast<int64_t>(options.tune_time_ms))) {
          break;
        }
        const auto trial_start = std::chrono::steady_clock::now();
        auto       trial_blast = synth::blast_region(rb, candidate, hooks);
        if (trial_blast.status != synth::Region_blast::Status::blasted || !trial_blast.source_state) {
          continue;
        }
        Budget trial_work      = work.slice(work.remaining / 4), trial_search{credits};
        trial_search.admission = work.admission;
        auto       trial       = synthesize_stateful_region(trial_blast.lnet,
                                                *trial_blast.source_state,
                                                synth::State_target::cmos,
                                                options.logical,
                                                trial_work,
                                                trial_search);
        const auto used        = trial_work.consumed;
        work.absorb(trial_work);
        const bool keep = trial.region && trial.report.after.total() < selected.report.after.total();
        options.tune_store->history(std::format(
            "{{\"region\":{},\"candidate\":{},\"kept\":{},\"native_logic\":{},\"validation\":\"structural\",\"time_ms\":{}}}",
            synth_attr::quote(rb.module_name),
            encoded(candidate),
            keep,
            trial.report.after.total(),
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - trial_start).count()));
        if (keep) {
          selected      = std::move(trial);
          blast         = std::move(trial_blast);
          blast_options = candidate;
          credit        = trial_search.credit_floor();
          structural    = used;
          cached.key.clear();
        }
        if (work.resource_exhausted) {
          fail(Status::search_exhausted, "synthesis tune process admission");
          return;
        }
      }
      if (!candidates.empty()) {
        const auto row = std::format(
            "{{\"options\":{},\"validation\":\"structural\",\"native_logic\":{},\"region\":{},\"score_kind\":\"native_logic\"}}",
            encoded(blast_options),
            selected.report.after.total(),
            synth_attr::quote(rb.module_name));
        options.tune_store->put(tune_key, row);
        options.tune_store->applied(rb.module_name, row);
        output->tune_keys[rb.module_name] = tune_key;
        if (options.tune_validate == "region" && rb.pre_body && rb.pre_lib) {
          if (!output->proof_library.copy_from(*rb.pre_lib, rb.pre_name)) {
            fail(Status::invalid, "cannot preserve tuning proof reference");
            return;
          }
          output->tune_refs[rb.module_name] = rb.pre_name;
        }
        std::print("[synth.tune] mapper=usyn region={} selected={} native_logic={} validation=structural\n",
                   rb.module_name,
                   encoded(blast_options),
                   selected.report.after.total());
      }
    }
    // P2-C always starts from the freshly validated behavioral expansion, even
    // on a selection-cache hit. Its search is separate from cached selection,
    // but consumes the same design-wide remainder before later regions run.
    std::vector<Literal_rewrite> literal_plan;
    if (!options.literal_stats.empty() || options.literal_extract) {
      const auto line = literal_stats_region(rb,
                                             blast,
                                             selected.region->selected,
                                             options.logical.endpoint,
                                             options.literal_stats.empty() ? nullptr : options.literal_tmap.get(),
                                             options.literal_tmap_provider,
                                             options.literal_extract ? &literal_plan : nullptr,
                                             options.literal_extract);
      if (!options.literal_stats.empty()) {
        std::ofstream stats(options.literal_stats, std::ios::app);
        stats << line << "\n";
      }
    }
    Budget cmos_search{search_left};
    cmos_search.admission          = work.admission;
    cmos_search.admission_interval = work.admission_interval;
    const Cmos_cleanup cleanup{options.logical.residual,
                               &cmos_search,
                               options.sop_tree,
                               options.multi_rep,
                               options.cost_model.get(),
                               options.gate_objective};
    auto               emitted  = emit_logical_region(rb,
                                       blast,
                                       *selected.region,
                                       work,
                                       options.logical.max_nodes,
                                       options.cmos_cleanup || options.sop_tree || options.multi_rep ? &cleanup : nullptr,
                                       literal_plan);
    search_left                -= std::min(search_left, cmos_search.consumed);
    if (options.literal_extract && !literal_plan.empty()) {
      livehd::diag::info("pass.usyn", "literal-extract", "progress")
          .msg("{}: rebuilt {} next state(s) as literal network (depth {}) + template",
               rb.module_name,
               literal_plan.size(),
               options.literal_extract)
          .emit();
    }

    if (emitted.status != Status::feasible) {
      fail(emitted.status, rb.module_name + ": " + emitted.reason);
      return;
    }
    if (!mapping_policy.empty()) {
      for (auto node : rb.body->body().nodes()) {
        node.attr(attrs::synth_policy).set(synth_attr::write(mapping_policy));
      }
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
                               structural,
                               std::move(emitted.cmos_cleanup),
                               cmos_search.credit_floor(),
                               std::move(emitted.choices)});
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
                                                               options.tune_profile == "on" && options.tune_validate == "region",
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

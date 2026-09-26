// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The region driver (region_driver.hpp): options, caching, translation,
// read-back, parallel lanes and ware trials around a mapping backend. Lifted
// from pass.abc's former Mapper, which is now this driver plus the ABC backend.
#include "region_driver.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <print>
#include <thread>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

#include "absl/container/btree_map.h"
#include "cell.hpp"
#include "diag.hpp"
#include "hhds/attrs/srcid.hpp"
#include "host_mem.hpp"
#include "node_util.hpp"
#include "predict_abc_size.hpp"
#include "rapidjson/document.h"
#include "region_cache.hpp"
#include "synthesis_cost.hpp"
#include "worker_pool.hpp"

namespace livehd::synth {
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
      // A delay change below 0.1% of the path is estimation noise, not a
      // reason to trade area (dino: a +54 um2 barrel kept for 0.23 ps).
      const float tolerance = std::max(0.001f, 0.001f * std::abs(baseline.delays[i]));
      if (std::abs(candidate.delays[i] - baseline.delays[i]) > tolerance) {
        return candidate.delays[i] < baseline.delays[i];
      }
    }
  }
  return candidate.area < baseline.area - 1e-6;
}

Region_driver::Region_driver(const Driver_options& options, std::unique_ptr<Region_backend> backend)
    : backend_(std::move(backend)), startup_opts_(options), opts_(options) {}

// Workers first: their sessions are theirs to release.
Region_driver::~Region_driver() {
  parallel_drivers_.clear();
  backend_->stop();
}

// Releases only THIS driver's session. It must not cascade into the lanes:
// map_regions calls stop() to drop a serial session before a batch, and
// tearing the lanes' sessions down there would restart each once per batch.
// finish_parallel() (and, as a backstop, the destructor) owns the lanes.
void Region_driver::stop() { backend_->stop(); }

void Region_driver::finish_parallel() {
  for (auto& worker : parallel_drivers_) {
    worker->stop();
  }
}

bool Region_driver::backend_started() const { return backend_->started() || lanes_started_; }

double Region_driver::reg_margin_ps() const {
  if (opts_.reg_margin == "auto") {
    // The cell the netlist's registers become. Without one (register=false, or
    // a library with no plain DFF) the flops stay native and are mapped later
    // by whoever consumes the netlist -- their overhead is unknown here, so
    // none is assumed rather than a guess that would silently move every
    // region's budget.
    return dff_.has_value() ? dff_->clk_to_q_ps + dff_->setup_ps : 0.0;
  }
  char*        end = nullptr;
  const double v   = std::strtod(opts_.reg_margin.c_str(), &end);
  return (end != opts_.reg_margin.c_str() && *end == '\0' && v > 0.0) ? v : 0.0;
}

float Region_driver::region_budget(float target, bool has_flops) const {
  if (target <= 0.0f) {
    return target;
  }
  if (!has_flops) {
    return target;
  }
  return std::max(1.0f, target - static_cast<float>(reg_margin_ps()));
}

bool Region_driver::timing_requested() const {
  // A backend sets its timing model up ONCE, when its session starts, before
  // any later region maps: a delay target anywhere in the run-level options or
  // the region_opts must be visible here, not just `opts_.delay` for the
  // current region.
  if (!startup_opts_.delay.empty() || !opts_.delay.empty()) {
    return true;
  }
  const auto timed = [](const Region_opts_map& map) {
    return std::any_of(map.begin(), map.end(), [](const auto& kv) {
      return kv.second.delay.has_value() && !kv.second.delay->empty();
    });
  };
  if (timed(region_opts_cli_)) {
    return true;
  }
  return std::any_of(graph_region_opts_.begin(), graph_region_opts_.end(), [&](const auto& kv) { return timed(kv.second); });
}

void Region_driver::ensure_dff_cells() {
  // Register mapping target: scan the Liberty for a plain posedge D-flop (ABC's
  // read_lib already dropped it, so this is a separate text scan). A missing DFF
  // cell is not fatal — the read-back keeps flops native (the same shape as
  // register=false) so the netlist stays correct, just not fully cell-mapped.
  if (!startup_opts_.map_register || dff_preset_) {
    return;
  }
  auto sel    = liberty::resolve_dff_cells(startup_opts_.library, startup_opts_.dff_cell);
  dff_              = sel.base;
  dff_ladder_       = sel.ladder;
  areset_ladder_[0] = sel.areset_ladder[0];
  areset_ladder_[1] = sel.areset_ladder[1];
  if (dff_.has_value() && dff_ladder_.empty()) {
    dff_ladder_.push_back(*dff_);  // a ladder always has its base rung
  }
  dff_preset_ = true;  // resolved once; the pick is constant for the run
}

void Region_driver::report_register_cell() const {
  if (startup_opts_.map_register) {
    if (dff_.has_value() && startup_opts_.verbose) {
      std::string rungs;
      for (const auto& c : dff_ladder_) {
        rungs += std::format("{}{} ({:.4f})", rungs.empty() ? "" : ", ", c.name, c.area);
      }
      std::print(
          "[pass.abc] register cell: {} (d={}, clk={}, {}={}{}); drive ladder: {}; overhead clk->Q {:.1f} + setup {:.1f} ps "
          "(reg_margin={} -> {:.1f} ps)\n",
          dff_->name,
          dff_->d_pin,
          dff_->clk_pin,
          dff_->q_inverted ? "qn" : "q",
          dff_->q_pin,
          dff_->q_inverted ? ", output inverted" : "",
          rungs,
          dff_->clk_to_q_ps,
          dff_->setup_ps,
          startup_opts_.reg_margin,
          reg_margin_ps(),
          opts_.reverse_barrel);
    }
    if (!dff_.has_value()) {
      livehd::diag::warn("pass.abc", "no-dff-cell", "unsupported")
          .msg("pass.abc register=true: no {} in '{}' — keeping flops native (no DFF-cell mapping)",
               startup_opts_.dff_cell.empty() ? "plain posedge D-flop cell" : std::format("cell '{}'", startup_opts_.dff_cell),
               startup_opts_.library)
          .emit();
    }
  }
}

namespace {

// One override entry {"flow":…,"delay":…,"load":…,"adder":…,"block_size":…,
// "multiplier":…} -> Region_opts. Unknown keys / bad values are hard errors:
// a mistyped agent hint must never silently no-op (2opt-freq contract).
bool parse_region_opts_entry(const rapidjson::Value& v, Region_opts& ro, std::string_view where, std::string_view color_key) {
  auto bad = [&](std::string_view what) {
    livehd::diag::err("pass.abc", "region-opts", "io").msg("{}: region_opts[\"{}\"]: {}", where, color_key, what).fatal();
    return false;
  };
  if (!v.IsObject()) {
    return bad("entry must be an object of per-region options");
  }
  for (const auto& mem : v.GetObject()) {
    const std::string_view key{mem.name.GetString(), mem.name.GetStringLength()};
    const auto&            val = mem.value;
    if (key == "ware") {
      if (!val.IsBool()) {
        return bad("'ware' must be true or false");
      }
      ro.ware = val.GetBool();
    } else if (key == "flow" || key == "delay" || key == "load") {
      if (!val.IsString()) {
        return bad(std::format("'{}' must be a string", key));
      }
      std::string s{val.GetString(), val.GetStringLength()};
      if (key == "flow") {
        ro.flow = std::move(s);
      } else if (key == "delay") {
        ro.delay = std::move(s);
      } else {
        ro.load = std::move(s);
      }
    } else if (key == "adder") {
      if (!val.IsString()) {
        return bad("'adder' must be a string (rca|cska|cla)");
      }
      auto a = arith::parse_adder_kind({val.GetString(), val.GetStringLength()});
      if (!a.has_value()) {
        return bad(std::format("unknown adder '{}' (use rca|cska|cla)", val.GetString()));
      }
      ro.adder = a.value();
    } else if (key == "barrel") {
      if (!val.IsString() || (std::string_view(val.GetString()) != "log" && std::string_view(val.GetString()) != "reverse")) {
        return bad("barrel must be log|reverse");
      }
      ro.reverse_barrel = std::string_view(val.GetString()) == "reverse";
    } else if (key == "multiplier") {
      if (!val.IsString()) {
        return bad("'multiplier' must be a string (array|tree)");
      }
      auto m = arith::parse_mult_kind({val.GetString(), val.GetStringLength()});
      if (!m.has_value()) {
        // No `auto` here, unlike the pass option: a region override IS the
        // explicit pick (leaving the key out is what keeps the search on), and
        // parse_mult_kind rejects the spelling -- advertising it made the
        // message name a value this very call refuses.
        return bad(std::format("unknown multiplier '{}' (use array|tree)", val.GetString()));
      }
      ro.multiplier = m.value();
    } else if (key == "block_size") {
      if (!val.IsInt() || val.GetInt() < 0) {
        return bad("'block_size' must be a non-negative integer");
      }
      ro.block_size = val.GetInt();
    } else {
      return bad(std::format("unknown option '{}' (use flow|delay|load|adder|block_size|multiplier|barrel|ware)", key));
    }
  }
  return true;
}

bool parse_region_opts_object(const rapidjson::Value& obj, Region_opts_map& out, std::string_view where) {
  if (!obj.IsObject()) {
    livehd::diag::err("pass.abc", "region-opts", "io")
        .msg("{}: region_opts must be a JSON object keyed by color id", where)
        .fatal();
    return false;
  }
  for (const auto& mem : obj.GetObject()) {
    const std::string_view key{mem.name.GetString(), mem.name.GetStringLength()};
    int                    color = 0;
    const auto*            b     = key.data();
    const auto*            e     = key.data() + key.size();
    auto [p, ec]                 = std::from_chars(b, e, color);
    if (ec != std::errc{} || p != e || color < 0) {
      livehd::diag::err("pass.abc", "region-opts", "io")
          .msg("{}: region_opts key '{}' is not a color id (non-negative integer)", where, key)
          .fatal();
      return false;
    }
    Region_opts ro;
    if (!parse_region_opts_entry(mem.value, ro, where, key)) {
      return false;
    }
    out[color] = std::move(ro);
  }
  return true;
}

}  // namespace

std::optional<Region_opts_map> parse_region_opts(std::string_view json, std::string_view where) {
  rapidjson::Document d;
  d.Parse(json.data(), json.size());
  if (d.HasParseError()) {
    livehd::diag::err("pass.abc", "region-opts", "io")
        .msg("{}: region_opts is not valid JSON (offset {})", where, d.GetErrorOffset())
        .fatal();
    return std::nullopt;
  }
  Region_opts_map m;
  if (!parse_region_opts_object(d, m, where)) {
    return std::nullopt;
  }
  return m;
}

void Region_driver::prepare_region_opts(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  for (const auto& g : graphs) {
    if (!g || graph_region_opts_.contains(g.get())) {
      continue;
    }
    graph_ware_policy_.emplace(g.get(),
                               ware_policy(*g, {startup_opts_.ware_arith, startup_opts_.ware_cmp, startup_opts_.ware_shift}));
    Region_opts_map options;
    if (auto info = g->get_input_node().attr(livehd::attrs::coloring_info); info.has()) {
      rapidjson::Document doc;
      const std::string   json{info.get()};
      doc.Parse(json.data(), json.size());
      if (!doc.HasParseError() && doc.IsObject()) {
        if (auto it = doc.FindMember("region_opts"); it != doc.MemberEnd()) {
          parse_region_opts_object(it->value, options, "coloring_info");
        }
      }
    }
    graph_region_opts_.emplace(g.get(), std::move(options));
  }
}

bool Region_driver::apply_region_overrides(const livehd::partition::Region_body& rb, Backend_overrides& overrides) {
  auto policy_it = graph_ware_policy_.find(rb.src);
  if (policy_it == graph_ware_policy_.end()) {
    policy_it
        = graph_ware_policy_.emplace(rb.src, ware_policy(*rb.src, {opts_.ware_arith, opts_.ware_cmp, opts_.ware_shift})).first;
  }
  const auto policy    = policy_it->second;
  opts_.ware_arith     = policy.arith;
  opts_.ware_cmp       = policy.cmp;
  opts_.ware_shift     = policy.shift;
  bool flow_overridden = false;
  auto apply           = [&](const Region_opts& ro, std::string_view src) {
    if (ro.ware.has_value()) {
      opts_.ware = *ro.ware;
    }
    if (ro.flow.has_value()) {
      overrides.flow  = *ro.flow;
      flow_overridden = !ro.flow->empty();
    }
    if (ro.delay.has_value()) {
      opts_.delay = *ro.delay;
    }
    if (ro.load.has_value()) {
      overrides.load = *ro.load;
    }
    if (ro.adder.has_value()) {
      opts_.adder      = *ro.adder;
      opts_.auto_adder = false;
    }
    if (ro.block_size.has_value()) {
      opts_.block_size = *ro.block_size;
      opts_.auto_adder = false;
    }
    if (ro.reverse_barrel.has_value()) {
      opts_.reverse_barrel = *ro.reverse_barrel;
      opts_.auto_barrel    = false;
    }
    if (ro.multiplier.has_value()) {
      opts_.multiplier      = *ro.multiplier;
      opts_.auto_multiplier = false;
      opts_.auto_adder      = false;
    }
    std::print("[pass.abc] region '{}': color {} options override applied ({})\n", rb.module_name, rb.color, src);
  };

  // Graph-embedded overrides first (the block-attribute channel writes a
  // "region_opts" member into coloring_info), CLI second so --set wins.
  auto git = graph_region_opts_.find(rb.src);
  if (git == graph_region_opts_.end()) {
    Region_opts_map m;
    if (auto a = rb.src->get_input_node().attr(livehd::attrs::coloring_info); a.has()) {
      const std::string   info{a.get()};
      rapidjson::Document d;
      d.Parse(info.data(), info.size());
      if (!d.HasParseError() && d.IsObject()) {
        if (auto ro = d.FindMember("region_opts"); ro != d.MemberEnd()) {
          parse_region_opts_object(ro->value, m, "coloring_info");  // diag on malformed, best-effort continue
        }
      }
    }
    git = graph_region_opts_.emplace(rb.src, std::move(m)).first;
  }
  if (auto it = git->second.find(rb.color); it != git->second.end()) {
    apply(it->second, "coloring_info");
  }
  if (auto it = region_opts_cli_.find(rb.color); it != region_opts_cli_.end()) {
    apply(it->second, "--set region_opts");
  }
  return flow_overridden;
}

namespace {
// Resolve the original source "file:line" of region output `po` into q.crit_*
// (2opt-freq A). Best-effort: a missing srcid or an unresolvable span just
// leaves crit_src empty — the QoR row is still useful without provenance.
void qor_src_of_output(const livehd::partition::Region_body& rb, size_t po, Region_qor& q) {
  q.crit_output = rb.outputs[po].name;
  auto drv      = rb.outputs[po].src_driver;
  if (drv.is_invalid()) {
    return;
  }
  auto onode = drv.get_master_node();
  if (onode.is_invalid()) {
    return;
  }
  auto span = node_span(rb, onode);
  if (!span.file.empty() && span.start_line.has_value()) {
    q.crit_src = span.file + ":" + std::to_string(*span.start_line);
  }
}

}  // namespace

// Memory admission (2opt-incr subtask 0). Deliberately MEASURED, not predicted:
// a static op/width model cannot see the phase that actually blows up. The peak
// is inside Cmd_CommandExecute's strash/&dch/&nf, which hold several network
// forms at once -- and an external calibration of "bytes per gate" is not even
// well defined here, because ABC's read_lib fixed cost dominates small designs
// (measured: a 96-gate region and a 4640-gate region had comparable RSS).
// What IS reliable is our own RSS while we translate.
//
// Projection: RSS grows roughly linearly in nodes blasted, so
//   projected_translation = rss_before + growth_so_far * total / blasted
// The projection can reject a clearly hopeless translation early, while the
// repeated exact samples and process backstop police later ABC network forms.
bool Region_driver::over_budget(std::string_view region, uint64_t rss_before, size_t blasted, size_t total, uint64_t pending_bytes) {
  const uint64_t budget = cost::budget_bytes(opts_.memory_budget_mb);
  if (budget == 0 || blasted == 0) {
    return false;  // unknown host and no explicit budget: unenforceable, do not gate
  }
  // Sample phys_footprint, not resident_size: it is the metric macOS jetsam
  // charges (and it counts compressed/paged pages resident_size drops under
  // pressure), so it is the number that actually decides whether we get killed.
  // NB it is equally STICKY after free() -- freed pages linger until the
  // allocator returns them. Subtract the color-entry baseline so a long run is
  // not refused merely because completed mapped modules remain in its output
  // library; this is still conservative within one color.
  const uint64_t sampled = cost::process_footprint_bytes();
  if (sampled == 0) {
    return false;
  }
  const uint64_t rss = sampled + pending_bytes;

  // RSS at the first sample moves in large allocator/startup steps. Multiplying
  // that extrapolation by a guessed post-translation ABC factor produced severe
  // false positives (ROB: 8.3 GiB measured, 363 GiB predicted at a 16 GiB
  // ceiling). Use translation growth itself, require a large signal, and refuse
  // early only when even the projection clears the budget by a wide margin.
  // The exact live-RSS check remains the resource guarantee.
  constexpr uint64_t kMinGrowthToProject = uint64_t{256} << 20;
  constexpr uint64_t kProjectionMargin   = 4;

  const uint64_t grown            = rss > rss_before ? rss - rss_before : 0;
  const double   fraction         = static_cast<double>(blasted) / static_cast<double>(total);
  const uint64_t projected_growth = static_cast<uint64_t>(static_cast<double>(grown) / fraction);

  // The exact reading is the guarantee; the projection is only allowed to make a
  // hopeless region die sooner.
  //
  // TWO ceilings, each measured against its OWN number. Per-color GROWTH is what
  // tells a single oversize region from a merely large run, but growth alone has
  // no ceiling on the accumulated footprint: process_footprint_bytes() is sticky
  // after free (host_mem.hpp), so the allocator-pressure relief at the color
  // boundary is part of the resource guarantee. Without it, a many-color run
  // whose colors each add well under the budget can still walk the process into
  // the address-space limit lhd_main armed as RLIMIT_AS -- and ABC does not
  // null-check its allocations, so that lands as a bare SIGSEGV with no
  // diagnostic and no qor.json.
  //
  // The absolute test must NOT reuse `budget`: pass.abc.memory_budget_mb is
  // documented and used as a per-color GROWTH knob, and lhd's own ~23 MiB
  // baseline already exceeds a modestly pinned one, so measuring TOTAL rss
  // against it would refuse the first region of every such run. Measure it
  // against the process-wide PHYSICAL budget instead. On Darwin RLIMIT_AS has
  // separate VA-only allocator headroom (host_mem.cpp); admission must not count
  // that as physical capacity. A zero budget still means "unenforceable".
  uint64_t total_ceiling = cost::configured_budget_bytes();
  if (coordinator_ && coordinator_->parallel_stats_.memory_limit != 0) {
    total_ceiling = total_ceiling ? std::min(total_ceiling, coordinator_->parallel_stats_.memory_limit)
                                  : coordinator_->parallel_stats_.memory_limit;
  }
  // Concurrent colors share the process reading; their growth cannot be
  // attributed to one region. Keep the aggregate allowance under the absolute
  // process ceiling above.
  const uint64_t growth_budget = gu::sat_mul(budget, coordinator_ ? coordinator_->parallel_stats_.limit : 1U);
  const bool     over_growth   = grown > growth_budget;
  const bool     over_total    = total_ceiling != 0 && rss > total_ceiling;
  const bool     over_now      = over_growth || over_total;
  const bool     over_projected
      = coordinator_ == nullptr && grown >= kMinGrowthToProject && projected_growth / kProjectionMargin > budget;
  if (!over_now && !over_projected) {
    return false;
  }

  const auto        mib = [](uint64_t b) { return b >> 20; };
  // Name the ceiling the refusal actually came from, or the message describes a
  // derivation that never happened. An explicit memory_budget_mb is taken
  // verbatim and no reserve is subtracted, so quoting a reserve there would be
  // a second such invention.
  const std::string budget_desc
      = coordinator_
            ? (over_total ? std::format("parallel process physical-memory ceiling {} MiB (half of physical RAM or the smaller "
                                        "configured process budget)",
                                        mib(total_ceiling))
                          : std::format("aggregate worker growth budget {} MiB ({} workers; process-wide growth since color entry)",
                                        mib(growth_budget),
                                        coordinator_->parallel_stats_.limit))
        : (!over_growth && over_total)
            ? std::format(
                  "process physical-memory ceiling {} MiB (LIVEHD_MEMORY_BUDGET_MB, else physical minus reserve; Darwin "
                  "RLIMIT_AS has separate VA-only allocator headroom) -- the whole-PROCESS footprint, not this color's growth",
                  mib(total_ceiling))
        : opts_.memory_budget_mb > 0 ? std::format("per-color growth budget {} MiB (pass.abc.memory_budget_mb)", mib(budget))
                                     : std::format("per-color growth budget {} MiB (physical {} MiB minus a {} MiB reserve)",
                                                   mib(budget),
                                                   mib(cost::physical_ram_bytes()),
                                                   mib(cost::reserve_bytes()));
  // Once earlier colors exist to blame, say so: their retained memory is the
  // cost, and the caller's stock `pass.color.synth.max_gate=<smaller>` hint is then the
  // wrong advice.
  const std::string cause = (coordinator_ || over_growth || qor_.empty())
                                ? std::string{}
                                : std::format(" (after {} completed color(s), whose retained memory is the cost)", qor_.size());
  refusal_                = std::format(
      "region '{}' does not fit in memory: {} of {} node(s) translated ({:.0f}%), RSS {} MiB{} "
      "(was {} MiB, {} added {} MiB){}, {}{}",
      region,
      blasted,
      total,
      100.0 * fraction,
      mib(rss),
      pending_bytes ? std::format(" (incl. {} MiB estimated for the ABC netlist not yet built)", mib(pending_bytes)) : std::string{},
      mib(rss_before),
      coordinator_ ? "process" : "color",
      mib(grown),
      over_now ? std::string{} : std::format(", projected color growth {} MiB", mib(projected_growth)),
      budget_desc,
      cause);
  return true;
}

namespace {
uint64_t parallel_memory_limit() {
  const auto half       = cost::physical_ram_bytes() / 2;
  const auto configured = cost::configured_budget_bytes();
  return configured && half ? std::min(half, configured) : half;
}
}  // namespace

void Region_driver::map_regions(std::span<const livehd::partition::Region_body> regions) {
  if (!refusal_.empty() || !time_refusal_.empty()) {
    return;
  }
  const auto     ware_begin    = ware_regions_.size();
  const unsigned limit         = synthesis_thread_limit(opts_.threads, std::thread::hardware_concurrency());
  parallel_stats_.limit        = limit;
  parallel_stats_.memory_limit = parallel_memory_limit();
  if (limit == 1 || regions.size() < 2 || parallel_stats_.memory_limit == 0) {
    for (const auto& rb : regions) {
      map_region(rb);
    }
    return;
  }

  // The partitioner owns every span/pre-body until this synchronous batch
  // returns. Workers share graph storage only while holding graph_mutex_.
  backend_->stop();  // release THIS driver's serial session; the lanes keep theirs
  const uint64_t        baseline = cost::process_footprint_bytes();
  std::vector<uint64_t> projections;
  for (const auto& rb : regions) {
    uint64_t aig = 0;
    for (const auto& node : rb.nodes) {
      aig = gu::sat_add(aig, gu::predict_abc_size(node));
    }
    projections.push_back(backend_->projected_memory(aig));
  }
  const uint32_t first_id  = next_region_id_;
  next_region_id_         += static_cast<uint32_t>(regions.size());
  struct Lane {
    Region_driver*       driver = nullptr;
    // NOT std::async/std::thread: Darwin gives a secondary thread 512 KiB and
    // both ABC's recursive DFS/mapping helpers and the HHDS read-back walk
    // overflow that in -c dbg (see core/worker_pool.hpp kWorkerStackBytes).
    livehd::Async_worker result;
    size_t               job        = 0;
    uint64_t             projection = 0;
  };
  std::vector<Lane> lanes(std::min<size_t>(limit, regions.size()));
  for (size_t i = 0; i < lanes.size() && i < parallel_drivers_.size(); ++i) {
    lanes[i].driver = parallel_drivers_[i].get();
  }
  std::vector<std::optional<Region_qor>> results(regions.size());
  std::exception_ptr                     failure;
  size_t                                 next     = 0;
  unsigned                               active   = 0;
  uint64_t                               reserved = 0;
  std::mutex                             done_mutex;
  std::condition_variable                done;
  // A refusal is a ROOT CAUSE, so the one the run reports must be the FIRST
  // region's in partition order -- not whichever lane happened to finish first.
  size_t                                 refusal_job = regions.size();
  std::string                            first_refusal, first_time_refusal;
  auto                                   note_refusal = [&](size_t job, const std::string& refusal, const std::string& timeout) {
    if ((refusal.empty() && timeout.empty()) || job >= refusal_job) {
      return;
    }
    refusal_job        = job;
    first_refusal      = refusal;
    first_time_refusal = timeout;
  };
  // Even a failed thread creation must join existing jobs before their captured
  // result buffers, condition variable and region views are destroyed.
  struct Drain {
    std::vector<Lane>& lanes;
    ~Drain() {
      for (auto& lane : lanes) {
        lane.result.join();
      }
    }
  } drain{lanes};

  auto harvest = [&] {
    for (auto& lane : lanes) {
      if (!lane.result.ready()) {
        continue;
      }
      try {
        lane.result.get();
      } catch (...) {
        if (!failure) {
          failure = std::current_exception();
        }
      }
      --active;
      reserved -= lane.projection;
      note_refusal(lane.job, lane.driver->refusal_, lane.driver->time_refusal_);
    }
  };

  // Nothing is running and no worker fits: there is no allocation to wait for,
  // so map this region on the caller's thread under the existing per-color and
  // process memory limits instead of spinning on the admission gate.
  auto map_serially = [&](size_t job) {
    const auto saved_id = next_region_id_;
    next_region_id_     = first_id + static_cast<uint32_t>(job);
    const auto n        = qor_.size();
    map_region(regions[job]);
    if (qor_.size() != n) {
      results[job] = std::move(qor_.back());
      qor_.pop_back();
    }
    next_region_id_ = saved_id;
    note_refusal(job, refusal_, time_refusal_);
  };

  while (next < regions.size() || active != 0) {
    harvest();
    if (failure || refusal_job != regions.size()) {
      next = regions.size();  // drain running work without launching more
    }
    bool launched = false;
    if (next < regions.size()) {
      for (auto& lane : lanes) {
        if (lane.result.valid()) {
          continue;
        }
        const uint64_t actual     = cost::process_footprint_bytes();
        const auto     projection = projections[next];
        if (!admit_abc_worker(parallel_stats_.memory_limit, actual, baseline, reserved, projection)) {
          ++parallel_stats_.memory_waits;
          if (active == 0) {
            map_serially(next++);
            launched = true;
          }
          break;
        }
        if (!lane.driver) {
          std::lock_guard lock(graph_mutex_);
          auto            worker     = std::make_unique<Region_driver>(startup_opts_, backend_->lane());
          worker->coordinator_       = this;
          worker->outlib_            = outlib_;
          worker->flat_              = flat_;
          worker->incr_              = incr_;
          worker->region_opts_cli_   = region_opts_cli_;
          worker->graph_region_opts_ = graph_region_opts_;
          worker->graph_ware_policy_ = graph_ware_policy_;
          worker->dff_               = dff_;
          worker->dff_ladder_        = dff_ladder_;
          worker->areset_ladder_[0]  = areset_ladder_[0];
          worker->areset_ladder_[1]  = areset_ladder_[1];
          worker->dff_preset_        = dff_preset_;
          lane.driver                = worker.get();
          parallel_drivers_.push_back(std::move(worker));
        }
        // Creating the worker's graph state may have waited for translation
        // under graph_mutex_. Recheck actual memory immediately before launch.
        if (!admit_abc_worker(parallel_stats_.memory_limit, cost::process_footprint_bytes(), baseline, reserved, projection)) {
          ++parallel_stats_.memory_waits;
          if (active == 0) {
            map_serially(next++);
            launched = true;
          }
          break;
        }
        // Nothing is committed until the thread actually starts: a failed
        // pthread_create must not leave a phantom reservation behind.
        lane.job                     = next;
        lane.projection              = projection;
        lane.driver->next_region_id_ = first_id + static_cast<uint32_t>(lane.job);
        lane.result.start([&, worker = lane.driver, job = lane.job] {
          struct Notify {
            std::mutex&              mu;
            std::condition_variable& cv;
            ~Notify() {
              {
                const std::lock_guard lock(mu);  // close the lost-wakeup window
              }
              cv.notify_one();
            }
          } notify{done_mutex, done};
          worker->map_region(regions[job]);
          if (!worker->qor_.empty()) {
            results[job] = std::move(worker->qor_.back());
            worker->qor_.clear();
          }
        });
        ++next;
        reserved += projection;
        ++active;
        parallel_stats_.peak_workers = std::max(parallel_stats_.peak_workers, active);
        launched                     = true;
        break;  // resample actual memory before each additional admission
      }
    }
    if (!launched && active != 0) {
      std::unique_lock lock(done_mutex);
      done.wait_for(lock, std::chrono::milliseconds(20));
    }
  }

  // All workers are joined: publish in partition order, independent of timing.
  // No ABC-owned pointer crosses sessions. Boundary refinement gets its own
  // session; only graph snapshots, options and plain QoR values are merged.
  for (auto& lane : lanes) {
    if (!lane.driver) {
      continue;
    }
    auto& worker    = *lane.driver;
    lanes_started_ |= worker.backend_->started();
    region_delay_targets_.insert(worker.region_delay_targets_.begin(), worker.region_delay_targets_.end());
    worker.region_delay_targets_.clear();
  }
  if (refusal_job != regions.size()) {
    refusal_      = first_refusal;  // the lowest-index region's, whoever mapped it
    time_refusal_ = first_time_refusal;
  }
  for (auto& row : results) {
    if (row) {
      qor_.push_back(std::move(*row));
    }
  }
  const auto order = [&](std::string_view module) {
    return std::find_if(regions.begin(), regions.end(), [&](const auto& rb) { return rb.module_name == module; }) - regions.begin();
  };
  std::stable_sort(ware_regions_.begin() + static_cast<std::ptrdiff_t>(ware_begin),
                   ware_regions_.end(),
                   [&](const auto& a, const auto& b) { return order(a.rb.module_name) < order(b.rb.module_name); });
  if (failure) {
    std::rethrow_exception(failure);
  }
}

void Region_driver::map_region(const livehd::partition::Region_body& rb) {
  std::unique_lock graph_lock(coordinator_ ? coordinator_->graph_mutex_ : graph_mutex_);
  if (!coordinator_) {
    parallel_stats_.limit        = synthesis_thread_limit(opts_.threads, std::thread::hardware_concurrency());
    parallel_stats_.memory_limit = parallel_memory_limit();
  }
  const auto backend_scope = backend_->region_scope();  // whatever the backend enters, it leaves
  // A refusal already happened: work() will make it fatal once the backend is
  // torn down, so translating the remaining regions can only burn time and
  // overwrite the FIRST refusal -- the one that is the actual root cause.
  if (!refusal_.empty() || !time_refusal_.empty()) {
    return;
  }

  // Source excerpts for this region's diagnostics. The srcids stamped on the
  // region's nodes resolve through the SOURCE graph's locator (which chains to
  // the library's shared srcmap), so every refusal below can print the original
  // Pyrope/Verilog line, not just a node id. Restored on scope exit.
  livehd::diag::Locator_scope diag_scope(rb.src != nullptr ? &rb.src->source_locator() : nullptr);

  // Per-region wall time: the only way to tell a cache that hits a lot from a
  // cache that saves time. A hit on a 200ms region and a miss on a 200s one
  // count the same in hits/misses and nothing alike in the total.
  const auto t_start = std::chrono::steady_clock::now();
  const auto since
      = [&t_start] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_start).count(); };
  const uint64_t process_peak_before = cost::process_peak_rss_bytes();
  uint64_t       sampled_peak_rss    = cost::process_footprint_bytes();
  // The ACCOUNTING baseline for color_peak_rss_kb: this region's footprint on
  // entry, captured unconditionally. Deliberately not the admission baseline
  // `rss_before` below, which is pinned to 0 under allow_oversize -- reusing it
  // made every color report the whole sticky process peak as its own growth,
  // exactly in the large-design mode the per-color number exists to explain.
  const uint64_t rss_entry           = sampled_peak_rss;
  const auto     trace_stage         = [&](std::string_view stage) {
    sampled_peak_rss = std::max(sampled_peak_rss, cost::process_footprint_bytes());
    if (!opts_.verbose) {
      return;
    }
    std::print("[pass.abc] region '{}': stage {} at {:.0f} ms\n", rb.module_name, stage, since());
    std::fflush(stdout);
  };

  // Reporting metadata belongs to the freshly emitted mapped library, not to
  // the persistent cache row. Stamp it once on the graph input on every exit
  // path (including a cache hit and the native-wiring fast path), after
  // reuse_hit may have replaced the complete body. Do not duplicate the region
  // string on every mapped cell; physical flattening propagates the compact id
  // only into its transient scratch nodes for OpenTimer.
  bool           resynthesized = incr_ == nullptr;
  const uint32_t region_id     = next_region_id_++;
  struct Region_report_stamp {
    const livehd::partition::Region_body* rb;
    uint32_t                              region_id;
    const bool*                           resynthesized;
    ~Region_report_stamp() {
      auto input = rb->body->get_input_node();
      input.attr(livehd::attrs::synth_region).set(rb->module_name);
      input.attr(livehd::attrs::synth_region_id).set(region_id);
      input.attr(livehd::attrs::color).set(rb->color);
      if (*resynthesized) {
        input.attr(livehd::attrs::resynth).set({});
      } else {
        input.attr(livehd::attrs::resynth).del();
      }
    }
  } report_stamp{&rb, region_id, &resynthesized};

  // Per-region options are temporary (every helper below reads opts_) and are
  // restored on every exit path.
  const Driver_options saved_opts = opts_;
  struct Opts_restore {
    Driver_options*       dst;
    const Driver_options* src;
    ~Opts_restore() { *dst = *src; }
  } opts_restore{&opts_, &saved_opts};
  // Structural input size belongs in every QoR row, including cache hits. It
  // makes recipe/runtime changes explainable without relying on module names:
  // mapped gates are only known after ABC and can move with the very recipe
  // being compared, while these two values describe the invariant input cone.
  const uint64_t input_nodes = rb.nodes.size();
  uint64_t       input_ge    = 0;
  uint64_t       pred_aig    = 0;
  for (const auto& node : rb.nodes) {
    input_ge += gu::synthesis_ge_weight(node);
    pred_aig  = gu::sat_add(pred_aig, gu::predict_abc_size(node));
  }

  uint64_t register_bits = 0;
  for (const auto& node : rb.nodes) {
    if (!gu::is_type_flop(node)) {
      continue;
    }
    const int bits  = gu::bits_of(node.create_driver_pin(0));
    register_bits  += static_cast<uint64_t>(std::max(bits, 1)) * pipeline_depth(node);
  }
  // Off by default (register_max_bits=0): every flop maps, as yosys does. The
  // old 4096-bit default was tripped by a single bit-blasted 64x64 memory
  // (mem_lower puts the entry flops in the memory's region) and silently
  // handed those flops to lhdtrack's yosys normalize instead of pass.abc. A
  // diag, not a print: the decision changes the netlist's cell mix and has to
  // land in the diagnostics stream next to memory-max-bits, where a QoR
  // reader looks for "why is this register native".
  if (opts_.map_register && opts_.register_max_bits != 0 && register_bits > opts_.register_max_bits) {
    opts_.map_register = false;
    livehd::diag::info("pass.abc", "register-kept-native", "unsupported")
        .msg(
            "pass.abc region '{}': keeping {} register bits native (above register_max_bits={}); the data cones are still "
            "mapped",
            rb.module_name,
            register_bits,
            opts_.register_max_bits)
        .emit();
  }

  // The backend's own command-string overrides of this region (region_opts
  // `flow`/`load`); a ware trial reuses the ones recorded with the region.
  Backend_overrides overrides = ware_trial_ ? trial_overrides_ : Backend_overrides{};
  if (!ware_trial_) {
    (void)apply_region_overrides(rb, overrides);
  }
  region_delay_targets_[rb.module_name] = ware_delay_target(opts_.delay);
  // The region's delay BUDGET: the target minus the register margin when the
  // region holds flops (mapped or native -- a native flop is mapped to the
  // same cell by whoever times the netlist, so its overhead is on the path
  // either way). Spelled into `{B}` before ANY flow string of this region is
  // resolved: the built-in tails' `dnsize`/`upsize` take it as `-D`, and the
  // recipe below therefore carries it verbatim. ABC's `-D` is an integer
  // (atoi), so the budget is floored to whole picoseconds and the ladder below
  // compares against the same value it sized to.
  float delay_target                    = 0.0f;
  {
    char*       end = nullptr;
    const float t   = std::strtof(opts_.delay.c_str(), &end);
    if (!opts_.delay.empty() && end != opts_.delay.c_str() && *end == '\0' && t > 0.0f) {
      delay_target = t;
    }
  }
  ensure_dff_cells();  // the auto margin needs the DFF pick (the backend starts lazily, below the cache lookup)
  const float budget = delay_target > 0.0f ? std::floor(region_budget(delay_target, register_bits > 0)) : -1.0f;

  Region_ctx ctx{rb, opts_};
  ctx.flow             = overrides.flow;
  ctx.load             = overrides.load;
  ctx.ware_trial       = ware_trial_;
  ctx.timing_requested = timing_requested();
  ctx.input_ge         = input_ge;
  ctx.budget           = budget;
  ctx.margin_ps        = reg_margin_ps();
  ctx.rss_entry        = rss_entry;
  ctx.trace_stage      = trace_stage;
  ctx.elapsed_ms       = since;
  const auto plan      = backend_->plan(ctx);
  if (opts_.verbose) {
    uint64_t input_bits  = 0;
    uint64_t output_bits = 0;
    for (const auto& port : rb.inputs) {
      input_bits += static_cast<uint64_t>(std::max(port.bits, 1));
    }
    for (const auto& port : rb.outputs) {
      output_bits += static_cast<uint64_t>(std::max(port.bits, 1));
    }
    std::print("[pass.abc] mapping region '{}': {} nodes, {} GE, {} register bits\n",
               rb.module_name,
               input_nodes,
               input_ge,
               register_bits);
    std::print("[pass.abc] region '{}': {} input port(s)/{} bits, {} output port(s)/{} bits\n",
               rb.module_name,
               rb.inputs.size(),
               input_bits,
               rb.outputs.size(),
               output_bits);
    std::fflush(stdout);
  }

  // Incremental reuse (2opt-incr A+C), lgraph-compare edition: the PARTITIONER
  // rebuilt the region's pre-ABC logic into a throwaway lib (rb.pre_body, via the
  // SAME build_module construction the classic path uses -- a byte-stable
  // compare artifact, unlike a hand re-derivation which drifts). Structurally
  // compare it (plus the resolved recipe) against the cache, and on a match
  // REPLACE this region's body with the cached mapped netlist IN PLACE -- ABC
  // never starts. `recipe`/`pre_g` live to the store site below (a miss
  // snapshots them). Null when reuse-ineligible or flattening (uncacheable).
  // The effective per-region state mode is part of the cache recipe. The comb
  // and seq command strings can be identical, but their read-back semantics are
  // not: one carries flops through ABC and the other preserves native state.
  if (!ware_trial_) {
    // One ware list per run: a worker records into the coordinator (still under
    // graph_lock, so the appends stay serialized) with ITS region's options.
    (coordinator_ ? *coordinator_ : *this).remember_ware(rb, opts_, overrides);
  }
  // satopt's per-bit mux facts are already applied to rb.src as an LGraph
  // rewrite (optimize_muxes, before partitioning), so the region body itself
  // carries them: nothing fact-specific reaches the blaster or the cache key.
  // The backend's recipe for this region, and the hook's, verbatim in the
  // cache key.
  const std::string recipe = opts_.region_hook ? std::format("{}|hook={}", plan.recipe, opts_.region_hook_recipe) : plan.recipe;
  hhds::Graph*      pre_g  = (incr_ != nullptr && rb.reuse_eligible) ? rb.pre_body : nullptr;
  // EXPERIMENTAL (ABC_INCR_COMPARE_ONLY): exercise compare/store with NO ABC -- a
  // fast diagnostic for why a region misses on a comment edit.
  if (incr_ != nullptr && std::getenv("ABC_INCR_COMPARE_ONLY") != nullptr) {
    bool hit = false;
    if (pre_g != nullptr) {
      hit = incr_->lookup_compare(rb, pre_g, recipe).hit;
      incr_->store_pre(rb, *rb.pre_lib, rb.pre_name, recipe);
    }
    std::print("COMPARE {} {}\n",
               rb.module_name,
               !rb.reuse_eligible ? "INELIGIBLE" : (pre_g == nullptr ? "REBUILD-FAIL" : (hit ? "HIT" : "MISS")));
    Region_qor q;
    q.module       = rb.module_name;
    q.color        = rb.color;
    q.ctrl         = rb.ctrl;
    q.input_nodes  = input_nodes;
    q.input_ge     = input_ge;
    q.pred_aig     = pred_aig;
    q.cache        = hit ? "hit" : "miss";
    q.resynth      = !hit;
    qor_.push_back(std::move(q));
    report_completion(qor_.back());
    return;
  }
  if (incr_ != nullptr && rb.reuse_eligible) {
    if (pre_g != nullptr) {
      auto                               res = incr_->lookup_compare(rb, pre_g, recipe);
      std::shared_ptr<const std::string> evidence;
      if (res.hit && opts_.evidence_valid) {
        evidence = incr_->read_evidence(*res.row);
        res.hit  = evidence && opts_.evidence_valid(res.row->module, *evidence);
      }
      if (res.hit && incr_->reuse_hit(rb, res, outlib_)) {
        if (opts_.evidence_replay && evidence) {
          opts_.evidence_replay(rb.module_name, res.row->module, *evidence);
        }
        Region_qor q;
        q.module       = rb.module_name;
        q.color        = rb.color;
        q.ctrl         = rb.ctrl;
        q.input_nodes  = input_nodes;
        q.input_ge     = input_ge;
        q.pred_aig     = pred_aig;
        q.gates        = res.row->gates;
        q.area         = res.row->area;
        q.delay        = res.row->delay;
        q.logic_depth  = res.row->logic_depth;
        q.crit_src     = res.row->crit_src;
        q.crit_output  = res.crit_output;
        q.div_blackbox = res.row->div_blackbox;
        q.cache        = "hit";
        q.resynth      = false;
        q.ms           = since();
        qor_.push_back(std::move(q));
        report_completion(qor_.back());
        return;
      }
    }
    incr_->note_miss();
  }

  resynthesized = true;

  // Do not pay the backend's start (an ABC session parses the Liberty) for an
  // all-hit rebuild. The cache salt has already folded the Liberty content and
  // run-level mapping modes, while the exact pre-body comparison authorized
  // the reused result. Only a real miss needs the backend session.
  // Session initialization touches only this lane's backend: let it overlap
  // too; large timing libraries dominate small colors.
  if (coordinator_) {
    graph_lock.unlock();
  }
  auto started = Region_backend::Start::failed;
  try {
    started = backend_->start(ctx);
  } catch (...) {
    if (!graph_lock.owns_lock()) {
      graph_lock.lock();
    }
    throw;
  }
  if (!graph_lock.owns_lock()) {
    graph_lock.lock();
  }
  if (started == Region_backend::Start::failed) {
    return;  // diagnostic already emitted
  }
  if (started == Region_backend::Start::started) {
    report_register_cell();
  }

  if (rewrite_single_shift(rb)) {
    Region_qor q;
    q.module      = rb.module_name;
    q.color       = rb.color;
    q.ctrl        = rb.ctrl;
    q.input_nodes = input_nodes;
    q.input_ge    = input_ge;
    q.pred_aig    = pred_aig;
    q.gates       = 0;
    q.area        = 0;
    q.delay       = 0;
    q.cache       = "miss";
    q.resynth     = true;
    q.ms          = since();
    qor_.push_back(std::move(q));
    report_completion(qor_.back());
    return;
  }

  // The region's logic, translated onto a backend-neutral RAW Lnet.
  Blast_options blast_options;
  blast_options.adder          = opts_.adder;
  blast_options.block_size     = opts_.block_size;
  blast_options.multiplier     = opts_.multiplier;
  blast_options.reverse_barrel = opts_.reverse_barrel;
  blast_options.map_register   = opts_.map_register;
  // A QN-only DFF cell under a flow that keeps every latch: the latch carries
  // ~next_state and the mapper folds the inversion into its phase assignment
  // (Seq_flop::d_inverted).
  blast_options.qn_encode      = dff_.has_value() && dff_->q_inverted && plan.preserves_latches;
  // Asynchronous-reset registers cross as latches only under a flow that keeps
  // every latch as crossed: the read-back attributes each cell's reset pin to
  // its source register by latch position (Seq_flop::async_reset).
  for (int v = 0; v < 2; ++v) {
    blast_options.areset_cell[v] = dff_.has_value() && plan.preserves_latches && !areset_ladder_[v].empty()
                                       ? static_cast<int8_t>(areset_ladder_[v].front().q_inverted ? 1 : 0)
                                       : static_cast<int8_t>(-1);
    blast_options.areset_low[v]  = !areset_ladder_[v].empty() && areset_ladder_[v].front().reset_low(v != 0);
  }
  blast_options.areset_flow_ok = plan.preserves_latches;
  blast_options.verbose        = opts_.verbose;
  Blast_hooks hooks;
  hooks.stage      = trace_stage;
  hooks.elapsed_ms = since;
  if (!opts_.allow_oversize) {
    hooks.over_budget = [&](uint64_t rss_before, size_t blasted, size_t total, size_t net_nodes) {
      // The Lnet is small; the backend network built from it is what costs
      // memory, so count it as already allocated (measured 250-300 bytes per
      // node or output for an ABC netlist: a node, a net, their fanin/fanout
      // arrays and id slots).
      constexpr uint64_t kBytesPerNetNode = 288;
      return over_budget(rb.module_name, rss_before, blasted, total, net_nodes * kBytesPerNetNode);
    };
  }
  hooks.rewrite_rems = [&] {
    auto& rewritten = coordinator_ ? coordinator_->rems_rewritten_graphs_ : rems_rewritten_graphs_;
    if (rewritten.insert(rb.src).second) {
      rewrite_trivial_rems(rb.src);
    }
  };
  auto blast = blast_region(rb, blast_options, hooks);
  if (blast.status != Region_blast::Status::blasted) {
    return;  // refused (diagnosed) or over budget: work() raises refusal_ after stop()
  }

  // Only the backend's private objects are touched while the graph lock is
  // paused. HHDS graph reads, mutation, cache operations and result
  // publication stay under graph_lock.
  struct Graph_pause {
    std::unique_lock<std::mutex>& lock;
    Region_driver*                owner;
    bool                          paused = false;
    void                          pause() {
      if (owner && !paused) {
        const auto active               = owner->active_backend_.fetch_add(1) + 1;
        owner->parallel_stats_.peak_abc = std::max(owner->parallel_stats_.peak_abc, active);
        paused                          = true;
        lock.unlock();
      }
    }
    void resume() {
      if (paused) {
        owner->active_backend_.fetch_sub(1);
        lock.lock();
        paused = false;
      }
    }
    ~Graph_pause() { resume(); }
  } graph_pause{graph_lock, coordinator_};
  if (!opts_.allow_oversize) {
    ctx.fits = [&](uint64_t rss_before, size_t done, size_t total) { return !over_budget(rb.module_name, rss_before, done, total); };
  }
  ctx.admission = [&](std::string_view stage) {
    const auto elapsed = since();
    if (opts_.time_budget_ms != 0 && elapsed > static_cast<double>(opts_.time_budget_ms)) {
      time_refusal_ = std::format("region '{}' took {:.0f} ms in ABC (soft limit {} ms), stopped at {}",
                                  rb.module_name,
                                  elapsed,
                                  opts_.time_budget_ms,
                                  stage);
      return false;
    }
    return opts_.allow_oversize || !over_budget(rb.module_name, blast.rss_before, blast.blast_total, blast.blast_total);
  };
  ctx.refuse_time     = [&](std::string why) { time_refusal_ = std::format("region '{}': {}", rb.module_name, why); };
  ctx.refuse_memory   = [&](std::string why) { refusal_ = std::format("region '{}': {}", rb.module_name, why); };
  ctx.pause_graph     = [&] { graph_pause.pause(); };
  ctx.resume_graph    = [&] { graph_pause.resume(); };
  ctx.critical_output = [&](size_t po, Region_qor& q) { qor_src_of_output(rb, po, q); };

  Region_qor q;
  q.module      = rb.module_name;
  q.color       = rb.color;
  q.ctrl        = rb.ctrl;
  q.input_nodes = input_nodes;
  q.input_ge    = input_ge;
  q.pred_aig    = pred_aig;
  for (const auto& bb : blast.bboxes) {
    if (bb.op == Ntype_op::Div || bb.op == Ntype_op::Rem) {
      ++q.div_blackbox;  // unmapped cone: the region score is partial
    }
  }
  // The hook sees only the Lnet: it runs with the graph lock released.
  Region_rewrite rewrite;
  if (opts_.region_hook) {
    graph_pause.pause();
    rewrite = opts_.region_hook(blast.lnet, ctx);
    graph_pause.resume();
    q.hook_evidence = std::make_shared<const std::string>(std::move(rewrite.evidence));
  }
  auto cells = backend_->map(ctx, blast, rewrite, q);
  graph_pause.resume();
  if (!cells) {
    return;  // refused (recorded; work() raises it after stop()) or failed (diagnosed)
  }
  qor_.push_back(std::move(q));

  // --- read back: the mapped cells -> the region body ---
  Region_writer::Counts          counts{qor_.back().gates, qor_.back().area, qor_.back().bypassed};
  const Region_writer::Registers registers{opts_.map_register, &dff_, &dff_ladder_, &areset_ladder_[0], &areset_ladder_[1]};
  writer_.set_outlib(outlib_);
  writer_.set_flat(flat_);
  if (!writer_.write(rb, blast, *cells, backend_->cells(), registers, counts, trace_stage)) {
    return;
  }
  qor_.back().gates    = counts.gates;
  qor_.back().area     = counts.area;
  qor_.back().bypassed = counts.bypassed;

  // rb.body now holds the complete mapped netlist: snapshot it (and the pre-abc
  // body built above) into the cache so the next run's identical region is a
  // whole-module copy, not an ABC run. A region whose pre-body could not be
  // rebuilt (pre_g == nullptr) is uncacheable and simply re-maps next time.
  if (incr_ != nullptr && pre_g != nullptr) {
    incr_->store(rb, *rb.pre_lib, rb.pre_name, qor_.back(), recipe, outlib_);
  }
  qor_.back().cache             = "miss";
  qor_.back().ms                = since();
  const uint64_t process_peak   = cost::process_peak_rss_bytes();
  qor_.back().peak_rss_kb       = process_peak >> 10;
  // A process HWM is sticky across colors. Charge this color the new HWM only
  // when it advanced; otherwise use the largest live-RSS stage sample. This
  // avoids attributing an early large color's retained HWM to every later tiny
  // color while still capturing peaks at the end of ABC's blocking flow.
  const uint64_t color_peak     = process_peak > process_peak_before ? process_peak : sampled_peak_rss;
  qor_.back().color_peak_rss_kb = coordinator_ == nullptr && color_peak > rss_entry ? (color_peak - rss_entry) >> 10 : 0;
  backend_->end_region();  // release the region's workspace, keep the session
  if (opts_.time_budget_ms != 0 && qor_.back().ms > static_cast<double>(opts_.time_budget_ms)) {
    time_refusal_
        = std::format("region '{}' took {:.0f} ms in synthesis (soft limit {} ms)", rb.module_name, qor_.back().ms, opts_.time_budget_ms);
  }
  // Publish the completed color before allocator housekeeping: a pressure scan
  // can itself take time, and the heartbeat should identify the finished work
  // immediately rather than making that pause look like part of ABC mapping.
  report_completion(qor_.back());
#if defined(__GLIBC__)
  // Hundreds of ROB colors showed the glibc heap RSS growing monotonically
  // even though the ABC networks above were deleted. Return completely free
  // heap pages at the color boundary so the next color's 16-GiB admission
  // check measures live state, not reusable pages retained by malloc arenas.
  // QoR's peak sample is intentionally taken before this trim.
  (void)malloc_trim(0);
#elif defined(__APPLE__)
  // Darwin's allocator has the same retained-page behavior, exposed more
  // directly by TASK_VM_INFO.phys_footprint. Do not scan every malloc zone
  // after each tiny color: thousands of needless maximal-relief calls fragment
  // Darwin's allocator address space and can make a later allocation fail even
  // while the physical footprint is safe. Once the process is genuinely under
  // pressure, periodically ask all zones to return a bounded amount of
  // reclaimable pages. The mapped HHDS body remains live and is untouched.
  // QoR's peak sample is intentionally taken first.
  //
  // Backend has more than 3,000 colors. Once it crossed the pressure threshold,
  // calling maximal all-zone relief after every color performed more than 2,000
  // complete zone scans and eventually made a later allocation fail while
  // physical footprint was still about 10 GiB below the process ceiling. Even
  // rate-limited maximal all-zone scans reproduced the failure at a different
  // color. The Darwin API documents a nonzero goal as best-effort bounded
  // release, so combine a 2-GiB goal with one all-zone scan per 64 completed
  // colors. A default-zone-only scan still let Backend's other zones cross the
  // physical ceiling at color 3,240. The nonzero goal bounds virtual-address
  // churn, and host_mem's Darwin VA allowance leaves room for the holes, while
  // retaining ample physical headroom between scans.
  const uint64_t     pressure_ceiling        = cost::configured_budget_bytes();
  constexpr uint64_t kPressureReliefInterval = 64;
  constexpr size_t   kPressureReliefGoal     = size_t{2} << 30;
  const bool relief_due = !pressure_relief_done_ || completed_regions_ - last_pressure_relief_region_ >= kPressureReliefInterval;
  if (pressure_ceiling != 0 && relief_due && cost::process_footprint_bytes() > pressure_ceiling - pressure_ceiling / 4) {
    (void)malloc_zone_pressure_relief(nullptr, kPressureReliefGoal);
    last_pressure_relief_region_ = completed_regions_;
    pressure_relief_done_        = true;
  }
#endif
}

void Region_driver::report_completion(const Region_qor& q) {
  if (coordinator_) {
    // The run-level heartbeat is the coordinator's, but the Darwin pressure-scan
    // rate limiter in map_region reads THIS driver's counter -- leaving it at 0
    // pinned `relief_due` false after the worker's very first color.
    ++completed_regions_;
    coordinator_->report_completion(q);
    return;
  }

  ++completed_regions_;
  const std::string_view cache = q.cache == nullptr || q.cache[0] == '\0' ? "none" : q.cache;
  std::string line = std::format("PROGRESS pass.abc completed={} region='{}' color={} resynth={} cache={} ge={} gates={} ms={:.1f}",
                                 completed_regions_,
                                 q.module,
                                 q.color,
                                 q.resynth ? 1 : 0,
                                 cache,
                                 q.input_ge,
                                 q.gates,
                                 q.ms);
  std::print("{}\n", line);  // captured in the pass's complete internal step log
  std::fflush(stdout);
  livehd::diag::sink().progress("pass.abc",
                                line,
                                {
                                    {"completed", std::to_string(completed_regions_)},
                                    {"region", q.module},
                                    {"color", std::to_string(q.color)},
                                    {"ctrl", q.ctrl ? "1" : "0"},
                                    {"resynth", q.resynth ? "1" : "0"},
                                    {"cache", std::string{cache}},
                                    {"input_nodes", std::to_string(q.input_nodes)},
                                    {"input_ge", std::to_string(q.input_ge)},
                                    {"gates", std::to_string(q.gates)},
                                    {"bypassed", std::to_string(q.bypassed)},
                                    {"area", std::format("{:.2f}", q.area)},
                                    {"delay", std::format("{:.2f}", q.delay)},
                                    {"ms", std::format("{:.1f}", q.ms)},
                                    {"color_peak_rss_kb", std::to_string(q.color_peak_rss_kb)},
                                    {"process_peak_rss_kb", std::to_string(q.peak_rss_kb)},
                                    {"critical_output", q.crit_output},
                                    {"critical_src", q.crit_src},
  });
}

// `options` are the REGION's resolved options (map_region's per-region overlay),
// which is not this driver's opts_ when a parallel worker records into the
// coordinator -- pass them explicitly rather than swapping a shared member.
void Region_driver::remember_ware(const livehd::partition::Region_body& rb, const Driver_options& options,
                                  const Backend_overrides& overrides) {
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
  w.options   = options;
  w.overrides = overrides;
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

void Region_driver::optimize_ware(hhds::GraphLibrary& outlib, std::string_view top) {
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
    Region_driver& m;
    Driver_options options;
    Region_cache*  cache;
    ~Restore() {
      m.opts_            = std::move(options);
      m.incr_            = cache;
      m.ware_trial_      = false;
      m.trial_overrides_ = {};
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
      Driver_options options;
      std::string    label;
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
        if (out.has_driver()) {
          continue;
        }
        // Both are graph output pins, i.e. SINKS: one driver each, not a set.
        if (const auto drv = old_body->get_output_pin(od.name).get_driver_pin(); !drv.is_invalid()) {
          if (drv.is_const()) {
            auto c = gu::create_const(*shell, gu::const_of(drv));
            c.connect_sink(out);
          } else if (gu::is_graph_input_pin(drv)) {
            shell->get_input_pin(gu::pin_name_of(drv)).connect_sink(out);
          }
        }
      }
      if (!outlib.replace_body_from(name, *shell)) {
        break;
      }
      opts_              = candidate.options;
      trial_overrides_   = w.overrides;
      w.rb.nodes         = w.nodes;
      const size_t count = qor_.size();
      incr_              = nullptr;
      std::unique_ptr<Region_cache> candidate_cache;
      if (saved_cache && w.rb.pre_body) {
        const auto& o   = candidate.options;
        const auto  key = std::format("a{}_b{}_m{}_s{}",
                                      static_cast<int>(o.adder),
                                      o.block_size,
                                      static_cast<int>(o.multiplier),
                                      o.reverse_barrel);
        candidate_cache = std::make_unique<Region_cache>(saved_cache->dir() + "/ware/" + name + "/" + key, saved_cache->salt(), true);
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
Design_ctx Region_driver::design_ctx() {
  return Design_ctx{qor_,
                    incr_,
                    region_delay_targets_,
                    dff_,
                    dff_ladder_,
                    areset_ladder_[0],
                    areset_ladder_[1],
                    [this](float target, bool has_flops) { return region_budget(target, has_flops); },
                    timing_requested()};
}

uint64_t Region_driver::refine_boundaries(hhds::GraphLibrary& outlib, std::string_view top) {
  return backend_->refine(outlib, top, design_ctx());
}

Ware_score Region_driver::score_ware(hhds::GraphLibrary& outlib, std::string_view top) {
  return backend_->score(outlib, top, design_ctx());
}

void report_stats(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view top, const Driver_options& opts) {
  std::print("pass.abc stats: top='{}' library='{}' register={} memory={}\n",
             top,
             opts.library,
             opts.map_register,
             memory_fold_name(opts.memory_fold));
  struct Op_stats {
    uint64_t nodes  = 0;
    uint64_t ge     = 0;
    uint64_t max_ge = 0;
  };
  struct Region_stats {
    uint64_t                               nodes         = 0;
    uint64_t                               ge            = 0;
    uint64_t                               register_bits = 0;
    absl::btree_map<std::string, uint64_t> op_ge;
  };
  absl::btree_map<std::string, Op_stats>                     by_op;
  absl::btree_map<std::pair<std::string, int>, Region_stats> by_region;
  uint64_t                                                   total_nodes = 0;
  uint64_t                                                   total_ge    = 0;
  for (const auto& graph : graphs) {
    for (const auto& node : graph->body().nodes()) {
      if (gu::is_builtin_node(node)) {
        continue;
      }
      const auto ge      = gu::synthesis_ge_weight(node);
      const auto op_name = std::string{Ntype::get_name(gu::type_op_of(node))};
      auto&      s       = by_op[op_name];
      ++s.nodes;
      s.ge     += ge;
      s.max_ge  = std::max(s.max_ge, ge);
      ++total_nodes;
      total_ge        += ge;
      // pass.partition treats an uncolored node as color zero, so the read-only
      // report must do the same. Newly extracted pattern definitions are
      // intentionally uncolored until the next color pass; omitting them here
      // hid exactly the shared body an optimization run needed to inspect.
      const int color  = gu::has_color(node) ? gu::color_of(node) : 0;
      auto&     rs     = by_region[{std::string{graph->get_name()}, color}];
      ++rs.nodes;
      rs.ge             += ge;
      rs.op_ge[op_name] += ge;
      if (gu::is_type_flop(node)) {
        rs.register_bits += static_cast<uint64_t>(std::max(gu::bits_of(node.create_driver_pin(0)), 1)) * pipeline_depth(node);
      }
    }
  }
  std::vector<std::pair<std::string_view, const Op_stats*>> ranked;
  ranked.reserve(by_op.size());
  for (const auto& [name, stats] : by_op) {
    ranked.emplace_back(name, &stats);
  }
  std::ranges::sort(ranked, [](const auto& lhs, const auto& rhs) { return lhs.second->ge > rhs.second->ge; });
  std::print("  operation GE: {} nodes, {} total synthesis GE across {} def(s)\n", total_nodes, total_ge, graphs.size());
  for (const auto& [name, stats] : ranked) {
    std::print("    {:<12} nodes {:>8}  GE {:>12}  max/node {:>10}\n", name, stats->nodes, stats->ge, stats->max_ge);
  }
  if (opts.verbose) {
    std::print("  regions (definition color: nodes, GE, register bits, leading operation GE):\n");
    for (const auto& [key, stats] : by_region) {
      std::vector<std::pair<std::string_view, uint64_t>> ops;
      ops.reserve(stats.op_ge.size());
      for (const auto& [name, ge] : stats.op_ge) {
        ops.emplace_back(name, ge);
      }
      std::ranges::sort(ops, [](const auto& lhs, const auto& rhs) {
        return lhs.second != rhs.second ? lhs.second > rhs.second : lhs.first < rhs.first;
      });
      std::string leaders;
      for (size_t i = 0; i < std::min<size_t>(ops.size(), 6); ++i) {
        leaders += std::format("{}{}={}", i == 0 ? "" : ",", ops[i].first, ops[i].second);
      }
      std::print("    {} c{}: nodes {}  GE {}  reg_bits {}  {}\n",
                 key.first,
                 key.second,
                 stats.nodes,
                 stats.ge,
                 stats.register_bits,
                 leaders);
    }
  }
  std::print("  (run with --emit-dir lg:DIR to produce the mapped netlist library)\n");
}

}  // namespace livehd::synth

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt.hpp"

#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <mutex>
#include <print>
#include <sstream>

#include "json_util.hpp"
#include "rapidjson/document.h"
#include "satopt_detail.hpp"
#include "satopt_salt.hpp"

namespace livehd::satopt {
namespace detail {
std::string source_key(hhds::Graph* graph, bool colors, uint64_t salt) {
  std::vector<std::string> rows;
  for (const auto n : graph->body().nodes()) {
    auto row = std::format("{}:{}", n.get_debug_nid(), static_cast<int>(gu::type_op_of(n)));
    if (gu::type_op_of(n) == Ntype_op::Sub) {
      // Which definition the instance calls: a descending proof read its body.
      const auto sio    = n.get_subnode_io();
      const auto callee = sio ? std::string(sio->get_name()) : std::string{};
      row              += std::format(":s{}:{}", callee.size(), callee);
    }
    if (colors) {
      row += std::format(":c{}", gu::color_of(n));
    }
    std::vector<std::string> edges;
    const auto               pin = [](const Pin& p) {
      std::string value;
      if (p.is_const()) {
        value = satopt_constant_key(gu::const_of(p));
      }
      return std::format("{}:{}:{}:{}:{}:{}",
                         p.get_master_node().get_debug_nid(),
                         p.get_port_id(),
                         gu::bits_of(p),
                         gu::is_unsign(p),
                         value.size(),
                         value);
    };
    for (const auto& in_pin : n.inp_sorted_pins()) {
      for (auto in_drv : in_pin.get_driver_pins()) {
        edges.push_back(std::format("{}={}", in_pin.get_port_id(), pin(in_drv)));
      }
    }
    for (const auto& e : n.out_edges()) {
      edges.push_back("o=" + pin(e.driver) + ">" + pin(e.sink));
    }
    // Exact proof-cache identity: the caller compares the full source key.
    // A commutative digest without this representation cannot authorize reuse.
    std::sort(edges.begin(), edges.end());
    for (const auto& edge : edges) {
      row += std::format("|{}:{}", edge.size(), edge);
    }
    rows.push_back(std::move(row));
  }
  // The interface as the prover reads it: each input's declared width and
  // sign seed its symbol, and each output's port binding.
  const auto gio = graph->get_io();
  for (const auto& d : gio->get_input_pin_decls()) {
    const auto p = graph->get_input_pin(d.name);
    rows.push_back(std::format("I:{}:{}:{}:{}:{}",
                               d.name.size(),
                               d.name,
                               d.port_id,
                               p.is_invalid() ? 0 : gu::real_width(p, *gio, d.name),
                               gio->is_unsign(d.name)));
  }
  for (const auto& d : gio->get_output_pin_decls()) {
    rows.push_back(std::format("O:{}:{}:{}", d.name.size(), d.name, d.port_id));
  }
  std::sort(rows.begin(), rows.end());
  std::string key = std::format("{}:{}:{}", salt, graph->get_name().size(), graph->get_name());
  for (const auto& row : rows) {
    key += std::format("\n{}:{}", row.size(), row);
  }
  return key;
}

std::string cache_path(std::string_view dir, std::string_view name, std::string_view suffix) {
  if (dir.empty()) {
    return {};
  }
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : name) {
    h = (h ^ c) * 1099511628211ULL;
  }
  return std::format("{}/{:016x}{}.json", dir, h, suffix);
}
void write_atomic(const std::string& path, const std::string& text) {
  if (path.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  if (ec) {
    return;
  }
  // Separate temporary files prevent concurrent invocations from interleaving
  // a definition descriptor from one proof run with another run's facts.
  auto      temporary = path + ".tmp.XXXXXX";
  const int fd        = mkstemp(temporary.data());
  if (fd < 0) {
    return;
  }
  close(fd);
  std::ofstream out(temporary);
  out << text;
  out.close();
  if (out) {
    std::filesystem::rename(temporary, path, ec);
  }
  std::filesystem::remove(temporary, ec);
}
}  // namespace detail

namespace {
using namespace detail;

// The combinational fan-in of `roots` (cut like the proofs) in forward
// topological order. Evaluating it in this order before the roots keeps the
// memoized Seeds/Cone recursion one level deep: a pass must not recurse on
// design depth.
std::vector<Pin> fanin_forward(hhds::Graph* graph, std::vector<Pin> pending) {
  absl::flat_hash_set<Node> cone;
  while (!pending.empty()) {
    const auto p = pending.back();
    pending.pop_back();
    if (p.is_invalid() || p.is_const() || cut(p)) {
      continue;
    }
    const auto n = p.get_master_node();
    if (!cone.insert(n).second) {
      continue;
    }
    for (const auto& in_pin : n.inp_sorted_pins()) {
      for (const auto& d : in_pin.get_driver_pins()) {
        pending.push_back(d);
      }
    }
  }
  std::vector<Pin> order;
  for (const auto n : graph->body().nodes(hhds::Node_order::forward)) {
    if (cone.contains(n)) {
      for (const auto& p : n.out_sorted_pins()) {
        order.push_back(p);
      }
    }
  }
  return order;
}

// Selectors of one cell, indexed like Select_fact::control. A Hotmux fallback
// has no control of its own. Only a one-bit Flop enable is a candidate.
std::vector<Pin> selectors(const Node& n) {
  const auto op = gu::type_op_of(n);
  if (op == Ntype_op::Flop) {
    const auto en = gu::get_driver_of_sink_name(n, "enable");
    if (en.is_invalid() || gu::bits_of(en) != 1) {
      return {};
    }
    return {en};
  }
  if (op != Ntype_op::Mux && op != Ntype_op::Hotmux) {
    return {};
  }
  auto arms = arms_of(n);
  if (!arms.hot) {
    return arms.controls.empty() ? std::vector<Pin>{} : std::vector<Pin>{arms.controls[0]};
  }
  if (!arms.controls.empty() && arms.controls.back().is_invalid()) {
    arms.controls.pop_back();
  }
  return arms.controls;
}
// `children` pins every definition the proofs descended into: a callee edit
// changes the facts although the definition itself did not change. `options`
// is the proof-relevant budget (Budget::proof_key); a row whose search ran out
// of budget is `complete=false` and never stands in for a full search.
constexpr int kSelectCacheVersion = 3;
struct Select_cached {
  std::string                                      source, options;
  bool                                             complete = false;
  uint64_t                                         work = 0, queries = 0;  // what the search cost (Meter::replay)
  std::vector<Select_fact>                         facts;
  std::vector<std::pair<std::string, std::string>> children;  // (name, exact source key)
};
std::map<std::string, Select_cached> saved_selects;
Select_cached                        read_selects(const std::string& path) {
  Select_cached row;
  if (path.empty()) {
    return row;
  }
  std::ifstream       input(path);
  std::string         text((std::istreambuf_iterator<char>(input)), {});
  rapidjson::Document doc;
  doc.Parse(text.c_str());
  if (!doc.IsObject() || !doc.HasMember("version") || !doc["version"].IsInt() || doc["version"].GetInt() != kSelectCacheVersion
      || !doc.HasMember("source") || !doc["source"].IsString() || !doc.HasMember("options") || !doc["options"].IsString()
      || !doc.HasMember("complete") || !doc["complete"].IsBool() || !doc.HasMember("work") || !doc["work"].IsUint64()
      || !doc.HasMember("queries") || !doc["queries"].IsUint64() || !doc.HasMember("facts") || !doc["facts"].IsArray()) {
    return row;
  }
  for (const auto& f : doc["facts"].GetArray()) {
    if (!f.IsArray() || f.Size() != 3 || !f[0].IsUint64() || !f[1].IsInt() || f[1].GetInt() < 0 || !f[2].IsBool()) {
      return {};
    }
    row.facts.push_back({f[0].GetUint64(), f[1].GetInt(), f[2].GetBool()});
  }
  if (!doc.HasMember("children") || !doc["children"].IsArray()) {
    return {};
  }
  for (const auto& c : doc["children"].GetArray()) {
    if (!c.IsArray() || c.Size() != 2 || !c[0].IsString() || !c[1].IsString()) {
      return {};
    }
    row.children.emplace_back(c[0].GetString(), c[1].GetString());
  }
  row.source   = doc["source"].GetString();
  row.options  = doc["options"].GetString();
  row.complete = doc["complete"].GetBool();
  row.work     = doc["work"].GetUint64();
  row.queries  = doc["queries"].GetUint64();
  return row;
}
bool children_match(hhds::Graph* graph, const Select_cached& row) {
  auto* lib = graph->get_io() ? graph->get_io()->get_library() : nullptr;
  for (const auto& [name, key] : row.children) {
    const auto io  = lib ? lib->find_io(name) : nullptr;
    const auto def = io ? io->get_graph() : nullptr;
    if (!def || source_key(def.get(), false, kSatoptSrcSalt) != key) {
      return false;
    }
  }
  return true;
}
void write_selects(const std::string& path, const Select_cached& row) {
  if (path.empty()) {
    return;
  }
  auto text = std::format(
      "{{\"version\":{},\"source\":\"{}\",\"options\":\"{}\",\"complete\":{},\"work\":{},\"queries\":{},\"facts\":[",
      kSelectCacheVersion,
      json_util::escape(row.source),
      json_util::escape(row.options),
      row.complete ? "true" : "false",
      row.work,
      row.queries);
  for (size_t i = 0; i < row.facts.size(); ++i) {
    const auto& f  = row.facts[i];
    text          += std::format("{}[{},{},{}]", i ? "," : "", f.node, f.control, f.value ? "true" : "false");
  }
  text += "],\"children\":[";
  for (size_t i = 0; i < row.children.size(); ++i) {
    text += std::format("{}[\"{}\",\"{}\"]",
                        i ? "," : "",
                        json_util::escape(row.children[i].first),
                        json_util::escape(row.children[i].second));
  }
  write_atomic(path, text + "]}\n");
}

std::mutex saved_mutex;

void apply_selects(Select_rewrite& rw, const Node& n, const std::map<int, bool>& known, Select_satopt& stats, Profile profile) {
  const auto op = gu::type_op_of(n);
  if (op == Ntype_op::Flop) {
    // A tied enable, not a removed one: a driverless sink is a malformed body.
    rw.tie(n, 4, known.at(0));
    ++stats.enables;
    return;
  }
  if (op == Ntype_op::Mux) {
    const bool value = known.at(0);
    rw.tie(n, 0, value);
    rw.tie(n, value ? 1 : 2, 0);
    ++stats.muxes;
    return;
  }
  // Priority semantics (the reference on an overlap): the first always-on
  // control wins, so every later arm and the fallback are never selected.
  const auto inputs = gu::hotmux_inputs(n);
  const int  arms   = static_cast<int>(inputs.arms.size());
  int        first  = arms;
  for (const auto& [control, value] : known) {
    if (value && control < first) {
      first = control;
    }
  }
  if (profile == Profile::shared) {
    // The controls are the `unique if` obligation: a control is tied only to
    // the value it was proven to always carry, so the check still sees every
    // overlap. Only data that is never selected (a control proven false, or
    // an arm after an always-true control) is tied to 0.
    for (int i = 0; i < arms; ++i) {
      const auto found = known.find(i);
      if (found != known.end()) {
        rw.tie(n, static_cast<hhds::Port_id>(2 * i), found->second);
        if (!found->second) {
          rw.tie(n, static_cast<hhds::Port_id>(2 * i + 1), 0);
        }
        ++stats.hotmux_arms;
      } else if (i > first) {
        rw.tie(n, static_cast<hhds::Port_id>(2 * i + 1), 0);
        ++stats.hotmux_arms;
      }
    }
    if (first < arms && !inputs.fallback.is_invalid()) {
      rw.tie(n, static_cast<hhds::Port_id>(2 * arms), 0);
    }
    return;
  }
  for (int i = 0; i < arms; ++i) {
    const auto found = known.find(i);
    if (i == first) {
      rw.tie(n, static_cast<hhds::Port_id>(2 * i), 1);
      ++stats.hotmux_arms;
    } else if (i > first || (found != known.end() && !found->second)) {
      rw.tie(n, static_cast<hhds::Port_id>(2 * i), 0);
      rw.tie(n, static_cast<hhds::Port_id>(2 * i + 1), 0);
      ++stats.hotmux_arms;
    }
  }
  if (first < arms && !inputs.fallback.is_invalid()) {
    rw.tie(n, static_cast<hhds::Port_id>(2 * arms), 0);
  }
}
}  // namespace

std::string satopt_constant_key(const Dlop& value) {
  constexpr char hex[] = "0123456789abcdef";
  std::string    key;
  for (unsigned char byte : value.serialize()) {
    key += hex[byte >> 4];
    key += hex[byte & 15];
  }
  return key;
}

std::string satopt_source_key(hhds::Graph* graph) { return detail::source_key(graph, false, kSatoptSrcSalt); }

struct Satopt_seeds::Impl {
  detail::Word_sim sim;
};
Satopt_seeds::Satopt_seeds(uint32_t samples)
    : impl_(std::make_unique<Impl>(detail::Word_sim(detail::sim_options(Budget{.samples = samples})))) {}
Satopt_seeds::~Satopt_seeds() = default;
std::optional<std::vector<Dlop>> Satopt_seeds::sample(const Pin& pin) {
  const auto* values = impl_->sim.values(pin);
  return values ? std::optional<std::vector<Dlop>>{*values} : std::nullopt;
}

Select_satopt optimize_selects(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view cache_dir, Profile profile,
                               Stage_report* report, Meter* meter) {
  std::lock_guard lock(saved_mutex);
  Select_satopt   stats;
  Stage_report    local;
  Meter           unlimited;
  auto&           rep     = report ? *report : local;
  auto&           m       = meter ? *meter : unlimited;
  const bool      shared  = profile == Profile::shared;
  const auto      options = m.budget().proof_key();
  for (const auto& graph : graphs) {
    if (!graph) {
      continue;
    }
    std::map<uint64_t, std::pair<Node, std::vector<Pin>>> cells;
    for (const auto n : graph->body().nodes()) {
      if (auto controls = selectors(n); !controls.empty()) {
        cells.emplace(static_cast<uint64_t>(n.get_debug_nid()), std::make_pair(n, std::move(controls)));
      }
    }
    if (cells.empty()) {
      continue;
    }
    const auto source = source_key(graph.get(), false, kSatoptSrcSalt);
    // A shared proof also rejects unstamped widths: its facts are a subset of
    // the synthesis ones and live in their own row.
    const auto path   = cache_path(cache_dir, graph->get_name(), shared ? "-select-shared" : "-select");
    auto&      row    = saved_selects[std::string(graph->get_name()) + (shared ? "\x01shared" : "")];
    const auto usable = [&] {
      return row.complete && row.source == source && row.options == options && children_match(graph.get(), row);
    };
    if (!usable()) {
      row = read_selects(path);
    }
    std::vector<Select_fact> facts;
    const bool               had_complete = usable();
    if (had_complete && m.replay(row.work, row.queries)) {
      stats.reused += row.facts.size();
      facts         = row.facts;
    } else {
      // A complete row this budget cannot replay stays for a later, larger
      // run: a partial search never replaces it.
      const auto prior = had_complete ? row : Select_cached{};
      const auto work0 = m.work_done(), queries0 = m.queries_done();
      Word_sim                                 sim(sim_options(m.budget(), true, shared));
      std::vector<std::pair<Select_fact, Pin>> candidates;
      std::vector<Pin>                         roots;
      for (const auto& [nid, cell] : cells) {
        roots.insert(roots.end(), cell.second.begin(), cell.second.end());
      }
      row = {source, options, true, 0, 0, {}, {}};
      // Work: the walk, then one unit per simulated column value. Out of
      // budget, the rest of the search is skipped and the row is partial.
      const auto order   = fanin_forward(graph.get(), std::move(roots));
      bool       afford  = m.work(order.size());
      uint64_t   charged = 0;
      for (const auto& p : order) {
        if (!afford) {
          break;
        }
        sim.values(p);
        afford  = m.work(sim.work() - charged);
        charged = sim.work();
      }
      // Every column agrees with the constant the select would be tied to.
      const auto agrees = [&](const std::vector<Dlop>& values, bool value) {
        return std::all_of(values.begin(), values.end(), [&](const Dlop& v) { return !v.is_known_zero() == value; });
      };
      for (const auto& [nid, cell] : cells) {
        const auto& controls = cell.second;
        for (size_t i = 0; i < controls.size(); ++i) {
          // An unstamped width would make the condition read only bit 0.
          const auto& c = controls[i];
          if (c.is_invalid() || c.is_const() || gu::bits_of(c) <= 0) {
            continue;
          }
          ++stats.candidates;
          if (!afford) {
            ++rep.budget_skips;
            row.complete = false;
            continue;
          }
          const auto* values = sim.values(c);
          if (values == nullptr) {
            // An unsupported cone or the simulator's size cap: the prover
            // still decides (the filter only rejects).
            candidates.push_back({{nid, static_cast<int>(i), true}, c});
            continue;
          }
          const bool value = !values->front().is_known_zero();
          if (agrees(*values, value)) {
            candidates.push_back({{nid, static_cast<int>(i), value}, c});
          } else {
            ++rep.sim_rejects;
          }
        }
      }
      stats.survivors += candidates.size();
      // cvc5 answers true/false at word level, through the same encoder whole-
      // design LEC checks this rewrite with, and descends into called
      // submodules (virtual flat). Only cvc5: ABC maps every region anyway, so
      // a satopt proof pays off where it goes beyond what ABC finds. Unknown (a
      // body-less Sub in the cone, an unsupported cell, a budget-out) leaves the
      // selector alone.
      formal::Prover prover(graph.get(), prove_options(m.budget(), true, shared));
      uint64_t solver_charged = 0;
      for (auto [fact, control] : candidates) {
        // A counterexample column added since this candidate was filtered
        // may already refute it; a value the simulator cannot give is left
        // to the prover.
        if (const auto* values = sim.values(control); values != nullptr) {
          if (!agrees(*values, fact.value)) {
            ++rep.sim_rejects;
            continue;
          }
        }
        if (!m.query()) {
          ++rep.budget_skips;
          row.complete = false;
          continue;
        }
        ++rep.queries;
        auto result = fact.value ? prover.is_true(control) : prover.is_false(control);
        if (result.verdict != formal::Verdict::Proven && sim.values(control) == nullptr) {
          // No sample fixed a polarity: try the other one (a query of its own).
          // A refuted `always 1` says nothing about `always 0`.
          if (m.query()) {
            ++rep.queries;
            fact.value = !fact.value;
            result     = fact.value ? prover.is_true(control) : prover.is_false(control);
          } else {
            ++rep.budget_skips;
            row.complete = false;
          }
        }
        m.work(prover.work() - solver_charged);
        solver_charged = prover.work();
        if (result.verdict == formal::Verdict::Proven) {
          row.facts.push_back(fact);
        } else if (result.verdict == formal::Verdict::Refuted) {
          ++rep.refuted;
          // Feed the counterexample back: later candidates it refutes need
          // no query.
          if (!result.model.empty() && sim.add_model(result.model)) {
            m.work(sim.work() - charged);
            charged = sim.work();
          }
        } else {
          ++rep.unknown;
        }
      }
      // Every body a proof or a simulated rejection read.
      auto defs = prover.descended();
      for (auto* def : sim.descended()) {
        if (std::find(defs.begin(), defs.end(), def) == defs.end()) {
          defs.push_back(def);
        }
      }
      for (auto* def : defs) {
        row.children.emplace_back(std::string(def->get_name()), source_key(def, false, kSatoptSrcSalt));
      }
      if (m.exhausted()) {
        row.complete = false;  // some charge failed: whatever it paid for went unsearched
      }
      row.work    = m.work_done() - work0;
      row.queries = m.queries_done() - queries0;
      facts       = row.facts;
      if (row.complete || !had_complete) {
        write_selects(path, row);
      } else {
        row = prior;
      }
    }
    stats.proven += facts.size();
    std::map<uint64_t, std::map<int, bool>> known;
    for (const auto& f : facts) {
      known[f.node][f.control] = f.value;
    }
    Select_rewrite rw{*graph, {}, shared};
    for (const auto& [nid, controls] : known) {
      if (auto it = cells.find(nid); it != cells.end()) {
        apply_selects(rw, it->second.first, controls, stats, profile);
      }
    }
    rw.sweep();
    rep.nodes_removed += rw.removed;
  }
  rep.candidates += stats.candidates;
  rep.proven += stats.proven;
  rep.reused += stats.reused;
  rep.applied += stats.muxes + stats.hotmux_arms + stats.enables;
  std::print(
      "[pass.satopt] select: {} candidates, {} seed survivors, {} proven constant ({} reused): {} mux, {} hotmux arm, {} flop "
      "enable\n",
      stats.candidates,
      stats.survivors,
      stats.proven,
      stats.reused,
      stats.muxes,
      stats.hotmux_arms,
      stats.enables);
  return stats;
}
uint64_t drop_dead_logic(hhds::Graph* graph) {
  if (graph == nullptr) {
    return 0;
  }
  Select_rewrite rw{*graph, {}};
  uint64_t       before = 0;
  for (const auto n : graph->body().nodes()) {
    ++before;
    const auto op = gu::type_op_of(n);
    if (Ntype::is_comb(op) && op != Ntype_op::Clock_cell && !n.has_out_edges()) {
      rw.released.push_back(n.create_driver_pin(0));
    }
  }
  if (rw.released.empty()) {
    return 0;
  }
  rw.sweep();
  uint64_t after = 0;
  for (const auto n : graph->body().nodes()) {
    (void)n;
    ++after;
  }
  return before - after;
}
}  // namespace livehd::satopt

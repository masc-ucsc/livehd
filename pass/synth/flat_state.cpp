// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "flat_state.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <functional>
#include <map>
#include <print>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "bit_facts.hpp"
#include "debug_env.hpp"
#include "hash_util.hpp"
#include "inline_sub.hpp"
#include "node_util.hpp"
#include "specialize.hpp"
#include "sweep.hpp"

namespace livehd::synth {
namespace gu = livehd::graph_util;

namespace {

struct Bit_origin {
  int32_t   context = -1;
  hhds::Nid nid     = 0;
  int       bit     = -1;
};

// Body nodes of `g` by debug nid (a content-named copy keeps its source's nids).
class Nid_index {
public:
  hhds::Node_class find(hhds::Graph& g, hhds::Nid nid) {
    auto& index = by_graph_[&g];
    if (index.empty()) {
      for (auto node : g.body().nodes()) {
        index.emplace(node.get_debug_nid(), node);
      }
    }
    auto it = index.find(nid);
    return it == index.end() ? hhds::Node_class{} : it->second;
  }

private:
  absl::flat_hash_map<const hhds::Graph*, absl::flat_hash_map<hhds::Nid, hhds::Node_class>> by_graph_;
};

}  // namespace

std::optional<Flat_state_stats> fold_flat_state(std::vector<std::shared_ptr<hhds::Graph>>& definitions, std::string_view top,
                                                Preparation_budget* budget) {
  if (top.empty() || livehd::debug::env<"LHD_NO_FLAT_STATE">() != nullptr) {
    return std::nullopt;
  }
  std::shared_ptr<hhds::Graph>                                 root;
  absl::flat_hash_map<hhds::Gid, std::shared_ptr<hhds::Graph>> by_gid;
  for (const auto& graph : definitions) {
    if (!graph) {
      continue;
    }
    by_gid.emplace(graph->get_gid(), graph);
    if (!root && graph->get_name() == top) {
      root = graph;
    }
  }
  if (!root || !root->get_io()) {
    return std::nullopt;
  }
  bool has_instance = false;
  for (auto node : root->body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Sub && node.get_subnode_graph() && !node.subnode_loop()) {
      has_instance = true;
      break;
    }
  }
  if (!has_instance || (budget != nullptr && !budget->check("flat-state"))) {
    return std::nullopt;
  }
  const bool debug = livehd::debug::env<"LHD_FLAT_STATE_DEBUG">() != nullptr;

  // ---- the flat analysis copy, swept like synth.flat_sweep sweeps the real one ----
  auto*                                     library = root->get_io()->get_library();
  std::vector<std::shared_ptr<hhds::Graph>> scratch;
  std::string                               analysis_name = std::format("__flat_state_{}", top);
  for (int n = 1; library->find_io(analysis_name); ++n) {
    analysis_name = std::format("__flat_state_{}_{}", top, n);
  }
  auto       analysis     = specialize::clone_definition(scratch, root, analysis_name);
  const auto analysis_gid = analysis->get_gid();

  Flat_state_stats   stats;
  sweep::Flat_origin origin;
  (void)sweep::flatten({analysis}, &origin);
  stats.contexts = origin.contexts.size();
  if (!origin.contexts.empty()) {
    origin.contexts[0].def = root->get_gid();  // the analysis copy stands for `top`
  }

  absl::flat_hash_map<hhds::Class_index, std::vector<Bit_origin>> provenance;
  absl::flat_hash_map<std::pair<int32_t, hhds::Nid>, int>         width_of;  // definition flop width
  for (auto node : analysis->body().nodes()) {
    if (gu::type_op_of(node) != Ntype_op::Flop) {
      continue;
    }
    auto it = origin.flops.find(node.get_class_index());
    if (it == origin.flops.end()) {
      continue;
    }
    const int w = gu::bits_of(node.get_driver_pin(0));
    if (w <= 0) {
      continue;
    }
    std::vector<Bit_origin> bits(static_cast<size_t>(w));
    for (int i = 0; i < w; ++i) {
      bits[static_cast<size_t>(i)] = {.context = it->second.first, .nid = it->second.second, .bit = i};
    }
    provenance.emplace(node.get_class_index(), std::move(bits));
    width_of.emplace(it->second, w);
  }

  // (context, definition flop nid) -> per-bit verdict (-1: state, 0/1: reads that constant)
  absl::flat_hash_map<std::pair<int32_t, hhds::Nid>, std::vector<int8_t>> verdicts;
  sweep::State_options                                                    options{.observe_markers = false};
  absl::flat_hash_set<std::pair<int32_t, hhds::Nid>>                      dropped_memories;  // (context, definition memory nid)
  options.on_memory_dropped = [&](const hhds::Node_class& mem) {
    if (auto it = origin.memories.find(mem.get_class_index()); it != origin.memories.end()) {
      dropped_memories.insert(it->second);
    }
  };
  absl::flat_hash_set<hhds::Class_index> rebuilt;  // flops the sweep cut (each bit got a verdict or a run)
  options.on_fold = [&](const hhds::Node_class& old, int bit, int8_t value) {
    rebuilt.insert(old.get_class_index());
    auto it = provenance.find(old.get_class_index());
    if (it == provenance.end() || bit < 0 || static_cast<size_t>(bit) >= it->second.size()
        || it->second[static_cast<size_t>(bit)].bit < 0) {
      ++stats.lost_bits;
      return;
    }
    const auto& o   = it->second[static_cast<size_t>(bit)];
    const auto  key = std::pair{o.context, o.nid};
    auto&       v   = verdicts[key];
    if (v.empty()) {
      v.assign(static_cast<size_t>(width_of[key]), -1);
    }
    if (static_cast<size_t>(o.bit) < v.size()) {
      v[static_cast<size_t>(o.bit)] = value > 1 ? int8_t{2} : (value != 0 ? int8_t{1} : int8_t{0});
    }
  };
  options.on_run = [&](const hhds::Node_class& old, int lo, int hi, const hhds::Node_class& run) {
    rebuilt.insert(old.get_class_index());
    auto it = provenance.find(old.get_class_index());
    if (it == provenance.end()) {
      return;
    }
    std::vector<Bit_origin> bits;
    for (int i = lo; i < hi; ++i) {
      bits.push_back(static_cast<size_t>(i) < it->second.size() ? it->second[static_cast<size_t>(i)] : Bit_origin{});
    }
    provenance[run.get_class_index()] = std::move(bits);
  };

  sweep::refine_unknown(*analysis);
  specialize::refold_private_body(analysis, specialize::Refold_cost::synth, "synth.flat_state");
  for (int round = 0; round < 16; ++round) {
    if (budget != nullptr && !budget->check("flat-state-round")) {
      analysis.reset();
      scratch.clear();
      library->delete_graph(analysis_gid);
      return std::nullopt;
    }
    const auto swept = sweep::sweep_state(*analysis, options);
    if (!swept.changed()) {
      break;
    }
    specialize::refold_private_body(analysis, specialize::Refold_cost::synth, "synth.flat_state");
  }
  if (debug) {
    absl::flat_hash_set<hhds::Class_index> live;
    for (auto node : analysis->body().nodes()) {
      if (gu::type_op_of(node) == Ntype_op::Flop) {
        live.insert(node.get_class_index());
      }
    }
    uint64_t vanished = 0, kept = 0;
    for (const auto& [ci, bits] : provenance) {
      if (live.contains(ci)) {
        kept += bits.size();
      } else if (!rebuilt.contains(ci)) {
        vanished += bits.size();
      }
    }
    std::print(stderr, "flat-state live_tracked_bits={} vanished_in_refold_bits={}\n", kept, vanished);
  }
  analysis.reset();
  scratch.clear();
  library->delete_graph(analysis_gid);
  if (verdicts.empty() && dropped_memories.empty()) {
    return stats;
  }

  // ---- instance signatures: equal signature, equal facts in the whole subtree ----
  const auto                                                   n = static_cast<int32_t>(origin.contexts.size());
  std::vector<std::vector<std::tuple<hhds::Nid, int, int8_t>>> own(static_cast<size_t>(n));
  for (const auto& [key, v] : verdicts) {
    for (size_t i = 0; i < v.size(); ++i) {
      if (v[i] >= 0) {
        own[static_cast<size_t>(key.first)].emplace_back(key.second, static_cast<int>(i), v[i]);
      }
    }
  }
  for (const auto& [context, nid] : dropped_memories) {
    own[static_cast<size_t>(context)].emplace_back(nid, -1, int8_t{0});  // bit -1: the whole memory, unobserved
  }
  std::vector<std::vector<std::pair<hhds::Nid, int32_t>>> children(static_cast<size_t>(n));
  for (int32_t c = 1; c < n; ++c) {
    children[static_cast<size_t>(origin.contexts[static_cast<size_t>(c)].parent)].emplace_back(
        origin.contexts[static_cast<size_t>(c)].sub,
        c);
  }
  absl::flat_hash_map<std::vector<int64_t>, int32_t> interned;
  std::vector<std::vector<int64_t>>                  sig_content;
  std::vector<bool>                                  sig_has;
  std::vector<int32_t>                               sig(static_cast<size_t>(n), -1);
  for (int32_t c = n - 1; c >= 0; --c) {  // a child's context comes after its parent's
    auto& mine = own[static_cast<size_t>(c)];
    std::ranges::sort(mine);
    auto& kids = children[static_cast<size_t>(c)];
    std::ranges::sort(kids);
    std::vector<int64_t> content;
    content.push_back(static_cast<int64_t>(origin.contexts[static_cast<size_t>(c)].def));
    bool has = !mine.empty();
    for (const auto& [nid, bit, value] : mine) {
      content.push_back(static_cast<int64_t>(nid));
      content.push_back(bit * 2 + value);
    }
    content.push_back(-1);
    for (const auto& [nid, child] : kids) {
      content.push_back(static_cast<int64_t>(nid));
      content.push_back(sig[static_cast<size_t>(child)]);
      has = has || sig_has[static_cast<size_t>(sig[static_cast<size_t>(child)])];
    }
    auto [it, added] = interned.emplace(content, static_cast<int32_t>(sig_content.size()));
    if (added) {
      sig_content.push_back(std::move(content));
      sig_has.push_back(has);
    }
    sig[static_cast<size_t>(c)] = it->second;
  }

  // ---- one definition per signature: `top` in place, a fresh copy for any other signature with facts ----
  std::vector<std::shared_ptr<hhds::Graph>> graph_of(sig_content.size());
  absl::flat_hash_map<const hhds::Graph*, const hhds::Graph*> copied_from;
  graph_of[static_cast<size_t>(sig[0])] = root;
  absl::flat_hash_set<std::string> used;
  for (int32_t c = 1; c < n; ++c) {
    const auto s = static_cast<size_t>(sig[static_cast<size_t>(c)]);
    if (graph_of[s]) {
      continue;
    }
    auto def = by_gid.find(origin.contexts[static_cast<size_t>(c)].def);
    if (def == by_gid.end()) {
      continue;  // a callee outside the private copy: left as compiled (never gains facts below)
    }
    if (!sig_has[s]) {
      graph_of[s] = def->second;
      continue;
    }
    std::string text;
    for (auto v : sig_content[s]) {
      text += std::format("{},", v);
    }
    auto name = std::format("{}__f{:016x}", def->second->get_name(), livehd::hash_util::fnv1a64(text));
    for (int k = 1; used.contains(name) || library->find_io(name); ++k) {
      name = std::format("{}__f{:016x}_{}", def->second->get_name(), livehd::hash_util::fnv1a64(text), k);
    }
    used.insert(name);
    graph_of[s] = specialize::clone_definition(definitions, def->second, name);
    copied_from.emplace(graph_of[s].get(), def->second.get());
    ++stats.copies;
  }

  Nid_index index;
  // Re-point each instance in a copy at its own signature's copy.
  for (int32_t c = 1; c < n; ++c) {
    const auto& ctx = origin.contexts[static_cast<size_t>(c)];
    const auto  ps  = static_cast<size_t>(sig[static_cast<size_t>(ctx.parent)]);
    const auto  s   = static_cast<size_t>(sig[static_cast<size_t>(c)]);
    if (!sig_has[ps] || !graph_of[ps] || !graph_of[s]) {
      continue;
    }
    auto node = index.find(*graph_of[ps], ctx.sub);
    if (node.is_invalid() || gu::type_op_of(node) != Ntype_op::Sub) {
      continue;
    }
    auto callee = node.get_subnode_graph();
    if (!callee || callee.get() != graph_of[s].get()) {
      node.set_subnode(graph_of[s]->get_io());
    }
  }
  // pass.color already ran: give the copies the per-path colors of the instances now pointing at them.
  rehome_hier_colors(definitions, root, copied_from);

  // Fold each signature's own facts into its definition, once.
  absl::flat_hash_set<const hhds::Graph*> done;
  for (int32_t c = 0; c < n; ++c) {
    const auto s = static_cast<size_t>(sig[static_cast<size_t>(c)]);
    auto&      g = graph_of[s];
    if (!g || own[static_cast<size_t>(c)].empty() || !done.insert(g.get()).second) {
      continue;
    }
    if (g != root && !std::ranges::any_of(definitions, [&](const auto& d) { return d.get() == g.get(); })) {
      continue;
    }
    absl::flat_hash_set<uint64_t> existed;
    for (auto node : g->body().nodes()) {
      existed.insert(node.get_debug_nid());
    }
    std::map<hhds::Nid, std::vector<std::pair<int, int8_t>>> per_flop;
    for (const auto& [nid, bit, value] : own[static_cast<size_t>(c)]) {
      per_flop[nid].emplace_back(bit, value);
    }
    bool changed = false;
    for (const auto& [nid, bits] : per_flop) {
      if (bits.size() == 1 && bits.front().first < 0) {
        if (sweep::drop_memory(*g, index.find(*g, nid))) {
          ++stats.memories_dropped;
          changed = true;
        }
        continue;
      }
      auto flop = index.find(*g, nid);
      if (flop.is_invalid() || gu::type_op_of(flop) != Ntype_op::Flop) {
        if (debug) {
          std::print(stderr, "flat-state miss {} nid={} bits={} (no such flop)\n", g->get_name(), nid, bits.size());
        }
        continue;
      }
      const int w = gu::bits_of(flop.get_driver_pin(0));
      if (w <= 0) {
        continue;
      }
      std::vector<int8_t> replace(static_cast<size_t>(w), -1);
      uint64_t            count = 0;
      for (const auto& [bit, value] : bits) {
        if (bit < w) {
          replace[static_cast<size_t>(bit)] = value > 1 ? int8_t{0} : value;  // an unobserved bit reads 0
          ++count;
        }
      }
      if (sweep::fold_flop_bits(*g, flop, replace)) {
        stats.folded_bits += count;
        changed            = true;
      } else if (debug) {
        std::print(stderr,
                   "flat-state refused {} {} w={} bits={} signed={}\n",
                   g->get_name(),
                   gu::node_name_of(flop),
                   w,
                   count,
                   !gu::is_unsign(flop.get_driver_pin(0)));
      }
    }
    if (changed) {
      ++stats.graphs_folded;
      specialize::refold_private_body(g, specialize::Refold_cost::synth, "synth.flat_state");
      specialize::inherit_colors(*g, &existed);
    }
  }

  // A folded copy whose output is now a constant: its parent (a copy too, or `top`) reads that constant. The copy is
  // instantiated only where the proof holds, so the output is that constant at every one of its instances. Repeat:
  // the folded parent may now return a constant to its own parent.
  std::vector<std::shared_ptr<hhds::Graph>> ours;
  for (size_t s = 0; s < graph_of.size(); ++s) {
    if (graph_of[s] && sig_has[s] && std::ranges::none_of(ours, [&](const auto& g) { return g.get() == graph_of[s].get(); })) {
      ours.push_back(graph_of[s]);
    }
  }
  for (int round = 0; round < 16; ++round) {
    bool any = false;
    for (const auto& g : ours) {
      absl::flat_hash_set<uint64_t> existed;
      for (auto node : g->body().nodes()) {
        existed.insert(node.get_debug_nid());
      }
      bool                          changed = false;
      std::vector<hhds::Node_class> subs;
      for (auto node : g->body().nodes()) {
        if (gu::type_op_of(node) == Ntype_op::Sub) {
          subs.push_back(node);
        }
      }
      for (const auto& node : subs) {
        auto callee = node.get_subnode_graph();
        auto io     = callee ? callee->get_io() : std::shared_ptr<hhds::GraphIO>{};
        if (!io || node.subnode_loop()) {
          continue;
        }
        for (const auto& decl : io->get_output_pin_decls()) {
          const auto out = node.get_driver_pin(decl.port_id);
          if (out.is_invalid() || out.out_edges().empty()) {
            continue;
          }
          const auto drv = callee->get_output_pin(decl.name).get_driver_pin();
          if (drv.is_invalid() || !drv.is_const() || gu::const_of(drv).has_unknowns()) {
            continue;
          }
          const auto value  = gu::create_const(*g, gu::const_of(drv));
          const auto fitted = gu::fit_to_port(*g, value, std::max<int>(1, static_cast<int>(decl.bits)), !decl.unsign);
          for (const auto& e : gu::Edge_vec(out.out_edges().begin(), out.out_edges().end())) {
            e.sink.del_sink(out);
            fitted.connect_sink(e.sink);
          }
          changed = true;
        }
      }
      if (changed) {
        specialize::refold_private_body(g, specialize::Refold_cost::synth, "synth.flat_state");
        specialize::inherit_colors(*g, &existed);
        ++stats.outputs_folded;
        any = true;
      }
    }
    if (!any) {
      break;
    }
  }
  if (debug) {
    std::print(
        stderr,
        "flat-state contexts={} signatures={} copies={} folded_bits={} memories={} graphs={} lost_bits={} outputs_folded={}\n",
        stats.contexts,
        sig_content.size(),
        stats.copies,
        stats.folded_bits,
        stats.memories_dropped,
        stats.graphs_folded,
        stats.lost_bits,
        stats.outputs_folded);
  }
  return stats;
}

namespace {

// Rewire every reader of `pin` in `g` to read it with `zero` bits cleared: a constant 0 when every bit is cleared,
// else `pin & ~zero`. Returns false when `pin` has no reader.
bool read_as_zero(hhds::Graph& g, const hhds::Pin_class& pin, const std::vector<bool>& zero, int bits, bool is_signed) {
  if (pin.is_invalid() || pin.out_edges().empty()) {
    return false;
  }
  const gu::Edge_vec readers(pin.out_edges().begin(), pin.out_edges().end());
  hhds::Pin_class    value;
  if (std::ranges::all_of(zero, [](bool z) { return z; })) {
    value = gu::fit_to_port(g, gu::create_const(g, *Dlop::create_integer(0)), std::max(1, bits), is_signed);
  } else {
    std::vector<bool> keep(zero.size());
    for (size_t i = 0; i < zero.size(); ++i) {
      keep[i] = !zero[i];
    }
    value = bit_facts::mask_pin(g, pin, keep);
  }
  const auto mask_node = value.is_const() ? hhds::Node_class{} : value.get_master_node();
  for (const auto& e : readers) {
    if (!mask_node.is_invalid() && e.sink.get_master_node() == mask_node) {
      continue;
    }
    e.sink.del_sink(pin);
    value.connect_sink(e.sink);
  }
  return true;
}

// Bits of `zero` not yet in `applied` (both `bits` wide); merges them into `applied`.
uint64_t new_zero_bits(const std::vector<bool>& zero, std::vector<bool>& applied) {
  uint64_t n = 0;
  for (size_t i = 0; i < zero.size() && i < applied.size(); ++i) {
    if (zero[i] && !applied[i]) {
      applied[i] = true;
      ++n;
    }
  }
  return n;
}

}  // namespace

void rehome_hier_colors(const std::vector<std::shared_ptr<hhds::Graph>>& definitions, const std::shared_ptr<hhds::Graph>& root,
                        const absl::flat_hash_map<const hhds::Graph*, const hhds::Graph*>& original_of) {
  if (!root || original_of.empty()) {
    return;
  }
  absl::flat_hash_set<const hhds::Graph*> ours;
  for (const auto& g : definitions) {
    if (g) {
      ours.insert(g.get());
    }
  }
  const auto original = [&](const hhds::Graph* g) {
    const auto it = original_of.find(g);
    return it == original_of.end() ? g : it->second;
  };
  // Each original's entries grouped by path (root + steps), so a reached (copy, path) pair copies its group at once.
  using Path_key = hhds::Hier_attr_key;
  absl::flat_hash_map<const hhds::Graph*,
                      absl::flat_hash_map<Path_key, std::vector<std::pair<hhds::Attr_key, int32_t>>, hhds::Hier_attr_key_hash>>
             grouped;
  const auto groups_of = [&](const hhds::Graph* orig) -> const auto& {
    auto [it, added] = grouped.try_emplace(orig);
    if (added) {
      if (const auto* store = orig->find_attr_store(livehd::attrs::hier_color)) {
        for (const auto& [key, color] : *store) {
          Path_key path{key.root_gid, key.steps, 0};
          it->second[path].emplace_back(key.flat_key, color);
        }
      }
    }
    return it->second;
  };
  const auto root_gid = root->get_gid();
  struct Frame {
    hhds::Graph*                      graph;
    std::vector<hhds::Hier_attr_step> new_steps;
    std::vector<hhds::Hier_attr_step> old_steps;
  };
  std::vector<Frame> work;
  work.push_back({root.get(), {}, {}});
  uint64_t guard = 0;
  while (!work.empty()) {
    auto frame = std::move(work.back());
    work.pop_back();
    if (++guard > 50'000'000) {
      break;  // a malformed (cyclic) hierarchy: leave the rest as is
    }
    auto*      g    = frame.graph;
    const auto orig = original(g);
    if (orig != g || frame.new_steps != frame.old_steps) {
      const auto& groups = groups_of(orig);
      if (const auto it = groups.find(Path_key{root_gid, frame.old_steps, 0}); it != groups.end()) {
        for (const auto& [flat_key, color] : it->second) {
          hhds::AttrRef<livehd::attrs::hier_color_t>{g, flat_key, root_gid, frame.new_steps}.set(color);
        }
      }
    }
    for (auto node : g->body().nodes()) {
      if (gu::type_op_of(node) != Ntype_op::Sub || node.subnode_loop()) {
        continue;
      }
      auto callee = node.get_subnode_graph();
      if (!callee || !ours.contains(callee.get())) {
        continue;
      }
      const auto site = node.get_definition_index();
      Frame      next{callee.get(), frame.new_steps, frame.old_steps};
      next.new_steps.push_back({site.gid, site.value, std::nullopt});
      next.old_steps.push_back({orig->get_gid(), site.value, std::nullopt});
      work.push_back(std::move(next));
    }
  }
}

std::optional<Const_bits_stats> propagate_constant_bits(std::vector<std::shared_ptr<hhds::Graph>>& definitions,
                                                        std::string_view top, Preparation_budget* budget) {
  Const_bits_stats stats;
  if (livehd::debug::env<"LHD_NO_CONST_BITS">() != nullptr) {
    return stats;
  }
  const bool                                                   debug = livehd::debug::env<"LHD_FLAT_STATE_DEBUG">() != nullptr;
  absl::flat_hash_map<const hhds::Graph*, std::shared_ptr<hhds::Graph>> ours;
  for (const auto& g : definitions) {
    if (g && g->get_io()) {
      ours.emplace(g.get(), g);
    }
  }
  const auto callee_of = [&](const hhds::Node_class& node) -> std::shared_ptr<hhds::Graph> {
    if (gu::type_op_of(node) != Ntype_op::Sub) {
      return nullptr;
    }
    auto callee = node.get_subnode_graph();
    if (!callee || !ours.contains(callee.get())) {
      return nullptr;
    }
    return ours.at(callee.get());
  };
  // Children before parents.
  std::vector<std::shared_ptr<hhds::Graph>> post;
  absl::flat_hash_set<const hhds::Graph*>   seen;
  std::function<void(const std::shared_ptr<hhds::Graph>&)> visit = [&](const std::shared_ptr<hhds::Graph>& g) {
    if (!seen.insert(g.get()).second) {
      return;
    }
    for (auto node : g->body().nodes()) {
      if (auto callee = callee_of(node)) {
        visit(callee);
      }
    }
    post.push_back(g);
  };
  for (const auto& g : definitions) {
    if (g && g->get_io()) {
      visit(g);
    }
  }

  const auto refold = [&](const std::shared_ptr<hhds::Graph>& g, const absl::flat_hash_set<uint64_t>& existed) {
    specialize::refold_private_body(g, specialize::Refold_cost::synth, "synth.const_bits");
    specialize::inherit_colors(*g, &existed);
    ++stats.graphs_folded;
  };
  const auto nids_of = [](const hhds::Graph& g) {
    absl::flat_hash_set<uint64_t> existed;
    for (auto node : const_cast<hhds::Graph&>(g).body().nodes()) {
      existed.insert(node.get_debug_nid());
    }
    return existed;
  };

  // (parent, instance nid, output port) -> zeros a parent already reads; (definition, input port) -> zeros applied inside
  absl::flat_hash_map<std::tuple<const hhds::Graph*, hhds::Nid, hhds::Port_id>, std::vector<bool>> out_applied;
  absl::flat_hash_map<std::pair<const hhds::Graph*, hhds::Port_id>, std::vector<bool>>             in_applied;

  for (int round = 0; round < 16; ++round) {
    if (budget != nullptr && !budget->check("const-bits")) {
      return std::nullopt;
    }
    bool any = false;
    // ---- outputs: a parent reads its instances' zero output bits as 0 (children first, so a pass-through parent's own
    // output zeros are visible to its parent in the same round)
    for (const auto& g : post) {
      std::vector<hhds::Node_class> subs;
      for (auto node : g->body().nodes()) {
        if (callee_of(node) && !node.subnode_loop()) {
          subs.push_back(node);
        }
      }
      if (subs.empty()) {
        continue;
      }
      auto existed = nids_of(*g);
      bool changed = false;
      for (const auto& node : subs) {
        const auto callee = callee_of(node);
        for (const auto& decl : callee->get_io()->get_output_pin_decls()) {
          const int bits = static_cast<int>(decl.bits);
          if (bits <= 0) {
            continue;
          }
          const auto out = node.get_driver_pin(decl.port_id);
          if (out.is_invalid() || out.out_edges().empty()) {
            continue;
          }
          const auto drv = callee->get_output_pin(decl.name).get_driver_pin();
          if (drv.is_invalid()) {
            continue;
          }
          const auto zero    = bit_facts::known_zero_bits(drv, bits);
          auto&      applied = out_applied[{g.get(), node.get_debug_nid(), decl.port_id}];
          if (applied.empty()) {
            applied.assign(static_cast<size_t>(bits), false);
          }
          const auto fresh = new_zero_bits(zero, applied);
          if (fresh == 0) {
            continue;
          }
          if (read_as_zero(*g, out, applied, bits, !decl.unsign)) {
            stats.output_bits += fresh;
            changed = true;
          }
        }
      }
      if (changed) {
        refold(g, existed);
        any = true;
      }
    }
    // ---- inputs: a definition reads as 0 the input bits EVERY instance drives with 0
    struct In_facts {
      bool                                                        unknown = false;  // an instance the analysis cannot see
      uint64_t                                                    count   = 0;
      absl::flat_hash_map<hhds::Port_id, std::vector<bool>>       zero;
    };
    absl::flat_hash_map<const hhds::Graph*, In_facts> facts;
    for (const auto& g : post) {
      for (auto node : g->body().nodes()) {
        const auto callee = callee_of(node);
        if (!callee) {
          continue;
        }
        auto& f = facts[callee.get()];
        if (node.subnode_loop()) {
          f.unknown = true;
          continue;
        }
        absl::flat_hash_map<hhds::Port_id, hhds::Pin_class> driver_of;
        for (const auto& sink : node.inp_sorted_pins()) {
          driver_of.emplace(sink.get_port_id(), sink.get_driver_pin());
        }
        ++f.count;
        for (const auto& decl : callee->get_io()->get_input_pin_decls()) {
          const int bits = static_cast<int>(decl.bits);
          if (bits <= 0) {
            continue;
          }
          auto       it   = driver_of.find(decl.port_id);
          const auto zero = it == driver_of.end() || it->second.is_invalid() ? std::vector<bool>(static_cast<size_t>(bits), false)
                                                                             : bit_facts::known_zero_bits(it->second, bits);
          auto [z, added] = f.zero.try_emplace(decl.port_id, zero);
          if (!added) {
            for (size_t i = 0; i < z->second.size(); ++i) {
              z->second[i] = z->second[i] && i < zero.size() && zero[i];
            }
          }
        }
      }
    }
    for (auto it = post.rbegin(); it != post.rend(); ++it) {  // parents first
      const auto& g  = *it;
      auto        fi = facts.find(g.get());
      if (g->get_name() == top || fi == facts.end() || fi->second.unknown || fi->second.count == 0) {
        continue;
      }
      auto existed = nids_of(*g);
      bool changed = false;
      for (const auto& decl : g->get_io()->get_input_pin_decls()) {
        auto z = fi->second.zero.find(decl.port_id);
        if (z == fi->second.zero.end()) {
          continue;
        }
        const int bits    = static_cast<int>(decl.bits);
        auto&     applied = in_applied[{g.get(), decl.port_id}];
        if (applied.empty()) {
          applied.assign(static_cast<size_t>(bits), false);
        }
        const auto fresh = new_zero_bits(z->second, applied);
        if (fresh == 0) {
          continue;
        }
        if (read_as_zero(*g, g->get_input_pin(decl.name), applied, bits, !decl.unsign)) {
          stats.input_bits += fresh;
          changed = true;
        }
      }
      if (changed) {
        refold(g, existed);
        any = true;
      }
    }
    if (!any) {
      break;
    }
    ++stats.rounds;
  }
  if (debug) {
    std::print(stderr, "const-bits rounds={} output_bits={} input_bits={} refolds={}\n", stats.rounds, stats.output_bits,
               stats.input_bits, stats.graphs_folded);
  }
  return stats;
}

}  // namespace livehd::synth

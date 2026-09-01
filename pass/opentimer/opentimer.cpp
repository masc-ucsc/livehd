//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <chrono>
#include <concepts>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "cell.hpp"
#include "flatten.hpp"
#include "hash_util.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "json_util.hpp"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"
#include "pass_opentimer.hpp"
#include "pass_partition.hpp"
#include "pin_tracker.hpp"
#include "semdiff.hpp"
#include "sta_salt.hpp"
#include "str_tools.hpp"

// WARNING: opentimer has a nasty "define has_member" that overlaps with perfetto methods
#undef has_member
#include "perf_tracing.hpp"

using livehd::graph_util::bits_of;
using livehd::graph_util::color_of;
using livehd::graph_util::create_const;
using livehd::graph_util::debug_name;
using livehd::graph_util::default_instance_name;
using livehd::graph_util::get_driver_of_sink_name;
using livehd::graph_util::has_color;
using livehd::graph_util::hydrate_const;
using livehd::graph_util::is_const_pin;
using livehd::graph_util::is_graph_input_pin;
using livehd::graph_util::is_graph_output_pin;
using livehd::graph_util::set_color;
using livehd::graph_util::type_op_of;
using livehd::graph_util::wire_name;

namespace {

// A Sub-instance pin's declared name looked up in the sub-graph's GraphIO by
// port id. Instance pins loaded from an lg: library may carry no pin_name of
// their own (only the def declares names), so resolving through the decl is
// what keeps OT pin names equal to the Liberty pin names.
template <typename Node>
[[nodiscard]] std::string sub_pin_name_from_decl(const Node& node, hhds::Port_id pid, bool is_driver) {
  auto io = node.get_subnode_io();
  if (!io) {
    return {};
  }
  const auto& decls = is_driver ? io->get_output_pin_decls() : io->get_input_pin_decls();
  for (const auto& d : decls) {
    if (d.port_id == pid) {
      return d.name;
    }
  }
  return {};
}

// Sink-pin name for a node + sink port. For Sub nodes the name is the
// sub-graph's IO declared name; for other nodes it's Ntype's sink_name.
template <typename Node, typename Pin>
[[nodiscard]] std::string sink_pin_name_of(const Node& node, const Pin& sink) {
  auto pid = sink.get_port_id();
  if (type_op_of(node) == Ntype_op::Sub) {
    auto n = sink.get_pin_name();
    if (!n.empty()) {
      return std::string{n};
    }
    if (auto dn = sub_pin_name_from_decl(node, pid, /*is_driver=*/false); !dn.empty()) {
      return dn;
    }
    return std::to_string(static_cast<uint32_t>(pid));
  }
  return Ntype::get_sink_name(type_op_of(node), pid);
}

// Driver-pin name for a node + driver port (used to name OT cell pins).
template <typename Node, typename Pin>
[[nodiscard]] std::string driver_pin_name_of(const Node& node, const Pin& dpin) {
  if (type_op_of(node) == Ntype_op::Sub) {
    auto n = dpin.get_pin_name();
    if (!n.empty()) {
      return std::string{n};
    }
    if (auto dn = sub_pin_name_from_decl(node, dpin.get_port_id(), /*is_driver=*/true); !dn.empty()) {
      return dn;
    }
    return std::to_string(static_cast<uint32_t>(dpin.get_port_id()));
  }
  return std::string{Ntype::get_driver_name(type_op_of(node))};
}

// Sub-graph cell type name (the module being instantiated).
template <typename Node>
[[nodiscard]] std::string sub_type_name(const Node& node) {
  auto io = node.get_subnode_io();
  if (!io) {
    return {};
  }
  return std::string{io->get_name()};
}

// Net name of a driver pin. In flat mode it is the plain wire_name. In the
// whole-design (flattened) walk an internal pin's net is its MASTER NODE's
// dotted hier path plus the numeric port id — mirroring wire_name's structure
// but with a per-instance prefix. Keying on the master node + port id (not the
// pin's own get_hier_name) is essential: node.out_pins() yields a driver pin
// with no pin_name while inp_edges().driver yields the SAME pin carrying its
// port name, so the pin's own get_hier_name differs between the two (one omits
// the port suffix, one appends it) and the driver/consumer nets would not meet.
// The master node's get_hier_name is the stable, representation-independent id
// (the same primitive LEC keys flops on). Module-IO pins keep their decl name.
//
// get_hier_name deliberately describes named hierarchy, not every virtual
// occurrence of a repeated site. OpenTimer, however, builds one physical timing
// occurrence per virtual lane. Qualify it with a stable hash of the complete
// occurrence path so repeated lanes receive distinct gate/net names without
// physically unrolling the source LGraph. Hashing keeps the already-long Minion
// names bounded; the same resolved driver path yields the same suffix at each
// consumer.
template <typename Node>
[[nodiscard]] std::string occurrence_name(const Node& node) {
  std::string name{node.get_hier_name()};
  if constexpr (std::same_as<Node, hhds::Occurrence_node>) {
    const auto path = node.get_occurrence_index().path;
    if (!path.steps().empty()) {
      uint64_t   hash = livehd::hash_util::kFnv1a64_offset;
      const auto mix  = [&](uint64_t value) { hash = livehd::hash_util::fnv1a64_u64(value, hash); };
      mix(static_cast<uint64_t>(path.root_gid()));
      for (const auto& step : path.steps()) {
        mix(static_cast<uint64_t>(step.subnode.gid));
        mix(static_cast<uint64_t>(step.subnode.value));
        mix(step.ordinal ? static_cast<uint64_t>(*step.ordinal) + 1 : 0);
      }
      absl::StrAppend(&name, "__occ", std::format("{:016x}", hash));
    }
  }
  return name;
}

template <typename Pin>
[[nodiscard]] std::string net_of(const Pin& dpin, bool hier) {
  if (!hier) {
    if (is_graph_input_pin(dpin) || is_graph_output_pin(dpin)) {
      if constexpr (std::same_as<Pin, hhds::Occurrence_pin>) {
        return wire_name(dpin.base_pin());
      } else {
        return wire_name(dpin);
      }
    }
    const auto suffix = absl::StrCat("__n", dpin.get_master_node().get_debug_nid());
    if constexpr (std::same_as<Pin, hhds::Occurrence_pin>) {
      return absl::StrCat(wire_name(dpin.base_pin()), suffix);
    } else {
      return absl::StrCat(wire_name(dpin), suffix);
    }
  }
  if (is_graph_input_pin(dpin) || is_graph_output_pin(dpin)) {
    return wire_name(dpin);  // module IO: the declared port name (root level)
  }
  auto master = dpin.get_master_node();
  auto base   = occurrence_name(master);
  auto pid    = dpin.get_port_id();
  return pid == 0 ? base : absl::StrCat(base, "_", static_cast<uint32_t>(pid));
}

// Same net name, but for a DRIVER pin obtained from the traversal node's
// out_pins()/create_driver_pin(): those pins do NOT carry the hier instance
// chain (dpin.get_master_node().get_hier_name() drops the prefix), whereas the
// traversal `owner` node does. A pin resolved through a hier edge (e.driver)
// keeps its chain, so consumers use net_of(); this owner-based form is the
// matching driver-side spelling. Both yield the same string for the same gate.
template <typename Node, typename Pin>
[[nodiscard]] std::string net_of_node(const Node& owner, const Pin& dpin, bool hier) {
  if (!hier) {
    if (is_graph_input_pin(dpin) || is_graph_output_pin(dpin)) {
      if constexpr (std::same_as<Pin, hhds::Occurrence_pin>) {
        return wire_name(dpin.base_pin());
      } else {
        return wire_name(dpin);
      }
    }
    const auto suffix = absl::StrCat("__n", owner.get_debug_nid());
    if constexpr (std::same_as<Pin, hhds::Occurrence_pin>) {
      return absl::StrCat(wire_name(dpin.base_pin()), suffix);
    } else {
      return absl::StrCat(wire_name(dpin), suffix);
    }
  }
  if (is_graph_input_pin(dpin) || is_graph_output_pin(dpin)) {
    return wire_name(dpin);
  }
  auto base = occurrence_name(owner);
  auto pid  = dpin.get_port_id();
  return pid == 0 ? base : absl::StrCat(base, "_", static_cast<uint32_t>(pid));
}

// Gate/instance name of a node: hier-unique get_hier_name when flattening, else
// the flat instance name.
template <typename Node>
[[nodiscard]] std::string inst_of(const Node& node, bool hier) {
  if (hier) {
    return occurrence_name(node);
  }
  if constexpr (std::same_as<Node, hhds::Occurrence_node>) {
    return absl::StrCat(default_instance_name(node.base_node()), "__n", node.get_debug_nid());
  } else {
    return absl::StrCat(default_instance_name(node), "__n", node.get_debug_nid());
  }
}

// Per-pin delay annotation helpers (replaces Lgraph's Node_pin::set_delay /
// has_delay / get_delay / del_delay).
inline void set_delay(const hhds::Pin_class& pin, float d) {
  if (pin.is_invalid()) {
    return;
  }
  pin.attr(livehd::attrs::pin_delay).set(d);
}
inline bool has_delay(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return false;
  }
  return pin.attr(livehd::attrs::pin_delay).has();
}
inline float get_delay(const hhds::Pin_class& pin) { return pin.attr(livehd::attrs::pin_delay).get(); }
inline void  del_delay(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return;
  }
  pin.attr(livehd::attrs::pin_delay).del();
}
inline void set_delay(const hhds::Occurrence_pin& pin, float d) { set_delay(pin.base_pin(), d); }
inline void del_delay(const hhds::Occurrence_pin& pin) { del_delay(pin.base_pin()); }

std::string jesc(std::string_view text) { return livehd::json_util::escape(text); }

// "file:line" of a node's srcid (empty when absent/unresolvable). Mapped gates
// carry the srcid of the output cone they feed (pass.abc carry-through), so a
// critical pin points back at the pre-synthesis RTL.
template <typename Node>
std::string src_of_node(const std::shared_ptr<hhds::Graph>& g, const Node& n) {
  const auto base = [&]() {
    if constexpr (std::same_as<Node, hhds::Occurrence_node>) {
      return n.base_node();
    } else {
      return n;
    }
  }();
  auto a = base.attr(hhds::attrs::srcid);
  if (!a.has() || a.get() == 0) {
    return {};
  }
  // A flattened leaf lives in a child graph, so its srcid indexes that child's
  // source map, not the top's — resolve against the node's own graph.
  auto* ng   = base.get_graph();
  auto  span = (ng != nullptr ? ng : g.get())->source_locator().resolve_span(a.get());
  if (span.file.empty() || !span.start_line.has_value()) {
    return {};
  }
  return span.file + ":" + std::to_string(*span.start_line);
}

}  // namespace

void Pass_opentimer::time_work(Eprp_var& var) {
  // Private physical library for the occurrence expansion below: pass.opentimer
  // is not occurrence-aware (it times a Sub as ONE instance), so a rolled
  // design is copied here and expanded before anything reads it. Declared
  // FIRST, ahead of every other local, so it outlives each graph handle that
  // points into it (locals die in reverse declaration order).
  hhds::GraphLibrary occurrence_library;

  Pass_opentimer pass(var);

  TRACE_EVENT("pass", "OPENTIMER_work");

  // One ot::Timer holds ONE design: building several graphs into the same
  // timer silently merges them, so exactly one def is analyzed per run
  // (top_filter picks it out of a multi-def library).
  std::vector<std::shared_ptr<hhds::Graph>> selected;
  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    if (!pass.top_filter.empty() && g->get_name() != pass.top_filter) {
      continue;
    }
    selected.push_back(g);
  }
  if (selected.empty()) {
    livehd::diag::err("pass.opentimer", "no-top", "unsupported")
        .msg("pass.opentimer: no module{} found in the input library",
             pass.top_filter.empty() ? std::string{} : std::format(" named '{}'", pass.top_filter))
        .fatal();
    return;
  }
  if (selected.size() > 1) {
    livehd::diag::err("pass.opentimer", "bad-option", "usage")
        .msg("pass.opentimer times one module per run ({} defs in the library): pass --top <module> to pick one", selected.size())
        .fatal();
    return;
  }

  auto g = selected.front();

  // A design compiled WITHOUT `compile.unroll` -- false is the DEFAULT, so this
  // is the ordinary path -- keeps ONE compact Sub standing for `count`
  // occurrences. pass.opentimer is not occurrence-aware — all three hier modes
  // walk a Sub as a single physical instance — so area, path count and every
  // QoR number would come out short by count-1. Expand up front, for every
  // mode, into opentimer's private library: the input library belongs to the
  // caller and this pass only reads it. A design with no comptime loops has no
  // compact Sub; nothing is copied and the input graph is timed in place.
  const auto design_defs = g->definitions().graphs();
  if (pass.stats_) {
    // pass.abc stamps the mapped-region identity on each region graph's input
    // node. Seed from definitions rather than timed gates so a zero-cell/native
    // wiring color still gets its required row. The later flattened gate walk
    // fills occurrence-weighted cell counts and end-to-end arrivals.
    for (const auto& def : design_defs) {
      auto input  = def->get_input_node();
      auto region = input.attr(livehd::attrs::synth_region);
      if (!region.has()) {
        continue;
      }
      Pass_opentimer::Color_qor row;
      if (auto id = input.attr(livehd::attrs::synth_region_id); id.has()) {
        row.region_id = id.get();
      }
      row.module = std::string{region.get()};
      if (auto color = input.attr(livehd::attrs::color); color.has()) {
        row.color = color.get();
      }
      row.resynth = input.attr(livehd::attrs::resynth).has();
      pass.color_qor_.push_back(std::move(row));
    }
    std::sort(pass.color_qor_.begin(), pass.color_qor_.end(), [](const auto& lhs, const auto& rhs) {
      return std::tie(lhs.module, lhs.color) < std::tie(rhs.module, rhs.color);
    });
  }
  // ---- STA reuse (sta_cache.hpp) -------------------------------------------
  // Keyed on the NETLIST (canonical digest, Merkle-folded through every region
  // body) plus the timing environment, so it hits exactly when re-timing would
  // reproduce the stored report. Placed before the occurrence expansion and the
  // hierarchy flatten: those are deterministic functions of `g` (the digest
  // folds each compact Sub's loop descriptor), so a hit skips them too -- and
  // they, plus build_circuit, are where the seconds are.
  std::unique_ptr<livehd::opentimer::Sta_cache> sta_cache;
  std::string                                   sta_key;
  if (!pass.cache_dir_.empty()) {
    const auto t0         = std::chrono::steady_clock::now();
    pass.cache_enabled_   = true;
    sta_cache             = std::make_unique<livehd::opentimer::Sta_cache>(pass.cache_dir_, livehd::opentimer::kStaSrcSalt);
    sta_key               = pass.cache_key(g);
    const auto* rec       = sta_key.empty() ? nullptr : sta_cache->lookup(sta_key);
    pass.cache_lookup_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (rec != nullptr) {
      pass.cache_hit_ = true;
      pass.replay(*rec);
      pass.write_qor();
      return;
    }
  }

  bool has_compact = false;
  for (const auto& def : design_defs) {
    for (auto n : def->body().nodes()) {
      // Deliberately NO get_subnode_graph() test: a body-less replicated black
      // box (a liberty/IP cell instantiated in a loop) is a compact group too —
      // pass/partition/flatten.cpp re-stamps its loop descriptor rather than
      // collapsing it, and materialize_occurrences keys off this flag alone.
      if (n.is_loop_subnode()) {
        has_compact = true;
        break;
      }
    }
    if (has_compact) {
      break;
    }
  }
  if (has_compact) {
    // copy_from is DEFINITION-LOCAL and a copied parent resolves
    // get_subnode_graph() through the DESTINATION library only, so the whole
    // callee closure has to come along — and every def copied gets
    // materialized, not just the top: a compact loop nested in a callee is
    // timed too (and, under hier=true, inlined by the flattener).
    auto  io  = g->get_io();
    auto* lib = io ? io->get_library() : nullptr;
    if (lib == nullptr) {
      livehd::diag::err("pass.opentimer", "scratch-copy", "internal")
          .msg("could not copy '{}' into opentimer's private physical library", g->get_name())
          .emit();
      return;
    }
    std::vector<std::shared_ptr<hhds::Graph>> occurrence_graphs;
    for (const auto& def : design_defs) {
      if (!occurrence_library.find_io(def->get_name())) {  // shared callee may already be copied
        if (!occurrence_library.copy_from(*lib, def->get_name())) {
          livehd::diag::err("pass.opentimer", "scratch-copy", "internal")
              .msg("could not copy '{}' into opentimer's private physical library", def->get_name())
              .emit();
          return;
        }
        auto def_io = occurrence_library.find_io(def->get_name());
        occurrence_graphs.push_back(def_io ? def_io->get_graph() : std::shared_ptr<hhds::Graph>{});
      }
      // Body-less defs (the liberty/tie cells that dominate a mapped netlist)
      // are IO-only, so definitions() never lists them as graphs. Clone the
      // decls this body references: without them the copied Sub instances
      // resolve to no subnode, and expansion/flatten drop the black box.
      for (auto n : def->body().nodes()) {
        if (livehd::graph_util::is_type_sub(n) && n.get_subnode_graph() == nullptr) {
          livehd::partition::resolve_or_clone_subdef(&occurrence_library, n);
        }
      }
    }
    auto top_io   = occurrence_library.find_io(g->get_name());
    auto top_copy = top_io ? top_io->get_graph() : std::shared_ptr<hhds::Graph>{};
    if (!top_copy) {
      livehd::diag::err("pass.opentimer", "scratch-copy", "internal")
          .msg("could not copy '{}' into opentimer's private physical library", g->get_name())
          .emit();
      return;
    }
    if (!livehd::graph_util::materialize_occurrences_all(occurrence_graphs, "pass.opentimer")) {
      return;  // diag already emitted
    }
    g = top_copy;
  }

  // Whole-design timing (`hier=true`): structurally inline the instance
  // hierarchy into a scratch def (pass/partition's flatten_hierarchy) and time
  // THAT through the classic single-module path below. The legacy forward_hier
  // walk kept module boundaries and stitched nets across them by name — the
  // multi-bit pass.abc get_mask/set_mask bus packing was never stitched, so
  // real netlists drowned in "can't connect pin" errors. Erasing the
  // boundaries instead makes the boundary glue plain class-context rewiring,
  // exactly what the pin tracker already handles in every mapped region. The
  // walk stays reachable as `hier=stitch` for debugging/comparison. The
  // scratch def exists only in memory (pass opentimer never saves the input
  // library) and is deleted after the run; node names carry the dotted
  // instance path, so reports still read hierarchically.
  std::shared_ptr<hhds::Graph> scratch;
  hhds::GraphLibrary*          scratch_lib = nullptr;
  std::string                  scratch_name;
  if (pass.hier_setting_ == "true") {
    bool has_hier = false;
    for (auto n : g->body().nodes(hhds::Node_order::forward)) {
      if (livehd::graph_util::is_type_sub(n) && n.get_subnode_graph() != nullptr) {
        has_hier = true;
        break;
      }
    }
    if (has_hier) {
      pass.report_module_ = std::string{g->get_name()};  // reports keep the real top name

      // flatten_hierarchy hard-refuses a replicated Sub (splicing one body copy
      // would silently drop count-1 occurrences); the prologue above already
      // materialized every one of them, so `g` is safe to splice. When it was
      // copied, this library is opentimer's own; otherwise the scratch def
      // lands in the input library and is deleted at the end of the run.
      scratch_lib  = g->get_io()->get_library();
      scratch_name = pass.report_module_ + "__ot_flat_tmp";
      scratch      = livehd::partition::flatten_hierarchy(g.get(), scratch_lib, scratch_name);
      if (!scratch) {
        return;  // diag already emitted
      }
      g = scratch;
    }
  }

  pass.setup_hier(g);
  pass.build_circuit(g);  // ensure_libs() runs here: a cache hit above never parses a Liberty
  pass.read_sdc_spef();
  pass.compute_timing(g);
  pass.populate_table(g);
  {
    // The Liberty time unit as a label, for the report and the cache record.
    if (auto u = pass.timer.time_unit(); u) {
      auto         near = [](double a, double b) { return a > b * 0.999 && a < b * 1.001; };
      const double v    = u->value();
      if (near(v, 1e-6)) {
        pass.time_unit_label_ = "us";
      } else if (near(v, 1e-9)) {
        pass.time_unit_label_ = "ns";
      } else if (near(v, 1e-12)) {
        pass.time_unit_label_ = "ps";
      }
    }
  }
  pass.write_qor();
  // Store only an ERROR-FREE analysis: an incomplete timing graph (an
  // unconnected Liberty pin, a non-converging pin tracker) already failed the
  // run, and caching its report would replay the failure's numbers as a pass.
  if (sta_cache && !sta_key.empty() && !pass.qor_blocks_.empty() && !livehd::diag::sink().has_errors()) {
    sta_cache->insert(sta_key, pass.snapshot());
    sta_cache->save();
  }

  if (scratch) {
    scratch_lib->delete_graph(scratch);
    scratch = nullptr;
    scratch_lib->delete_graphio(scratch_name);
  }
}

// Legacy hier-walk mode selection. `hier=true` no longer lands here as a walk:
// time_work structurally flattens the design first (pass/partition
// flatten_hierarchy) and the flat single-module path times the scratch def —
// no cross-boundary stitching at all. This walk (forward_hier + name-keyed
// overwrite_hnet_ stitching) remains reachable as `hier=stitch` for
// debugging/comparison only: it chains combinational gate-to-gate paths across
// module boundaries but multi-bit buses crossing them (the pass.abc
// get_mask/set_mask packing glue) are not stitched — resolve_hier_driver
// returns those child module input ports as leaves, so a hierarchical top
// reports unconnected boundary nets.
void Pass_opentimer::setup_hier(const std::shared_ptr<hhds::Graph>& g) {
  const auto& lib     = timer.celllib(ot::MAX);
  auto        is_cell = [&](std::string_view name) { return lib && lib->cell(std::string{name}) != nullptr; };

  hier_mode_ = (hier_setting_ == "stitch");
  if (!hier_mode_) {
    return;
  }

  // Every instantiated Liberty cell in the hierarchy is a non-descend leaf; the
  // hier walk then yields those as gates and recurses only through design
  // modules. hier_range visits one instance per subnode at every depth.
  for (auto inst : g->grouped_hierarchy().instances()) {
    auto tg = inst.get_target_graph();
    if (tg && is_cell(tg->get_name())) {
      opaque_gids_.insert(inst.get_target_gid());
    }
  }
}

void Pass_opentimer::power_work(Eprp_var& var) {
  Pass_opentimer pass(var);

  TRACE_EVENT("pass", "OPENTIMER_work");

  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    pass.build_circuit(g);
    pass.read_sdc_spef();
    pass.read_vcd();
    pass.compute_power(g);
  }
}

std::string Pass_opentimer::get_driver_net_name(const hhds::Occurrence_pin& dpin) const {
  if (hier_mode_) {
    auto hn = net_of(dpin, true);
    auto it = overwrite_hnet_.find(hn);
    return it != overwrite_hnet_.end() ? it->second : hn;
  }
  auto it = overwrite_dpin2net.find(dpin.get_class_index());
  if (it != overwrite_dpin2net.end()) {
    return it->second;
  }
  return net_of(dpin, false);
}

std::string Pass_opentimer::driver_net_of(const hhds::Occurrence_node& owner, const hhds::Occurrence_pin& dpin) const {
  if (hier_mode_) {
    auto key = net_of_node(owner, dpin, true);
    auto it  = overwrite_hnet_.find(key);
    return it != overwrite_hnet_.end() ? it->second : key;
  }
  auto it = overwrite_dpin2net.find(dpin.get_class_index());
  if (it != overwrite_dpin2net.end()) {
    return it->second;
  }
  return net_of_node(owner, dpin, false);
}

std::vector<hhds::Occurrence_node> Pass_opentimer::leaf_nodes(const std::shared_ptr<hhds::Graph>& g) const {
  std::vector<hhds::Occurrence_node> v;
  const auto*                        opq  = opaque_gids_.empty() ? nullptr : &opaque_gids_;
  auto                               view = g->occurrences(opq);
  if (hier_mode_) {
    for (auto n : view.nodes(hhds::Node_order::forward)) {
      v.push_back(n);
    }
  } else {
    for (auto n : g->body().nodes()) {
      v.push_back(view.lift(n));
    }
  }
  return v;
}

void Pass_opentimer::read_vcd() {
  vcd_list.resize(vcd_file_list.size());

  for (auto i = 0u; i < vcd_file_list.size(); ++i) {
    const auto& f = vcd_file_list[i];

    bool ok = vcd_list[i].open(f);
    if (!ok) {
      livehd::diag::err("pass.opentimer", "missing-file", "io").msg("could not read vcd {} file", f).fatal();
    }
  }
}

// SDC and SPEF must be read after the circuit is created
void Pass_opentimer::read_sdc_spef() {
  for (const auto& f : sdc_file_list) {
    read_sdc(f);
  }
  for (const auto& f : spef_file_list) {
    timer.read_spef(f);
  }
}

void Pass_opentimer::set_input_delays(const std::string& pname) {
  timer.set_at(pname, ot::MIN, ot::FALL, 0.0);
  timer.set_at(pname, ot::MIN, ot::RISE, 0.0);
  timer.set_at(pname, ot::MAX, ot::FALL, 0.0);
  timer.set_at(pname, ot::MAX, ot::RISE, 0.0);

  timer.set_slew(pname, ot::MAX, ot::FALL, 0.0);
  timer.set_slew(pname, ot::MAX, ot::RISE, 0.0);
  timer.set_slew(pname, ot::MIN, ot::FALL, 0.0);
  timer.set_slew(pname, ot::MIN, ot::RISE, 0.0);
}

void Pass_opentimer::set_output_delays(const std::string& pname) {
  timer.set_rat(pname, ot::MIN, ot::FALL, 0.0);
  timer.set_rat(pname, ot::MIN, ot::RISE, 0.0);
  timer.set_rat(pname, ot::MAX, ot::FALL, 0.0);
  timer.set_rat(pname, ot::MAX, ot::RISE, 0.0);
}

// The `native-comb-boundary` warning, from the counters build_circuit filled.
// Factored out so a STA cache hit re-emits the identical diagnostic instead of
// dropping it (docs/opt_loop_incr.md: a warm run must be diagnostic-equal).
void Pass_opentimer::emit_boundary_warning() const {
  if (opaque_logic_nodes_ == 0) {
    return;
  }
  auto w = livehd::diag::warn("pass.opentimer", "native-comb-boundary", "unsupported");
  w.msg(
      "pass.opentimer cut {} native combinational-logic node(s) into zero-arrival timing boundaries; the reported delay "
      "covers mapped cones around them but is not an end-to-end score through the cut",
      opaque_logic_nodes_);
  if (!opaque_logic_examples_.empty()) {
    w.hint("remove the combinational feedback to obtain an all-standard-cell netlist and complete timing");
  }
  if (ambiguous_or_nodes_ != 0) {
    w.hint("technology-map overlapping OR inputs before pass.opentimer to model their Boolean delay");
    w.note(std::format("{} timing boundary node(s) contain overlapping live OR inputs", ambiguous_or_nodes_));
  }
  for (const auto& name : opaque_logic_examples_) {
    w.note(std::format("timing boundary: {}", name));
  }
  if (opaque_logic_nodes_ > opaque_logic_examples_.size()) {
    w.note(std::format("... and {} more node(s)", opaque_logic_nodes_ - opaque_logic_examples_.size()));
  }
  for (const auto& name : ambiguous_or_examples_) {
    w.note(std::format("overlapping OR timing boundary: {}", name));
  }
  w.emit();
}

void Pass_opentimer::build_circuit(const std::shared_ptr<hhds::Graph>& g) {
  // The Liberty parse is DEFERRED to here (a ~0.7 s sky130 read the STA reuse
  // cache must not pay on a hit), and this is the first place that needs it:
  // the queued cell names are validated against the loaded library. Every
  // caller — time_work and power_work — goes through here, so no caller has to
  // remember. Idempotent.
  ensure_libs();

  TRACE_EVENT("pass", "OPENTIMER_build_circuit");

  overwrite_dpin2net.clear();

  constexpr std::string_view kZeroNet = "__lhd_ot_zero__";
  Pin_tracker<std::string>   pin_tracker(std::string{kZeroNet});
  // A tracker result can be known zero (padding, an out-of-range unsigned
  // slice, or a shift fill). Give those cell inputs a real, driverless OT net:
  // constants have no transition/arrival, while falling back to the glue
  // node's synthetic name would leave the timing pin unconnected.
  timer.insert_net(std::string{kZeroNet});

  auto gio = g->get_io();

  // THE one "is this Sub a Liberty cell?" question, shared by the 3rd phase
  // (net population + pin tracker) and the 5th phase (gate instantiation). Two
  // phases answering it differently is how a wide boundary gets modelled as a
  // single Boolean pin in one place and as a black box in the other. Memoized
  // by DEF NAME: a mapped netlist has a handful of cell types over millions of
  // instances, so the Celllib lookup (and its std::string) happens once per
  // type. The celllib is read and flushed in the constructor, so the answer is
  // already stable here -- setup_hier relies on the same thing to build
  // opaque_gids_.
  absl::flat_hash_map<std::string, bool> liberty_cell_memo;
  const auto                             is_liberty_cell = [&](const auto& node) -> bool {
    auto io = node.get_subnode_io();
    if (!io) {
      return false;
    }
    const std::string tname{io->get_name()};
    if (auto it = liberty_cell_memo.find(tname); it != liberty_cell_memo.end()) {
      return it->second;
    }
    const auto& lib     = timer.celllib(ot::MAX);
    const bool  is_cell = lib && lib->cell(tname) != nullptr;
    liberty_cell_memo.emplace(tname, is_cell);
    return is_cell;
  };
  // ABC's builtin tie cells (emitted when the Liberty has no constant cells).
  // Named once so the 3rd and 5th phases agree these are NOT gates.
  const auto is_tie_cell = [](const auto& node) -> bool {
    auto io = node.get_subnode_io();
    if (!io) {
      return false;
    }
    const auto tname = io->get_name();
    return tname == "_const0_" || tname == "_const1_";
  };

  // Pin-level `bits` on a graph-IO pin is usually unset (widths live on the
  // GraphIO decl) — the pin tracker needs the real width, so fall back to the
  // decl when the driver is a module input.
  auto io_bits_of = [&](const auto& dpin) -> int32_t {
    auto b = bits_of(dpin);
    if (b != 0 || !is_graph_input_pin(dpin)) {
      return b;
    }
    auto n = dpin.get_pin_name();
    if (n.empty()) {
      return b;
    }
    // Resolve the declared width against the pin's OWN graph IO: a flattened
    // leaf input can be a child-module port, not a top port, so the top gio
    // would not declare it (get_bits asserts on an unknown name).
    auto pio = gio;
    if (hier_mode_) {
      auto* pg = dpin.get_master_node().get_graph();
      pio      = (pg != nullptr) ? pg->get_io() : nullptr;
    }
    if (!pio) {
      return b;
    }
    return bits_of(dpin, *pio, n);
  };

  // Tracker id for a driver pin. pass.partition names region boundary ports
  // after the SOURCE-side wire (a port can literally be called "get_mask_20"),
  // so an internal trackable node's synthetic wire_name can collide with a
  // port name and silently redefine the port's bus inside the string-keyed
  // tracker. Trackable outputs are pure rewiring — never real OT nets — so
  // decorate exactly those with a prefix no port/net name ever carries;
  // tracker LEAVES (ports, gate output nets) stay undecorated, which keeps
  // every pv root a name that exists as an OT net.
  auto trk_id = [&](const auto& pin) -> std::string {
    if (is_graph_input_pin(pin) || is_graph_output_pin(pin)) {
      return net_of(pin, hier_mode_);
    }
    auto master = pin.get_master_node();
    if (!master.is_invalid() && Ntype::is_pin_trackable(type_op_of(master))) {
      return absl::StrCat("n$", net_of(pin, hier_mode_));
    }
    return net_of(pin, hier_mode_);
  };

  // A hierarchy-resolved operand can land directly on a pin-trackable node in
  // a sibling mapped region. Its pin annotation is not guaranteed to survive
  // that boundary, but the tracker's vector is the exact wiring width once the
  // producer has been processed.
  auto tracked_bits_of = [&](const auto& dpin) -> int32_t {
    auto bits = io_bits_of(dpin);
    if (bits > 0) {
      return bits;
    }
    const auto& pv = pin_tracker.get_pin_vector(trk_id(dpin));
    return static_cast<int32_t>(pv.size());
  };

  // Record a driver pin -> net-name override. Keyed by the hier-unique net name
  // when flattening (Class_index collides across instances), by Class_index in
  // flat mode (byte-for-byte the original behaviour).
  // key_net is the driver's net name computed with the correct hier context by
  // the caller (net_of_node for a traversal-node driver, net_of for a resolved
  // edge driver); the flat map keys on the pin's Class_index as before.
  auto set_overwrite = [&](const std::string& key_net, const auto& dpin, const std::string& netname) {
    if (hier_mode_) {
      overwrite_hnet_.insert_or_assign(key_net, netname);
    } else {
      overwrite_dpin2net.insert_or_assign(dpin.get_class_index(), netname);
    }
  };
  auto is_overwritten = [&](const std::string& key_net, const auto& dpin) -> bool {
    return hier_mode_ ? overwrite_hnet_.contains(key_net) : overwrite_dpin2net.contains(dpin.get_class_index());
  };

  // Driver feeding a named sink, HIER-resolved. get_driver_of_sink_name reads the
  // pin-level inp_edges (Graph::inp_edges(Pin_class)), which is LOCAL-only — it
  // never crosses a module boundary — so the tracker would build on a child
  // module's own input port (a bare "io_x") instead of the parent's driver.
  // node.inp_edges() (the Node overload) is the hier-resolving one, so route the
  // tracker's operand lookups through it when flattening.
  auto hier_driver_of = [&](const hhds::Occurrence_node& n, std::string_view sname) -> hhds::Occurrence_pin {
    for (auto& e : n.inp_edges()) {
      if (sink_pin_name_of(n, e.sink) == sname) {
        return e.driver;
      }
    }
    return {};
  };

  auto is_resolved_const = [&](const auto& dpin) {
    if (is_const_pin(dpin)) {
      return true;
    }
    // HHDS singleton constants carry a valid pin but an intentionally invalid
    // regular Node_class; test the reserved nid before type_op_of().
    return dpin.get_master_node().get_debug_nid() == hhds::Graph::CONST_NODE;
  };
  auto operand_bits_of = [&](const hhds::Occurrence_node& owner, std::string_view sname, const auto& dpin) -> int32_t {
    if (const auto bits = tracked_bits_of(dpin); bits > 0) {
      return bits;
    }
    // A descended helper input can resolve through multiple call boundaries
    // to an ownerless singleton constant. The callee's local GraphIO remains
    // the finite-width operation contract (e.g. abc's 870-bit input splitter).
    auto local = get_driver_of_sink_name(owner.base_node(), sname);
    if (local.is_invalid()) {
      return 0;
    }
    if (const auto bits = bits_of(local); bits > 0) {
      return bits;
    }
    if (!is_graph_input_pin(local)) {
      return 0;
    }
    auto* pg = local.get_graph();
    auto  io = pg != nullptr ? pg->get_io() : nullptr;
    auto  pn = local.get_pin_name();
    return io && !pn.empty() ? bits_of(local, *io, pn) : 0;
  };
  // A Liberty technology-cell (Sub) output is ONE Boolean pin: bit 0 is the
  // physical net and any wider pin stamp is padding. The producer stamps that
  // shape with add_scalar when the walk reaches it (3rd phase, below), but the
  // forward walk does NOT guarantee the producer comes first: hhds emits a
  // loop_break Sub (a bodyless sequential stub keeps that flag -- pass/partition
  // and pass/abc carry decl.loop_break onto the clone) as a forward SOURCE,
  // whose out-edges add no in-degree to its consumers, and a preserved comb
  // cycle falls out in raw storage order. Deferring the consumer cannot fix it
  // (a Sub whose only driver pin resolves to a graph IO pin never reaches
  // add_scalar at all, and the loop's no-progress guard would then fatal the
  // whole run); seed the producer's own entry from here instead. It is not a
  // guess -- it is the same add_scalar(key, bits_of(dpin)) call the producer
  // makes on the same key -- so the producer's later stamp is an idempotent
  // rewrite. Without it the tracker's add_input fallback mints a provisional
  // {net.0 .. net.N-1} bus that the consumer COPIES, and bit k>0 resolves to
  // "net.k", a net that is never inserted: an unconnected Liberty input pin.
  //
  // Only a LIBERTY cell is one Boolean pin, so the seed carries add_scalar's
  // precondition with it. (A native_comb_boundary node is never a Sub --
  // pass/abc keeps Sub out of the preserved-SCC candidate set -- so the Sub
  // test already excludes the wide per-bit boundary shape.)
  auto seed_cell_output = [&](const auto& dpin) {
    if (is_resolved_const(dpin)) {
      return;
    }
    const auto master = dpin.get_master_node();
    if (master.is_invalid() || type_op_of(master) != Ntype_op::Sub || !is_liberty_cell(master)) {
      return;
    }
    pin_tracker.add_scalar_if_absent(trk_id(dpin), bits_of(dpin));
  };
  auto seed_operand = [&](const auto& dpin, int32_t bits) {
    if (is_resolved_const(dpin)) {
      pin_tracker.add_constant(trk_id(dpin), bits);
      return;
    }
    seed_cell_output(dpin);
  };

  // The node set for the forward (net-population) walk. Flat mode iterates the
  // def's own forward_class; the whole-design walk iterates forward_hier
  // (descending design modules, yielding Liberty-cell leaves via opaque_gids_)
  // and is snapshot into a vector first: processing materializes port-0 driver
  // pins on shared child bodies, which must not mutate a live hier iterator.
  auto forward_nodes = [&]() {
    std::vector<hhds::Occurrence_node> v;
    const auto*                        opq  = opaque_gids_.empty() ? nullptr : &opaque_gids_;
    auto                               view = g->occurrences(opq);
    if (hier_mode_) {
      for (auto n : view.nodes(hhds::Node_order::forward)) {
        v.push_back(n);
      }
    } else {
      for (auto n : g->body().nodes(hhds::Node_order::forward)) {
        v.push_back(view.lift(n));
      }
    }
    return v;
  };

  // 1st: primary inputs.
  if (gio) {
    for (const auto& d : gio->get_input_pin_decls()) {
      auto pin = g->get_input_pin(d.name);
      if (pin.is_invalid()) {
        continue;
      }
      std::string driver_name{d.name};
      auto        bits = bits_of(pin, *gio, d.name);
      if (bits == 0) {
        bits = static_cast<int32_t>(d.bits);
      }

      timer.insert_primary_input(driver_name);
      timer.insert_net(driver_name);
      for (auto i = 1; i < bits; ++i) {
        auto bus_bit_name = absl::StrCat(driver_name, ".", str_tools::to_s(i));
        timer.insert_primary_input(bus_bit_name);
        timer.insert_net(bus_bit_name);
      }
      pin_tracker.add_input(driver_name, bits);
    }
  }

  // 2nd: primary outputs.
  if (gio) {
    for (const auto& d : gio->get_output_pin_decls()) {
      auto out_sink = g->get_output_pin(d.name);
      if (out_sink.is_invalid()) {
        continue;
      }
      auto inps = out_sink.inp_edges();
      if (inps.empty()) {
        continue;
      }
      auto driver_dpin = inps.front().driver;

      std::string driver_name{d.name};
      auto        bits = bits_of(driver_dpin);
      if (bits == 0) {
        bits = static_cast<int32_t>(d.bits);
      }

      timer.insert_primary_output(driver_name);
      timer.insert_net(driver_name);
      for (auto i = 1; i < bits; ++i) {
        auto bus_bit_name = absl::StrCat(driver_name, ".", str_tools::to_s(i));
        timer.insert_primary_output(bus_bit_name);
        timer.insert_net(bus_bit_name);
      }
      // Multiple outputs sharing the same driver dpin is legal — last one wins
      // (mirrors the original code's behaviour). driver_dpin is a resolved edge
      // driver (its master carries the hier chain), so net_of is the right key.
      // A feed-through (output driven straight by a graph INPUT — the flatten
      // as-top shape) must NOT overwrite: it would rename the input's net onto
      // the PO net, silently un-timing every gate cone fed by that input (the
      // PI arrival lands on the original net). The feed-through PO itself has
      // no combinational content to time. Consts likewise never carry a net.
      // A native combinational boundary (pass.abc's preserved SCC remainder) is
      // the same case: STA cuts the path at that node's OWN output net, where
      // make_opaque_logic_boundary puts the zero-arrival PI — and OpenTimer
      // cannot put a PI on the PO's pin name (insert_primary_input asserts the
      // name is unused). Renaming its net onto the PO would point every
      // consumer at a net nothing drives (a PO pin is not an rct root), leaving
      // the cut PI dangling and the whole cone behind it unscored. As with a
      // flop that drives a PO, the PO itself simply carries no arrival.
      const bool driver_is_boundary = !is_const_pin(driver_dpin) && !driver_dpin.get_master_node().is_invalid()
                                      && driver_dpin.get_master_node().attr(livehd::attrs::native_comb_boundary).has();
      if (!is_graph_input_pin(driver_dpin) && !is_const_pin(driver_dpin) && !driver_is_boundary) {
        set_overwrite(net_of(driver_dpin, hier_mode_), driver_dpin, driver_name);
      }
    }
  }

  auto tracker_ready = [&](const auto& dpin) {
    if (is_const_pin(dpin) || is_graph_input_pin(dpin)) {
      return true;
    }
    const auto master = dpin.get_master_node();
    return master.is_invalid() || !Ntype::is_pin_trackable(type_op_of(master)) || pin_tracker.has_pin(trk_id(dpin));
  };

  // 3rd: populate all the net names (forward walk, pin-tracker for trackable
  // ops). The occurrence forward walk is topological inside each definition,
  // but not globally across sibling region instances: a consumer in __c1 can
  // be yielded before its pure-wiring producer in __c3. Defer only a consumer
  // that actually encounters such a dependency. This keeps the common path at
  // one hierarchy-resolving edge walk per operand; a separate readiness scan
  // doubles OpenTimer setup time on Minion's 1.5-million-node mapped design.
  auto       pending_nodes          = forward_nodes();
  // Members, not locals: the STA reuse cache has to replay this warning on a
  // hit, so its payload must outlive build_circuit (a hit that silently dropped
  // the diagnostic would be a cache that changes what the run reports).
  auto&      opaque_logic_nodes     = opaque_logic_nodes_;
  auto&      opaque_logic_examples  = opaque_logic_examples_;
  auto&      ambiguous_or_nodes     = ambiguous_or_nodes_;
  auto&      ambiguous_or_examples  = ambiguous_or_examples_;
  // Counting is per NODE (the warning says "node(s)"), while the cut itself is
  // per DRIVER NET, so the counter lives outside the boundary builder: a node
  // with several output nets is counted, and named in the examples, once.
  // record_example=false keeps the feedback-specific example list (whose hint
  // says "remove the combinational feedback") free of boundaries cut for a
  // different reason; the COUNT still includes them, so the total stays honest.
  const auto note_opaque_logic_node = [&](const hhds::Occurrence_node& node, bool& counted, bool record_example) {
    if (counted) {
      return;
    }
    counted = true;
    ++opaque_logic_nodes;
    if (record_example && opaque_logic_examples.size() < 5) {
      opaque_logic_examples.push_back(debug_name(node));
    }
  };
  // Width of THIS output pin. get_driver_pin(0)/out_edges() are only the
  // fallback for a pin with no `bits` annotation: sizing one pin's cut by the
  // widest of ALL the node's outputs is wrong the moment a boundary node has
  // more than one. It cannot today -- pass/abc keeps Sub/Memory/registers out
  // of the preserved-SCC remainder and only those cells expose several driver
  // pins -- so this is a no-op rewrite that keeps the helper honest.
  //
  // hint_bits: a caller that already knows the exact RESOLVED width (the pin
  // tracker's own vector) passes it here. The driver pin stamp alone can be
  // NARROWER than the bus the tracker resolved, and a boundary narrower than
  // its consumers leaves the upper bit-PIs uninserted -- every Get_mask reader
  // above the cut then clamps to nothing or resolves onto a net never created.
  const auto boundary_bits_of
      = [&](const hhds::Occurrence_node& node, const hhds::Occurrence_pin& dpin, int32_t hint_bits) -> int32_t {
    auto bits = bits_of(dpin);
    if (bits <= 0) {
      bits = bits_of(node.get_driver_pin(0));
    }
    if (bits <= 0) {
      for (const auto& e : node.out_edges()) {
        bits = std::max(bits, bits_of(e.driver));
      }
    }
    return std::max({bits, hint_bits, 1});
  };
  const auto make_opaque_logic_boundary = [&](const hhds::Occurrence_node& node,
                                              const hhds::Occurrence_pin&  dpin,
                                              const std::string&           wname,
                                              bool&                        counted,
                                              int32_t                      hint_bits      = 0,
                                              bool                         record_example = true) {
    const auto bits = boundary_bits_of(node, dpin, hint_bits);
    pin_tracker.add_opaque(wname, bits);
    timer.insert_primary_input(wname);
    set_input_delays(wname);
    for (int32_t i = 1; i < bits; ++i) {
      const auto bit_name = absl::StrCat(wname, ".", str_tools::to_s(i));
      timer.insert_primary_input(bit_name);
      set_input_delays(bit_name);
    }
    note_opaque_logic_node(node, counted, record_example);
  };
  // Same boundary, but for a driver net that phase 2 already renamed onto a
  // primary output. A PI on the stale net would cut the path at a net no
  // consumer resolves to, and the PO's own name cannot take a PI (OpenTimer
  // asserts on a name that is already a pin). Leave the net and the tracker
  // entry resolvable -- the driverless-net shape the plain gate case already
  // leaves for a PO-driving cell -- so a pin-trackable consumer still lands on
  // a net that exists: Pin_tracker auto-add_input()s a missing source, and
  // connect_pin to an absent net silently un-nets the pin, which compute_timing
  // then reports as an incomplete timing graph.
  const auto make_renamed_logic_boundary
      = [&](const hhds::Occurrence_node& node, const hhds::Occurrence_pin& dpin, const std::string& wname, bool& counted) {
          const auto bits = boundary_bits_of(node, dpin, 0);
          timer.insert_net(wname);
          for (int32_t i = 1; i < bits; ++i) {
            timer.insert_net(absl::StrCat(wname, ".", str_tools::to_s(i)));
          }
          pin_tracker.add_opaque(wname, bits);
          note_opaque_logic_node(node, counted, true);
        };
  while (!pending_nodes.empty()) {
    std::vector<hhds::Occurrence_node> deferred_nodes;
    bool                               net_progress = false;
    for (auto& node : pending_nodes) {
      auto op = type_op_of(node);
      if (op == Ntype_op::Nconst || op == Ntype_op::AttrSet) {
        continue;
      }

      // One node can expose several driver nets; the warning counts NODES.
      bool boundary_counted = false;
      bool root_track       = Ntype::is_pin_trackable(op);
      if (root_track) {
        auto       dpin0                = node.get_driver_pin(0);
        // This trackable node's OWN output: name it from the traversal node (its
        // hier chain is intact; a create_driver_pin/out_pins handle drops it). The
        // "n$" prefix keeps the pure-rewiring output out of the real-net space.
        auto       wname                = hier_mode_ ? absl::StrCat("n$", net_of_node(node, dpin0, true)) : trk_id(dpin0);
        const bool native_comb_boundary = node.base_node().attr(livehd::attrs::native_comb_boundary).has();
        if (native_comb_boundary) {
          // `wname` is the reserved "n$" tracker name, never a PO pin name, and
          // it is exactly what the pv-driven overwrite below points consumers
          // at — so this cut is NOT skipped when the node drives a PO.
          make_opaque_logic_boundary(node, dpin0, wname, boundary_counted);
        } else if (op == Ntype_op::Set_mask) {
          auto a_dpin     = hier_driver_of(node, "a");
          auto mask_dpin  = hier_driver_of(node, "mask");
          auto value_dpin = hier_driver_of(node, "value");
          if (a_dpin.is_invalid() || mask_dpin.is_invalid() || value_dpin.is_invalid()) {
            livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
                .msg("Invalid corrupt set_mask node {} (cprop should have deleted it)", debug_name(node))
                .fatal();
            return;
          }
          if (!tracker_ready(a_dpin) || !tracker_ready(value_dpin)) {
            deferred_nodes.push_back(node);
            continue;
          }
          if (!is_const_pin(mask_dpin)) {
            livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
                .msg("opentimer can not handle non-constant masks on node {} (cprop/tmap first)", debug_name(node))
                .fatal();
            return;
          }
          auto       mask_const = hydrate_const(mask_dpin);
          const auto a_bits     = operand_bits_of(node, "a", a_dpin);
          seed_operand(a_dpin, a_bits);
          seed_operand(value_dpin, static_cast<int32_t>(mask_const.get_bits()));
          pin_tracker.add_set_mask(wname, trk_id(a_dpin), a_bits, mask_const, trk_id(value_dpin));
        } else if (op == Ntype_op::Get_mask) {
          auto a_dpin    = hier_driver_of(node, "a");
          auto mask_dpin = hier_driver_of(node, "mask");
          if (a_dpin.is_invalid() || mask_dpin.is_invalid()) {
            livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
                .msg("Invalid corrupt get_mask node {} (cprop should have deleted it)", debug_name(node))
                .fatal();
            return;
          }
          if (!tracker_ready(a_dpin)) {
            deferred_nodes.push_back(node);
            continue;
          }
          if (!is_const_pin(mask_dpin)) {
            livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
                .msg("opentimer can not handle non-constant masks on node {} (cprop/tmap first)", debug_name(node))
                .fatal();
            return;
          }
          auto       mask_const = hydrate_const(mask_dpin);
          const auto a_bits     = operand_bits_of(node, "a", a_dpin);
          seed_operand(a_dpin, a_bits);
          pin_tracker.add_get_mask(wname, trk_id(a_dpin), a_bits, mask_const);
        } else if (op == Ntype_op::SRA) {
          auto a_dpin = hier_driver_of(node, "a");
          auto b_dpin = hier_driver_of(node, "b");
          if (a_dpin.is_invalid() || b_dpin.is_invalid()) {
            livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
                .msg("Invalid corrupt SRA node {} (cprop should have deleted it)", debug_name(node))
                .fatal();
            return;
          }
          if (!tracker_ready(a_dpin)) {
            deferred_nodes.push_back(node);
            continue;
          }
          if (!is_const_pin(b_dpin)) {
            livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
                .msg("opentimer can not handle non-constant SRA on node {} (cprop/tmap first)", debug_name(node))
                .fatal();
            return;
          }
          auto b_const = hydrate_const(b_dpin);
          auto a_bits  = operand_bits_of(node, "a", a_dpin);
          if (a_bits <= 0 && is_resolved_const(a_dpin) && b_const.is_just_i64()) {
            const auto shift = b_const.to_just_i64();
            const auto out   = bits_of(node.get_driver_pin(0));
            if (shift >= 0 && shift <= std::numeric_limits<int32_t>::max() - std::max(out, 1)) {
              // Constants have zero arrival on every bit. Preserve only the
              // trackable result arity: SRA emits max(a_bits-shift, 1) bits.
              a_bits = static_cast<int32_t>(shift) + std::max(out, 1);
            }
          }
          if (a_bits <= 0) {
            livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
                .msg("SRA input has no usable width on node {} (operand {}, kind {}, graph_input={}, tracker={})",
                     debug_name(node),
                     debug_name(a_dpin.get_master_node()),
                     Ntype::get_name(type_op_of(a_dpin.get_master_node())),
                     is_graph_input_pin(a_dpin),
                     trk_id(a_dpin))
                .hint("bitwidth/cprop must stamp the shifted operand before pass.opentimer tracks its wiring")
                .fatal();
            return;
          }
          seed_operand(a_dpin, a_bits);
          pin_tracker.add_sra(wname, trk_id(a_dpin), a_bits, b_const);
        } else if (op == Ntype_op::Sext) {
          auto a_dpin = hier_driver_of(node, "a");
          auto b_dpin = hier_driver_of(node, "b");
          if (a_dpin.is_invalid() || b_dpin.is_invalid()) {
            livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
                .msg("Invalid corrupt Sext node {} (cprop should have deleted it)", debug_name(node))
                .fatal();
            return;
          }
          if (!tracker_ready(a_dpin)) {
            deferred_nodes.push_back(node);
            continue;
          }
          if (!is_const_pin(b_dpin)) {
            livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
                .msg("opentimer can not handle non-constant Sext on node {} (cprop/tmap first)", debug_name(node))
                .fatal();
            return;
          }
          auto       b_const = hydrate_const(b_dpin);
          const auto a_bits  = operand_bits_of(node, "a", a_dpin);
          seed_operand(a_dpin, a_bits);
          pin_tracker.add_sext(wname, trk_id(a_dpin), a_bits, b_const);
        } else if (op == Ntype_op::SHL) {
          auto a_dpin = hier_driver_of(node, "a");
          if (a_dpin.is_invalid()) {
            livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
                .msg("Invalid corrupt SHL node {} (cprop should have deleted it)", debug_name(node))
                .fatal();
            return;
          }
          if (!tracker_ready(a_dpin)) {
            deferred_nodes.push_back(node);
            continue;
          }
          // SHL b is single-driver (the one-hot multi-shift form was removed).
          auto b_dpin = hier_driver_of(node, "b");
          if (!is_const_pin(b_dpin)) {
            livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
                .msg("opentimer can not handle non-constant SHL on node {} (cprop/tmap first)", debug_name(node))
                .fatal();
            return;
          }
          auto       b_const = hydrate_const(b_dpin);
          const auto a_bits  = operand_bits_of(node, "a", a_dpin);
          seed_operand(a_dpin, a_bits);
          pin_tracker.add_shl(wname, trk_id(a_dpin), a_bits, b_const);
        } else if (op == Ntype_op::Concat) {
          // Wiring/packing, NOT logic: every result bit keeps the identity of the
          // lane bit it came from, so the tracker threads timing straight through
          // and the cell contributes zero delay. Before this arm existed, Concat
          // was not pin-trackable at all and fell into the "needs a tmap netlist"
          // refusal below.
          //
          // Widths come from the BASE lane table (the interleaved const sinks);
          // lane VALUES must come from the occurrence edges, since the base table
          // drops the hier chain that trk_id/net_of need. Same split lec/encode
          // uses: decode from base, resolve the value by sink pid.
          const auto lanes = livehd::graph_util::concat_lanes(node.base_node());
          if (lanes.empty()) {
            livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
                .msg("malformed concat (missing lane operand, or a non-constant lane width) on node {}", debug_name(node))
                .fatal();
            return;
          }
          if (const auto lane_bad = livehd::graph_util::concat_lane_violation(lanes); !lane_bad.empty()) {
            livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
                .msg("{} on concat '{}' ({}) in module '{}'{}",
                     lane_bad,
                     debug_name(node),
                     occurrence_name(node),
                     node.get_graph() != nullptr ? node.get_graph()->get_name() : std::string_view{"?"},
                     src_of_node(g, node).empty() ? std::string{} : std::format(" at {}", src_of_node(g, node)))
                .fatal();
            return;
          }
          absl::flat_hash_map<hhds::Port_id, hhds::Occurrence_pin> lane_by_pid;
          bool                                                     lanes_ready = true;
          for (auto& e : node.inp_edges()) {
            lane_by_pid.insert_or_assign(e.sink.get_port_id(), e.driver);
            lanes_ready = lanes_ready && tracker_ready(e.driver);
          }
          if (!lanes_ready) {
            deferred_nodes.push_back(node);
            continue;
          }
          std::vector<Pin_tracker<std::string>::Concat_src> srcs;
          srcs.reserve(lanes.size());
          for (size_t i = 0; i < lanes.size(); ++i) {
            auto it = lane_by_pid.find(static_cast<hhds::Port_id>(2 * i));  // lane i value = sink pid 2i
            if (it == lane_by_pid.end()) {
              livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
                  .msg("concat lane {} has no value driver on node {}", i, debug_name(node))
                  .fatal();
              return;
            }
            const auto lane_bits = tracked_bits_of(it->second) > 0 ? tracked_bits_of(it->second) : lanes[i].width;
            seed_operand(it->second, lane_bits);
            srcs.push_back({trk_id(it->second), lane_bits, lanes[i].width, lanes[i].offset});
          }
          // Literal sum(w) width of a result that is never negative. The driver
          // pin's stamp is deliberately not
          // consulted: a narrowed stamp must not move a lane.
          pin_tracker.add_concat(wname, srcs, livehd::graph_util::concat_total_width(lanes));
        } else if (op == Ntype_op::Or) {
          auto inps = node.inp_edges();
          if (std::any_of(inps.begin(), inps.end(), [&](const auto& e) { return !tracker_ready(e.driver); })) {
            deferred_nodes.push_back(node);
            continue;
          }
          for (auto e : inps) {
            // add_or IGNORES an untracked operand (it returns early), so an
            // unseeded cell output silently drops its whole timing arc and the
            // Or result collapses onto the zero net.
            seed_cell_output(e.driver);
            pin_tracker.add_or(wname, trk_id(e.driver));
          }
          // A packed OR of disjoint shifted lanes is wiring and every result
          // bit resolves to one source bit. Overlapping live lanes are real
          // Boolean logic: no bit rename can express "bit i is a OR b".
          //
          // Falling through is NOT safe. The driver loop below inspects only
          // pv[0], so an overlap in a HIGHER bit renames the whole net onto
          // pv[0]'s lane and DELETES the other fan-in cone from the timing
          // graph with no report; an overlap in bit 0 collapses to kZeroNet,
          // destroying the bit identity every Get_mask reader above needs.
          //
          // Model it honestly instead: the same explicit zero-arrival cut used
          // for an ABC-preserved native combinational SCC. Mapped cones on both
          // sides stay scored, each bit keeps a real per-bit net, and the cut is
          // counted and named. Never fatal — an lg: netlist built by an older
          // pass.abc, by hand, or by a foreign flow (i.e. one carrying no
          // livehd::attrs::native_comb_boundary) is not a reason to abandon
          // timing for the entire design.
          if (pin_tracker.has_ambiguous(wname)) {
            // Width BEFORE add_opaque rewrites the entry: add_or already
            // resolved the true bus width (max over the inputs), which the
            // driver pin stamp may under-report.
            const auto tracked_w = static_cast<int32_t>(pin_tracker.get_pin_vector(wname).size());
            make_opaque_logic_boundary(node, dpin0, wname, boundary_counted, tracked_w, /*record_example=*/false);
            ++ambiguous_or_nodes;
            if (ambiguous_or_examples.size() < 5) {
              auto src = src_of_node(g, node);
              ambiguous_or_examples.push_back(src.empty() ? debug_name(node) : absl::StrCat(debug_name(node), " @ ", src));
            }
          }
        } else if (op == Ntype_op::And) {
          Dlop                 a_mask = *Dlop::create_integer(-1);
          hhds::Occurrence_pin a_dpin;
          auto                 inps = node.inp_edges();
          if (std::any_of(inps.begin(), inps.end(), [&](const auto& e) { return !tracker_ready(e.driver); })) {
            deferred_nodes.push_back(node);
            continue;
          }
          for (auto e : inps) {
            if (is_const_pin(e.driver)) {
              a_mask = a_mask.and_op(hydrate_const(e.driver));
            } else {
              if (!a_dpin.is_invalid()) {
                livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
                    .msg("pin_tracker needed for netlist can not handle multiple unknowns on node {}", debug_name(node))
                    .fatal();
                return;
              }
              a_dpin = e.driver;
            }
          }
          if (!a_dpin.is_invalid()) {
            seed_cell_output(a_dpin);  // add_and, like add_or, drops an untracked operand
            pin_tracker.add_and(wname, trk_id(a_dpin), a_mask);
          }
        } else {
          livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
              .msg("opentimer needs a tmap/synthesized netlist; got node {}", debug_name(node))
              .fatal();
          return;
        }
      }

      const bool native_comb_boundary = node.base_node().attr(livehd::attrs::native_comb_boundary).has();

      // Setup driver pins and nets from one node-level edge expansion. Plain
      // cells (trackable ops, flops) drive through an implicit port-0 pin that
      // out_pins() misses, while node.out_edges() includes it. Do not probe each
      // pin with dpin.out_edges(): resolving and materializing a wide hierarchical
      // fanout once per pin dominated Minion STA setup (and the later loop probed
      // every pin a second time).
      std::vector<hhds::Occurrence_pin> dpins;
      absl::flat_hash_set<std::string>  seen_dnet;  // by NET NAME: two handles can name one port
      const auto                        push_dpin = [&](const hhds::Occurrence_pin& dpin) {
        if (dpin.is_invalid()) {
          return;
        }
        if (seen_dnet.insert(net_of_node(node, dpin, hier_mode_)).second) {
          dpins.push_back(dpin);
        }
      };
      const auto node_out_edges = node.out_edges();
      for (const auto& e : node_out_edges) {
        push_dpin(e.driver);
      }
      for (auto& dpin : dpins) {
        if (is_graph_input_pin(dpin) || is_graph_output_pin(dpin)) {
          I(!root_track);
          continue;
        }
        // Driver-side net name comes from the traversal node (hier chain intact).
        auto dnet  = net_of_node(node, dpin, hier_mode_);
        auto wname = root_track ? (hier_mode_ ? absl::StrCat("n$", dnet) : trk_id(dpin)) : dnet;

        if (root_track) {
          const auto& pv = pin_tracker.get_pin_vector(wname);

          if (pv.empty()) {
            set_overwrite(dnet, dpin, std::string{kZeroNet});
          } else if (pv.size() == 1) {  // single bit tracking result
            if (pv[0].pos < 0) {
              set_overwrite(dnet, dpin, std::string{kZeroNet});
              continue;
            }
            if (pv[0].pos) {
              auto bus_bit_name = absl::StrCat(pv[0].id(), ".", str_tools::to_s(pv[0].pos));
              set_overwrite(dnet, dpin, bus_bit_name);
            } else {
              set_overwrite(dnet, dpin, pv[0].id());
            }
          } else if (pv.size() > 1 && pv[0].pos < 0 && !is_overwritten(dnet, dpin)) {
            set_overwrite(dnet, dpin, std::string{kZeroNet});
          } else if (pv.size() > 1 && !is_overwritten(dnet, dpin)) {
            // MULTI-bit tracker result: a module-boundary packed bus (pass.abc
            // glue). bit 0 is the real signal, higher bits are const padding for a
            // wide port, so a 1-bit cell reading this driver reads bit 0 — map it
            // to pv[0]. A consumer that reads a specific higher bit goes through a
            // Get_mask, which resolves inside the tracker (not via this overwrite);
            // the is_overwritten guard keeps a phase-2 primary-output net intact.
            if (pv[0].pos) {
              set_overwrite(dnet, dpin, absl::StrCat(pv[0].id(), ".", str_tools::to_s(pv[0].pos)));
            } else {
              set_overwrite(dnet, dpin, pv[0].id());
            }
          }
        } else if (native_comb_boundary) {
          // The exact node remains in the LGraph, but its output is an explicit
          // path boundary for this partial STA model.
          if (is_overwritten(dnet, dpin)) {
            // drives a primary output directly: already a PO net
            make_renamed_logic_boundary(node, dpin, wname, boundary_counted);
          } else {
            make_opaque_logic_boundary(node, dpin, wname, boundary_counted);
          }
        } else if (op != Ntype_op::Sub) {
          // Flop/Latch/Memory/Div/Rem: the 4th phase turns each output into a
          // per-bit primary input, and an untracked driver falls through to the
          // tracker's own add_input, so `<net>.k` resolves to a net that exists.
          timer.insert_net(wname);
        } else if (is_liberty_cell(node)) {
          // ONE Boolean pin: exactly Pin_tracker::add_scalar's precondition,
          // and the same predicate the 5th phase uses to insert this gate.
          timer.insert_net(wname);
          pin_tracker.add_scalar(wname, bits_of(dpin));
        } else if (is_tie_cell(node)) {
          // The 5th phase instantiates no gate for a tie; its net stays
          // driverless. Every bit is a real known constant — not "bit 0 is the
          // signal, the rest is padding".
          timer.insert_net(wname);
          pin_tracker.add_constant(wname, std::max(bits_of(dpin), 1));
        } else {
          // A Sub that is NOT a Liberty cell is not one Boolean timing pin, so
          // it must not get add_scalar: that would retire every bit above 0 to
          // the zero-arrival net. Two sub-cases, mirroring the 5th phase:
          //   * a design module with a body under hier=stitch is DESCENDED —
          //     the occurrence resolver hands consumers the child's internal
          //     driver, never this instance pin, so there is nothing to track;
          //   * a body-less black box is refused outright by the 5th phase
          //     ("whole-design timing hit black-box ..." / "not a cell in the
          //     Liberty library"), which aborts before any QoR is published.
          // Leaving no tracker entry keeps that refusal the single place this
          // is decided. If that fatal is ever relaxed (a real IP macro), the
          // replacement is NOT add_input/nothing — it is add_opaque plus one
          // inserted net per bit, the shape the 4th phase gives a flop; without
          // the bit nets, OpenTimer log-and-skips the connects and
          // compute_timing reports unconnected Liberty input pins.
          timer.insert_net(wname);
        }
      }
      net_progress = true;
    }
    if (!deferred_nodes.empty() && !net_progress) {
      const auto& node = deferred_nodes.front();
      livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
          .msg("pin-tracker dependency did not converge at node {}", debug_name(node))
          .hint("check for a combinational cycle or an unresolved mapped-region boundary")
          .fatal();
      return;
    }
    pending_nodes = std::move(deferred_nodes);
  }
  emit_boundary_warning();

  // 4th: create every sequential-boundary PI BEFORE queuing any gate
  // connection. leaf_nodes() is deliberately not ordered in flat mode, so a
  // single mixed loop can encounter a consuming gate before the flop/memory
  // that creates its bit-net PI. OpenTimer then rejects the connection while
  // still returning success, and the PI appears later -- leaving a silently
  // incomplete timing graph.
  for (auto& node : leaf_nodes(g)) {
    auto op = type_op_of(node);
    if (op != Ntype_op::Flop && op != Ntype_op::Latch && op != Ntype_op::Memory && op != Ntype_op::Div && op != Ntype_op::Rem) {
      continue;
    }
    // Path boundary, not a cell (2opt-freq D): pass.abc keeps flops, latches,
    // memories, and unsupported Div/Rem operators native — the Liberty stays
    // combinational. Each consumed output becomes a virtual primary input
    // arriving at 0, so the mapped cones on either side are still scored. A
    // latch is deliberately a hard break here: transparency and time borrowing
    // are not modeled. Clock/reset nets are not timed (no clock tree estimate).
    //
    // out_pins() does NOT materialize these outputs — a flop Q is an implicit
    // port-0 pin, and a MEMORY exposes each read-data port only on its
    // consuming edges (out_pins() is empty). Collect the actually-driven output
    // pins from out_edges (deduped by pin) so every read port becomes a net.
    std::vector<hhds::Occurrence_pin>           bpins;
    absl::flat_hash_set<hhds::Occurrence_index> seen;
    for (const auto& e : node.out_edges()) {
      if (!e.driver.is_invalid() && seen.insert(e.driver.get_occurrence_index()).second) {
        bpins.push_back(e.driver);
      }
    }
    if (bpins.empty()) {  // no consuming edge (a dead flop): fall back to port 0
      auto dpin0 = node.get_driver_pin(0);
      if (!dpin0.is_invalid()) {
        bpins.push_back(dpin0);
      }
    }
    for (auto& dpin : bpins) {
      if (dpin.is_invalid() || dpin.out_edges().empty()) {
        continue;
      }
      auto dnet = net_of_node(node, dpin, hier_mode_);  // flop/mem output (driver side)
      if (is_overwritten(dnet, dpin)) {
        continue;  // drives a primary output directly: already a PO net
      }
      auto       wname = dnet;
      const auto bits  = bits_of(dpin);
      timer.insert_primary_input(wname);  // idempotent net insert underneath
      set_input_delays(wname);
      for (auto i = 1; i < bits; ++i) {
        auto bus_bit_name = absl::StrCat(wname, ".", str_tools::to_s(i));
        timer.insert_primary_input(bus_bit_name);
        set_input_delays(bus_bit_name);
      }
    }
  }

  // 5th: populate the combinational cells (Sub instances). Whole-design mode
  // iterates the flattened leaf set (forward_hier descends design modules;
  // Liberty-cell leaves are yielded via opaque_gids_), so gates from every
  // instance land in the single ot::Timer under their hier-unique names.
  for (auto& node : leaf_nodes(g)) {
    auto op = type_op_of(node);
    if (op == Ntype_op::Nconst || op == Ntype_op::AttrSet || Ntype::is_pin_trackable(op)
        || node.base_node().attr(livehd::attrs::native_comb_boundary).has() || op == Ntype_op::Flop || op == Ntype_op::Latch
        || op == Ntype_op::Memory || op == Ntype_op::Div || op == Ntype_op::Rem) {
      continue;
    }
    if (op != Ntype_op::Sub) {
      livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
          .msg("opentimer pass needs the lgraph to be tmap, found cell {} with type {}", debug_name(node), Ntype::get_name(op))
          .fatal();
      return;
    }

    auto instance_name = inst_of(node, hier_mode_);
    auto type_name     = sub_type_name(node);

    // ABC's builtin tie cells (emitted when the Liberty has no constant
    // cells): a constant never transitions, so it contributes no arrival —
    // leave its output net driverless and skip the gate.
    if (is_tie_cell(node)) {
      continue;
    }

    // A materialized property marker (`fproperty` / `lgassert`) is not hardware:
    // it drives nothing and has no Liberty cell, so there is no arc to time.
    // pass.abc drops these before the netlist, but a design timed straight from
    // its source graph (`lhd pass opentimer` on a pre-ABC library) still carries
    // them, and refusing one would report a black box for an assertion.
    if (livehd::graph_util::is_property_marker(node)) {
      continue;
    }
    // A Sub that DRIVES NOTHING is on no timing path, whatever it is: with no
    // output pin there is no arc to annotate and no arrival to propagate. That
    // is exactly the shape of a property marker whose module declaration went
    // missing (pass.partition rebuilds the instance but does not clone the
    // `lgassert`/`fproperty` decl, so the Sub arrives here unbound and its type
    // name reads empty) -- and refusing the WHOLE design over an assertion is
    // the wrong answer either way. A genuine black box that matters for timing
    // has outputs and still reaches the refusal below.
    if (node.out_edges().empty()) {
      continue;
    }

    // A Sub that is not a Liberty cell. In whole-design mode a design module is
    // descended into by the hier walk (never yielded here); reaching this with
    // a body means a black-box the flatten cannot enter — skip it defensively
    // (its boundary nets simply stay unscored). ot::Timer::insert_gate would
    // otherwise log-and-skip, yielding silent garbage. The celllib is loaded.
    const auto& lib = timer.celllib(ot::MAX);
    if (!lib || lib->cell(type_name) == nullptr) {
      if (hier_setting_ != "false") {
        if (node.get_subnode_graph() != nullptr) {
          continue;  // design-module body already flattened in via the hier walk
        }
        livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
            .msg("whole-design timing hit black-box '{}' (instance {}): no Liberty cell and no descendable body to flatten",
                 type_name,
                 instance_name)
            .fatal();
        return;
      }
      livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
          .msg(
              "module instantiates '{}' (instance {}), which is not a cell in the Liberty library — hier=false times one "
              "tech-mapped module per run. Pass --top of a mapped region (<mod>__c<N>), or drop hier=false: the default "
              "(pass.opentimer.hier=true) flattens the whole design across the instance hierarchy",
              type_name,
              instance_name)
          .fatal();
      return;
    }

    timer.insert_gate(instance_name, type_name);

    // Setup driver pins and nets. out_pins() is a LAZY VIEW and can come back
    // empty for a driver that is nonetheless materialized and consumed -- the
    // abc-mapped MSB inverter of a shifted bus, whose only reader is a
    // pin-trackable Concat, is exactly that shape, and a Sub is not exempt
    // (nothing then creates its output net, and every consumer the pin tracker
    // resolves onto that net fails to connect -- an incomplete timing graph
    // whose first symptom is four unconnected Liberty pins). out_edges() sees
    // the pin, and a driver with no consumer needs no net at all, so take the
    // union of both and dedup by pin.
    std::vector<hhds::Occurrence_pin> dpins;
    for (auto& dpin : node.out_pins()) {
      if (!dpin.is_invalid() && !dpin.out_edges().empty()) {
        dpins.push_back(dpin);
      }
    }
    for (const auto& e : node.out_edges()) {
      if (!e.driver.is_invalid()) {
        dpins.push_back(e.driver);
      }
    }
    // Dedup on the LIBERTY PIN NAME, not on the pin handle: the two sources
    // above can hand back different Occurrence_pin handles (base vs
    // hier-qualified) for the same physical port, and OpenTimer asserts on a
    // second connect of a pin that already has a net.
    absl::flat_hash_set<std::string> connected_pin;
    for (auto& dpin : dpins) {
      auto pin_name = absl::StrCat(instance_name, ":", driver_pin_name_of(node, dpin));
      if (!connected_pin.insert(pin_name).second) {
        continue;
      }
      auto wire = driver_net_of(node, dpin);
      timer.connect_pin(pin_name, wire);
    }

    // connect input pins
    for (auto& e : node.inp_edges()) {
      I(!(is_graph_input_pin(e.driver) && bits_of(e.driver) > 2));

      // A hierarchy-flattened literal can arrive as HHDS's reserved singleton
      // CONST_NODE: it is a valid occurrence pin whose regular Node_class is
      // intentionally invalid, so is_const_pin alone does not recognize it.
      // Constants have zero arrival and all share the real driverless zero net.
      const bool singleton_const = e.driver.get_master_node().get_debug_nid() == hhds::Graph::CONST_NODE;
      auto       wire     = (is_const_pin(e.driver) || singleton_const) ? std::string{kZeroNet} : get_driver_net_name(e.driver);
      auto       pin_name = absl::StrCat(instance_name, ":", sink_pin_name_of(node, e.sink));
      timer.connect_pin(pin_name, wire);
    }
  }

  // 6th: zero-default the inputs/outputs at the OT side.
  if (gio) {
    for (const auto& d : gio->get_input_pin_decls()) {
      auto pin = g->get_input_pin(d.name);
      if (pin.is_invalid()) {
        continue;
      }
      std::string pname{d.name};
      auto        bits = bits_of(pin, *gio, d.name);
      if (bits == 0) {
        bits = static_cast<int32_t>(d.bits);
      }
      set_input_delays(pname);
      for (auto i = 1; i < bits; ++i) {
        set_input_delays(absl::StrCat(pname, ".", str_tools::to_s(i)));
      }
    }
    for (const auto& d : gio->get_output_pin_decls()) {
      auto pin = g->get_output_pin(d.name);
      if (pin.is_invalid()) {
        continue;
      }
      std::string pname{d.name};
      int32_t     bits = 0;
      auto        inps = pin.inp_edges();
      if (!inps.empty()) {
        bits = bits_of(inps.front().driver);
      }
      if (bits == 0) {
        bits = static_cast<int32_t>(d.bits);
      }
      set_output_delays(pname);
      for (auto i = 1; i < bits; ++i) {
        set_output_delays(absl::StrCat(pname, ".", str_tools::to_s(i)));
      }
    }
  }
}

void Pass_opentimer::compute_timing(const std::shared_ptr<hhds::Graph>& g) {
  TRACE_EVENT("pass", "OPENTIMER_compute_timing");

  timer.update_timing();

  // OpenTimer logs and skips a connect_pin whose pin/net is absent, but does
  // not return an error to its caller. Never publish QoR from such a partial
  // graph: every Liberty-cell input in a mapped netlist must have a net.
  size_t                   unconnected = 0;
  std::vector<std::string> examples;
  for (const auto& [name, pin] : timer.pins()) {
    if (pin.gate() != nullptr && pin.is_input() && pin.net() == nullptr) {
      ++unconnected;
      if (examples.size() < 4) {
        examples.push_back(name);
      }
    }
  }
  if (unconnected != 0) {
    std::string sample;
    for (const auto& name : examples) {
      sample += sample.empty() ? name : std::format(", {}", name);
    }
    livehd::diag::err("pass.opentimer", "unconnected-pin", "internal")
        .msg("pass.opentimer built an incomplete timing graph: {} Liberty input pin(s) are unconnected (first: {})",
             unconnected,
             sample)
        .fatal();
    return;
  }

  max_delay     = 0;
  auto& max_pin = max_pin_;  // member: the STA cache replays this summary line
  max_pin.clear();

  const auto& pins = timer.pins();

  // Every annotated gate output, kept for the timing report (2opt-freq D).
  struct Arrival {
    float                 delay;
    std::string           pin;
    hhds::Occurrence_node node;
  };
  std::vector<Arrival>  arrivals;
  hhds::Occurrence_node max_node;

  // OT gate/instance name -> node, to source-attribute the path points below.
  absl::flat_hash_map<std::string, hhds::Occurrence_node> inst2node;

  for (auto& node : leaf_nodes(g)) {
    auto op = type_op_of(node);
    if (op != Ntype_op::Sub) {
      continue;
    }

    auto instance_name = inst_of(node, hier_mode_);
    inst2node.emplace(instance_name, node);

    Color_qor* color_row = nullptr;
    if (stats_) {
      auto     base      = node.base_node();
      uint32_t region_id = 0;
      if (auto id = base.attr(livehd::attrs::synth_region_id); id.has()) {
        region_id = id.get();
      } else {
        // Flat/direct timing keeps mapped cells in their region definition,
        // where the compact key lives once on the graph input. Whole-design
        // physical flatten stamps the same key directly on each scratch node.
        if (auto* graph = base.get_graph(); graph != nullptr) {
          if (auto graph_id = graph->get_input_node().attr(livehd::attrs::synth_region_id); graph_id.has()) {
            region_id = graph_id.get();
          }
        }
      }
      if (region_id != 0) {
        auto it
            = std::find_if(color_qor_.begin(), color_qor_.end(), [&](const Color_qor& row) { return row.region_id == region_id; });
        if (it != color_qor_.end()) {
          color_row = &*it;
          ++color_row->cells;
        }
      }
    }

    for (auto& dpin : node.out_pins()) {
      if (dpin.is_invalid() || dpin.out_edges().empty()) {
        continue;
      }
      auto pin_name = absl::StrCat(instance_name, ":", driver_pin_name_of(node, dpin));

      auto it = pins.find(pin_name);
      if (it == pins.end()) {
        continue;
      }

      auto at_f = it->second.at(ot::MAX, ot::FALL);
      auto at_r = it->second.at(ot::MAX, ot::RISE);

      float delay = 0.0;
      if (at_f) {
        delay = *at_f;
      }
      if (at_r && *at_r > delay) {
        delay = *at_r;
      }
      if (delay > 0) {
        set_delay(dpin, delay);
        arrivals.push_back({delay, pin_name, node});

        if (color_row != nullptr && delay > color_row->max_arrival) {
          color_row->max_arrival  = delay;
          color_row->critical_pin = pin_name;
          color_row->critical_src = src_of_node(g, node);
        }

        if (delay > max_delay) {
          max_delay = delay;
          max_pin   = pin_name;
          max_node  = node;
        }
      } else {
        del_delay(dpin);
      }
    }
  }

  if (!max_pin.empty()) {
    if (margin) {
      margin_delay = (max_delay / 100.0) * (100 - margin);
      std::print("slowest delay:{} pin:{} margin:{}% (margin_delay:{})\n", max_delay, max_pin, margin, margin_delay);
    } else {
      std::print("slowest delay:{} pin:{} NO MARGIN selected\n", max_delay, max_pin);
    }
  }

  if (qor_path.empty()) {
    return;
  }

  // Critical-path trace for the report: one point per pin, source -> critical
  // pin/endpoint. report_timing ranks slack over ENDPOINTS, which exist only
  // at RAT'd primary outputs (flops stay native, so no Liberty timing tests)
  // — its path is used only when it actually reaches the critical arrival;
  // otherwise (a flop-din cone) reconstruct by walking max-arrival fanin
  // backward from the critical pin. Best-effort: an empty path still leaves a
  // renderable summary + endpoints table.
  struct Path_point {
    std::string     pin;
    float           at;
    bool            rise;
    const ot::Gate* gate;  // nullptr for ports / flop-boundary virtual PIs
  };
  std::vector<Path_point> path_pts;
  if (!max_pin.empty()) {
    auto paths = timer.report_timing(1, ot::MAX);
    if (!paths.empty() && !paths.front().empty() && paths.front().back().at >= max_delay - 1e-4F * std::max(1.0F, max_delay)) {
      for (const auto& p : paths.front()) {
        path_pts.push_back({p.pin.name(), p.at, p.transition == ot::RISE, p.pin.gate()});
      }
    }
  }
  if (path_pts.empty() && !max_pin.empty()) {
    constexpr float kNinf      = -std::numeric_limits<float>::infinity();
    // Worst MAX-corner arrival of a pin over both edges: {at, is_rise}.
    auto            arrival_of = [](const ot::Pin& p) -> std::pair<float, bool> {
      auto  af = p.at(ot::MAX, ot::FALL);
      auto  ar = p.at(ot::MAX, ot::RISE);
      float f  = af ? *af : kNinf;
      float r  = ar ? *ar : kNinf;
      return r >= f ? std::pair{r, true} : std::pair{f, false};
    };
    // net -> its driver (rct-root) pin, for the backward net hop of the walk.
    absl::flat_hash_map<const ot::Net*, const ot::Pin*> net_driver;
    for (const auto& [pname, p] : pins) {
      if (p.net() != nullptr && p.is_rct_root()) {
        net_driver.emplace(p.net(), &p);
      }
    }

    std::vector<Path_point>             rev;
    absl::flat_hash_set<const ot::Pin*> seen;  // a comb loop must not cycle the walk
    auto                                pit = pins.find(max_pin);
    const ot::Pin*                      cur = pit != pins.end() ? &pit->second : nullptr;
    while (cur != nullptr && seen.insert(cur).second) {
      auto [at, rise] = arrival_of(*cur);
      rev.push_back({cur->name(), at == kNinf ? 0.0F : at, rise, cur->gate()});
      if (cur->primary_input() != nullptr) {
        break;  // module input or flop/memory virtual PI: the path source
      }
      const ot::Pin* next = nullptr;
      if (cur->is_input()) {  // gate input: hop the net to its driver
        auto nd = net_driver.find(cur->net());
        if (nd != net_driver.end() && nd->second != cur) {
          next = nd->second;
        }
      } else if (const auto* gate = cur->gate(); gate != nullptr) {
        // gate output: greedy max-arrival input pin of the same gate. A
        // backward walk must be time-monotonic: an input arriving LATER than
        // this output has no timing arc to it (a sequential cell's D vs Q —
        // greedily picking D would splice the previous cycle's path in front
        // of this one, with the Time column running backward). Bound the pick
        // by the current arrival (+ float noise); at a DFF this follows the
        // clock pin (the launch path) or ends the walk.
        const float bound = at + 1e-4F * std::max(1.0F, std::abs(at));
        float       best  = kNinf;
        for (const ot::Pin* ip : gate->pins()) {
          if (ip == nullptr || !ip->is_input()) {
            continue;
          }
          if (auto [ia, ir] = arrival_of(*ip); ia > best && ia <= bound) {
            best = ia;
            next = ip;
          }
        }
      }
      cur = next;  // nullptr (untimed fanin: a tie cell / clock) ends the walk
    }
    path_pts.assign(rev.rbegin(), rev.rend());
  }

  // One JSON block per analyzed design: max delay + the critical-path points
  // + the worst endpoints, source-attributed through the gates' srcid (agent
  // edit targets).
  std::sort(arrivals.begin(), arrivals.end(), [](const Arrival& a, const Arrival& b) { return a.delay > b.delay; });
  constexpr size_t kMaxEndpoints = 10;

  std::string j = std::format("{{\"module\":\"{}\"", jesc(report_module_.empty() ? std::string{g->get_name()} : report_module_));
  if (!max_pin.empty()) {
    j += std::format(",\"max_delay\":{:.6g},\"critical_pin\":\"{}\"", max_delay, jesc(max_pin));
    if (auto src = src_of_node(g, max_node); !src.empty()) {
      j += std::format(",\"critical_src\":\"{}\"", jesc(src));
    }
  }
  j             += ",\"path\":[";
  float prev_at  = 0.0F;
  for (size_t i = 0; i < path_pts.size(); ++i) {
    const auto& p = path_pts[i];
    if (i != 0) {
      j += ",";
    }
    j += std::format("{{\"pin\":\"{}\",\"at\":{:.6g},\"delay\":{:.6g},\"dir\":\"{}\"",
                     jesc(p.pin),
                     p.at,
                     p.at - prev_at,
                     p.rise ? "rise" : "fall");
    if (p.gate != nullptr) {
      j += std::format(",\"cell\":\"{}\"", jesc(p.gate->cell_name()));
      if (auto it = inst2node.find(p.gate->name()); it != inst2node.end()) {
        if (auto src = src_of_node(g, it->second); !src.empty()) {
          j += std::format(",\"src\":\"{}\"", jesc(src));
        }
      }
    }
    j       += "}";
    prev_at  = p.at;
  }
  j += "],\"endpoints\":[";
  for (size_t i = 0; i < arrivals.size() && i < kMaxEndpoints; ++i) {
    if (i != 0) {
      j += ",";
    }
    j += std::format("{{\"pin\":\"{}\",\"delay\":{:.6g}", jesc(arrivals[i].pin), arrivals[i].delay);
    if (auto src = src_of_node(g, arrivals[i].node); !src.empty()) {
      j += std::format(",\"src\":\"{}\"", jesc(src));
    }
    j += "}";
  }
  j          += "]";
  // The design block ends here; the per-color `--stats` tail is rendered
  // separately so the STA reuse cache can store the block verbatim and re-stamp
  // each row's `resynth` (which describes what pass.abc did in THIS run, not
  // the netlist) on a hit.
  sta_block_  = j;
  j          += render_colors();
  j          += "}";
  qor_blocks_.push_back(std::move(j));
}

std::string Pass_opentimer::render_colors() const {
  if (!stats_) {
    return {};
  }
  std::string j = ",\"colors\":[";
  for (size_t i = 0; i < color_qor_.size(); ++i) {
    const auto& row = color_qor_[i];
    if (i != 0) {
      j += ",";
    }
    j += std::format("{{\"module\":\"{}\",\"color\":{},\"cells\":{},\"resynth\":{}",
                     jesc(row.module),
                     row.color,
                     row.cells,
                     row.resynth ? 1 : 0);
    if (row.max_arrival >= 0) {
      j += std::format(",\"max_arrival\":{:.6g},\"critical_pin\":\"{}\"", row.max_arrival, jesc(row.critical_pin));
      if (!row.critical_src.empty()) {
        j += std::format(",\"critical_src\":\"{}\"", jesc(row.critical_src));
      }
    }
    j += "}";
  }
  j += "]";
  return j;
}

// ---- STA reuse cache: key, replay, snapshot --------------------------------

std::string Pass_opentimer::cache_key(const std::shared_ptr<hhds::Graph>& g) const {
  auto  io  = g ? g->get_io() : nullptr;
  auto* lib = io ? io->get_library() : nullptr;
  if (lib == nullptr) {
    // Same contract as the `!d.valid` bail below: an empty key means this run
    // cannot be cached, and `cache_digestable_` is the ONE field that tells a
    // benchmark row "the netlist was undigestable" apart from "the digest was
    // fine and simply missed". Returning empty without stamping it reported a
    // permanently-uncacheable run as a plain miss, run after run.
    cache_digestable_ = false;
    return {};
  }
  // Merkle fold: an edited region body must change the top's digest, because
  // the timed design is the whole hierarchy flattened into one module. The map
  // holds each resolved child's shared_ptr so the raw pointers the resolver
  // hands out stay alive for the whole walk.
  absl::flat_hash_map<hhds::Gid, std::shared_ptr<hhds::Graph>> resolved;
  livehd::semdiff::Digest_resolver                             resolve = [&](hhds::Gid gid) -> hhds::Graph* {
    if (auto it = resolved.find(gid); it != resolved.end()) {
      return it->second.get();
    }
    // Mapped netlists legitimately contain Liberty cells and other leaf Subs
    // whose GraphIO has no LiveHD body. The canonical digest treats an
    // unresolved Sub as a black box; do not call GraphLibrary::get_graph for
    // one, because its contract asserts that the gid has a materialized (or
    // pending) graph body.
    if (!lib->has_graph(gid)) {
      return nullptr;
    }
    auto child = lib->get_graph(gid);
    resolved.emplace(gid, child);
    return child.get();
  };
  const auto d = livehd::semdiff::canonical_digest(g.get(), resolve, livehd::semdiff::Sub_fold::merkle, /*matching_io_names=*/true);
  if (!d.valid) {
    // An anonymous state cell keys off a per-run debug nid: not reproducible, so
    // this netlist is deliberately not cacheable. Re-time, every run.
    cache_digestable_ = false;
    return {};
  }
  const uint64_t env = livehd::opentimer::Sta_cache::env_hash(timing_file_list, top_filter, hier_setting_, margin, stats_);
  return std::format("{:016x}{:016x}{:016x}", d.h0, d.h1, env);
}

void Pass_opentimer::replay(const livehd::opentimer::Sta_record& rec) {
  time_unit_label_ = rec.time_unit;
  max_delay        = static_cast<float>(rec.max_delay);
  max_pin_         = rec.max_pin;

  // `resynth` is THIS run's pass.abc verdict, never the cache's: color_qor_ was
  // already seeded from the graph (module/color/resynth) before the lookup, so
  // only the timing half of each row comes from the record.
  absl::flat_hash_map<std::string, const livehd::opentimer::Sta_record::Color_row*> by_key;
  by_key.reserve(rec.colors.size());
  for (const auto& row : rec.colors) {
    by_key.emplace(std::format("{}\x1f{}", row.module, row.color), &row);
  }
  for (auto& row : color_qor_) {
    auto it = by_key.find(std::format("{}\x1f{}", row.module, row.color));
    if (it == by_key.end()) {
      continue;
    }
    row.cells        = it->second->cells;
    row.max_arrival  = static_cast<float>(it->second->max_arrival);
    row.critical_pin = it->second->critical_pin;
    row.critical_src = it->second->critical_src;
  }

  opaque_logic_nodes_    = rec.opaque_nodes;
  ambiguous_or_nodes_    = rec.ambiguous_nodes;
  opaque_logic_examples_ = rec.opaque_examples;
  ambiguous_or_examples_ = rec.or_examples;
  emit_boundary_warning();

  if (!max_pin_.empty()) {
    if (margin) {
      margin_delay = (max_delay / 100.0F) * static_cast<float>(100 - margin);
      std::print("slowest delay:{} pin:{} margin:{}% (margin_delay:{})\n", max_delay, max_pin_, margin, margin_delay);
    } else {
      std::print("slowest delay:{} pin:{} NO MARGIN selected\n", max_delay, max_pin_);
    }
  }
  sta_block_ = rec.block;
  qor_blocks_.push_back(rec.block + render_colors() + "}");
}

livehd::opentimer::Sta_record Pass_opentimer::snapshot() const {
  livehd::opentimer::Sta_record rec;
  rec.time_unit       = time_unit_label_;
  rec.max_delay       = max_delay;
  rec.max_pin         = max_pin_;
  rec.block           = sta_block_;
  rec.opaque_nodes    = opaque_logic_nodes_;
  rec.ambiguous_nodes = ambiguous_or_nodes_;
  rec.opaque_examples = opaque_logic_examples_;
  rec.or_examples     = ambiguous_or_examples_;
  for (const auto& row : color_qor_) {
    rec.colors.push_back({row.module, row.color, row.cells, row.max_arrival, row.critical_pin, row.critical_src});
  }
  return rec;
}

void Pass_opentimer::write_qor() const {
  if (qor_path.empty()) {
    return;
  }
  // The Liberty time unit as a label (arrivals are plain numbers in this unit),
  // so the pretty renderer can print real units. Computed in the live path
  // (time_unit_label_) and REPLAYED from the cache on a hit, which never parses
  // a Liberty at all.
  std::string j = "{\"schema_version\":1,\"kind\":\"sta\",";
  if (!time_unit_label_.empty()) {
    j += std::format("\"time_unit\":\"{}\",", time_unit_label_);
  }
  j += "\"designs\":[";
  for (size_t i = 0; i < qor_blocks_.size(); ++i) {
    if (i != 0) {
      j += ",";
    }
    j += qor_blocks_[i];
  }
  j += "]";
  // The reuse tier's own counters, in the same place pass.abc puts its
  // `incremental` object, so one harvester serves every tier. Emitted even when
  // the tier is OFF (enabled=false + zero counters), for the same reason
  // pass.abc does: a benchmark row must be able to tell an honestly disabled
  // cache from an old binary that reports no telemetry at all.
  j += std::format(",\"incremental\":{{\"enabled\":{},\"hits\":{},\"misses\":{},\"digestable\":{},\"lookup_ms\":{:.3f}}}",
                   cache_enabled_ ? "true" : "false",
                   cache_hit_ ? 1 : 0,
                   (cache_enabled_ && !cache_hit_) ? 1 : 0,
                   cache_digestable_ ? "true" : "false",
                   cache_lookup_ms_);
  j += "}";
  std::ofstream ofs(qor_path, std::ios::binary | std::ios::trunc);
  if (!ofs) {
    livehd::diag::err("pass.opentimer", "qor-write", "io").msg("pass.opentimer: cannot write timing file '{}'", qor_path).fatal();
    return;
  }
  ofs << j << "\n";
}

void Pass_opentimer::compute_power(const std::shared_ptr<hhds::Graph>& g) {
  TRACE_EVENT("pass", "OPENTIMER_compute_power");

  timer.update_timing();

  const auto& gates = timer.gates();

  double total_cap  = 0;
  double total_ipwr = 0;

  float voltage = 1;
  auto  x       = timer.cell_voltage();
  if (x) {
    voltage = *x;
  }

  double cap_unit   = timer.capacitance_unit()->value();
  double timeunit   = timer.time_unit()->value();
  double power_unit = timer.power_unit()->value();

  for (auto& pvcd : vcd_list) {
    pvcd.set_timescale(timeunit);
  }
  std::cout << "================================\n";
  for (auto node : g->body().nodes()) {
    auto op = type_op_of(node);
    if (op != Ntype_op::Sub) {
      continue;
    }

    auto instance_name = default_instance_name(node);

    auto it2 = gates.find(instance_name);
    if (it2 == gates.end()) {
      std::print("WEIRD. Where is the gate named {}? (node {})\n", instance_name, debug_name(node));
      continue;
    }

    for (const auto* pin : it2->second.pins()) {
      auto [cap, ipwr] = pin->power();

      cap  *= static_cast<float>(freq * power_unit * 0.5 * voltage * voltage * cap_unit / timeunit);
      ipwr *= static_cast<float>(freq * power_unit * cap_unit / timeunit);

      total_cap  += cap;
      total_ipwr += ipwr;

      // WARNING: Replace last ':' for ','
      // -OpenTimer uses as pin name: "whatever":"pin"
      // -Power_vcd uses "whatever","pin"
      std::string pin_name{pin->name()};
      auto        last_colon_pos = pin_name.rfind(':');
      I(last_colon_pos != std::string::npos);
      pin_name[last_colon_pos] = ',';

      for (auto& pvcd : vcd_list) {
        pvcd.add(pin_name, ipwr + cap);
      }

      std::print("iname:{} pin:{} ipwr:{} cap:{}\n", instance_name, pin_name, ipwr, cap);
    }
  }

  std::cout << "================================\n";
  for (auto& pvcd : vcd_list) {
    pvcd.compute(odir);
    std::print("AVG power:{} for {}\n", pvcd.get_power_average(), pvcd.get_filename());
  }

  std::print("TOTAL power:{} DYNAMIC power:{} INTERNAL power:{} W voltage:{} V freq={}MHz\n",
             total_cap + total_ipwr,
             total_cap,
             total_ipwr,
             voltage,
             freq / 1e6);
}

void Pass_opentimer::populate_table(const std::shared_ptr<hhds::Graph>& g) {
  TRACE_EVENT("pass", "OPENTIMER_populate_table");

  if (margin_delay <= 0 || max_delay <= 0) {
    return;
  }

  // Clear any pre-existing colors before annotating critical paths.
  for (auto node : g->body().nodes()) {
    node.attr(livehd::attrs::color).del();
  }

  for (auto node : g->body().nodes()) {
    for (auto& dpin : node.out_pins()) {
      if (dpin.is_invalid() || dpin.out_edges().empty()) {
        continue;
      }
      if (!has_delay(dpin)) {
        continue;
      }

      auto delay = get_delay(dpin);
      if (delay < margin_delay) {
        continue;
      }

      int color = 100 * ((delay - margin_delay) / (max_delay - margin_delay));

      if (has_color(node)) {
        auto co = color_of(node);
        if (co >= color) {
          continue;
        }
      }
      set_color(node, color);

      backpath_set_color(node, color);
    }
  }
}

void Pass_opentimer::backpath_set_color(hhds::Node_class node, int color) {
  while (true) {
    I(color_of(node) == color);

    hhds::Pin_class dpin;
    float           dpin_delay = 0;
    for (const auto& edge : node.inp_edges()) {
      if (!has_delay(edge.driver)) {
        continue;
      }
      const auto delay = get_delay(edge.driver);
      if (delay >= dpin_delay) {
        dpin       = edge.driver;
        dpin_delay = delay;
      }
    }

    if (dpin_delay <= 0 || is_graph_input_pin(dpin) || is_graph_output_pin(dpin)) {
      return;
    }

    auto       back_node = dpin.get_master_node();
    const auto back_op   = type_op_of(back_node);
    if (Ntype::is_loop_first(back_op) || Ntype::is_loop_last(back_op)) {
      return;  // Do not cross constants/flops/memories.
    }

    if (!has_color(back_node)) {
      set_color(back_node, color);
      return;
    }
    if (color_of(back_node) >= color) {
      return;
    }

    set_color(back_node, color);
    node = back_node;
  }
}

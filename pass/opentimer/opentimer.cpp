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
#include "provisional_cell.hpp"
#include "semdiff.hpp"
#include "sta_salt.hpp"
#include "str_tools.hpp"

// WARNING: opentimer has a nasty "define has_member" that overlaps with perfetto methods
#undef has_member
#include "perf_tracing.hpp"

using livehd::graph_util::bits_of;
using livehd::graph_util::color_of;
using livehd::graph_util::const_of;
using livehd::graph_util::create_const;
using livehd::graph_util::debug_name;
using livehd::graph_util::default_instance_name;
using livehd::graph_util::get_driver_of_sink_name;
using livehd::graph_util::has_color;
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
// with no pin_name while a consumer sink's get_driver_pins() hands back the SAME
// pin carrying its port name, so the pin's own get_hier_name differs (one omits
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
  // NOT guarded with assert_pin_attr_role<pin_delay_t> -- see the note in
  // graph/node_util.hpp on assert_pin_attr_role. Its only caller iterates
  // node.out_pins(), whose handles report is_sink() because
  // Graph::get_driver_pins(Node_class) CLEARS the driver bit that
  // create_driver_pin sets. Until those two agree, is_driver() cannot be used as
  // the role test here.
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
  if (n.is_invalid()) {
    return {};
  }
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
    livehd::diag::err("pass.opentimer", "bad-option", "io")
        .msg("pass.opentimer times one module per run ({} defs in the library): pass --top <module> to pick one", selected.size())
        .fatal();
    return;
  }

  auto       g              = selected.front();
  const auto annotation_top = g;
  if (!pass.physical_rc_) {
    if (auto stored = g->get_input_node().attr(livehd::attrs::placement_rc); stored.has()) {
      pass.physical_rc_ = std::make_shared<livehd::physical::Parasitics>(livehd::physical::deserialize_parasitics(stored.get()));
    }
  }
  if (pass.physical_rc_) {
    pass.physical_timing_cuts_.insert(pass.physical_rc_->timing_cuts.begin(), pass.physical_rc_->timing_cuts.end());
  }

  pass.validate_physical_rc(g);  // validate before a warm STA result can be replayed

  if (auto session = std::exchange(var.physical_timing_session, {})) {
    auto report                 = var;
    report.graphs               = {g};
    report.physical_rc          = pass.physical_rc_;
    report.physical_timing_cuts = pass.physical_timing_cuts_;
    if (session->write_report(report)) {
      return;
    }
  }

  // A design compiled WITHOUT `compile.unroll` -- false is the DEFAULT, so this
  // is the ordinary path -- keeps ONE compact Sub standing for `count`
  // occurrences. pass.opentimer is not occurrence-aware — all three hier modes
  // walk a Sub as a single physical instance — so area, path count and every
  // QoR number would come out short by count-1. Expand up front, for every
  // mode, into opentimer's private library: the input library belongs to the
  // caller and this pass only reads it. A design with no comptime loops has no
  // compact Sub; nothing is copied and the input graph is timed in place.
  const auto design_defs = g->definitions().graphs();
  pass.prepare_color_report(g);
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
    if (rec != nullptr && (!pass.critical_ || pass.apply_annotations(annotation_top, rec->annotations, rec->annotation_summary))) {
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
    if (pass.critical_) {
      // copy_from retains body indices. Record those identities before compact
      // instances are replaced; each new replica maps back to its source node.
      for (const auto& copy : occurrence_graphs) {
        const auto source_io = copy ? lib->find_io(copy->get_name()) : nullptr;
        const auto source    = source_io ? source_io->get_graph() : std::shared_ptr<hhds::Graph>{};
        if (!source) {
          livehd::diag::err("pass.opentimer", "scratch-copy", "internal")
              .msg("could not map a copied definition back to its source for critical annotations")
              .emit();
          return;
        }
        std::map<uint64_t, hhds::Node_class> originals;
        for (auto n : source->body().nodes()) {
          originals.emplace(n.get_debug_nid(), n);
        }
        std::vector<hhds::Node_class> compact;
        for (auto n : copy->body().nodes()) {
          if (auto it = originals.find(n.get_debug_nid()); it != originals.end()) {
            pass.timing_origins_[timing_node_key(n)] = it->second;
          }
          if (n.is_loop_subnode()) {
            compact.push_back(n);
          }
        }
        for (auto n : compact) {
          const auto                    source_node = pass.timing_origins_.at(timing_node_key(n));
          std::vector<hhds::Node_class> replicas;
          if (!livehd::graph_util::materialize_occurrence(copy.get(), n, "pass.opentimer", &replicas)) {
            return;
          }
          pass.timing_origins_.erase(timing_node_key(n));
          for (auto replica : replicas) {
            pass.timing_origins_[timing_node_key(replica)] = source_node;
          }
        }
      }
    } else if (!livehd::graph_util::materialize_occurrences_all(occurrence_graphs, "pass.opentimer")) {
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
      livehd::partition::Flat_origin_map origins;
      scratch = livehd::partition::flatten_hierarchy(g.get(), scratch_lib, scratch_name, pass.critical_ ? &origins : nullptr);
      for (const auto& [node, origin] : origins) {
        pass.timing_origins_[timing_node_key(node)] = origin.src_node;
      }
      if (!scratch) {
        return;  // diag already emitted
      }
      g = scratch;
    }
  }

  pass.setup_hier(g);
  pass.build_circuit(g);  // ensure_libs() runs here: a cache hit above never parses a Liberty
  pass.read_sdc_spef();
  pass.apply_case_analysis();
  pass.apply_physical_rc(g);
  pass.apply_physical_timing_cuts(pass.physical_timing_cuts_);
  pass.prepare_critical_targets();
  pass.apply_physical_captures();
  pass.annotate_critical(g);
  pass.compute_timing(g);
  if (!pass.critical_) {
    pass.populate_table(g);
  }
  if (pass.critical_ && !livehd::diag::sink().has_errors()
      && !pass.apply_annotations(annotation_top, pass.annotation_records_, pass.annotation_summary_)) {
    livehd::diag::err("pass.opentimer", "critical-identity", "internal")
        .msg("could not map timing annotations to input nodes")
        .fatal();
  }
  pass.set_time_unit_label();
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
  // SDC clocks are ideal until explicitly propagated. Clock-to-Q data arcs
  // and all data-net RC remain timed; only the traced clock network is ideal.
  std::vector<std::string> ideal;
  for (const auto& [name, target] : timing_targets_) {
    if ((!target.ports.empty() || !target.pin.empty()) && !propagated_clocks_.contains(name)) {
      ideal.push_back(name);
    }
  }
  timer.set_ideal_clocks(std::move(ideal), {physical_clock_endpoints_.begin(), physical_clock_endpoints_.end()});
  for (const auto& f : spef_file_list) {
    // The reader currently cannot certify annotation coverage on a remapped
    // netlist. Keep reporting timing, but do not certify a complete QoR gate.
    constraints_complete_ = false;
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
  if (io_load_ff_ >= 0) {
    if (auto unit = timer.capacitance_unit(); unit) {
      const float load = static_cast<float>(io_load_ff_ * 1e-15 / unit->value());
      for (auto split : {ot::MIN, ot::MAX}) {
        for (auto tran : {ot::FALL, ot::RISE}) {
          timer.set_load(pname, split, tran, load);
        }
      }
    } else {
      constraints_complete_ = false;
    }
  }
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
    const std::string_view tname = io->get_name();
    if (auto it = liberty_cell_memo.find(tname); it != liberty_cell_memo.end()) {
      return it->second;
    }
    const auto& lib     = timer.celllib(ot::MAX);
    const bool  is_cell = lib && lib->cell(std::string{tname}) != nullptr;
    liberty_cell_memo.emplace(std::string{tname}, is_cell);
    return is_cell;
  };

  // A macro port can be a Liberty bus. Scalar standard cells retain their
  // one-bit tracker semantics even when LGraph carries a padded pin width.
  absl::flat_hash_map<std::string, std::vector<std::string>> liberty_port_memo;
  const auto liberty_port = [&](const auto& node, const std::string& port) -> const std::vector<std::string>& {
    auto key               = sub_type_name(node) + ":" + port;
    auto [entry, inserted] = liberty_port_memo.try_emplace(key);
    if (!inserted) {
      return entry->second;
    }
    const auto& lib  = timer.celllib(ot::MAX);
    const auto* cell = lib ? lib->cell(sub_type_name(node)) : nullptr;
    if (!cell || cell->cellpin(port)) {
      entry->second.push_back(port);
      return entry->second;
    }
    std::vector<std::pair<int, std::string>> bits;
    const auto                               prefix = port + "[";
    for (const auto& [name, pin] : cell->cellpins) {
      if (name.starts_with(prefix) && name.ends_with("]")) {
        bits.emplace_back(std::stoi(name.substr(prefix.size(), name.size() - prefix.size() - 1)), name);
      }
    }
    std::ranges::sort(bits);
    for (const auto& [bit, name] : bits) {
      entry->second.push_back(name);
    }
    if (entry->second.empty()) {
      entry->second.push_back(port);  // the ordinary missing-pin diagnostic
    }
    return entry->second;
  };

  // The boundary and gate phases below walk the same immutable leaf set. Reuse
  // one traversal result instead of recreating it independently for each.
  const auto nodes       = leaf_nodes(g);
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
  // class-level pin fan-in (Pin_class::get_driver_pins), which is LOCAL-only — it
  // never crosses a module boundary — so the tracker would build on a child
  // module's own input port (a bare "io_x") instead of the parent's driver.
  // The OCCURRENCE readers are the hier-resolving ones — Occurrence_pin's
  // get_driver_pins() resolves across an instance boundary exactly as the
  // occurrence edge walk did — so route the tracker's operand lookups through
  // them when flattening.
  //
  // inp_sorted_pins(), NOT the raw inp_pins(): hhds stores port 0 as the node
  // itself, so the raw list OMITS it — and port 0 is where a banked cell's first
  // operand lives. The sorted reader yields the node-as-pin first and then
  // ascending port order, which is what the old node-level edge walk visited.
  auto hier_driver_of = [&](const hhds::Occurrence_node& n, std::string_view sname) -> hhds::Occurrence_pin {
    for (const auto& sink : n.inp_sorted_pins()) {
      if (sink_pin_name_of(n, sink) != sname) {
        continue;
      }
      for (const auto& driver : sink.get_driver_pins()) {
        return driver;  // first driver of the first matching sink, as the edge walk returned
      }
    }
    return {};
  };

  // Every driver feeding a node, in the order the node-level edge walk yielded
  // them (sorted sink ports, node-as-pin first; see hier_driver_of above for why
  // the SORTED reader is load-bearing). get_driver_pins() is the PLURAL reader on
  // purpose: a compact loop's carry-in is the one sink livehd sanctions with two
  // drivers (external seed plus the self edge), and the singular get_driver_pin()
  // would silently drop the seed. One entry per driver is therefore one entry per
  // old in-EDGE, so an operand-counting client still counts the same.
  auto inp_driver_pins = [](const hhds::Occurrence_node& n) {
    std::vector<hhds::Occurrence_pin> drivers;
    for (const auto& sink : n.inp_sorted_pins()) {
      for (const auto& driver : sink.get_driver_pins()) {
        drivers.push_back(driver);
      }
    }
    return drivers;
  };

  auto is_resolved_const = [&](const auto& dpin) { return dpin.is_const(); };
  auto operand_bits_of   = [&](const hhds::Occurrence_node& owner, std::string_view sname, const auto& dpin) -> int32_t {
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
    const auto& pins = liberty_port(master, driver_pin_name_of(master, dpin));
    if (pins.size() > 1) {
      pin_tracker.add_opaque(trk_id(dpin), static_cast<int32_t>(pins.size()));
    } else {
      pin_tracker.add_scalar_if_absent(trk_id(dpin), bits_of(dpin));
    }
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
      // A graph output pin is a sink: one driver (graph/cell.hpp).
      auto driver_dpin = out_sink.get_driver_pin();
      if (driver_dpin.is_invalid()) {
        continue;
      }

      std::string driver_name{d.name};
      // The interface can extend or truncate its driver. Every declared bit
      // is a distinct load, including sign-extension aliases on one net.
      const auto  bits = d.bits ? static_cast<int32_t>(d.bits) : std::max(1, bits_of(driver_dpin));

      timer.insert_primary_output(driver_name);
      timer.insert_net(driver_name);
      for (auto i = 1; i < bits; ++i) {
        auto bus_bit_name = absl::StrCat(driver_name, ".", str_tools::to_s(i));
        timer.insert_primary_output(bus_bit_name);
        timer.insert_net(bus_bit_name);
      }
      // Keep each driver on its canonical physical net. Outputs are attached
      // after pin tracking resolves wiring nodes, so aliases contribute separate
      // loads without renaming the producer or disconnecting other readers.
    }
  }

  auto tracker_ready = [&](const auto& dpin) {
    if (dpin.is_const() || is_graph_input_pin(dpin)) {
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
      if (op == Ntype_op::AttrSet) {
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
          auto       a_dpin     = hier_driver_of(node, "a");
          const auto mask_dpin  = livehd::graph_util::bit_range(node);
          auto       value_dpin = hier_driver_of(node, "value");
          if (a_dpin.is_invalid() || !mask_dpin.has_value() || value_dpin.is_invalid()) {
            livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
                .msg("Invalid corrupt set_mask node {} (cprop should have deleted it)", debug_name(node))
                .fatal();
            return;
          }
          if (!tracker_ready(a_dpin) || !tracker_ready(value_dpin)) {
            deferred_nodes.push_back(node);
            continue;
          }
          if (!mask_dpin.has_value()) {
            livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
                .msg("opentimer can not handle non-constant masks on node {} (cprop/tmap first)", debug_name(node))
                .fatal();
            return;
          }
          const auto& mask_const = livehd::graph_util::mask_window_const(mask_dpin->first, mask_dpin->second);
          const auto  a_bits     = operand_bits_of(node, "a", a_dpin);
          seed_operand(a_dpin, a_bits);
          seed_operand(value_dpin, static_cast<int32_t>(mask_const.get_signed_bits()));
          pin_tracker.add_set_mask(wname, trk_id(a_dpin), a_bits, trk_id(value_dpin), mask_dpin->first, mask_dpin->second);
        } else if (op == Ntype_op::Get_mask) {
          auto       a_dpin    = hier_driver_of(node, "a");
          const auto mask_dpin = livehd::graph_util::bit_range(node);
          if (a_dpin.is_invalid() || !mask_dpin.has_value()) {
            livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
                .msg("Invalid corrupt get_mask node {} (cprop should have deleted it)", debug_name(node))
                .fatal();
            return;
          }
          if (!tracker_ready(a_dpin)) {
            deferred_nodes.push_back(node);
            continue;
          }
          if (!mask_dpin.has_value()) {
            livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
                .msg("opentimer can not handle non-constant masks on node {} (cprop/tmap first)", debug_name(node))
                .fatal();
            return;
          }
          const auto a_bits = operand_bits_of(node, "a", a_dpin);
          seed_operand(a_dpin, a_bits);
          pin_tracker.add_get_mask(wname, trk_id(a_dpin), a_bits, mask_dpin->first, mask_dpin->second);
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
          if (!b_dpin.is_const()) {
            livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
                .msg("opentimer can not handle non-constant SRA on node {} (cprop/tmap first)", debug_name(node))
                .fatal();
            return;
          }
          const auto& b_const = const_of(b_dpin);
          auto        a_bits  = operand_bits_of(node, "a", a_dpin);
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
          // The SRA's own driver width bounds what any consumer can read.
          pin_tracker.add_sra(wname, trk_id(a_dpin), a_bits, b_const, std::max(bits_of(node.get_driver_pin(0)), 0));
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
          if (!b_dpin.is_const()) {
            livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
                .msg("opentimer can not handle non-constant Sext on node {} (cprop/tmap first)", debug_name(node))
                .fatal();
            return;
          }
          const auto& b_const = const_of(b_dpin);
          const auto  a_bits  = operand_bits_of(node, "a", a_dpin);
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
          if (!b_dpin.is_const()) {
            livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
                .msg("opentimer can not handle non-constant SHL on node {} (cprop/tmap first)", debug_name(node))
                .fatal();
            return;
          }
          const auto& b_const = const_of(b_dpin);
          const auto  a_bits  = operand_bits_of(node, "a", a_dpin);
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
          for (const auto& sink : node.inp_sorted_pins()) {
            for (const auto& driver : sink.get_driver_pins()) {
              lane_by_pid.insert_or_assign(sink.get_port_id(), driver);
              lanes_ready = lanes_ready && tracker_ready(driver);
            }
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
          const auto inps = inp_driver_pins(node);
          if (std::any_of(inps.begin(), inps.end(), [&](const auto& dpin) { return !tracker_ready(dpin); })) {
            deferred_nodes.push_back(node);
            continue;
          }
          for (const auto& dpin : inps) {
            // add_or IGNORES an untracked operand (it returns early), so an
            // unseeded cell output silently drops its whole timing arc and the
            // Or result collapses onto the zero net.
            seed_cell_output(dpin);
            pin_tracker.add_or(wname, trk_id(dpin));
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
          const auto           inps = inp_driver_pins(node);
          if (std::any_of(inps.begin(), inps.end(), [&](const auto& dpin) { return !tracker_ready(dpin); })) {
            deferred_nodes.push_back(node);
            continue;
          }
          for (const auto& dpin : inps) {
            if (dpin.is_const()) {
              a_mask = a_mask.and_op(const_of(dpin));
            } else {
              if (!a_dpin.is_invalid()) {
                livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
                    .msg("pin_tracker needed for netlist can not handle multiple unknowns on node {}", debug_name(node))
                    .fatal();
                return;
              }
              a_dpin = dpin;
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
          const auto& pins = liberty_port(node, driver_pin_name_of(node, dpin));
          timer.insert_net(wname);
          if (pins.size() > 1) {
            pin_tracker.add_opaque(wname, static_cast<int32_t>(pins.size()));
            for (size_t bit = 1; bit < pins.size(); ++bit) {
              timer.insert_net(std::format("{}.{}", wname, bit));
            }
          } else {
            pin_tracker.add_scalar(wname, bits_of(dpin));
          }
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

  auto state_clock = [&](const hhds::Occurrence_node& node, hhds::Port_id port = 2) {
    if (type_op_of(node) == Ntype_op::Latch) {
      port = 4;  // a native latch's gate is its enable, not the reserved clock pin
    }
    auto local = node.base_node().try_get_sink_pin(port);
    if (local.is_invalid()) {
      return std::string{};
    }
    const auto drivers = local.get_driver_pins();
    if (drivers.empty() || drivers.front().is_const()) {
      return std::string{};
    }
    auto       pin    = g->occurrences().lift(drivers.front());
    const auto values = pin_tracker.get_pin_vector(trk_id(pin));
    if (!values.empty() && values.front().pos >= 0) {
      return values.front().pos == 0 ? std::string{values.front().id()}
                                     : std::format("{}.{}", values.front().id(), values.front().pos);
    }
    return get_driver_net_name(pin);
  };
  if (critical_) {
    for (const auto& node : nodes) {
      // One entry per driver pin, not per edge: a W-bit driver with fanout F
      // would otherwise list the node W x F times.
      absl::flat_hash_set<hhds::Occurrence_index> drivers;
      for (const auto& edge : node.out_edges()) {
        if (edge.driver.is_invalid() || edge.driver.is_const() || !drivers.insert(edge.driver.get_occurrence_index()).second) {
          continue;
        }
        const auto& values = pin_tracker.get_pin_vector(trk_id(edge.driver));
        if (values.empty()) {
          timing_net_nodes_[get_driver_net_name(edge.driver)].push_back(node.base_node());
        } else {
          for (const auto& value : values) {
            if (value.pos >= 0) {
              timing_net_nodes_[value.pos == 0 ? value.id() : std::format("{}.{}", value.id(), value.pos)].push_back(
                  node.base_node());
            }
          }
        }
      }
    }
  }

  // 4th: create every sequential-boundary PI BEFORE queuing any gate
  // connection. leaf_nodes() is deliberately not ordered in flat mode, so a
  // single mixed loop can encounter a consuming gate before the flop/memory
  // that creates its bit-net PI. OpenTimer then rejects the connection while
  // still returning success, and the PI appears later -- leaving a silently
  // incomplete timing graph.
  for (auto node : nodes) {
    auto op = type_op_of(node);
    if (op != Ntype_op::Flop && op != Ntype_op::Latch && op != Ntype_op::Memory && op != Ntype_op::Div && op != Ntype_op::Rem) {
      continue;
    }
    if (critical_ && (op == Ntype_op::Div || op == Ntype_op::Rem)) {
      livehd::diag::err("pass.opentimer", "critical-coverage", "unsupported")
          .msg("critical annotations require a timing model for native {}", Ntype::get_name(op))
          .fatal();
    }
    auto launch_clock = critical_ && op != Ntype_op::Memory ? state_clock(node) : std::string{};
    if ((critical_ || physical_rc_) && op == Ntype_op::Memory) {
      // A port without a clock (asynchronous read, constant clock pin) does not
      // vote: the memory launches in the one clock its clocked ports share.
      for (auto sink : node.inp_sorted_pins()) {
        if (sink.get_port_id() % Ntype::Memory_port_stride != 2) {
          continue;
        }
        const auto clock = state_clock(node, sink.get_port_id());
        if (clock.empty()) {
          continue;
        }
        if (critical_ && !launch_clock.empty() && clock != launch_clock) {
          livehd::diag::err("pass.opentimer", "critical-memory-clock", "unsupported")
              .msg("critical annotations require a Liberty timing model for a native memory with multiple clocks")
              .fatal();
        }
        launch_clock = clock;
      }
    }
    if (critical_ && launch_clock.empty()) {
      livehd::diag::err("pass.opentimer", "critical-clock-unresolved", "io")
          .msg("native {} '{}' has no resolvable clock net",
               Ntype::get_name(op),
               livehd::graph_util::node_name_of(node.base_node()))
          .fatal();
    }
    ++native_state_nodes_;
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
      if (is_overwritten(dnet, dpin) && !(physical_rc_ && op == Ntype_op::Memory)) {
        continue;  // drives a primary output directly: already a PO net
      }
      auto       wname         = physical_rc_ && op == Ntype_op::Memory ? get_driver_net_name(dpin) : dnet;
      const auto bits          = bits_of(dpin);
      const auto physical_wire = wname;
      if (physical_rc_ && op == Ntype_op::Memory && is_overwritten(dnet, dpin)) {
        wname = std::format("__sta_mem_src_{}_{}", inst_of(node, hier_mode_), dpin.get_port_id());
      }
      if (physical_rc_ && op == Ntype_op::Memory) {
        for (int bit = 0; bit < std::max(1, bits); ++bit) {
          physical_native_pins_[std::format("{}:out_{}[{}]", node.get_name(), dpin.get_port_id(), bit)]
              = bit ? std::format("{}.{}", wname, bit) : wname;
        }
      }
      timer.insert_primary_input(wname);  // idempotent net insert underneath
      set_input_delays(wname);
      if (wname != physical_wire) {
        timer.disconnect_pin(wname).connect_pin(wname, physical_wire);
      }
      if (critical_ || (physical_rc_ && op == Ntype_op::Memory)) {
        timing_pin_nodes_[wname]    = node.base_node();
        native_start_clocks_[wname] = launch_clock;
      }
      for (auto i = 1; i < bits; ++i) {
        auto bus_bit_name = absl::StrCat(wname, ".", str_tools::to_s(i));
        timer.insert_primary_input(bus_bit_name);
        set_input_delays(bus_bit_name);
        if (wname != physical_wire) {
          timer.disconnect_pin(bus_bit_name).connect_pin(bus_bit_name, std::format("{}.{}", physical_wire, i));
        }
        if (critical_ || (physical_rc_ && op == Ntype_op::Memory)) {
          timing_pin_nodes_[bus_bit_name]    = node.base_node();
          native_start_clocks_[bus_bit_name] = launch_clock;
        }
      }
    }
  }

  // Native state has no Liberty test arc. Attach a zero-load virtual output
  // to each sampled data/control bit so it participates in required-time
  // propagation, with the actual capture clock resolved through hierarchy.
  if (critical_ || physical_rc_) {
    for (const auto& node : nodes) {
      const auto op = type_op_of(node);
      if (op != Ntype_op::Flop && op != Ntype_op::Latch && op != Ntype_op::Memory) {
        continue;
      }
      for (const auto& sink : node.inp_sorted_pins()) {
        const auto name = sink_pin_name_of(node, sink);
        const bool data = name.ends_with("din") || (op != Ntype_op::Latch && name.ends_with("enable")) || name == "update"
                          || name.ends_with("addr");
        const auto off  = sink.get_port_id() % Ntype::Memory_port_stride;
        const bool memory_physical = physical_rc_ && op == Ntype_op::Memory
                                     && (off == 0 || off == 2 || off == 3 || off == 4 || off == 12 || off == 13 || off == 14);
        if (!data && !memory_physical) {
          continue;
        }
        for (const auto& driver : sink.get_driver_pins()) {
          if (driver.is_const()) {
            continue;
          }
          auto         values = pin_tracker.get_pin_vector(trk_id(driver));
          const size_t bits   = values.empty() ? static_cast<size_t>(std::max(1, bits_of(driver))) : values.size();
          for (size_t bit = 0; bit < bits; ++bit) {
            if (!values.empty() && values[bit].pos < 0) {
              continue;
            }
            const std::string wire
                = values.empty() ? (bit == 0 ? get_driver_net_name(driver) : std::format("{}.{}", get_driver_net_name(driver), bit))
                  : values[bit].pos == 0 ? std::string{values[bit].id()}
                                         : std::format("{}.{}", values[bit].id(), values[bit].pos);
            const auto endpoint = std::format("__sta_end_{}_{}_{}", inst_of(node, hier_mode_), name, bit);
            timer.insert_primary_output(endpoint).disconnect_pin(endpoint).connect_pin(endpoint, wire);
            timing_pin_nodes_[endpoint] = node.base_node();
            if (memory_physical) {
              physical_native_pins_[std::format("{}:in_{}[{}]", node.get_name(), sink.get_port_id(), bit)] = endpoint;
            }
            const auto clock_port
                = op == Ntype_op::Memory
                      ? static_cast<hhds::Port_id>((sink.get_port_id() / Ntype::Memory_port_stride) * Ntype::Memory_port_stride + 2)
                      : hhds::Port_id{2};
            native_end_clocks_[endpoint] = state_clock(node, clock_port);
            if ((critical_ || memory_physical) && native_end_clocks_[endpoint].empty() && op == Ntype_op::Memory) {
              // An asynchronous read port has no clock of its own: it is timed in the memory's clock domain.
              for (auto other : node.inp_sorted_pins()) {
                if (other.get_port_id() % Ntype::Memory_port_stride != 2) {
                  continue;
                }
                if (auto c = state_clock(node, other.get_port_id()); !c.empty()) {
                  native_end_clocks_[endpoint] = c;
                  break;
                }
              }
            }
            if (memory_physical && off != 2) {
              physical_memory_captures_[endpoint] = native_end_clocks_[endpoint];
            } else if (memory_physical) {
              physical_clock_endpoints_.insert(endpoint);  // carries clock-net RC only; never a data endpoint
            }
            if (critical_ && native_end_clocks_[endpoint].empty()) {
              livehd::diag::err("pass.opentimer", "critical-clock-unresolved", "io")
                  .msg("native {} '{}' pin {} has no resolvable clock net (clock port {})",
                       Ntype::get_name(op),
                       livehd::graph_util::node_name_of(node.base_node()),
                       name,
                       clock_port)
                  .fatal();
            }
          }
        }
      }
    }
  }

  // 5th: populate the combinational cells (Sub instances). Whole-design mode
  // iterates the flattened leaf set (forward_hier descends design modules;
  // Liberty-cell leaves are yielded via opaque_gids_), so gates from every
  // instance land in the single ot::Timer under their hier-unique names.
  for (auto node : nodes) {
    auto op = type_op_of(node);
    if (op == Ntype_op::AttrSet || Ntype::is_pin_trackable(op) || node.base_node().attr(livehd::attrs::native_comb_boundary).has()
        || op == Ntype_op::Flop || op == Ntype_op::Latch || op == Ntype_op::Memory || op == Ntype_op::Div || op == Ntype_op::Rem) {
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
    // A non-Liberty Sub that drives nothing is on no timing path. That
    // is exactly the shape of a property marker whose module declaration went
    // missing (pass.partition rebuilds the instance but does not clone the
    // `lgassert`/`fproperty` decl, so the Sub arrives here unbound and its type
    // name reads empty) -- and refusing the WHOLE design over an assertion is
    // the wrong answer either way. A characterized cell must still be inserted:
    // its input pins load upstream nets even if all its outputs are unused.
    if (node.out_edges().empty() && !is_liberty_cell(node)) {
      continue;
    }

    // A Sub that is not a Liberty cell. In whole-design mode a design module is
    // descended into by the hier walk (never yielded here); reaching this with
    // a body means a black-box the flatten cannot enter — skip it defensively
    // (its boundary nets simply stay unscored). ot::Timer::insert_gate would
    // otherwise log-and-skip, yielding silent garbage. The celllib is loaded.
    const auto& lib = timer.celllib(ot::MAX);
    if (!lib || lib->cell(type_name) == nullptr) {
      if (livehd::is_provisional_domino_cell(type_name)) {
        livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
            .msg("'{}' (instance {}) is a provisional domino cell with no Liberty timing model", type_name, instance_name)
            .hint("time domino designs with the provisional stage model (pass/domplace); run them with synth.opentimer=false")
            .fatal();
        return;
      }
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
    if (critical_) {
      for (const auto& [name, pin] : lib->cell(type_name)->cellpins) {
        (void)pin;
        timing_pin_nodes_[instance_name + ":" + name] = node.base_node();
      }
    }

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
    for (auto dpin : node.out_sorted_pins()) {
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
      const auto& pins = liberty_port(node, driver_pin_name_of(node, dpin));
      const auto  wire = driver_net_of(node, dpin);
      for (size_t bit = 0; bit < pins.size(); ++bit) {
        auto pin_name = absl::StrCat(instance_name, ":", pins[bit]);
        if (connected_pin.insert(pin_name).second) {
          timer.connect_pin(pin_name, bit == 0 ? wire : std::format("{}.{}", wire, bit));
        }
      }
    }

    // connect input pins. The Liberty pin list depends only on the SINK, so it is
    // resolved once per sink and the driver walk nests inside: inp_sorted_pins()
    // keeps the node-as-pin (port 0) operand the raw inp_pins() list would drop
    // and the ascending port order the edge walk had, and the PLURAL
    // get_driver_pins() still visits both drivers of a two-driver carry-in sink,
    // so the connect_pin sequence is the one the edge walk emitted.
    for (const auto& sink : node.inp_sorted_pins()) {
      const auto& pins = liberty_port(node, sink_pin_name_of(node, sink));
      for (const auto& driver : sink.get_driver_pins()) {
        const auto& values = pin_tracker.get_pin_vector(trk_id(driver));
        for (size_t bit = 0; bit < pins.size(); ++bit) {
          std::string wire{kZeroNet};
          if (!is_resolved_const(driver)) {
            if (pins.size() == 1) {
              wire = get_driver_net_name(driver);
            } else if (bit < values.size() && values[bit].pos >= 0) {
              wire = values[bit].pos == 0 ? values[bit].id() : std::format("{}.{}", values[bit].id(), values[bit].pos);
            } else if (values.empty() && bit < static_cast<size_t>(std::max(bits_of(driver), 1))) {
              const auto base = get_driver_net_name(driver);
              wire            = bit == 0 ? base : std::format("{}.{}", base, bit);
            }
          }
          timer.connect_pin(absl::StrCat(instance_name, ":", pins[bit]), wire);
          if (is_resolved_const(driver) && bit < 64 && const_of(driver).is_just_i64()) {
            timing_constant_pins_[absl::StrCat(instance_name, ":", pins[bit])]
                = (static_cast<uint64_t>(const_of(driver).to_just_i64()) >> bit) & 1U;
          }
        }
      }
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
      int32_t     bits   = 0;
      const auto  driver = pin.get_driver_pin();  // one driver per sink pin
      if (driver.is_invalid()) {
        continue;
      }
      const auto drv         = g->occurrences().lift(driver);
      const auto driver_bits = tracked_bits_of(drv);
      bits                   = d.bits ? static_cast<int32_t>(d.bits) : std::max(1, driver_bits);
      // A wiring node can redirect its driver after phase 2 named the output
      // net. Attach the PO to the resolved physical net, just like a gate
      // input. Each alias remains a separate load on their common producer.
      const auto& values     = pin_tracker.get_pin_vector(trk_id(drv));
      for (int32_t bit = 0; bit < std::max(bits, 1); ++bit) {
        std::string wire{kZeroNet};
        auto        source_bit = bit;
        if (driver_bits > 0 && bit >= driver_bits) {
          source_bit = livehd::graph_util::is_unsign(drv) ? -1 : driver_bits - 1;
        }
        if (!is_resolved_const(drv) && source_bit >= 0) {
          if (driver_bits == 1) {
            wire = get_driver_net_name(drv);
          } else if (static_cast<size_t>(source_bit) < values.size() && values[source_bit].pos >= 0) {
            wire = values[source_bit].pos == 0 ? values[source_bit].id()
                                               : std::format("{}.{}", values[source_bit].id(), values[source_bit].pos);
          } else if (values.empty()) {
            const auto base = get_driver_net_name(drv);
            wire            = source_bit == 0 ? base : std::format("{}.{}", base, source_bit);
          }
        }
        const auto output_pin = bit == 0 ? pname : std::format("{}.{}", pname, bit);
        if (wire != output_pin) {
          timer.disconnect_pin(output_pin);
          timer.connect_pin(output_pin, wire);
        }
      }
      set_output_delays(pname);
      for (auto i = 1; i < bits; ++i) {
        set_output_delays(absl::StrCat(pname, ".", str_tools::to_s(i)));
      }
    }
  }
}

// Certify one ideal virtual clock only when every real PI/PO transition and
// both early/late corners have explicit clock-relative delays. No state or
// physical clock model is inferred from an unclocked arrival graph.
std::string Pass_opentimer::clock_qor_json() {
  if (virtual_clock_period_ <= 0) {
    return {};
  }
  bool complete = sdc_clocks_ == 1 && native_state_nodes_ == 0 && opaque_logic_nodes_ == 0;
  for (const auto& [name, input] : timer.primary_inputs()) {
    (void)input;
    complete &= clock_inputs_[name] == 15;
  }
  float setup = std::numeric_limits<float>::infinity();
  float hold  = std::numeric_limits<float>::infinity();
  for (const auto& [name, output] : timer.primary_outputs()) {
    (void)output;
    complete &= clock_outputs_[name] == 15;
    for (const auto transition : {ot::RISE, ot::FALL}) {
      const auto late  = timer.report_slack(name, ot::MAX, transition);
      const auto early = timer.report_slack(name, ot::MIN, transition);
      if (!late || !early || !std::isfinite(*late) || !std::isfinite(*early)) {
        complete = false;
      } else {
        setup = std::min(setup, *late);
        hold  = std::min(hold, *early);
      }
    }
  }
  complete              &= std::isfinite(setup) && std::isfinite(hold);
  constraints_complete_ &= complete;
  std::string result
      = std::format(",\"clock_constraints\":{{\"model\":\"single_virtual_clock\",\"name\":\"{}\",\"period\":{:.9g},\"complete\":{}",
                    jesc(virtual_clock_name_),
                    virtual_clock_period_,
                    constraints_complete_);
  if (std::isfinite(setup) && std::isfinite(hold)) {
    result += std::format(",\"setup_slack\":{:.9g},\"hold_slack\":{:.9g}", setup, hold);
  }
  return result + "}";
}

double Pass_opentimer::endpoint_tns(ot::Split split) {
  update_timing_profiled();
  std::map<std::string, float> endpoints;
  const auto                   add = [&](const std::string& name, std::optional<float> slack) {
    if (!slack || !std::isfinite(*slack)) {
      return;
    }
    auto [it, inserted] = endpoints.emplace(name, *slack);
    if (!inserted) {
      it->second = std::min(it->second, *slack);
    }
  };
  for (const auto& [name, output] : timer.primary_outputs()) {
    for (auto tran : {ot::RISE, ot::FALL}) {
      add(name, output.slack(split, tran));
    }
  }
  for (const auto& test : timer.tests()) {
    for (auto tran : {ot::RISE, ot::FALL}) {
      add(test.constrained_pin().name(), test.slack(split, tran));
    }
  }
  // OpenSTA counts the worst check/transition once per endpoint. OpenTimer's
  // report_tns sums transitions and tests, which double-counts many endpoints.
  double total        = 0;
  negative_endpoints_ = "[";
  bool comma          = false;
  for (const auto& [name, slack] : endpoints) {
    total += std::min(0.0f, slack);
    if (slack < 0 && !feedback_) {
      if (comma) {
        negative_endpoints_ += ',';
      }
      negative_endpoints_ += std::format("[\"{}\",{:.9g}]", jesc(name), slack);
      comma                = true;
    }
  }
  negative_endpoints_ += ']';
  return total;
}

void Pass_opentimer::update_timing_profiled() {
  const auto start = std::chrono::steady_clock::now();
  timer.update_timing();
  timing_propagation_seconds_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  ++timing_update_calls_;
}

void Pass_opentimer::compute_timing(const std::shared_ptr<hhds::Graph>& g) {
  TRACE_EVENT("pass", "OPENTIMER_compute_timing");

  update_timing_profiled();

  // OpenTimer logs and skips a connect_pin whose pin/net is absent, but does
  // not return an error to its caller. Never publish QoR from such a partial
  // graph. A physical import can explicitly preserve open input terminals;
  // those are allowed only while the current graph also leaves them open.
  std::set<std::string> unconnected;
  for (const auto& [name, pin] : timer.pins()) {
    if (pin.gate() != nullptr && pin.is_input() && pin.net() == nullptr) {
      unconnected.insert(name);
    }
  }
  if (!unconnected.empty()) {
    for (const auto& node : leaf_nodes(g)) {
      const auto open = node.base_node().attr(livehd::attrs::physical_open_inputs);
      if (!open.has() || type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      const auto physical = physical_instances_.find(std::string(node.get_name()));
      const auto instance = physical != physical_instances_.end() ? physical->second : inst_of(node, hier_mode_);
      absl::flat_hash_set<std::string> connected;
      // Include the node-as-pin input at port zero, just as build_circuit does.
      for (const auto& pin : node.inp_sorted_pins()) {
        if (!pin.get_driver_pins().empty()) {
          connected.insert(sink_pin_name_of(node, pin));
        }
      }
      for (const auto& name : open.get()) {
        if (!connected.contains(name)) {
          unconnected.erase(instance + ":" + name);
        }
      }
    }
  }
  if (!unconnected.empty()) {
    std::string sample;
    size_t      examples = 0;
    for (const auto& name : unconnected) {
      sample += sample.empty() ? name : std::format(", {}", name);
      if (++examples == 4) {
        break;
      }
    }
    livehd::diag::err("pass.opentimer", "unconnected-pin", "internal")
        .msg("pass.opentimer built an incomplete timing graph: {} Liberty input pin(s) are unconnected (first: {})",
             unconnected.size(),
             sample)
        .fatal();
    return;
  }

  max_delay     = 0;
  auto& max_pin = max_pin_;  // member: the STA cache replays this summary line
  max_pin.clear();

  const auto& pins   = timer.pins();
  const bool  render = !qor_path.empty();

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
  // region_id -> its color_qor_ row. A linear find per cell was O(cells x
  // regions): 10.6M cells x 6.4k regions on xs Rob.
  absl::flat_hash_map<uint32_t, Color_qor*>               qor_row;
  if (stats_) {
    qor_row.reserve(color_qor_.size());
    for (auto& row : color_qor_) {
      qor_row.try_emplace(row.region_id, &row);  // first row wins, as the find did
    }
  }

  for (auto& node : leaf_nodes(g)) {
    auto op = type_op_of(node);
    if (op != Ntype_op::Sub) {
      continue;
    }

    const auto physical_name
        = physical_instances_.empty() ? physical_instances_.end() : physical_instances_.find(std::string(node.get_name()));
    auto instance_name = physical_name != physical_instances_.end() ? physical_name->second : inst_of(node, hier_mode_);
    if (render) {
      inst2node.emplace(instance_name, node);
    }

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
        if (auto it = qor_row.find(region_id); it != qor_row.end()) {
          color_row = it->second;
          ++color_row->cells;
        }
      }
    }

    for (auto dpin : node.out_sorted_pins()) {
      if (dpin.is_invalid() || dpin.out_edges().empty()) {
        continue;
      }
      auto pin_name = absl::StrCat(instance_name, ":", driver_pin_name_of(node, dpin));

      auto it = pins.find(pin_name);
      if (it == pins.end() || it->second.is_ideal_clock()) {
        del_delay(dpin);
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
        if (render) {
          arrivals.push_back({delay, pin_name, node});
        }

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

  if (physical_rc_) {
    // Include capture/primary-output wire delay in the placed timing report.
    // Looking only at gate outputs omits the last interconnect segment and
    // leaves a memory-only design with no reported arrival at all.
    for (const auto& [name, terminal] : timer.primary_outputs()) {
      (void)terminal;
      const auto it = pins.find(name);
      if (it == pins.end() || physical_clock_endpoints_.contains(name) || it->second.is_ideal_clock()) {
        continue;
      }
      float delay = -1;
      for (const auto tran : {ot::RISE, ot::FALL}) {
        if (const auto at = it->second.at(ot::MAX, tran)) {
          delay = std::max(delay, *at);
        }
      }
      if (delay < 0) {
        continue;
      }
      if (render) {
        arrivals.push_back({delay, name, {}});
      }
      if (delay > max_delay) {
        max_delay = delay;
        max_pin   = name;
        max_node  = {};
      }
    }
  }
  if (!max_pin.empty() && (!feedback_ || session_report_)) {
    if (margin) {
      margin_delay = (max_delay / 100.0) * (100 - margin);
      std::print("slowest delay:{} pin:{} margin:{}% (margin_delay:{})\n", max_delay, max_pin, margin, margin_delay);
    } else {
      std::print("slowest delay:{} pin:{} NO MARGIN selected\n", max_delay, max_pin);
    }
  }

  if (!render && !feedback_) {
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

  std::string j
      = render ? std::format("{{\"module\":\"{}\"", jesc(report_module_.empty() ? std::string{g->get_name()} : report_module_))
               : std::string{};
  bool        timing_cells_complete = true;
  std::string incomplete_cell;
  bool        area_cells_complete = true;
  bool        macro_area_missing  = false;  // an interface-only macro without an `area` attribute
  double      known_area          = 0;
  for (const auto corner : {ot::MIN, ot::MAX}) {
    const auto& celllib = timer.celllib(corner);
    for (const auto& [name, gate] : timer.gates()) {
      (void)name;
      const auto* cell = celllib ? celllib->cell(gate.cell_name()) : nullptr;
      if (!cell) {
        timing_cells_complete = false;
        area_cells_complete   = false;
        if (incomplete_cell.empty()) {
          incomplete_cell = gate.cell_name() + " (not in Liberty)";
        }
        continue;
      }
      const bool interface_only = std::none_of(cell->cellpins.begin(), cell->cellpins.end(), [](const auto& entry) {
        return !entry.second.timings.empty();
      });
      if (!cell->area && interface_only) {
        macro_area_missing = true;  // a placeholder macro states no area: it counts as 0 (see the sum below)
      } else if (!cell->area || !std::isfinite(*cell->area) || *cell->area < 0) {
        area_cells_complete = false;
      } else if (corner == ot::MIN) {
        known_area += *cell->area;
      }
      const bool has_inputs = std::any_of(cell->cellpins.begin(), cell->cellpins.end(), [](const auto& entry) {
        return entry.second.direction == ot::CellpinDirection::INPUT;
      });
      if (!has_inputs) {
        continue;  // a constant cell has no data arc
      }
      // An interface-only macro: a Liberty cell with pins and capacitances but not one timing group (a
      // hard-IP placeholder such as litepci's `pcie_us`, whose outputs only start paths). It has no arc to
      // lack, so it does not make the design's timing incomplete; a cell with SOME arcs still must have
      // every output covered.
      const bool any_arc = std::any_of(cell->cellpins.begin(), cell->cellpins.end(), [](const auto& entry) {
        return !entry.second.timings.empty();
      });
      if (!any_arc) {
        continue;
      }
      for (const auto& [pin_name, pin] : cell->cellpins) {
        (void)pin_name;
        if (virtual_clock_period_ > 0) {
          if (pin.is_clock.value_or(false)) {
            constraints_complete_ = false;
          }
          for (const auto& arc : pin.timings) {
            const auto type = arc.type.value_or(ot::TimingType::COMBINATIONAL);
            if (type != ot::TimingType::COMBINATIONAL && type != ot::TimingType::COMBINATIONAL_RISE
                && type != ot::TimingType::COMBINATIONAL_FALL) {
              constraints_complete_ = false;
            }
          }
        }
        if (pin.direction != ot::CellpinDirection::OUTPUT) {
          continue;
        }
        if (std::none_of(pin.timings.begin(), pin.timings.end(), [](const auto& arc) { return !arc.is_constraint(); })) {
          timing_cells_complete = false;
          incomplete_cell       = gate.cell_name() + "/" + pin_name + " (no data arc)";
        }
        for (const auto& arc : pin.timings) {
          // Directional arcs (combinational_rise/fall, async preset/clear) move the output one way:
          // their one table pair is complete. Pulse-width, period and tree-path records carry no delay.
          const auto type      = arc.type.value_or(ot::TimingType::COMBINATIONAL);
          const bool rise_pair = arc.cell_rise && arc.rise_transition;
          const bool fall_pair = arc.cell_fall && arc.fall_transition;
          const bool no_delay  = type == ot::TimingType::MIN_PULSE_WIDTH || type == ot::TimingType::MINIMUM_PERIOD
                                 || type == ot::TimingType::MAX_CLOCK_TREE_PATH || type == ot::TimingType::MIN_CLOCK_TREE_PATH;
          const bool tables    = no_delay                                                            ? true
                                 : type == ot::TimingType::COMBINATIONAL_RISE                        ? rise_pair
                                 : type == ot::TimingType::COMBINATIONAL_FALL                        ? fall_pair
                                 : (type == ot::TimingType::PRESET || type == ot::TimingType::CLEAR) ? (rise_pair || fall_pair)
                                                                                                     : (rise_pair && fall_pair);
          if (!arc.is_constraint() && !tables) {
            timing_cells_complete = false;
            incomplete_cell
                = gate.cell_name() + "/" + pin_name + " (arc from " + arc.related_pin + " lacks a delay/transition table)";
          }
        }
      }
    }
  }
  if (critical_ && !timing_cells_complete) {
    livehd::diag::err("pass.opentimer", "critical-coverage", "unsupported")
        .msg("critical annotations require complete cell timing tables in the supplied Liberty corners: {}", incomplete_cell)
        .fatal();
  }
  if (feedback_) {
    if (!timing_cells_complete || !area_cells_complete) {
      throw std::runtime_error("physical timing requires complete cell timing and area feedback (e.g. " + incomplete_cell + ")");
    }
    feedback_->delay = max_delay;
    feedback_->area  = known_area;
    if (const auto wns = timer.report_wns(ot::MAX); wns && std::isfinite(*wns)) {
      feedback_->wns = *wns;
    }
    feedback_->tns = endpoint_tns(ot::MAX);
    if (const auto hold = timer.report_wns(ot::MIN); hold && std::isfinite(*hold)) {
      feedback_->hold_wns = *hold;
    }
  }
  if (render) {
    j += clock_qor_json();
  }
  std::unordered_map<std::string, std::pair<unsigned, double>> electrical_pins;
  if (physical_rc_) {
    if (render) {
      j += std::format(
          ",\"case_analysis\":{{\"constant_nets\":{},\"disabled_arcs\":{},\"narrowed_arcs\":{},\"disabled_conditional_arcs\":{}}}",
          case_constants_,
          case_disabled_,
          case_narrowed_,
          case_conditions_);
      j += std::format(",\"parasitics\":{{\"model\":\"placement_flute\",\"identity\":\"{}\",\"approximate\":{},\"nets\":{}}}",
                       physical_rc_->identity,
                       physical_rc_->approximate,
                       physical_rc_->nets.size());
      if (const auto wns = timer.report_wns(ot::MAX); wns && std::isfinite(*wns)) {
        j += std::format(",\"wns\":{:.9g}", *wns);
      }
      j += std::format(",\"tns\":{:.12g},\"tns_model\":\"worst_per_endpoint\"", endpoint_tns(ot::MAX));
      j += ",\"negative_setup_endpoints\":" + negative_endpoints_;
      if (const auto hold = timer.report_wns(ot::MIN); hold && std::isfinite(*hold)) {
        j += std::format(",\"hold_wns\":{:.9g}", *hold);
      }
      if (!physical_native_pins_.empty()) {
        j += ",\"memory_timing_model\":\"clock_boundary_zero_internal_delay\"";
      }
    }
    // Normalize electrical violations into counts and a dimensionless excess.
    // The optimizer must not trade a new slew/cap/fanout violation for slack.
    unsigned violations = 0;
    double   excess     = 0;
    for (const auto& [name, pin] : timer.pins()) {
      if (pin.is_ideal_clock()) {
        continue;
      }
      for (const auto split : {ot::MIN, ot::MAX}) {
        const auto* cp = pin.cellpin(split);
        if (!cp) {
          continue;
        }
        const auto check = [&](double actual, const std::optional<float>& limit) {
          if (limit && *limit > 0 && actual > *limit * (1 + 1e-6)) {
            ++violations;
            excess       += actual / *limit - 1;
            auto& detail  = electrical_pins[name];
            ++detail.first;
            detail.second += actual / *limit - 1;
          }
        };
        for (const auto tran : {ot::RISE, ot::FALL}) {
          if (const auto slew = pin.slew(split, tran)) {
            check(*slew, cp->max_transition);
          }
          if (pin.is_output() && pin.net()) {
            if (const auto load = timer.report_load(pin.net()->name(), split, tran)) {
              check(*load, cp->max_capacitance);
            }
          }
        }
        if (pin.is_output() && pin.net()) {
          check((pin.net()->num_pins() - 1), cp->max_fanout);
        }
      }
    }
    if (feedback_) {
      feedback_->violations              = violations;
      feedback_->electrical_excess       = excess;
      feedback_->electrical_pin_feedback = true;
    }
    if (render) {
      j += std::format(",\"electrical\":{{\"violations\":{},\"excess\":{:.9g}}}", violations, excess);
      j += ",\"electrical_pin_feedback\":true";
    }
  }
  if (physical_rc_) {
    const double cap_scale = timer.capacitance_unit() ? timer.capacitance_unit()->value() / 1e-15 : 1;
    const double unit_ps   = timer.time_unit() ? timer.time_unit()->value() * 1e12 : 1000;
    if (feedback_) {
      feedback_->time_unit_ps = unit_ps;
    }
    if (render) {
      j += std::format(",\"time_unit_ps\":{},\"physical_pins\":[", unit_ps);
    }
    bool comma = false;
    for (const auto& [physical, name] : physical_timing_pins_) {
      const auto it = pins.find(name);
      if (it == pins.end()) {
        continue;
      }
      const auto&                   pin = it->second;
      livehd::timing::Feedback::Pin value;
      if (const auto electrical = electrical_pins.find(name); electrical != electrical_pins.end()) {
        value.violations        = electrical->second.first;
        value.electrical_excess = electrical->second.second;
      }
      const auto extreme = [&](auto getter, bool largest) {
        std::optional<float> best;
        for (const auto tran : {ot::RISE, ot::FALL}) {
          if (const auto v = getter(tran); v && std::isfinite(*v)) {
            best = best ? (largest ? std::max(*best, *v) : std::min(*best, *v)) : *v;
          }
        }
        return best;
      };
      value.arrival    = extreme([&](ot::Tran t) { return pin.at(ot::MAX, t); }, true);
      value.required   = extreme([&](ot::Tran t) { return pin.rat(ot::MAX, t); }, false);
      value.slack      = extreme([&](ot::Tran t) { return pin.slack(ot::MAX, t); }, false);
      value.hold_slack = extreme([&](ot::Tran t) { return pin.slack(ot::MIN, t); }, false);
      value.slew       = extreme([&](ot::Tran t) { return pin.slew(ot::MAX, t); }, true);
      if (pin.net() && (pin.is_output() || pin.primary_input())) {
        if (auto load = extreme([&](ot::Tran t) { return timer.report_load(pin.net()->name(), ot::MAX, t); }, true)) {
          value.load_ff = *load * cap_scale;
        }
      }
      const auto* cp = pin.cellpin(ot::MAX);
      if (cp && cp->capacitance && std::isfinite(*cp->capacitance)) {
        value.capacitance_ff = *cp->capacitance * cap_scale;
      }
      bool clock = pin.is_ideal_clock() || (cp && cp->is_clock.value_or(false));
      for (const auto& [clock_name, target] : timing_targets_) {
        (void)clock_name;
        clock |= std::find(target.ports.begin(), target.ports.end(), name) != target.ports.end();
      }
      if (const auto colon = physical.rfind(":in_"); colon != std::string::npos && physical_native_pins_.contains(physical)) {
        const auto pid  = std::stoul(physical.substr(colon + 4));
        clock          |= pid % Ntype::Memory_port_stride == 2;
      }
      value.clock = clock;
      if (feedback_) {
        feedback_->pins.emplace(physical, value);
      }
      if (render) {
        if (comma) {
          j += ",";
        }
        comma  = true;
        j     += std::format("{{\"pin\":\"{}\",\"timer_pin\":\"{}\"", jesc(physical), jesc(name));
        if (value.violations) {
          j += std::format(",\"violations\":{},\"electrical_excess\":{:.9g}", value.violations, value.electrical_excess);
        }
        const auto field = [&](const char* key, std::optional<double> n) {
          if (n) {
            j += std::format(",\"{}\":{:.9g}", key, *n);
          }
        };
        field("arrival", value.arrival);
        field("required", value.required);
        field("slack", value.slack);
        field("hold_slack", value.hold_slack);
        field("slew", value.slew);
        field("load_ff", value.load_ff);
        field("capacitance_ff", value.capacitance_ff);
        j += std::format(",\"clock\":{}}}", clock);
      }
    }
    if (render) {
      j += "]";
    }
  }
  if (feedback_) {
    // Only path pins need reverse-name lookup. Borrow names from the current
    // path and immutable physical map instead of copying the whole design.
    // The first physical spelling still wins when several aliases map to one
    // timer pin, matching the previous ordered-map emplace behavior.
    std::map<std::string_view, const std::string*> physical;
    for (const auto& point : path_pts) {
      physical.try_emplace(point.pin, nullptr);
    }
    size_t remaining = physical.size();
    for (const auto& [name, timer_name] : physical_timing_pins_) {
      if (!remaining) {
        break;
      }
      if (auto it = physical.find(timer_name); it != physical.end() && !it->second) {
        it->second = &name;
        --remaining;
      }
    }
    for (const auto& point : path_pts) {
      const auto* name = physical.at(point.pin);
      feedback_->path.push_back(name ? *name : point.pin);
    }
  }
  if (!render) {
    return;
  }
  if (const auto area = timer.report_area(); area_cells_complete && area && std::isfinite(*area) && *area >= 0) {
    j += std::format(",\"area\":{:.9g}", *area);
  } else if (area_cells_complete && macro_area_missing) {
    // OpenTimer refuses the whole sum when one cell has no area; the placeholder macros that state none
    // are constant across placement trials, so the other cells' area is the comparable total.
    j += std::format(",\"area\":{:.9g}", known_area);
  }
  j += std::format(
      ",\"cells\":{},\"opaque_logic_nodes\":{},\"native_state_nodes\":{},\"constraints_complete\":{},\"timing_cells_complete\":{}",
      timer.num_gates(),
      opaque_logic_nodes_,
      native_state_nodes_,
      constraints_complete_,
      timing_cells_complete);
  if (!max_pin.empty() || physical_rc_) {
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
  j += "]";
  // The design block ends here; the per-color `--stats` tail is rendered
  // separately so the STA reuse cache can store the block verbatim and re-stamp
  // each row's `resynth` (which describes what pass.abc did in THIS run, not
  // the netlist) on a hit.
  if (!annotation_summary_.empty()) {
    j += ",\"critical\":" + annotation_summary_;
  }
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
  const uint64_t env       = livehd::opentimer::Sta_cache::env_hash(timing_file_list, top_filter, hier_setting_, margin, stats_);
  uint64_t       cuts_hash = livehd::hash_util::kFnv1a64_offset;
  for (const auto& pin : physical_timing_cuts_) {
    cuts_hash = livehd::hash_util::fnv1a64_u64(pin.size(), cuts_hash);
    cuts_hash = livehd::hash_util::fnv1a64(pin, cuts_hash);
  }
  // The functional digest ignores physical open-input intent. A cached valid
  // import must not hide a missing-connection error after that intent changes.
  std::vector<std::tuple<std::string, uint64_t, std::string>> open_inputs;
  for (const auto& def : g->definitions().graphs()) {
    for (const auto node : def->body().nodes()) {
      if (const auto open = node.attr(livehd::attrs::physical_open_inputs); open.has()) {
        for (const auto& pin : open.get()) {
          open_inputs.emplace_back(def->get_name(), node.get_debug_nid(), pin);
        }
      }
    }
  }
  std::sort(open_inputs.begin(), open_inputs.end());
  uint64_t open_hash = livehd::hash_util::kFnv1a64_offset;
  for (const auto& [definition, node, pin] : open_inputs) {
    open_hash = livehd::hash_util::fnv1a64_u64(definition.size(), open_hash);
    open_hash = livehd::hash_util::fnv1a64(definition, open_hash);
    open_hash = livehd::hash_util::fnv1a64_u64(node, open_hash);
    open_hash = livehd::hash_util::fnv1a64_u64(pin.size(), open_hash);
    open_hash = livehd::hash_util::fnv1a64(pin, open_hash);
  }
  return std::format(
      "{:016x}{:016x}{:016x}:load={:.9g}:critical={}:percent={:.9g}:targets={}:nodes={}:physical={}:cuts={:016x}:open={:016x}",
      d.h0,
      d.h1,
      env,
      io_load_ff_,
      critical_,
      critical_percent_,
      targets_text_,
      annotation_identity(g),
      physical_rc_ ? physical_rc_->identity : std::string{},
      cuts_hash,
      open_hash);
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
  rec.time_unit          = time_unit_label_;
  rec.max_delay          = max_delay;
  rec.max_pin            = max_pin_;
  rec.block              = sta_block_;
  rec.annotations        = annotation_records_;
  rec.annotation_summary = annotation_summary_;
  rec.opaque_nodes       = opaque_logic_nodes_;
  rec.ambiguous_nodes    = ambiguous_or_nodes_;
  rec.opaque_examples    = opaque_logic_examples_;
  rec.or_examples        = ambiguous_or_examples_;
  for (const auto& row : color_qor_) {
    rec.colors.push_back({row.module, row.color, row.cells, row.max_arrival, row.critical_pin, row.critical_src});
  }
  return rec;
}

void Pass_opentimer::prepare_color_report(const std::shared_ptr<hhds::Graph>& g) {
  color_qor_.clear();
  if (stats_) {
    // pass.abc stamps the mapped-region identity on each region graph's input
    // node. Seed from definitions rather than timed gates so a zero-cell/native
    // wiring color still gets its required row. The later flattened gate walk
    // fills occurrence-weighted cell counts and end-to-end arrivals.
    for (const auto& def : g->definitions().graphs()) {
      auto input  = def->get_input_node();
      auto region = input.attr(livehd::attrs::synth_region);
      if (!region.has()) {
        continue;
      }
      Color_qor row;
      if (auto id = input.attr(livehd::attrs::synth_region_id); id.has()) {
        row.region_id = id.get();
      }
      row.module = std::string{region.get()};
      if (auto color = input.attr(livehd::attrs::color); color.has()) {
        row.color = color.get();
      }
      row.resynth = input.attr(livehd::attrs::resynth).has();
      color_qor_.push_back(std::move(row));
    }
    std::sort(color_qor_.begin(), color_qor_.end(), [](const auto& lhs, const auto& rhs) {
      return std::tie(lhs.module, lhs.color) < std::tie(rhs.module, rhs.color);
    });
  }
}

void Pass_opentimer::set_time_unit_label() {
  time_unit_label_.clear();
  // The Liberty time unit as a label, for the report and the cache record.
  if (auto u = timer.time_unit(); u) {
    auto         near = [](double a, double b) { return a > b * 0.999 && a < b * 1.001; };
    const double v    = u->value();
    if (near(v, 1e-6)) {
      time_unit_label_ = "us";
    } else if (near(v, 1e-9)) {
      time_unit_label_ = "ns";
    } else if (near(v, 1e-12)) {
      time_unit_label_ = "ps";
    }
  }
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
  j += std::format(",\"physical_session_reused\":{}", session_report_ && !cache_hit_);
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

  update_timing_profiled();

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
    for (auto dpin : node.out_sorted_pins()) {
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
    // PLURAL, and the `continue` stays in the INNER loop so it advances one
    // driver exactly as the old per-edge `continue` advanced one edge. This
    // picks the slowest input, so a dropped driver can pick the wrong critical
    // path.
    for (const auto& in_pin : node.inp_sorted_pins()) {
      for (const auto& in_drv : in_pin.get_driver_pins()) {
        if (!has_delay(in_drv)) {
          continue;
        }
        const auto delay = get_delay(in_drv);
        if (delay >= dpin_delay) {
          dpin       = in_drv;
          dpin_delay = delay;
        }
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

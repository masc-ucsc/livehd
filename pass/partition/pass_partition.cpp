// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_partition.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "color_common.hpp"
#include "diag.hpp"
#include "flatten.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"
#include "str_tools.hpp"

using namespace livehd::graph_util;  // type_op_of, node_color_of, is_const_pin, ...
using livehd::color::is_partitionable;
using livehd::color::NO_COLOR;

static Pass_plugin sample("pass_partition", Pass_partition::setup);

Pass_partition::Pass_partition(const Eprp_var& var) : Pass("pass.partition", var) {}

void Pass_partition::setup() {
  Eprp_method m("pass.partition", "Build a new graph_library from the active coloring (one module per region)", &Pass_partition::partition);
  // The top module is the shared kernel `--top` flag (lhd plumbs it into the
  // `top` label), not a per-pass --set option.
  m.add_label_optional("out", "output graph_library directory (the --emit-dir lg: slot)", "");
  m.add_label_optional("debug_color", "diagnose same-color region interface mismatches", "false");
  m.add_label_optional("flatten",
                       "auto|true|false whole-design flatten: inline the instance hierarchy and partition the flat "
                       "design as one def (auto = flatten exactly when the active coloring is `pass.color flat`); a "
                       "single resulting region is emitted directly under the top name — one output module",
                       "auto");
  register_pass(m);
}

namespace {

namespace gu = livehd::graph_util;

using livehd::color::Union_find;  // region = connected component of same color

std::string sanitize(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out.push_back((std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_');
  }
  if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) {
    out.insert(out.begin(), '_');
  }
  return out;
}

struct SinkRef {
  hhds::Node_class node;
  hhds::Port_id    pid = 0;
};
struct InputPort {
  std::string          name;
  hhds::Pin_class      driver;        // external driver pin (its net feeds this port)
  bool                 from_primary = false;
  std::string          primary_name;  // io name when from_primary
  std::vector<SinkRef> sinks;         // sinks inside the region fed by this port
};
struct OutputPort {
  std::string     name;
  hhds::Pin_class driver;  // internal driver pin exported by this port
};
struct IntEdge {
  hhds::Pin_class  driver;
  hhds::Node_class snode;
  hhds::Port_id    spid = 0;
};
struct ConstEdge {
  hhds::Pin_class  cdriver;
  hhds::Node_class snode;
  hhds::Port_id    spid = 0;
};
// How a new-top primary output is driven.
struct OutWire {
  std::string     oname;
  enum Kind { Region, Primary, Const } kind = Region;
  hhds::Pin_class driver;        // Region: the internal driver pin (look up out port)
  std::string     primary_name;  // Primary: source io name
  hhds::Pin_class cdriver;       // Const: the constant pin
};

class Partitioner {
public:
  Partitioner(hhds::Graph* g, hhds::GraphLibrary* outlib, std::string top, bool debug_color,
              livehd::partition::Body_builder hook = {}, bool flatten = false)
      : g_(g), outlib_(outlib), top_(std::move(top)), debug_color_(debug_color), hook_(std::move(hook)), flatten_(flatten) {}

  bool run();
  void report_stats();

private:
  hhds::Graph*                    g_;
  hhds::GraphLibrary*             outlib_;
  std::string                     top_;
  bool                            debug_color_;
  bool                            saw_uncolored_ = false;  // any color-0 node seen in collect()
  livehd::partition::Body_builder hook_;
  // Whole-design flatten mode: same-color regions merge even when structurally
  // disconnected (one region per color), and a single-region result is emitted
  // directly under `top_` (no wrapper) — see build_module_as_top.
  bool flatten_ = false;

  Union_find uf_;

  absl::flat_hash_map<hhds::Node_class, std::vector<hhds::Node_class>> region_nodes_;

  absl::flat_hash_map<hhds::Node_class, std::vector<InputPort>>                        module_inputs_;
  absl::flat_hash_map<hhds::Node_class, absl::flat_hash_map<hhds::Pin_class, size_t>>  in_index_;
  absl::flat_hash_map<hhds::Node_class, std::vector<OutputPort>>                       module_outputs_;
  absl::flat_hash_map<hhds::Pin_class, std::pair<hhds::Node_class, size_t>>            out_index_;
  absl::flat_hash_map<hhds::Node_class, std::vector<IntEdge>>                          internal_edges_;
  absl::flat_hash_map<hhds::Node_class, std::vector<ConstEdge>>                        const_edges_;
  std::vector<OutWire>                                                                top_outputs_;

  absl::flat_hash_map<hhds::Node_class, std::shared_ptr<hhds::GraphIO>> module_gio_;
  absl::flat_hash_map<hhds::Node_class, int>                            region_color_;

  [[nodiscard]] hhds::Node_class region_of(const hhds::Node_class& n) { return uf_.find(n); }

  void ensure_input_port(const hhds::Node_class& r, const hhds::Pin_class& driver, const SinkRef& sink, bool from_primary,
                         std::string_view pname) {
    auto& idx = in_index_[r];
    auto  it  = idx.find(driver);
    size_t i;
    if (it == idx.end()) {
      i = module_inputs_[r].size();
      module_inputs_[r].push_back(InputPort{std::string{}, driver, from_primary, std::string{pname}, {}});
      idx[driver] = i;
    } else {
      i = it->second;
    }
    module_inputs_[r][i].sinks.push_back(sink);
  }

  void ensure_output_port(const hhds::Pin_class& driver) {
    if (out_index_.contains(driver)) {
      return;
    }
    auto   rd = region_of(driver.get_master_node());
    size_t i  = module_outputs_[rd].size();
    module_outputs_[rd].push_back(OutputPort{std::string{}, driver});
    out_index_[driver] = {rd, i};
  }

  [[nodiscard]] std::string out_port_name(const hhds::Pin_class& driver) {
    auto it = out_index_.find(driver);
    return it == out_index_.end() ? std::string{} : module_outputs_[it->second.first][it->second.second].name;
  }

  bool collect();
  void name_ports();
  void diagnose_colors();
  void build_module(const hhds::Node_class& r);
  void build_module_as_top(const hhds::Node_class& r);
  void build_top();
  // Regions in a reproducible order: by color, then by the smallest member node
  // id (invariant to which member the union-find picks as representative).
  // `region_nodes_` / `module_gio_` are flat_hash_maps whose iteration order is
  // unspecified; creating the per-region modules, Sub instances and boundary
  // pins in that order made the serialized top lg (and downstream cgen/LEC)
  // nondeterministic. Iterate this instead wherever construction order matters.
  [[nodiscard]] std::vector<hhds::Node_class> ordered_regions();
  void carry_node_attrs(hhds::Graph* body, const hhds::Node_class& orig, const hhds::Node_class& neo);
  void carry_driver_attrs(const hhds::Pin_class& orig, const hhds::Pin_class& neo);
};

void Partitioner::carry_node_attrs(hhds::Graph* body, const hhds::Node_class& orig, const hhds::Node_class& neo) {
  if (gu::has_name(orig)) {
    neo.attr(hhds::attrs::name).set(std::string{gu::node_name_of(orig)});
  }
  if (auto a = orig.attr(livehd::attrs::lut); a.has()) {
    neo.attr(livehd::attrs::lut).set(std::string{a.get()});
  }
  if (auto a = orig.attr(hhds::attrs::srcid); a.has() && a.get() != 0) {
    auto newid = body->source_locator().import_from(g_->source_locator(), a.get());
    neo.attr(hhds::attrs::srcid).set(newid);
  }
}

void Partitioner::carry_driver_attrs(const hhds::Pin_class& orig, const hhds::Pin_class& neo) {
  if (auto b = gu::bits_of(orig); b != 0) {
    gu::set_bits(neo, b);
  }
  if (!gu::is_unsign(orig)) {
    gu::set_sign(neo);
  }
  auto pn = gu::pin_name_of(orig);
  if (!pn.empty()) {
    gu::set_pin_name(neo, pn);
  }
  if (auto o = orig.attr(livehd::attrs::pin_offset); o.has()) {
    neo.attr(livehd::attrs::pin_offset).set(o.get());
  }
}

bool Partitioner::collect() {
  auto gio = g_->get_io();

  // Regions = connected components of same-color partitionable nodes. Color 0
  // (NO_COLOR) means the node was never colored — either no `pass.color` ran, or
  // it ran and left this node out. Color 0 is treated as just another color:
  // uncolored nodes merge into their own color-0 region(s) exactly like any
  // colored set, so the decomposition (and `pass.abc`) runs on an uncolored
  // design without a prior color pass. We only warn once (below) so the implicit
  // coloring is visible without being fatal.
  // Whole-design flatten: one region per color, even for structurally
  // disconnected same-color cones (two cones linked only through a primary
  // input or a constant never union-merge below). The flat def must come out
  // as ONE module, and a same-color multi-module split would defeat it.
  absl::flat_hash_map<int, hhds::Node_class> color_anchor;

  for (auto n : g_->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    auto c = node_color_of(n);
    if (c == NO_COLOR) {
      saw_uncolored_ = true;
    }
    uf_.find(n);
    if (flatten_) {
      auto [it, inserted] = color_anchor.try_emplace(c, n);
      if (!inserted) {
        uf_.merge(n, it->second);
      }
    }
    for (const auto& e : n.out_edges()) {
      auto sn = e.sink.get_master_node();
      if (is_partitionable(sn) && node_color_of(sn) == c) {
        uf_.merge(n, sn);
      }
    }
  }
  if (saw_uncolored_) {
    // One warning per def (not per node): the dedup sink collapses repeats.
    livehd::diag::warn("pass.partition", "uncolored-node", "io")
        .msg("top '{}' has uncolored nodes (color 0); treating them as a single color-0 region — run pass.color "
             "first for an explicit partitioning",
             top_)
        .emit();
  }

  // Region membership + color.
  for (auto n : g_->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    auto r = region_of(n);
    region_nodes_[r].push_back(n);
    region_color_[r] = node_color_of(n);
  }

  // Classify every edge feeding a region node (via inp_edges).
  for (auto n : g_->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    auto r = region_of(n);
    for (const auto& e : n.inp_edges()) {
      auto dn  = e.driver.get_master_node();
      auto spid = e.sink.get_port_id();
      if (gu::is_const_pin(e.driver)) {
        const_edges_[r].push_back(ConstEdge{e.driver, n, spid});
      } else if (gu::is_graph_input_pin(e.driver)) {
        // pin_name_of resolves the graph input's declared port name directly.
        ensure_input_port(r, e.driver, SinkRef{n, spid}, /*from_primary=*/true, std::string{gu::pin_name_of(e.driver)});
      } else if (is_partitionable(dn)) {
        auto rd = region_of(dn);
        if (rd == r) {
          internal_edges_[r].push_back(IntEdge{e.driver, n, spid});
        } else {
          ensure_output_port(e.driver);
          ensure_input_port(r, e.driver, SinkRef{n, spid}, /*from_primary=*/false, std::string{});
        }
      }
      // else: unexpected builtin driver — skip.
    }
  }

  // Primary outputs.
  if (gio) {
    for (const auto& decl : gio->get_output_pin_decls()) {
      auto opin = g_->get_output_pin(decl.name);
      if (opin.is_invalid()) {
        continue;
      }
      for (const auto& e : opin.inp_edges()) {
        auto d  = e.driver;
        auto dn = d.get_master_node();
        if (gu::is_const_pin(d)) {
          top_outputs_.push_back(OutWire{decl.name, OutWire::Const, {}, {}, d});
        } else if (gu::is_graph_input_pin(d)) {
          top_outputs_.push_back(OutWire{decl.name, OutWire::Primary, {}, std::string{gu::pin_name_of(d)}, {}});
        } else if (is_partitionable(dn)) {
          ensure_output_port(d);
          top_outputs_.push_back(OutWire{decl.name, OutWire::Region, d, {}, {}});
        }
      }
    }
  }
  return true;
}

void Partitioner::name_ports() {
  // A boundary port becomes a wire in the region module; it must not collide
  // with a recreated internal node's name. The classic failure is a flop whose
  // q is a region output: the port would otherwise take the flop's own
  // wire_name and cgen would emit two same-named declarations (the output reg
  // and the internal flop reg) at different widths. Reserve every internal
  // recreated-node name per region so port naming routes around them.
  absl::flat_hash_map<hhds::Node_class, absl::flat_hash_set<std::string>> internal_names;
  for (auto& [r, nodes] : region_nodes_) {
    auto& set = internal_names[r];
    for (const auto& n : nodes) {
      // Reserve the name cgen will ACTUALLY emit for this node, which is its
      // user name OR — when unnamed — the synthetic `<type>_<nid>`
      // (default_instance_name). Reserving only user-named nodes let an unnamed
      // node's synthetic name (e.g. `mux_56`) collide with a same-named output
      // port; cgen then declares the wire twice at mismatched widths and emits
      // invalid Verilog. This only bit when a node and the port it drives land
      // in the same region — rare under a per-node coloring, but the norm for an
      // uncolored design that folds the whole graph into one color-0 region.
      set.insert(sanitize(gu::default_instance_name(n)));
    }
  }

  for (auto& [r, ports] : module_inputs_) {
    std::sort(ports.begin(), ports.end(), [](const InputPort& a, const InputPort& b) {
      if (a.driver.get_debug_nid() != b.driver.get_debug_nid()) {
        return a.driver.get_debug_nid() < b.driver.get_debug_nid();
      }
      return a.driver.get_port_id() < b.driver.get_port_id();
    });
    // re-index after sort
    auto& idx = in_index_[r];
    idx.clear();
    absl::flat_hash_set<std::string> used = internal_names[r];
    for (size_t i = 0; i < ports.size(); ++i) {
      ports[i].name = [&] {
        std::string base = ports[i].from_primary && !ports[i].primary_name.empty() ? ports[i].primary_name
                                                                                   : std::string{gu::wire_name(ports[i].driver)};
        base             = sanitize(base.empty() ? std::string{"in"} : base);
        std::string nm   = base;
        int         k    = 1;
        while (used.contains(nm)) {
          nm = base + "_" + std::to_string(k++);
        }
        used.insert(nm);
        return nm;
      }();
      idx[ports[i].driver] = i;
    }
  }
  for (auto& [r, ports] : module_outputs_) {
    std::sort(ports.begin(), ports.end(), [](const OutputPort& a, const OutputPort& b) {
      if (a.driver.get_debug_nid() != b.driver.get_debug_nid()) {
        return a.driver.get_debug_nid() < b.driver.get_debug_nid();
      }
      return a.driver.get_port_id() < b.driver.get_port_id();
    });
  }
  out_index_.clear();  // rebuilt below after the sort settles indices
  for (auto& [r, ports] : module_outputs_) {
    absl::flat_hash_set<std::string> used = internal_names[r];
    for (size_t i = 0; i < ports.size(); ++i) {
      std::string base = sanitize(std::string{gu::wire_name(ports[i].driver)});
      if (base.empty()) {
        base = "out";
      }
      // A region output driven by a stateful node (flop/memory) shares that
      // node's emitted reg wire name. cgen declares the output reg (from the IO
      // decl) AND the node's own reg under that one name, at mismatched
      // width/sign -> broken Verilog (it also skips the port=node assign when
      // the names match). Give the port a distinct name so cgen emits a clean
      // `port = node` assign, exactly like a normal module's flop-driven output.
      auto dop = gu::type_op_of(ports[i].driver.get_master_node());
      if (dop == Ntype_op::Flop || dop == Ntype_op::Memory) {
        base += "_o";
      }
      std::string nm = base;
      int         k  = 1;
      while (used.contains(nm)) {
        nm = base + "_" + std::to_string(k++);
      }
      used.insert(nm);
      ports[i].name      = nm;
      out_index_[ports[i].driver] = {r, i};
    }
  }
}

std::vector<hhds::Node_class> Partitioner::ordered_regions() {
  std::vector<hhds::Node_class> regs;
  regs.reserve(region_nodes_.size());
  for (auto& [r, nodes] : region_nodes_) {
    (void)nodes;
    regs.push_back(r);
  }
  auto min_nid = [&](const hhds::Node_class& r) {
    uint64_t m = std::numeric_limits<uint64_t>::max();
    for (const auto& n : region_nodes_[r]) {
      m = std::min<uint64_t>(m, n.get_debug_nid());
    }
    return m;
  };
  std::sort(regs.begin(), regs.end(), [&](const hhds::Node_class& a, const hhds::Node_class& b) {
    if (region_color_[a] != region_color_[b]) {
      return region_color_[a] < region_color_[b];
    }
    return min_nid(a) < min_nid(b);
  });
  return regs;
}

void Partitioner::build_module(const hhds::Node_class& r) {
  int         color = region_color_[r];
  std::string name  = std::format("{}__c{}", top_, color);
  // Disambiguate if this color has multiple regions.
  if (outlib_->find_io(name)) {
    int suffix = 1;
    std::string base = name;
    while (outlib_->find_io(name)) {
      name = std::format("{}_r{}", base, suffix++);
    }
  }

  auto gio = outlib_->create_io(name);
  module_gio_[r] = gio;

  hhds::Port_id pid = 1;
  for (auto& p : module_inputs_[r]) {
    gio->add_input(p.name, pid++);
    if (auto b = gu::bits_of(p.driver); b != 0) {
      gio->set_bits(p.name, static_cast<uint32_t>(b));
    }
    gio->set_unsign(p.name, gu::is_unsign(p.driver));
  }
  for (auto& p : module_outputs_[r]) {
    gio->add_output(p.name, pid++);
    if (auto b = gu::bits_of(p.driver); b != 0) {
      gio->set_bits(p.name, static_cast<uint32_t>(b));
    }
    gio->set_unsign(p.name, gu::is_unsign(p.driver));
  }

  auto body = gio->create_graph();

  // Stamp the per-pin `pin_signed` attribute on the materialized IO pins. The
  // GraphIO declared sign (`gio->set_unsign` above) is NOT auto-propagated to the
  // pin objects, and readers like pass.lec consult the per-pin attr
  // (`gu::is_unsign(pin)`), not the declaration — so a signed region boundary
  // (e.g. a signed shift/multiply operand crossing into a region) would otherwise
  // default to unsigned and be mis-encoded (an arithmetic `>>` read as logical, a
  // signed `*` read as unsigned). Mirrors the tolg front-end, which stamps the
  // per-pin sign right after materializing each port. No-op for unsigned ports
  // (the attr's absence IS unsigned).
  for (auto& p : module_inputs_[r]) {
    // The input port, inside the body, is a DRIVER (it sources the internal logic,
    // which reads its sign) — stamp it. The output port is a SINK; its sign is on
    // the GraphIO (set_unsign above) for the declaration, never read off the sink,
    // so we do NOT stamp it (signed is a driver-pin property; node_util asserts it).
    // Width too: tolg stamps `bits` on every materialized input pin and readers
    // (e.g. pass.abc's bit-blast re-consuming this module) size the port from the
    // PIN attr, not the decl — without it an 8-bit input reads as 1 bit.
    auto ip = body->get_input_pin(p.name);
    if (auto b = gu::bits_of(p.driver); b != 0) {
      gu::set_bits(ip, b);
    }
    gu::is_unsign(p.driver) ? gu::set_unsign(ip) : gu::set_sign(ip);
  }

  // Body-builder hook (task 2a-abc): hand the region interface + contents to the
  // caller, which fills the body (e.g. an ABC-mapped netlist) instead of the
  // original logic. The IO pins are already materialized on `body`.
  if (hook_) {
    livehd::partition::Region_body rb;
    rb.body        = body.get();
    rb.src         = g_;
    rb.color       = color;
    rb.module_name = name;
    for (auto& p : module_inputs_[r]) {
      rb.inputs.push_back({p.name, p.driver, gu::bits_of(p.driver), !gu::is_unsign(p.driver)});
    }
    for (auto& p : module_outputs_[r]) {
      rb.outputs.push_back({p.name, p.driver, gu::bits_of(p.driver), !gu::is_unsign(p.driver)});
    }
    rb.nodes = region_nodes_[r];
    hook_(rb);
    body->commit();
    return;
  }

  // Recreate region nodes.
  absl::flat_hash_map<hhds::Node_class, hhds::Node_class> node_map;
  for (const auto& n : region_nodes_[r]) {
    auto op  = gu::type_op_of(n);
    auto neo = gu::create_typed_node(*body, op);
    if (op == Ntype_op::Sub) {
      // Hierarchical input: re-link the instance to the partitioned child def
      // in the output library. Children are partitioned before their parents,
      // so a def WITH a body already exists in outlib_; a body-less black box
      // (a liberty cell in an already-mapped netlist) is cloned as an IO-only
      // decl. Without this the Sub is left dangling (its body wires float) and
      // the child def is lost.
      if (n.get_subnode_io()) {
        auto out_child = livehd::partition::resolve_or_clone_subdef(outlib_, n);
        if (out_child) {
          neo.set_subnode(out_child);
        } else {
          livehd::diag::err("pass.partition", "missing-subdef", "unsupported")
              .msg("sub-instance '{}' in '{}' references child def '{}' missing from the output library",
                   gu::debug_name(n),
                   top_,
                   std::string{n.get_subnode_io()->get_name()})
              .fatal();
        }
      }
    }
    node_map[n] = neo;
    carry_node_attrs(body.get(), n, neo);
  }

  // Track which Sub output ports get a real (edge-carrying) pin, so the
  // declared-outputs completion below does not clobber their carried attrs.
  absl::flat_hash_map<hhds::Node_class, absl::flat_hash_set<uint32_t>> sub_outs_made;

  auto driver_pin = [&](const hhds::Pin_class& orig) {
    auto master = orig.get_master_node();
    auto neo    = node_map[master].create_driver_pin(orig.get_port_id());
    carry_driver_attrs(orig, neo);
    if (gu::type_op_of(master) == Ntype_op::Sub) {
      sub_outs_made[master].insert(static_cast<uint32_t>(orig.get_port_id()));
    }
    return neo;
  };

  // Internal edges.
  for (const auto& e : internal_edges_[r]) {
    auto dp = driver_pin(e.driver);
    auto sp = node_map[e.snode].create_sink_pin(e.spid);
    dp.connect_sink(sp);
  }
  // Constant edges (recreated inside the module).
  for (const auto& e : const_edges_[r]) {
    auto cp = gu::create_const(*body, gu::hydrate_const(e.cdriver));
    auto sp = node_map[e.snode].create_sink_pin(e.spid);
    cp.connect_sink(sp);
  }
  // Boundary inputs.
  for (const auto& p : module_inputs_[r]) {
    auto ipin = body->get_input_pin(p.name);
    for (const auto& s : p.sinks) {
      auto sp = node_map[s.node].create_sink_pin(s.pid);
      ipin.connect_sink(sp);
    }
  }
  // Boundary outputs.
  for (const auto& p : module_outputs_[r]) {
    auto dp = driver_pin(p.driver);
    dp.connect_sink(body->get_output_pin(p.name));
  }

  // Sub instances: materialize the declared outputs that carried no edge
  // (edge-less driver pin), mirroring tolg. Readers probe every declared
  // output (cgen create_subs, LEC pairing) and hhds find_pin asserts on a
  // pin that was never created. Width/sign come from the child decl (the
  // source pin is edge-less too, so hhds cannot enumerate it); ports wired
  // above keep their carried attrs.
  for (const auto& n : region_nodes_[r]) {
    if (gu::type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    auto neo = node_map[n];
    auto sio = neo.get_subnode_io();
    if (!sio) {
      continue;
    }
    auto& made = sub_outs_made[n];
    for (const auto& d : sio->get_output_pin_decls()) {
      if (made.contains(static_cast<uint32_t>(d.port_id))) {
        continue;
      }
      auto np = neo.create_driver_pin(d.port_id);
      if (d.bits != 0) {
        gu::set_bits(np, static_cast<int>(d.bits));
      }
      // No sign stamp: decl.unsign==false also means "unspecified", and an
      // edge-less pin has no reader — leave the attr absent (unsigned default).
    }
  }

  body->commit();
}

// Whole-design flatten, single region: the region IS the design. Emit it
// directly under the top's own name with the top's own port list (no wrapper,
// no __c suffix) — `pass.color flat` + `pass.abc` then yields exactly one
// output module. Ports keep the primary IO names (decls cloned verbatim), so
// the result is a drop-in replacement for the original def; name_ports()'s
// collision renaming is bypassed on purpose (internal child nodes are
// instance-path-prefixed by the flattener and cannot collide with port names).
void Partitioner::build_module_as_top(const hhds::Node_class& r) {
  auto src_gio = g_->get_io();
  auto gio     = outlib_->create_io(top_);
  module_gio_[r] = gio;
  for (const auto& decl : src_gio->get_input_pin_decls()) {
    gio->add_input(decl.name, decl.port_id, decl.loop_break);
    if (decl.bits != 0) {
      gio->set_bits(decl.name, decl.bits);
    }
    gio->set_unsign(decl.name, decl.unsign);
  }
  for (const auto& decl : src_gio->get_output_pin_decls()) {
    gio->add_output(decl.name, decl.port_id, decl.loop_break);
    if (decl.bits != 0) {
      gio->set_bits(decl.name, decl.bits);
    }
    gio->set_unsign(decl.name, decl.unsign);
  }
  auto body = gio->create_graph();

  // Stamp bits/sign on the materialized input pins (same invariant as
  // build_module/build_top: the decl is not auto-propagated to pin attrs and
  // every reader sizes from the pin).
  for (const auto& decl : src_gio->get_input_pin_decls()) {
    auto ip = body->get_input_pin(decl.name);
    if (decl.bits != 0) {
      gu::set_bits(ip, static_cast<int>(decl.bits));
    }
    decl.unsign ? gu::set_unsign(ip) : gu::set_sign(ip);
  }

  // With one region there is no other region to drive a boundary port, so
  // every input port must come from a primary input.
  for (const auto& p : module_inputs_[r]) {
    if (!p.from_primary) {
      livehd::diag::err("pass.partition", "flatten-input", "internal")
          .msg("flatten: single-region input port unexpectedly driven by a non-primary pin in '{}'", top_)
          .fatal();
      return;
    }
  }

  if (hook_) {
    livehd::partition::Region_body rb;
    rb.body        = body.get();
    rb.src         = g_;
    rb.color       = region_color_[r];
    rb.module_name = top_;
    for (const auto& p : module_inputs_[r]) {
      rb.inputs.push_back({p.primary_name, p.driver, gu::bits_of(p.driver), !gu::is_unsign(p.driver)});
    }
    for (const auto& ow : top_outputs_) {
      if (ow.kind == OutWire::Region) {
        rb.outputs.push_back({ow.oname, ow.driver, gu::bits_of(ow.driver), !gu::is_unsign(ow.driver)});
      }
    }
    rb.nodes = region_nodes_[r];
    hook_(rb);
  } else {
    // Recreate the region logic (the LEC-equivalent twin), boundary-wired to
    // the primary ports instead of region ports.
    absl::flat_hash_map<hhds::Node_class, hhds::Node_class> node_map;
    for (const auto& n : region_nodes_[r]) {
      auto op  = gu::type_op_of(n);
      auto neo = gu::create_typed_node(*body, op);
      if (op == Ntype_op::Sub && n.get_subnode_io()) {
        auto out_child = livehd::partition::resolve_or_clone_subdef(outlib_, n);
        if (out_child) {
          neo.set_subnode(out_child);
        } else {
          livehd::diag::err("pass.partition", "missing-subdef", "unsupported")
              .msg("sub-instance '{}' in '{}' references child def '{}' missing from the output library",
                   gu::debug_name(n),
                   top_,
                   std::string{n.get_subnode_io()->get_name()})
              .fatal();
        }
      }
      node_map[n] = neo;
      carry_node_attrs(body.get(), n, neo);
    }

    auto driver_pin = [&](const hhds::Pin_class& orig) {
      auto neo = node_map[orig.get_master_node()].create_driver_pin(orig.get_port_id());
      carry_driver_attrs(orig, neo);
      return neo;
    };

    for (const auto& e : internal_edges_[r]) {
      driver_pin(e.driver).connect_sink(node_map[e.snode].create_sink_pin(e.spid));
    }
    for (const auto& e : const_edges_[r]) {
      gu::create_const(*body, gu::hydrate_const(e.cdriver)).connect_sink(node_map[e.snode].create_sink_pin(e.spid));
    }
    for (const auto& p : module_inputs_[r]) {
      auto ipin = body->get_input_pin(p.primary_name);
      for (const auto& s : p.sinks) {
        ipin.connect_sink(node_map[s.node].create_sink_pin(s.pid));
      }
    }
    for (const auto& ow : top_outputs_) {
      if (ow.kind == OutWire::Region) {
        driver_pin(ow.driver).connect_sink(body->get_output_pin(ow.oname));
      }
    }

    // Materialize declared-but-unwired Sub outputs (same reader invariant as
    // build_module; "has an edge" is exact here because every created driver
    // pin above is immediately connected).
    for (const auto& n : region_nodes_[r]) {
      if (gu::type_op_of(n) != Ntype_op::Sub) {
        continue;
      }
      auto neo = node_map[n];
      auto sio = neo.get_subnode_io();
      if (!sio) {
        continue;
      }
      absl::flat_hash_set<uint32_t> made;
      for (const auto& e : neo.out_edges()) {
        made.insert(static_cast<uint32_t>(e.driver.get_port_id()));
      }
      for (const auto& d : sio->get_output_pin_decls()) {
        if (made.contains(static_cast<uint32_t>(d.port_id))) {
          continue;
        }
        auto np = neo.create_driver_pin(d.port_id);
        if (d.bits != 0) {
          gu::set_bits(np, static_cast<int>(d.bits));
        }
      }
    }
  }

  // Primary-driven and constant-driven outputs bypass the region in both the
  // hook and twin shapes (the wrapper used to wire these; there is no wrapper).
  for (const auto& ow : top_outputs_) {
    if (ow.kind == OutWire::Const) {
      gu::create_const(*body, gu::hydrate_const(ow.cdriver)).connect_sink(body->get_output_pin(ow.oname));
    } else if (ow.kind == OutWire::Primary) {
      body->get_input_pin(ow.primary_name).connect_sink(body->get_output_pin(ow.oname));
    }
  }

  body->commit();
}

void Partitioner::build_top() {
  auto src_gio = g_->get_io();
  auto tgio    = outlib_->create_io(top_);
  for (const auto& decl : src_gio->get_input_pin_decls()) {
    tgio->add_input(decl.name, decl.port_id, decl.loop_break);
    if (decl.bits != 0) {
      tgio->set_bits(decl.name, decl.bits);
    }
    tgio->set_unsign(decl.name, decl.unsign);
  }
  for (const auto& decl : src_gio->get_output_pin_decls()) {
    tgio->add_output(decl.name, decl.port_id, decl.loop_break);
    if (decl.bits != 0) {
      tgio->set_bits(decl.name, decl.bits);
    }
    tgio->set_unsign(decl.name, decl.unsign);
  }
  auto t = tgio->create_graph();

  // Stamp bits/sign on the materialized top input pins, mirroring tolg: the
  // GraphIO decl is not auto-propagated to the pin attrs, and readers that
  // re-consume this wrapper (pass.abc re-mapping a netlist, cgen) size a port
  // from the PIN attr — without it an 8-bit input reads as 1 bit.
  for (const auto& decl : src_gio->get_input_pin_decls()) {
    auto ip = t->get_input_pin(decl.name);
    if (decl.bits != 0) {
      gu::set_bits(ip, static_cast<int>(decl.bits));
    }
    decl.unsign ? gu::set_unsign(ip) : gu::set_sign(ip);
  }

  // One Sub instance per region.
  absl::flat_hash_map<hhds::Node_class, hhds::Node_class>                     sub_of;
  absl::flat_hash_map<hhds::Node_class, absl::flat_hash_map<std::string, hhds::Pin_class>> sub_out_pin;
  for (const auto& r : ordered_regions()) {
    auto gio = module_gio_[r];
    auto sub = gu::create_typed_node(*t, Ntype_op::Sub);
    sub.set_subnode(gio);
    sub.attr(hhds::attrs::name).set(std::format("u_{}", std::string{gio->get_name()}));
    sub_of[r] = sub;
  }

  // Sub output pin for the net carried by original driver `d` (idempotent per
  // instance+port). Stamp bits/sign so cgen declares the top-level wire at the
  // right width (a Sub output that fans straight into other Subs otherwise
  // defaults to 1 bit).
  auto get_sub_out = [&](const hhds::Pin_class& d) {
    auto  rd    = region_of(d.get_master_node());
    auto  pname = out_port_name(d);
    auto& m     = sub_out_pin[rd];
    auto  it    = m.find(pname);
    if (it != m.end()) {
      return it->second;
    }
    auto p = sub_of[rd].create_driver_pin(pname);
    if (auto b = gu::bits_of(d); b != 0) {
      gu::set_bits(p, b);
    }
    if (!gu::is_unsign(d)) {
      gu::set_sign(p);
    }
    m[pname] = p;
    return p;
  };

  // Wire each module's input ports.
  for (const auto& r : ordered_regions()) {
    auto it = module_inputs_.find(r);
    if (it == module_inputs_.end()) {
      continue;
    }
    for (const auto& p : it->second) {
      // The sub's GraphIO input port already carries the width+sign (create_io
      // above); cgen declares the port from there and sizes the connecting wire
      // from the driver. `bits`/`signed` are driver-pin properties, so do NOT
      // stamp this boundary SINK (node_util.hpp set_bits asserts driver-only).
      auto spin = sub_of[r].create_sink_pin(p.name);
      if (p.from_primary) {
        t->get_input_pin(p.primary_name).connect_sink(spin);
      } else {
        get_sub_out(p.driver).connect_sink(spin);
      }
    }
  }

  // Wire the new top's primary outputs.
  for (const auto& ow : top_outputs_) {
    auto out_sink = t->get_output_pin(ow.oname);
    if (ow.kind == OutWire::Region) {
      get_sub_out(ow.driver).connect_sink(out_sink);
    } else if (ow.kind == OutWire::Primary) {
      t->get_input_pin(ow.primary_name).connect_sink(out_sink);
    } else {  // Const
      gu::create_const(*t, gu::hydrate_const(ow.cdriver)).connect_sink(out_sink);
    }
  }

  t->commit();
}

// Group regions by color and report the per-region interface signature so a
// same-color interface divergence (almost always an upstream color-pass bug)
// can be pinpointed. Module-per-region keeps the rebuild correct regardless,
// but a mismatch means the color pass violated "same id => identical region".
void Partitioner::diagnose_colors() {
  absl::flat_hash_map<int, std::vector<hhds::Node_class>> by_color;
  for (auto& [r, nodes] : region_nodes_) {
    (void)nodes;
    by_color[region_color_[r]].push_back(r);
  }
  auto sig = [&](const hhds::Node_class& r) {
    std::vector<int> in_w;
    std::vector<int> out_w;
    if (module_inputs_.contains(r)) {
      for (auto& p : module_inputs_[r]) {
        in_w.push_back(gu::bits_of(p.driver));
      }
    }
    if (module_outputs_.contains(r)) {
      for (auto& p : module_outputs_[r]) {
        out_w.push_back(gu::bits_of(p.driver));
      }
    }
    std::sort(in_w.begin(), in_w.end());
    std::sort(out_w.begin(), out_w.end());
    std::string s = "in[";
    for (int w : in_w) {
      s += std::to_string(w) + ",";
    }
    s += "] out[";
    for (int w : out_w) {
      s += std::to_string(w) + ",";
    }
    s += "]";
    return s;
  };
  for (auto& [color, regions] : by_color) {
    if (regions.size() < 2) {
      continue;
    }
    std::string first = sig(regions.front());
    for (size_t i = 1; i < regions.size(); ++i) {
      std::string s = sig(regions[i]);
      if (s != first) {
        std::print("[partition.debug_color] color {} region interface mismatch: '{}' vs '{}' "
                   "(node {} vs {}) -- the color pass produced non-identical same-id regions\n",
                   color,
                   first,
                   s,
                   gu::debug_name(regions.front()),
                   gu::debug_name(regions[i]));
      }
    }
  }
}

bool Partitioner::run() {
  if (!collect()) {
    return false;
  }
  name_ports();
  if (debug_color_) {
    diagnose_colors();
  }
  auto regs = ordered_regions();
  // Whole-design flatten with a single region (the `pass.color flat` case):
  // the region IS the design — emit it directly under the top's own name and
  // port list, no wrapper. Multi-color flatten keeps the wrapper+regions shape
  // (regions just span the former hierarchy).
  if (flatten_ && regs.size() == 1) {
    build_module_as_top(regs.front());
    return true;
  }
  for (const auto& r : regs) {
    build_module(r);
  }
  build_top();
  return true;
}

void Partitioner::report_stats() {
  if (!collect()) {
    return;
  }
  name_ports();
  size_t total_ports = 0;
  std::print("pass.partition stats for top '{}':\n", top_);
  std::print("  regions: {}\n", region_nodes_.size());
  for (auto& [r, nodes] : region_nodes_) {
    size_t in  = module_inputs_.contains(r) ? module_inputs_[r].size() : 0;
    size_t out = module_outputs_.contains(r) ? module_outputs_[r].size() : 0;
    total_ports += in + out;
    std::print("  region color={} nodes={} in_ports={} out_ports={}\n", region_color_[r], nodes.size(), in, out);
  }
  std::print("  total boundary ports: {}\n", total_ports);
}

// Resolve the top graph from `graphs` and compute the reachable defs in
// children-before-parents (post-order) order, so each parent's Sub instances can
// re-link to an already-partitioned child def in the output library. A flat
// single-module top yields just itself. Returns the top graph (nullptr if not
// found); `top` is filled in when it was empty.
hhds::Graph* resolve_order(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string& top,
                           std::vector<hhds::Graph*>& order) {
  hhds::Graph*                                 g = nullptr;
  absl::flat_hash_map<hhds::Gid, hhds::Graph*> gid2graph;
  for (const auto& sp : graphs) {
    if (!sp) {
      continue;
    }
    gid2graph[sp->get_gid()] = sp.get();
    if (g == nullptr && (top.empty() || sp->get_name() == top)) {
      g = sp.get();
      if (top.empty()) {
        top = std::string{sp->get_name()};
      }
    }
  }
  if (g == nullptr) {
    return nullptr;
  }
  absl::flat_hash_set<hhds::Gid>    seen;
  std::function<void(hhds::Graph*)> dfs = [&](hhds::Graph* gg) {
    if (gg == nullptr || !seen.insert(gg->get_gid()).second) {
      return;
    }
    for (auto n : gg->forward_class()) {
      if (gu::is_type_sub(n)) {
        auto it = gid2graph.find(n.get_subnode_gid());
        if (it != gid2graph.end()) {
          dfs(it->second);
        }
      }
    }
    order.push_back(gg);
  };
  dfs(g);
  return g;
}

// Resolve Flatten_mode::automatic against g's active coloring: a
// `pass.color flat` coloring means whole-design synthesis. Same substring
// probe as color_common's has_seeded_coloring — the blob is machine-written
// by build_coloring_info_json.
bool flatten_resolved(hhds::Graph* g, livehd::partition::Flatten_mode mode) {
  if (mode == livehd::partition::Flatten_mode::on) {
    return true;
  }
  if (mode == livehd::partition::Flatten_mode::off) {
    return false;
  }
  if (auto a = g->get_input_node().attr(livehd::attrs::coloring_info); a.has()) {
    return std::string_view{a.get()}.find("\"algorithm\":\"flat\"") != std::string_view::npos;
  }
  return false;
}

}  // namespace

namespace livehd::partition {

std::shared_ptr<hhds::GraphIO> resolve_or_clone_subdef(hhds::GraphLibrary* outlib, const hhds::Node_class& inst) {
  auto child = inst.get_subnode_io();
  if (!child) {
    return nullptr;
  }
  if (auto out_child = outlib->find_io(child->get_name())) {
    return out_child;
  }
  if (inst.get_subnode_graph() != nullptr) {
    return nullptr;  // has a body: children-first ordering should have partitioned it already
  }
  // Body-less black-box def: clone the IO decl so the instance stays opaque.
  auto io = outlib->create_io(std::string{child->get_name()});
  for (const auto& d : child->get_input_pin_decls()) {
    io->add_input(d.name, d.port_id, d.loop_break);
    if (d.bits != 0) {
      io->set_bits(d.name, d.bits);
    }
    io->set_unsign(d.name, d.unsign);
  }
  for (const auto& d : child->get_output_pin_decls()) {
    io->add_output(d.name, d.port_id, d.loop_break);
    if (d.bits != 0) {
      io->set_bits(d.name, d.bits);
    }
    io->set_unsign(d.name, d.unsign);
  }
  return io;
}

}  // namespace livehd::partition

bool Pass_partition::build_decomposition(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, hhds::GraphLibrary* outlib,
                                         std::string_view top_in, bool debug_color,
                                         const livehd::partition::Body_builder& hook,
                                         livehd::partition::Flatten_mode        flatten) {
  std::string               top{top_in};
  std::vector<hhds::Graph*> order;
  auto*                     g = resolve_order(graphs, top, order);
  if (g == nullptr) {
    livehd::diag::err("pass.partition", "no-top", "unsupported")
        .msg("partition: top module '{}' not found in the input library", top)
        .fatal();
    return false;
  }

  if (flatten_resolved(g, flatten)) {
    // Inline the hierarchy into a scratch def in the output library, run ONE
    // Partitioner on it (top's own name), then drop the scratch def — it must
    // never persist or be emitted. A hierarchy-less top skips the clone.
    hhds::Graph*                 flat_src = g;
    std::shared_ptr<hhds::Graph> flat_holder;
    std::string                  flat_name;
    if (order.size() > 1) {
      flat_name   = top + "__flatten_tmp";
      flat_holder = livehd::partition::flatten_hierarchy(g, outlib, flat_name);
      if (!flat_holder) {
        return false;  // diag already emitted
      }
      flat_src = flat_holder.get();
    }
    Partitioner p(flat_src, outlib, top, debug_color, hook, /*flatten=*/true);
    bool        ok = p.run();
    if (flat_holder) {
      outlib->delete_graph(flat_holder);
      outlib->delete_graphio(flat_name);
    }
    return ok;
  }

  for (auto* def : order) {
    Partitioner p(def, outlib, std::string{def->get_name()}, debug_color, hook);
    if (!p.run()) {
      return false;
    }
  }
  return true;
}

livehd::partition::Flatten_mode livehd::partition::parse_flatten_mode(std::string_view v, std::string_view pass) {
  if (v == "auto" || v.empty()) {
    return Flatten_mode::automatic;
  }
  if (v == "true" || v == "1") {
    return Flatten_mode::on;
  }
  if (v == "false" || v == "0") {
    return Flatten_mode::off;
  }
  livehd::diag::err(pass, "bad-flatten", "io").msg("{}: flatten must be auto|true|false, got '{}'", pass, v).fatal();
  return Flatten_mode::off;
}

void Pass_partition::partition(Eprp_var& var) {
  auto top = std::string{var.get("top", "")};
  auto out = std::string{var.get("out", "")};
  bool dbg = var.get("debug_color", "false") != "false" && var.get("debug_color", "false") != "0";
  auto flatten = livehd::partition::parse_flatten_mode(var.get("flatten", "auto"), "pass.partition");

  if (out.empty()) {
    // Stats-only mode: count colors/regions/ports per reachable def without
    // building an output library. Flatten applies here too — the reported
    // decomposition must match what an emit run would build.
    std::string               t = top;
    std::vector<hhds::Graph*> order;
    auto*                     g = resolve_order(var.graphs, t, order);
    if (g == nullptr) {
      livehd::diag::err("pass.partition", "no-top", "unsupported")
          .msg("partition: top module '{}' not found in the input library", t)
          .fatal();
      return;
    }
    if (flatten_resolved(g, flatten)) {
      auto* lib = g->get_io() ? g->get_io()->get_library() : nullptr;
      hhds::Graph*                 flat_src = g;
      std::shared_ptr<hhds::Graph> flat_holder;
      std::string                  flat_name;
      if (order.size() > 1 && lib != nullptr) {
        // Scratch def in the graph's OWN library: stats mode never saves it,
        // and it is deleted right after (same lifecycle as the emit path).
        flat_name   = t + "__flatten_tmp";
        flat_holder = livehd::partition::flatten_hierarchy(g, lib, flat_name);
        if (!flat_holder) {
          return;  // diag already emitted
        }
        flat_src = flat_holder.get();
      }
      Partitioner p(flat_src, nullptr, t, dbg, {}, /*flatten=*/true);
      p.report_stats();
      if (flat_holder) {
        lib->delete_graph(flat_holder);
        flat_holder = nullptr;
        lib->delete_graphio(flat_name);
      }
      return;
    }
    for (auto* def : order) {
      Partitioner p(def, nullptr, std::string{def->get_name()}, dbg);
      p.report_stats();
    }
    return;
  }

  auto& outlib = livehd::Hhds_graph_library::instance(out);
  build_decomposition(var.graphs, &outlib, top, dbg, {}, flatten);
}

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_partition.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "color_common.hpp"
#include "diag.hpp"
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
  m.add_label_optional("top", "top module whose coloring is partitioned", "");
  m.add_label_optional("out", "output graph_library directory (the --emit-dir lg: slot)", "");
  m.add_label_optional("debug_color", "diagnose same-color region interface mismatches", "false");
  register_pass(m);
}

namespace {

namespace gu = livehd::graph_util;

// Union-find over node identities (region = connected component of same color).
class Union_find {
public:
  hhds::Node_class find(const hhds::Node_class& n) {
    auto it = parent_.find(n);
    if (it == parent_.end()) {
      parent_[n] = n;
      return n;
    }
    if (it->second == n) {
      return n;
    }
    auto root  = find(it->second);
    parent_[n] = root;
    return root;
  }
  void merge(const hhds::Node_class& a, const hhds::Node_class& b) {
    auto ra = find(a);
    auto rb = find(b);
    if (ra != rb) {
      parent_[ra] = rb;
    }
  }

private:
  absl::flat_hash_map<hhds::Node_class, hhds::Node_class> parent_;
};

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
  Partitioner(hhds::Graph* g, hhds::GraphLibrary* outlib, std::string top, bool debug_color)
      : g_(g), outlib_(outlib), top_(std::move(top)), debug_color_(debug_color) {}

  bool run();
  void report_stats();

private:
  hhds::Graph*        g_;
  hhds::GraphLibrary* outlib_;
  std::string         top_;
  bool                debug_color_;

  Union_find uf_;

  absl::flat_hash_map<hhds::Pin_class, std::string> pin_in_name_;   // primary input pin -> io name
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
  void build_top();
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
  // Primary-input pin -> io name.
  auto gio = g_->get_io();
  if (gio) {
    for (const auto& decl : gio->get_input_pin_decls()) {
      auto pin = g_->get_input_pin(decl.name);
      if (!pin.is_invalid()) {
        pin_in_name_[pin] = decl.name;
      }
    }
  }

  // Regions = connected components of same-color partitionable nodes.
  for (auto n : g_->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    auto c = node_color_of(n);
    if (c == NO_COLOR) {
      livehd::diag::err("pass.partition", "uncolored-node", "unsupported")
          .msg("node {} in top '{}' has color 0; run pass.color first (the coloring must be complete and nonzero)",
               gu::debug_name(n),
               top_)
          .fatal();
      return false;
    }
    uf_.find(n);
    for (const auto& e : n.out_edges()) {
      auto sn = e.sink.get_master_node();
      if (is_partitionable(sn) && node_color_of(sn) == c) {
        uf_.merge(n, sn);
      }
    }
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
        std::string ioname = pin_in_name_.contains(e.driver) ? pin_in_name_[e.driver] : std::string{};
        ensure_input_port(r, e.driver, SinkRef{n, spid}, /*from_primary=*/true, ioname);
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
          std::string ioname = pin_in_name_.contains(d) ? pin_in_name_[d] : std::string{};
          top_outputs_.push_back(OutWire{decl.name, OutWire::Primary, {}, ioname, {}});
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
    absl::flat_hash_set<std::string> used;
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
    absl::flat_hash_set<std::string> used;
    for (size_t i = 0; i < ports.size(); ++i) {
      std::string base = sanitize(std::string{gu::wire_name(ports[i].driver)});
      if (base.empty()) {
        base = "out";
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

  // Recreate region nodes.
  absl::flat_hash_map<hhds::Node_class, hhds::Node_class> node_map;
  for (const auto& n : region_nodes_[r]) {
    auto neo     = gu::create_typed_node(*body, gu::type_op_of(n));
    node_map[n]  = neo;
    carry_node_attrs(body.get(), n, neo);
  }

  auto driver_pin = [&](const hhds::Pin_class& orig) {
    auto neo = node_map[orig.get_master_node()].create_driver_pin(orig.get_port_id());
    carry_driver_attrs(orig, neo);
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

  body->commit();
}

void Partitioner::build_top() {
  auto src_gio = g_->get_io();
  auto tgio    = outlib_->create_io(top_);
  for (const auto& decl : src_gio->get_input_pin_decls()) {
    tgio->add_input(decl.name, decl.port_id, decl.loop_last);
    if (decl.bits != 0) {
      tgio->set_bits(decl.name, decl.bits);
    }
    tgio->set_unsign(decl.name, decl.unsign);
  }
  for (const auto& decl : src_gio->get_output_pin_decls()) {
    tgio->add_output(decl.name, decl.port_id, decl.loop_last);
    if (decl.bits != 0) {
      tgio->set_bits(decl.name, decl.bits);
    }
    tgio->set_unsign(decl.name, decl.unsign);
  }
  auto t = tgio->create_graph();

  // One Sub instance per region.
  absl::flat_hash_map<hhds::Node_class, hhds::Node_class>                     sub_of;
  absl::flat_hash_map<hhds::Node_class, absl::flat_hash_map<std::string, hhds::Pin_class>> sub_out_pin;
  for (auto& [r, gio] : module_gio_) {
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
  for (auto& [r, ports] : module_inputs_) {
    for (const auto& p : ports) {
      auto spin = sub_of[r].create_sink_pin(p.name);
      if (auto b = gu::bits_of(p.driver); b != 0) {
        gu::set_bits(spin, b);
      }
      if (!gu::is_unsign(p.driver)) {
        gu::set_sign(spin);
      }
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
  for (auto& [r, nodes] : region_nodes_) {
    (void)nodes;
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

}  // namespace

void Pass_partition::partition(Eprp_var& var) {
  auto top = std::string{var.get("top", "")};
  auto out = std::string{var.get("out", "")};
  bool dbg = var.get("debug_color", "false") != "false" && var.get("debug_color", "false") != "0";

  // Resolve the top graph.
  hhds::Graph* g = nullptr;
  for (const auto& sp : var.graphs) {
    if (sp && (top.empty() || sp->get_name() == top)) {
      g = sp.get();
      if (top.empty()) {
        top = std::string{sp->get_name()};
      }
      break;
    }
  }
  if (g == nullptr) {
    livehd::diag::err("pass.partition", "no-top", "unsupported").msg("partition: top module '{}' not found in the input library", top).fatal();
    return;
  }

  if (out.empty()) {
    // Stats-only mode: count colors/regions/ports without building an output library.
    Partitioner p(g, nullptr, top, dbg);
    p.report_stats();
    return;
  }

  auto& outlib = livehd::Hhds_graph_library::instance(out);
  Partitioner p(g, &outlib, top, dbg);
  p.run();
}

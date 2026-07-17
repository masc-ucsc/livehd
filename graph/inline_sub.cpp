// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inline_sub.hpp"

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "diag.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "node_util.hpp"

namespace livehd::graph_util {

namespace {

class Sub_inliner {
public:
  Sub_inliner(hhds::Graph* parent, const hhds::Node_class& inst, std::string_view from_pass)
      : parent_(parent), inst_(inst), from_pass_(from_pass) {}

  bool run();

private:
  hhds::Graph*     parent_;
  hhds::Node_class inst_;
  std::string_view from_pass_;
  hhds::Graph*     child_ = nullptr;
  std::string      prefix_;

  absl::flat_hash_map<hhds::Node_class, hhds::Node_class> node_map_;   // child node -> parent clone
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class>   pin_cache_;  // child driver -> parent driver
  absl::flat_hash_set<hhds::Pin_class>                    resolving_;  // feed-through cycle guard
  absl::flat_hash_map<std::string, uint32_t>              in_name2pid_;
  absl::flat_hash_map<uint32_t, std::string>              out_pid2name_;
  bool                                                    failed_ = false;

  void                            create_nodes();
  void                            wire_edges();
  void                            rewire_instance_outputs();
  [[nodiscard]] hhds::Pin_class   resolve_driver(const hhds::Pin_class& d);
  [[nodiscard]] hhds::Pin_class   resolve_output_of(std::string_view oname);
  [[nodiscard]] hhds::Pin_class   driver_feeding_inst_port(uint32_t pid);
  void                            carry_node_attrs(const hhds::Node_class& orig, const hhds::Node_class& neo);
  void                            carry_driver_attrs(const hhds::Pin_class& orig, const hhds::Pin_class& neo);
};

void Sub_inliner::carry_node_attrs(const hhds::Node_class& orig, const hhds::Node_class& neo) {
  if (has_name(orig)) {
    auto nm = std::string{node_name_of(orig)};
    // fproperty/lgassert marker Subs pack "<kind>\x1f<loc>\x1f<msg>" into the name
    // attr; a prefix would corrupt the kind field (cgen/pass.formal parse it up to
    // the first \x1f -- an assume would re-emit as an assert). The payload is
    // parsed, never used as an identifier: copy it verbatim.
    neo.attr(hhds::attrs::name).set(nm.find('\x1f') == std::string::npos ? prefix_ + nm : nm);
  }
  if (auto a = orig.attr(livehd::attrs::lut); a.has()) {
    neo.attr(livehd::attrs::lut).set(std::string{a.get()});
  }
  if (auto a = orig.attr(hhds::attrs::srcid); a.has() && a.get() != 0) {
    auto newid = parent_->source_locator().import_from(child_->source_locator(), a.get());
    neo.attr(hhds::attrs::srcid).set(newid);
  }
  // A color on an absorbed node is meaningless in the parent's id space (colors
  // are per-def), and pass.color recolors the parent right after. Dropping it is
  // the honest move: carrying it would silently claim the child's region ids mean
  // something here.
  if (auto a = orig.attr(livehd::attrs::proven); a.has()) {
    neo.attr(livehd::attrs::proven).set(a.get());
  }
  if (auto a = orig.attr(livehd::attrs::runtime_check); a.has()) {
    neo.attr(livehd::attrs::runtime_check).set(a.get());
  }
}

void Sub_inliner::carry_driver_attrs(const hhds::Pin_class& orig, const hhds::Pin_class& neo) {
  if (auto b = bits_of(orig); b != 0) {
    set_bits(neo, b);
  }
  if (!is_unsign(orig)) {
    set_sign(neo);
  }
  if (auto a = orig.attr(livehd::attrs::pin_name); a.has()) {
    set_pin_name(neo, prefix_ + std::string{a.get()});  // wire names are per-def: prefix like node names
  }
  if (auto o = orig.attr(livehd::attrs::pin_offset); o.has()) {
    neo.attr(livehd::attrs::pin_offset).set(o.get());
  }
}

void Sub_inliner::create_nodes() {
  for (auto n : child_->forward_class()) {
    if (n.is_invalid() || is_builtin_node(n) || node_map_.contains(n)) {
      continue;
    }
    auto op = type_op_of(n);
    if (op == Ntype_op::IO || op == Ntype_op::Invalid || op == Ntype_op::Nconst) {
      continue;  // IO dissolves into the boundary; constants are recreated per consuming edge
    }
    auto neo = create_typed_node(*parent_, op);
    if (op == Ntype_op::Sub) {
      // A sub inside the child stays a sub: this transform is single-level, and
      // the target def already lives in the same library the parent resolves
      // through, so the gid binds without cloning anything.
      if (auto io = n.get_subnode_io()) {
        neo.set_subnode(io);
      }
    }
    node_map_[n] = neo;
    carry_node_attrs(n, neo);
  }
}

// The parent-side driver feeding instance port `pid`, or an invalid pin when the
// parent left that port unconnected.
hhds::Pin_class Sub_inliner::driver_feeding_inst_port(uint32_t pid) {
  for (const auto& e : inst_.inp_edges()) {
    if (static_cast<uint32_t>(e.sink.get_port_id()) != pid) {
      continue;
    }
    if (is_const_pin(e.driver)) {
      return create_const(*parent_, hydrate_const(e.driver));
    }
    return e.driver;  // already a parent pin -- single-level inline needs no hop
  }
  return {};
}

// Parent driver pin for the child-local driver pin `d`. A child graph input pops
// out to whatever the parent wired into the instance port; anything else is the
// clone's own driver.
hhds::Pin_class Sub_inliner::resolve_driver(const hhds::Pin_class& d) {
  if (auto it = pin_cache_.find(d); it != pin_cache_.end()) {
    return it->second;
  }
  if (!resolving_.insert(d).second) {
    livehd::diag::err(from_pass_, "inline-cycle", "unsupported")
        .msg("inline: combinational feed-through cycle through instance '{}' at '{}{}'",
             default_instance_name(inst_),
             prefix_,
             wire_name(d))
        .fatal();
    failed_ = true;
    return {};
  }

  hhds::Pin_class res;
  auto            dn = d.get_master_node();
  if (is_graph_input_pin(d)) {
    if (auto pit = in_name2pid_.find(std::string{pin_name_of(d)}); pit != in_name2pid_.end()) {
      res = driver_feeding_inst_port(pit->second);
    }
  } else if (auto it = node_map_.find(dn); it != node_map_.end()) {
    res = it->second.create_driver_pin(d.get_port_id());
    carry_driver_attrs(d, res);
  }

  resolving_.erase(d);
  if (!res.is_invalid()) {
    pin_cache_[d] = res;
  }
  return res;
}

// The parent driver behind the child's output port `oname` -- the child-internal
// driver, itself resolved (so a child feed-through lands on the parent's own
// driver, and a constant output is cloned into the parent).
hhds::Pin_class Sub_inliner::resolve_output_of(std::string_view oname) {
  auto opin = child_->get_output_pin(std::string{oname});
  if (opin.is_invalid()) {
    return {};
  }
  for (const auto& e : opin.inp_edges()) {
    if (is_const_pin(e.driver)) {
      return create_const(*parent_, hydrate_const(e.driver));
    }
    return resolve_driver(e.driver);
  }
  return {};  // declared but undriven
}

void Sub_inliner::wire_edges() {
  for (auto n : child_->forward_class()) {
    if (failed_) {
      return;
    }
    auto it = node_map_.find(n);
    if (it == node_map_.end()) {
      continue;  // consts and builtins: not cloned
    }
    auto neo = it->second;
    for (const auto& e : n.inp_edges()) {
      auto sp = neo.create_sink_pin(e.sink.get_port_id());
      if (is_const_pin(e.driver)) {
        create_const(*parent_, hydrate_const(e.driver)).connect_sink(sp);
      } else if (auto dp = resolve_driver(e.driver); !dp.is_invalid()) {
        dp.connect_sink(sp);
      }
    }
  }
}

// Everything the instance drove now reads the child-internal driver directly.
void Sub_inliner::rewire_instance_outputs() {
  // Snapshot: out_edges() is a LAZY view over live edge storage, and the loop
  // below both adds edges and (via the caller) deletes the node it is walking.
  std::vector<std::pair<uint32_t, hhds::Pin_class>> readers;
  for (const auto& e : inst_.out_edges()) {
    readers.emplace_back(static_cast<uint32_t>(e.driver.get_port_id()), e.sink);
  }
  for (const auto& [pid, sink] : readers) {
    if (failed_) {
      return;
    }
    auto oit = out_pid2name_.find(pid);
    if (oit == out_pid2name_.end()) {
      continue;  // a driver port with no decl: nothing on the child side to bind
    }
    if (auto src = resolve_output_of(oit->second); !src.is_invalid()) {
      src.connect_sink(sink);
    }
    // An undriven child output leaves the reader unconnected -- exactly what the
    // instance did.
  }
}

bool Sub_inliner::run() {
  auto cg = inst_.get_subnode_graph();
  if (!cg) {
    livehd::diag::err(from_pass_, "inline-no-body", "internal")
        .msg("inline: instance '{}' has no body to inline", default_instance_name(inst_))
        .fatal();
    return false;
  }
  child_  = cg.get();
  prefix_ = default_instance_name(inst_) + ".";

  auto gio = child_->get_io();
  if (!gio) {
    livehd::diag::err(from_pass_, "inline-no-io", "internal")
        .msg("inline: instance '{}' target has no GraphIO", default_instance_name(inst_))
        .fatal();
    return false;
  }
  for (const auto& d : gio->get_input_pin_decls()) {
    in_name2pid_[d.name] = static_cast<uint32_t>(d.port_id);
  }
  for (const auto& d : gio->get_output_pin_decls()) {
    out_pid2name_[static_cast<uint32_t>(d.port_id)] = d.name;
  }

  create_nodes();
  if (!failed_) {
    wire_edges();
  }
  if (!failed_) {
    rewire_instance_outputs();
  }
  if (failed_) {
    return false;
  }
  inst_.del_node();  // its edges go with it; everything it carried is now inline
  return true;
}

}  // namespace

bool inline_sub_instance(hhds::Graph* parent, const hhds::Node_class& inst, std::string_view from_pass) {
  Sub_inliner s(parent, inst, from_pass);
  return s.run();
}

}  // namespace livehd::graph_util

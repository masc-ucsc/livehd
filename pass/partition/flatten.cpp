// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "flatten.hpp"

#include <deque>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "diag.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "node_util.hpp"
#include "pass_partition.hpp"

namespace gu = livehd::graph_util;

namespace {

// One inlined instance of a def: the top graph is the root context (no parent);
// every design Sub gets its own context, so two instances of the same def are
// cloned independently (their node/pin maps must not alias — hhds Node/Pin
// identity is nid-only, shared across instances of one def).
struct Ictx {
  hhds::Graph*     src    = nullptr;  // the def body this context clones
  Ictx*            parent = nullptr;  // enclosing context (nullptr for top)
  hhds::Node_class inst;              // the Sub node in parent->src (invalid for top)
  std::string      prefix;            // dotted instance path ("" for top)

  absl::flat_hash_map<hhds::Node_class, hhds::Node_class> node_map;   // src node -> flat node (cloned nodes only)
  absl::flat_hash_map<hhds::Node_class, Ictx*>            child_ctx;  // src design-Sub node -> child context
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class>   pin_cache;  // src driver pin -> flat driver pin
  absl::flat_hash_set<hhds::Pin_class>                    resolving;  // cycle guard for resolve_driver

  // Child-decl port-id <-> name maps (lazy, per context; needed to hop a module
  // boundary: a Sub sink/driver port id pairs with the child GraphIO decl).
  absl::flat_hash_map<std::string, uint32_t> in_name2pid;   // this def's INPUT decls
  absl::flat_hash_map<uint32_t, std::string> out_pid2name;  // this def's OUTPUT decls
};

class Flattener {
public:
  Flattener(hhds::Graph* top, hhds::GraphLibrary* lib) : top_(top), lib_(lib) {}

  std::shared_ptr<hhds::Graph> run(std::string_view flat_name);

private:
  hhds::Graph*        top_;
  hhds::GraphLibrary* lib_;
  hhds::Graph*        flat_ = nullptr;
  std::deque<Ictx>    arena_;  // stable pointers
  bool                failed_ = false;

  // Defs on the current instantiation path — a def re-entered while still open
  // is a recursive hierarchy (would recurse forever / overflow the stack).
  absl::flat_hash_set<hhds::Gid> inline_stack_;

  Ictx* make_ctx(hhds::Graph* src, Ictx* parent, const hhds::Node_class& inst, std::string prefix);
  void  create_nodes(Ictx* ctx);
  void  wire_edges(Ictx* ctx);
  void  wire_top_outputs(Ictx* top_ctx);
  void  complete_bbox_outputs(Ictx* ctx);

  [[nodiscard]] hhds::Pin_class resolve_driver(Ictx* ctx, const hhds::Pin_class& d);
  [[nodiscard]] hhds::Pin_class resolve_output_of(Ictx* cctx, std::string_view oname);

  void carry_node_attrs(Ictx* ctx, const hhds::Node_class& orig, const hhds::Node_class& neo);
  void carry_driver_attrs(Ictx* ctx, const hhds::Pin_class& orig, const hhds::Pin_class& neo);
};

Ictx* Flattener::make_ctx(hhds::Graph* src, Ictx* parent, const hhds::Node_class& inst, std::string prefix) {
  auto& c  = arena_.emplace_back();
  c.src    = src;
  c.parent = parent;
  c.inst   = inst;
  c.prefix = std::move(prefix);
  if (auto gio = src->get_io()) {
    for (const auto& d : gio->get_input_pin_decls()) {
      c.in_name2pid[d.name] = static_cast<uint32_t>(d.port_id);
    }
    for (const auto& d : gio->get_output_pin_decls()) {
      c.out_pid2name[static_cast<uint32_t>(d.port_id)] = d.name;
    }
  }
  return &c;
}

void Flattener::carry_node_attrs(Ictx* ctx, const hhds::Node_class& orig, const hhds::Node_class& neo) {
  if (gu::has_name(orig)) {
    auto nm = std::string{gu::node_name_of(orig)};
    // fproperty/lgassert marker Subs pack "<kind>\x1f<loc>\x1f<msg>" into the
    // name attr; a prefix would corrupt the kind field (cgen/pass.formal parse
    // it up to the first \x1f — an assume would re-emit as an assert). The
    // payload is parsed, never used as an identifier: copy it verbatim.
    neo.attr(hhds::attrs::name).set(nm.find('\x1f') == std::string::npos ? ctx->prefix + nm : nm);
  }
  if (auto a = orig.attr(livehd::attrs::lut); a.has()) {
    neo.attr(livehd::attrs::lut).set(std::string{a.get()});
  }
  if (auto a = orig.attr(hhds::attrs::srcid); a.has() && a.get() != 0) {
    auto newid = flat_->source_locator().import_from(ctx->src->source_locator(), a.get());
    neo.attr(hhds::attrs::srcid).set(newid);
  }
  // The flat per-def color is what pass.partition consumes downstream — carry
  // it verbatim so the flattened def partitions exactly like the hierarchy did
  // (per-instance hier colors are a different storage; flatten works on the
  // compact per-def coloring like the Partitioner itself).
  if (auto c = gu::node_color_of(orig); c != 0) {
    neo.attr(livehd::attrs::color).set(c);
  }
  // Formal markers ride the node (pass.abc reads `proven` off fproperty Subs to
  // exploit discharged assumes; cgen keeps runtime checks alive through them).
  if (auto a = orig.attr(livehd::attrs::proven); a.has()) {
    neo.attr(livehd::attrs::proven).set(a.get());
  }
  if (auto a = orig.attr(livehd::attrs::runtime_check); a.has()) {
    neo.attr(livehd::attrs::runtime_check).set(a.get());
  }
}

void Flattener::carry_driver_attrs(Ictx* ctx, const hhds::Pin_class& orig, const hhds::Pin_class& neo) {
  if (auto b = gu::bits_of(orig); b != 0) {
    gu::set_bits(neo, b);
  }
  if (!gu::is_unsign(orig)) {
    gu::set_sign(neo);
  }
  // Wire names are per-def; two instances of one def would collide in the flat
  // module, so prefix like node names.
  if (auto a = orig.attr(livehd::attrs::pin_name); a.has()) {
    gu::set_pin_name(neo, ctx->prefix + std::string{a.get()});
  }
  if (auto o = orig.attr(livehd::attrs::pin_offset); o.has()) {
    neo.attr(livehd::attrs::pin_offset).set(o.get());
  }
}

void Flattener::create_nodes(Ictx* ctx) {
  for (auto n : ctx->src->forward_class()) {
    if (failed_) {
      return;
    }
    if (n.is_invalid() || gu::is_builtin_node(n) || ctx->node_map.contains(n)) {
      continue;
    }
    auto op = gu::type_op_of(n);
    if (op == Ntype_op::IO || op == Ntype_op::Invalid || op == Ntype_op::Nconst) {
      continue;  // IO dissolves; constants are recreated per consuming edge
    }
    if (op == Ntype_op::Sub && ctx->child_ctx.contains(n)) {
      continue;
    }
    if (op == Ntype_op::Sub && n.get_subnode_graph() != nullptr) {
      // Design instance: recurse — its internals become flat nodes with a
      // longer prefix; the Sub itself dissolves (edges hop through it in
      // resolve_driver). The child shared_ptr is owned by the source library
      // for the process lifetime; the context's raw pointer cannot dangle.
      auto cgid = n.get_subnode_gid();
      if (!inline_stack_.insert(cgid).second) {
        livehd::diag::err("pass.partition", "flatten-recursive", "unsupported")
            .msg("flatten: recursive instantiation of '{}' (instance '{}')",
                 std::string{n.get_subnode_io()->get_name()},
                 ctx->prefix + gu::default_instance_name(n))
            .fatal();
        failed_ = true;
        return;
      }
      auto* child = make_ctx(n.get_subnode_graph().get(), ctx, n, ctx->prefix + gu::default_instance_name(n) + ".");
      ctx->child_ctx[n] = child;
      create_nodes(child);
      inline_stack_.erase(cgid);
      continue;
    }
    auto neo = gu::create_typed_node(*flat_, op);
    if (op == Ntype_op::Sub) {
      // Body-less black box (liberty/tie cell, external IP, fproperty marker):
      // stays an opaque instance; clone its IO decl into the flat graph's lib.
      auto io = livehd::partition::resolve_or_clone_subdef(lib_, n);
      if (io) {
        neo.set_subnode(io);
      } else if (n.get_subnode_io()) {
        livehd::diag::err("pass.partition", "flatten-subdef", "unsupported")
            .msg("flatten: cannot resolve black-box def '{}' for instance '{}'",
                 std::string{n.get_subnode_io()->get_name()},
                 ctx->prefix + gu::default_instance_name(n))
            .fatal();
        failed_ = true;
        return;
      }
    }
    ctx->node_map[n] = neo;
    carry_node_attrs(ctx, n, neo);
  }
}

// Flat driver pin for the def-local driver pin `d` seen from `ctx`. Hops module
// boundaries in both directions: a def input pops to the parent-side driver of
// the instance's sink port; a design-Sub output dives into the child body's
// output-driving pin. Constants are NOT handled here (callers clone them per
// consuming edge, mirroring the Partitioner's const_edges_ handling). Returns
// an invalid pin for a genuinely undriven path (dangling input / unconnected
// port) — the caller leaves the sink unconnected, exactly like the original.
hhds::Pin_class Flattener::resolve_driver(Ictx* ctx, const hhds::Pin_class& d) {
  if (auto it = ctx->pin_cache.find(d); it != ctx->pin_cache.end()) {
    return it->second;
  }
  if (!ctx->resolving.insert(d).second) {
    livehd::diag::err("pass.partition", "flatten-cycle", "unsupported")
        .msg("flatten: combinational feed-through cycle through module boundaries at '{}{}'", ctx->prefix, gu::wire_name(d))
        .fatal();
    failed_ = true;
    return {};
  }

  hhds::Pin_class res;
  auto            dn = d.get_master_node();

  if (gu::is_graph_input_pin(d)) {
    if (ctx->parent == nullptr) {
      res = flat_->get_input_pin(std::string{gu::pin_name_of(d)});
    } else {
      // Hop UP: find the parent-side edge feeding this port of the instance.
      auto pit = ctx->in_name2pid.find(std::string{gu::pin_name_of(d)});
      if (pit != ctx->in_name2pid.end()) {
        for (const auto& e : ctx->inst.inp_edges()) {
          if (static_cast<uint32_t>(e.sink.get_port_id()) != pit->second) {
            continue;
          }
          if (gu::is_const_pin(e.driver)) {
            res = gu::create_const(*flat_, gu::hydrate_const(e.driver));
          } else {
            res = resolve_driver(ctx->parent, e.driver);
          }
          break;
        }
      }
      // No edge: the parent left the port unconnected — stay invalid.
    }
  } else if (gu::type_op_of(dn) == Ntype_op::Sub && dn.get_subnode_graph() != nullptr) {
    // Hop DOWN: the driver is a design instance's output — resolve to the
    // child-internal driver of that output port.
    if (auto cit = ctx->child_ctx.find(dn); cit != ctx->child_ctx.end()) {
      auto* cctx = cit->second;
      auto  oit  = cctx->out_pid2name.find(static_cast<uint32_t>(d.get_port_id()));
      if (oit != cctx->out_pid2name.end()) {
        res = resolve_output_of(cctx, oit->second);
      }
    }
  } else if (auto it = ctx->node_map.find(dn); it != ctx->node_map.end()) {
    res = it->second.create_driver_pin(d.get_port_id());
    carry_driver_attrs(ctx, d, res);
  }
  // else: unexpected builtin driver — stay invalid (mirrors Partitioner's skip).

  ctx->resolving.erase(d);
  if (!res.is_invalid()) {
    ctx->pin_cache[d] = res;
  }
  return res;
}

// Flat driver pin behind child output port `oname` (the child body's internal
// driver, itself resolved recursively — handles child feed-throughs and consts).
hhds::Pin_class Flattener::resolve_output_of(Ictx* cctx, std::string_view oname) {
  auto opin = cctx->src->get_output_pin(std::string{oname});
  if (opin.is_invalid()) {
    return {};
  }
  for (const auto& e : opin.inp_edges()) {
    if (gu::is_const_pin(e.driver)) {
      return gu::create_const(*flat_, gu::hydrate_const(e.driver));
    }
    return resolve_driver(cctx, e.driver);
  }
  return {};  // declared but undriven
}

void Flattener::wire_edges(Ictx* ctx) {
  for (auto n : ctx->src->forward_class()) {
    if (failed_) {
      return;
    }
    auto it = ctx->node_map.find(n);
    if (it == ctx->node_map.end()) {
      continue;  // design Subs, consts, builtins: not cloned
    }
    auto neo = it->second;
    for (const auto& e : n.inp_edges()) {
      auto sp = neo.create_sink_pin(e.sink.get_port_id());
      if (gu::is_const_pin(e.driver)) {
        gu::create_const(*flat_, gu::hydrate_const(e.driver)).connect_sink(sp);
      } else {
        auto dp = resolve_driver(ctx, e.driver);
        if (!dp.is_invalid()) {
          dp.connect_sink(sp);
        }
      }
    }
  }
}

void Flattener::wire_top_outputs(Ictx* top_ctx) {
  auto gio = top_ctx->src->get_io();
  if (!gio) {
    return;
  }
  for (const auto& decl : gio->get_output_pin_decls()) {
    auto opin = top_ctx->src->get_output_pin(decl.name);
    if (opin.is_invalid()) {
      continue;
    }
    for (const auto& e : opin.inp_edges()) {
      auto sink = flat_->get_output_pin(decl.name);
      if (gu::is_const_pin(e.driver)) {
        gu::create_const(*flat_, gu::hydrate_const(e.driver)).connect_sink(sink);
      } else {
        auto dp = resolve_driver(top_ctx, e.driver);
        if (!dp.is_invalid()) {
          dp.connect_sink(sink);
        }
      }
    }
  }
}

// Surviving black-box Subs: materialize declared outputs that carried no edge,
// mirroring tolg/partition (readers probe every declared output; hhds find_pin
// asserts on a pin that was never created).
void Flattener::complete_bbox_outputs(Ictx* ctx) {
  for (auto& [orig, neo] : ctx->node_map) {
    if (gu::type_op_of(orig) != Ntype_op::Sub) {
      continue;
    }
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

std::shared_ptr<hhds::Graph> Flattener::run(std::string_view flat_name) {
  auto src_gio = top_->get_io();
  if (!src_gio) {
    livehd::diag::err("pass.partition", "flatten-no-io", "unsupported")
        .msg("flatten: top '{}' has no GraphIO", std::string{top_->get_name()})
        .fatal();
    return nullptr;
  }

  auto gio = lib_->create_io(std::string{flat_name});
  for (const auto& d : src_gio->get_input_pin_decls()) {
    gio->add_input(d.name, d.port_id, d.loop_break);
    if (d.bits != 0) {
      gio->set_bits(d.name, d.bits);
    }
    gio->set_unsign(d.name, d.unsign);
  }
  for (const auto& d : src_gio->get_output_pin_decls()) {
    gio->add_output(d.name, d.port_id, d.loop_break);
    if (d.bits != 0) {
      gio->set_bits(d.name, d.bits);
    }
    gio->set_unsign(d.name, d.unsign);
  }
  auto flat = gio->create_graph();
  flat_     = flat.get();

  // Stamp bits/sign on the materialized flat input pins (the decl is not
  // auto-propagated to pin attrs; every downstream reader sizes from the pin).
  for (const auto& d : src_gio->get_input_pin_decls()) {
    auto ip = flat_->get_input_pin(d.name);
    if (d.bits != 0) {
      gu::set_bits(ip, static_cast<int>(d.bits));
    }
    d.unsign ? gu::set_unsign(ip) : gu::set_sign(ip);
  }

  // Carry the coloring_info blob (region_opts / seeded-region channel) so
  // pass.abc's per-color overrides keep working against the flat src.
  if (auto a = top_->get_input_node().attr(livehd::attrs::coloring_info); a.has()) {
    flat_->get_input_node().attr(livehd::attrs::coloring_info).set(std::string{a.get()});
  }

  auto* top_ctx = make_ctx(top_, nullptr, {}, "");
  inline_stack_.insert(top_->get_gid());
  create_nodes(top_ctx);
  if (!failed_) {
    for (auto& ctx : arena_) {
      wire_edges(&ctx);
      if (failed_) {
        break;
      }
    }
  }
  if (!failed_) {
    wire_top_outputs(top_ctx);
  }
  if (!failed_) {
    for (auto& ctx : arena_) {
      complete_bbox_outputs(&ctx);
    }
  }
  if (failed_) {
    return nullptr;
  }
  return flat;
}

}  // namespace

namespace livehd::partition {

std::shared_ptr<hhds::Graph> flatten_hierarchy(hhds::Graph* top, hhds::GraphLibrary* lib, std::string_view flat_name) {
  Flattener f(top, lib);
  return f.run(flat_name);
}

}  // namespace livehd::partition

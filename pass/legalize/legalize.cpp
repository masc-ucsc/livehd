// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "legalize.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "attr_carry.hpp"
#include "attrs.hpp"
#include "cell.hpp"
#include "diag.hpp"
#include "hlop/dlop.hpp"
#include "loop_split.hpp"
#include "node_util.hpp"
#include "semdiff.hpp"
#include "split_selfref.hpp"

namespace gu = livehd::graph_util;

namespace livehd::legalize {

namespace {

constexpr std::string_view kPass = "pass.legalize";

// ---------------------------------------------------------------------------
// Frozen-structure ledger. Keyed by the graph OBJECT (see the header: gids are
// name hashes shared across libraries). Process-local: freezing is an integrity
// check, never persisted state. Sub_fold::interface deliberately -- each def is
// frozen on its OWN structure, so an edited child is reported against the CHILD
// rather than smeared over every ancestor that instantiates it.
// ---------------------------------------------------------------------------

struct Frozen_entry {
  bool                              owned = false;  // `owner` is meaningful (shared_ptr overload)
  std::weak_ptr<hhds::Graph>        owner;
  livehd::semdiff::Canonical_digest digest;
};

absl::flat_hash_map<const hhds::Graph*, Frozen_entry>& frozen_ledger() {
  static absl::flat_hash_map<const hhds::Graph*, Frozen_entry> ledger;
  return ledger;
}

livehd::semdiff::Canonical_digest structure_of(hhds::Graph* g) {
  return livehd::semdiff::canonical_digest(g, {}, livehd::semdiff::Sub_fold::interface);
}

// A graph whose library entry was deleted keeps its object alive for whoever
// still holds it, but its IO is gone and its body asserts on access.
bool is_live(hhds::Graph* g) { return g != nullptr && g->get_io() != nullptr; }

void freeze_into(hhds::Graph* g, bool owned, std::weak_ptr<hhds::Graph> owner) {
  if (!is_live(g)) {
    return;
  }
  auto d = structure_of(g);
  if (!d.valid) {
    // An unresolvable shape (a cyclic instance graph, an anonymous state cell
    // the digest refuses to fold) has no stable structural identity, so there
    // is nothing to check against later. Silent on purpose: freeze runs inside
    // the compile-cache diagnostic window, and a record about THIS process's
    // ledger would be stored and replayed on every warm hit.
    frozen_ledger().erase(g);
    return;
  }
  frozen_ledger()[g] = Frozen_entry{.owned = owned, .owner = std::move(owner), .digest = d};
}

// ---------------------------------------------------------------------------
// Copy helpers, shared by the whole-def rebuild and the loop-split halves.
// ---------------------------------------------------------------------------

// IO declarations, verbatim. Declaration ORDER is Verilog positional-argument
// order, and a Sub instance's sink/driver pids index straight into these, so
// neither is renumbered. `keep_out == nullptr` keeps every output.
void copy_io_decls(const hhds::GraphIO& src, hhds::GraphIO& dst, const absl::flat_hash_set<hhds::Port_id>* keep_out) {
  for (const auto& in : src.get_input_pin_decls()) {
    dst.add_input(in.name, in.port_id, in.loop_break);
    if (in.bits != 0) {
      dst.set_bits(in.name, in.bits);
    }
    dst.set_unsign(in.name, in.unsign);
  }
  for (const auto& out : src.get_output_pin_decls()) {
    if (keep_out != nullptr && !keep_out->contains(out.port_id)) {
      continue;
    }
    dst.add_output(out.name, out.port_id, out.loop_break);
    if (out.bits != 0) {
      dst.set_bits(out.name, out.bits);
    }
    dst.set_unsign(out.name, out.unsign);
  }
}

// Copy the nodes of `src` accepted by `keep` (nullptr: all) into `dst`, with
// every edge among them, the edges into the OUTPUT node whose port `keep_out`
// accepts (nullptr: all), and every attribute -- node, pin, singleton and
// declared-input. A nested Sub re-binds to `rebind`'s def of the same name when
// it has one; otherwise it keeps the source GraphIO (a body-less black box --
// a liberty cell, external IP, an fproperty marker -- is shared, not rebuilt).
// A loop descriptor is kept: dropping it would collapse a replicated instance
// to a single occurrence.
void copy_body(hhds::Graph* src, hhds::Graph* dst, hhds::GraphLibrary* rebind, const absl::flat_hash_set<hhds::Node_class>* keep,
               const absl::flat_hash_set<hhds::Port_id>* keep_out) {
  auto* dst_lib = dst->get_io() ? dst->get_io()->get_library() : nullptr;

  const auto accepted = [&](const hhds::Node_class& n) {
    return !n.is_invalid() && !gu::is_builtin_node(n) && gu::type_op_of(n) != Ntype_op::Invalid
           && (keep == nullptr || keep->contains(n));
  };

  // ---- pass 1: create every node, in FORWARD order.
  //
  // Two passes rather than one because forward order only guarantees that a
  // COMBINATIONAL driver precedes its sink. A loop-last node (Flop / Memory /
  // Sub) is visited before the logic feeding it, so its inputs cannot be wired
  // until every node exists. Creating first is also what makes the destination
  // table dense in dependency order -- the whole point of the rebuild.
  absl::flat_hash_map<hhds::Node_class, hhds::Node_class> node_map;
  for (auto n : src->body().nodes(hhds::Node_order::forward)) {
    if (!accepted(n)) {
      continue;
    }
    const auto op  = gu::type_op_of(n);
    auto       neo = gu::create_typed_node(*dst, op);
    if (op == Ntype_op::Sub) {
      if (auto src_sub_io = n.get_subnode_io()) {
        auto bind = rebind != nullptr ? rebind->find_io(src_sub_io->get_name()) : nullptr;
        if (!bind) {
          bind = src_sub_io;
        }
        if (auto loop = n.subnode_loop()) {
          neo.set_subnode(bind, *loop);
        } else {
          neo.set_subnode(bind);
        }
      }
    }
    gu::carry_node_attrs(n, neo);
    gu::carry_srcid(n, neo, src, dst_lib);
    node_map.emplace(n, neo);
  }
  // The singletons exist on both sides; their node attributes still have to
  // move (a coloring_info or synth_region on the INPUT node is real data).
  gu::carry_node_attrs(src->get_input_node(), dst->get_input_node());
  gu::carry_node_attrs(src->get_output_node(), dst->get_output_node());

  // Map one SOURCE pin onto its destination counterpart. A constant is not a
  // node but a pin on the singleton CONST node, so it is re-created in `dst`
  // rather than looked up; IO pins live on the INPUT/OUTPUT singletons.
  const auto map_pin = [&](const hhds::Pin_class& p, bool want_driver) -> hhds::Pin_class {
    if (p.is_invalid()) {
      return {};
    }
    if (want_driver && gu::is_const_pin(p)) {
      return gu::create_const(*dst, gu::hydrate_const(p));
    }
    auto owner = p.get_master_node();
    if (owner.is_invalid()) {
      return {};
    }
    const auto pid = p.get_port_id();
    if (gu::is_builtin_node(owner)) {
      auto host = want_driver ? dst->get_input_node() : dst->get_output_node();
      return want_driver ? host.create_driver_pin(pid) : host.create_sink_pin(pid);
    }
    auto it = node_map.find(owner);
    if (it == node_map.end()) {
      return {};
    }
    return want_driver ? it->second.create_driver_pin(pid) : it->second.create_sink_pin(pid);
  };

  // ---- pass 2: wire every edge, and carry the per-pin attributes.
  //
  // Driven from the SINK side so each edge is visited exactly once (a driver's
  // fanout can be large and out_edges() is a lazy view; inp_edges() is small,
  // eager, and available per NODE). A driver's attributes are stamped once, not
  // once per fanout edge.
  absl::flat_hash_set<hhds::Pin_class> stamped;
  const auto                           wire_edge = [&](const hhds::Edge_class& e) {
    auto neo_sink = map_pin(e.sink, /*want_driver=*/false);
    auto neo_drv  = map_pin(e.driver, /*want_driver=*/true);
    if (neo_sink.is_invalid() || neo_drv.is_invalid()) {
      return;
    }
    gu::carry_pin_attrs(e.sink, neo_sink);
    if (stamped.insert(e.driver).second) {
      gu::carry_pin_attrs(e.driver, neo_drv);
    }
    neo_drv.connect_sink(neo_sink);
  };
  for (auto n : src->body().nodes(hhds::Node_order::forward)) {
    if (!accepted(n)) {
      continue;
    }
    for (const auto& e : n.inp_edges()) {
      wire_edge(e);
    }
    // A single-output cell with NO fanout never appears on any sink's inp_edges,
    // so its driver width/sign would be lost. Port 0 is the sole driver of every
    // single-output op. Multi-driver ops (Sub/Memory/IO) keep their decl-based widths.
    if (!Ntype::has_multiple_driver_pins(gu::type_op_of(n))) {
      auto d = n.create_driver_pin(0);
      if (stamped.insert(d).second) {
        gu::carry_pin_attrs(d, node_map.at(n).create_driver_pin(0));
      }
    }
  }
  // The output singleton is walked too: its sinks are compare points, not
  // decoration, and a rebuild that dropped them would emit a module whose ports
  // are undriven.
  for (const auto& e : src->get_output_node().inp_edges()) {
    if (keep_out != nullptr && !keep_out->contains(e.sink.get_port_id())) {
      continue;
    }
    wire_edge(e);
  }
  // Declared INPUT pins carry width/sign whether or not anything reads them; an
  // unused input has no edge to carry them from.
  if (auto sio = src->get_io(); sio && dst->get_io()) {
    auto sin = src->get_input_node();
    auto din = dst->get_input_node();
    for (const auto& in : sio->get_input_pin_decls()) {
      if (dst->get_io()->has_input_with_port_id(in.port_id)) {
        gu::carry_pin_attrs(sin.get_driver_pin(in.port_id), din.get_driver_pin(in.port_id));
      }
    }
  }
}

}  // namespace

void freeze(hhds::Graph* g) { freeze_into(g, /*owned=*/false, {}); }
void freeze(const std::shared_ptr<hhds::Graph>& g) { freeze_into(g.get(), /*owned=*/true, g); }

bool verify_frozen(hhds::Graph* g, std::string_view who) {
  if (!is_live(g)) {
    return true;
  }
  auto& ledger = frozen_ledger();
  auto  it     = ledger.find(g);
  if (it == ledger.end()) {
    return true;  // never frozen: nothing claimed, nothing to check
  }
  if (it->second.owned) {
    auto owner = it->second.owner.lock();
    if (!owner || owner.get() != g) {
      ledger.erase(it);  // the frozen graph died; this is a new object at its address
      return true;
    }
  }
  auto now = structure_of(g);
  if (now.valid && now == it->second.digest) {
    return true;
  }
  livehd::diag::err(kPass, "frozen-graph-mutated", "internal")
      .msg("'{}' structurally changed '{}' after pass.legalize froze it", who, std::string{g->get_name()})
      .hint(
          "a pass may write ATTRIBUTES (color, place, proven, match) but must not add, delete, retype, re-widen "
          "or re-wire a node -- emit a new graph instead, the way lnast.tolg and pass.cprop do")
      .emit();
  return false;
}

size_t frozen_count() { return frozen_ledger().size(); }

std::shared_ptr<hhds::GraphIO> clone_io_decls(hhds::Graph* src, hhds::GraphLibrary& dst_lib) {
  if (src == nullptr || src->get_io() == nullptr) {
    return nullptr;
  }
  auto src_io = src->get_io();
  auto dst_io = dst_lib.create_io(std::string{src_io->get_name()});
  if (!dst_io) {
    return nullptr;
  }
  copy_io_decls(*src_io, *dst_io, nullptr);
  return dst_io;
}

std::shared_ptr<hhds::Graph> rebuild_def(hhds::Graph* src, const std::shared_ptr<hhds::GraphIO>& dst_gio) {
  if (src == nullptr || !dst_gio) {
    return nullptr;
  }
  auto dst = dst_gio->create_graph();
  if (!dst) {
    livehd::diag::err(kPass, "rebuild-no-body", "internal")
        .msg("legalize: could not create a body for '{}'", std::string{dst_gio->get_name()})
        .emit();
    return nullptr;
  }
  copy_body(src, dst.get(), dst_gio->get_library(), nullptr, nullptr);
  return dst;
}

// ---------------------------------------------------------------------------
// Loop split
// ---------------------------------------------------------------------------

namespace {

// One half: its node set, the body INPUT ports that set reads, and the carry
// ports it owns (in/out paired by index).
struct Half {
  absl::flat_hash_set<hhds::Node_class> nodes;
  absl::flat_hash_set<hhds::Port_id>    reads_inputs;
  std::vector<hhds::Port_id>            carry_in;
  std::vector<hhds::Port_id>            carry_out;
};

// Backward cone of the driver feeding `out_port` on `body`'s output node, down
// to the body inputs (recorded by port) and constants. Used to test whether the
// two halves share logic or read each other's carries.
void cone_of_output(hhds::Graph* body, hhds::Port_id out_port, Half& half) {
  const auto                   in_node = body->get_input_node();
  std::vector<hhds::Pin_class> work;
  for (const auto& e : body->get_output_node().inp_edges()) {
    if (e.sink.get_port_id() == out_port) {
      work.push_back(e.driver);
    }
  }
  while (!work.empty()) {
    auto d = work.back();
    work.pop_back();
    if (d.is_invalid()) {
      continue;
    }
    auto n = d.get_master_node();
    if (n.is_invalid()) {
      continue;
    }
    if (n == in_node) {
      half.reads_inputs.insert(d.get_port_id());
      continue;
    }
    if (gu::is_builtin_node(n) || !half.nodes.insert(n).second) {
      continue;
    }
    for (const auto& e : n.inp_edges()) {
      work.push_back(e.driver);
    }
  }
}

// Build one half's callee body: the nodes of `half`, the body's full input
// declaration set (an unused input is harmless and keeps port ids stable), and
// only `half`'s carry outputs. A same-named def already in the library is a
// STALE half from an earlier compile and is replaced.
std::shared_ptr<hhds::Graph> make_half(hhds::GraphLibrary& lib, hhds::Graph* body, const Half& half, const std::string& name,
                                       Split_state& state) {
  if (auto stale = lib.find_io(name)) {
    if (auto g = stale->get_graph()) {
      state.removed.push_back(g);
      lib.delete_graph(g);
    }
    lib.delete_graphio(name);
  }
  auto gio = lib.create_io(name);
  if (!gio) {
    return nullptr;
  }
  absl::flat_hash_set<hhds::Port_id> keep_out(half.carry_out.begin(), half.carry_out.end());
  copy_io_decls(*body->get_io(), *gio, &keep_out);
  auto dst = gio->create_graph();
  if (!dst) {
    return nullptr;
  }
  copy_body(body, dst.get(), &lib, &half.nodes, &keep_out);
  state.added.push_back(dst);
  return dst;
}

void refuse(hhds::Graph* host, std::string_view code, std::string_view why) {
  livehd::diag::info(kPass, code, "progress")
      .msg("legalize: loop in '{}' not split; {}", std::string{host->get_name()}, why)
      .emit();
}

}  // namespace

int split_loops(hhds::Graph* host, hhds::GraphLibrary& lib, Split_state* state) {
  if (host == nullptr) {
    return 0;
  }
  Split_state local;
  if (state == nullptr) {
    state = &local;
  }
  // Snapshot: the rewrite creates and deletes nodes in `host`, and body().nodes()
  // is a live view.
  std::vector<hhds::Node_class> loops;
  for (auto n : host->body().nodes(hhds::Node_order::forward)) {
    if (!n.is_invalid() && gu::type_op_of(n) == Ntype_op::Sub && n.is_loop_subnode()) {
      loops.push_back(n);
    }
  }

  int split = 0;
  for (const auto& sub : loops) {
    auto cls = gu::classify_loop(sub);
    if (!cls.valid) {
      continue;
    }
    Half par;
    Half ind;
    for (const auto& c : cls.carries) {
      // Lane-independent slice writes on one side; every recurrence (a genuine
      // induction AND an associative reduction, which still threads state across
      // lanes) on the other.
      //
      // `needs_disjoint_proof` is deliberately NOT consulted. Whether two lanes'
      // slices overlap has no bearing on whether the SPLIT is correct: the two
      // carries do not interact in the original body (checked below), so running
      // them as two loops over the same domain computes exactly what one loop
      // did, overlapping or not. Disjointness is what a CONSUMER must discharge
      // before treating the parallel half's lanes as independent, and it travels
      // with the classification for that purpose.
      auto& side = c.is_parallel() ? par : ind;
      side.carry_in.push_back(c.in_port);
      side.carry_out.push_back(c.out_port);
    }
    if (par.carry_out.empty() || ind.carry_out.empty()) {
      continue;  // already one-sided: nothing to split
    }

    auto body = sub.get_subnode_graph();
    auto desc = sub.subnode_loop();
    if (!body || !body->get_io() || !desc.has_value()) {
      continue;
    }

    // A callee output that is not a carry (a per-lane "final" the host reads,
    // the descriptor's next_active_output) has no half to live in: each half
    // declares only its own carry outputs.
    absl::flat_hash_set<hhds::Port_id> carry_outs(par.carry_out.begin(), par.carry_out.end());
    carry_outs.insert(ind.carry_out.begin(), ind.carry_out.end());
    bool extra_output = desc->next_active_output.has_value();
    for (const auto& o : body->get_io()->get_output_pin_decls()) {
      extra_output = extra_output || !carry_outs.contains(o.port_id);
    }
    if (extra_output) {
      refuse(host, "loop-split-extra-output", "its body has an output that is not a carry");
      continue;
    }

    // Each half owns the backward cone of ITS carry outputs. Computed here for
    // both sides rather than reusing `cls.induction_nodes`, which only covers
    // carries classified `induction` -- an associative reduction is on the
    // recurrence side too and its cone must come with it.
    for (const auto& port : ind.carry_out) {
      cone_of_output(body.get(), port, ind);
    }
    for (const auto& port : par.carry_out) {
      cone_of_output(body.get(), port, par);
    }
    // v1: shared logic would have to be duplicated into both halves. Refuse and
    // leave the loop whole -- correct, just not yet optimized.
    bool shared = false;
    for (const auto& n : par.nodes) {
      shared = shared || ind.nodes.contains(n);
    }
    if (shared) {
      refuse(host, "loop-split-shared", "its parallel and recurrence cones share logic");
      continue;
    }
    // A half self-wires only ITS carries; a cone that reads the other half's
    // carry-in port would read that carry's seed in every lane instead of the
    // running value. The classifier already sends a carry whose position or
    // value depends on ANY carry to the recurrence side, so this is the
    // belt-and-braces check on the split itself.
    const absl::flat_hash_set<hhds::Port_id> par_in(par.carry_in.begin(), par.carry_in.end());
    const absl::flat_hash_set<hhds::Port_id> ind_in(ind.carry_in.begin(), ind.carry_in.end());
    bool                                     crossed = false;
    for (const auto port : par.reads_inputs) {
      crossed = crossed || ind_in.contains(port);
    }
    for (const auto port : ind.reads_inputs) {
      crossed = crossed || par_in.contains(port);
    }
    if (crossed) {
      refuse(host, "loop-split-cross-carry", "one half reads the other half's carry");
      continue;
    }

    // Derived, STABLE names: abc_incr keys its region cache on the module name,
    // so a name that reshuffles when an unrelated loop appears would invalidate
    // entries that did not change. One pair per body per run: two loops may
    // share one body def.
    const auto                   body_gid = body->get_gid();
    std::shared_ptr<hhds::Graph> par_body;
    std::shared_ptr<hhds::Graph> ind_body;
    if (auto it = state->halves.find(body_gid); it != state->halves.end()) {
      par_body = it->second.first;
      ind_body = it->second.second;
    } else {
      const std::string base = std::string{body->get_name()};
      par_body               = make_half(lib, body.get(), par, base + "__par", *state);
      ind_body               = make_half(lib, body.get(), ind, base + "__ind", *state);
      if (par_body && ind_body) {
        state->halves.emplace(body_gid, std::make_pair(par_body, ind_body));
      }
    }
    if (!par_body || !ind_body) {
      continue;
    }

    auto par_sub = gu::create_typed_node(*host, Ntype_op::Sub);
    auto ind_sub = gu::create_typed_node(*host, Ntype_op::Sub);
    par_sub.set_subnode(par_body->get_io(), *desc);
    ind_sub.set_subnode(ind_body->get_io(), *desc);
    gu::carry_node_attrs(sub, par_sub);
    gu::carry_node_attrs(sub, ind_sub);
    gu::carry_srcid(sub, par_sub);
    gu::carry_srcid(sub, ind_sub);
    // Two instances must not share one instance name: semdiff keys its cut
    // points and hier LEC its pairing on the hierarchical name.
    if (auto nm = sub.attr(hhds::attrs::name); nm.has()) {
      par_sub.attr(hhds::attrs::name).set(std::string{nm.get()} + "__par");
      ind_sub.attr(hhds::attrs::name).set(std::string{nm.get()} + "__ind");
    }

    // External inputs (invariants, index, activation, every carry's seed) feed
    // BOTH halves; a carry self-edge is re-created per half below. The other
    // half's seed lands on a port that half never self-wires, which makes it a
    // plain (unused) input there.
    for (const auto& e : sub.inp_edges()) {
      if (e.driver.get_master_node() == sub) {
        continue;  // the old self-edge
      }
      const auto pid = e.sink.get_port_id();
      auto       ps  = par_sub.create_sink_pin(pid);
      auto       is  = ind_sub.create_sink_pin(pid);
      gu::carry_pin_attrs(e.sink, ps);
      gu::carry_pin_attrs(e.sink, is);
      e.driver.connect_sink(ps);
      e.driver.connect_sink(is);
    }
    const auto self_wire = [](const hhds::Node_class& n, const Half& h) {
      for (size_t i = 0; i < h.carry_in.size(); ++i) {
        n.create_driver_pin(h.carry_out[i]).connect_sink(n.create_sink_pin(h.carry_in[i]));
      }
    };
    self_wire(par_sub, par);
    self_wire(ind_sub, ind);

    // External readers move to whichever half now owns that output port. The
    // new edges are added only AFTER the old Sub is gone: a reader may be the
    // carry seed of a later loop that hhds has already validated, and its debug
    // hook re-validates that loop on every edge change -- with both drivers
    // present for a moment it would see two seeds and throw. delete_node drops
    // the old edges unhooked, so the one checked mutation leaves exactly one.
    struct Rewire {
      hhds::Pin_class driver;
      hhds::Pin_class sink;
    };
    std::vector<hhds::Edge_class> readers;
    for (const auto& e : sub.out_edges()) {
      if (e.sink.get_master_node() != sub) {
        readers.push_back(e);
      }
    }
    const absl::flat_hash_set<hhds::Port_id> par_out(par.carry_out.begin(), par.carry_out.end());
    std::vector<Rewire>                      rewires;
    rewires.reserve(readers.size());
    for (const auto& e : readers) {
      const auto pid   = e.driver.get_port_id();
      auto       owner = par_out.contains(pid) ? par_sub : ind_sub;
      auto       d     = owner.create_driver_pin(pid);
      gu::carry_pin_attrs(e.driver, d);
      rewires.push_back(Rewire{.driver = d, .sink = e.sink});
    }
    sub.del_node();
    for (const auto& r : rewires) {
      r.driver.connect_sink(r.sink);
    }
    state->split_bodies.push_back(body_gid);
    ++split;
  }
  return split;
}

namespace {

// `graphs` in callee-before-host order over the loop-body relation: a host
// snapshots its body into the halves, so the body's own repair and nested
// splits have to be done first. (A loop body cannot instantiate its host.)
std::vector<std::shared_ptr<hhds::Graph>> loop_body_order(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  absl::flat_hash_map<hhds::Gid, std::shared_ptr<hhds::Graph>> by_gid;
  for (const auto& g : graphs) {
    if (is_live(g.get())) {
      by_gid.emplace(g->get_gid(), g);
    }
  }
  std::vector<std::shared_ptr<hhds::Graph>> order;
  absl::flat_hash_set<hhds::Gid>            done;
  const auto                                visit = [&](auto& self, const std::shared_ptr<hhds::Graph>& g) -> void {
    if (!done.insert(g->get_gid()).second) {
      return;
    }
    for (auto n : g->body().nodes(hhds::Node_order::forward)) {
      if (n.is_invalid() || gu::type_op_of(n) != Ntype_op::Sub || !n.is_loop_subnode()) {
        continue;
      }
      if (auto it = by_gid.find(n.get_subnode_gid()); it != by_gid.end()) {
        self(self, it->second);
      }
    }
    order.push_back(g);
  };
  for (const auto& g : graphs) {
    if (is_live(g.get())) {
      visit(visit, g);
    }
  }
  return order;
}

}  // namespace

Legalize_result legalize_design(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, bool freeze_graphs) {
  Legalize_result out;
  Split_state     state;

  // 1. ACYCLIC REPAIR, over every graph first: a false combinational loop
  //    through a pure-comb Sub is broken by inlining that instance, so every
  //    consumer's scheduler can linearize the body. Intra-def wire
  //    self-references are already resolved upstream (split_packed_selfref_wire
  //    in lnast.tolg); what survives to here is only the cross-boundary shape,
  //    and this is a no-op when it is absent, which is the common case. Before
  //    any split, so a half is copied from a repaired body.
  for (const auto& g : graphs) {
    if (is_live(g.get())) {
      std::vector<std::string> inlined;
      (void)gu::flatten_false_loop_subs(g.get(), &inlined);
      auto attr = g->get_input_node().attr(livehd::attrs::legalize_inlined);
      attr.del();
      if (!inlined.empty()) {
        std::sort(inlined.begin(), inlined.end());
        inlined.erase(std::unique(inlined.begin(), inlined.end()), inlined.end());
        std::string encoded;
        for (const auto& name : inlined) {
          encoded += name;
          encoded.push_back('\n');
        }
        attr.set(std::move(encoded));
      }
    }
  }

  // 2. Loop split, callee-first.
  absl::flat_hash_set<hhds::GraphLibrary*> libs;
  for (const auto& g : loop_body_order(graphs)) {
    if (auto* lib = g->get_io()->get_library(); lib != nullptr) {
      (void)split_loops(g.get(), *lib, &state);
      libs.insert(lib);
    }
  }

  // 3. Drop a split body that nothing instantiates any more. Splitting removed
  //    the only instance it had, but the SAME def may be instantiated by another
  //    graph (one body, several loops) or by a half (a nested loop), so this
  //    counts across the whole design and only ever considers defs a split
  //    actually replaced -- never an ordinary unreferenced def, which may
  //    legitimately be a top.
  if (!state.split_bodies.empty()) {
    absl::flat_hash_set<hhds::Gid> still_used;
    const auto                     count_uses = [&](const std::shared_ptr<hhds::Graph>& g) {
      if (!is_live(g.get())) {
        return;
      }
      for (auto n : g->body().nodes(hhds::Node_order::forward)) {
        if (!n.is_invalid() && gu::type_op_of(n) == Ntype_op::Sub) {
          still_used.insert(n.get_subnode_gid());
        }
      }
    };
    for (const auto& g : graphs) {
      count_uses(g);
    }
    for (const auto& g : state.added) {
      count_uses(g);
    }
    for (const auto gid : state.split_bodies) {
      if (still_used.contains(gid)) {
        continue;
      }
      for (auto* lib : libs) {
        if (!lib->has_graph(gid)) {
          continue;
        }
        auto dead = lib->get_graph(gid);
        if (!dead) {
          continue;
        }
        // BOTH halves, the way pass.partition drops its scratch def: deleting
        // the body alone leaves the GraphIO declaration behind, and that is what
        // an emit/save walks. The caller drops the object from its own view.
        const std::string dead_name{dead->get_name()};
        out.removed.push_back(dead);
        lib->delete_graph(dead);
        lib->delete_graphio(dead_name);
        break;
      }
    }
  }
  out.added = state.added;
  out.removed.insert(out.removed.end(), state.removed.begin(), state.removed.end());

  // 4. Freeze AFTER every structural change, so the recorded digest is the
  //    shape downstream actually sees -- the halves included.
  if (freeze_graphs) {
    for (const auto& g : graphs) {
      freeze(g);
    }
    for (const auto& g : out.added) {
      freeze(g);
    }
  }
  return out;
}

int verify_design_frozen(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view who) {
  int moved = 0;
  for (const auto& g : graphs) {
    if (g && !verify_frozen(g.get(), who)) {
      ++moved;
    }
  }
  return moved;
}

}  // namespace livehd::legalize

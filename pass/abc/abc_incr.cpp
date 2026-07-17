// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "abc_incr.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "abc_map.hpp"  // Region_qor
#include "cell.hpp"
#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "node_util.hpp"
#include "rapidjson/document.h"

namespace livehd::abc {

namespace {

namespace gu = livehd::graph_util;

// ---------------------------------------------------------------------------
// Hash toolkit -- the semdiff canonical-digest primitives (semdiff.cpp keeps
// them file-private, and this digest must be free to evolve with the MAPPER's
// invalidation needs rather than the differ's, so the 15 lines are duplicated
// deliberately).
// ---------------------------------------------------------------------------
constexpr uint64_t mix64(uint64_t x) {
  x ^= x >> 33U;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33U;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33U;
  return x;
}
constexpr uint64_t hcombine(uint64_t h, uint64_t v) { return mix64(h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U))); }
uint64_t           hstr(std::string_view s) {
  uint64_t h = 1469598103934665603ULL;  // FNV-1a
  for (char c : s) {
    h ^= static_cast<unsigned char>(c);
    h *= 1099511628211ULL;
  }
  return h;
}

// Two independent lanes carried through the WHOLE computation, not just the
// final fold: a reused region with a colliding digest is a MISCOMPILE, so the
// 64-bit internal token stream that is fine for semdiff's diff verdicts is not
// the right margin here (cone_digest makes the same call, cone_abc.cpp:633).
struct Sig {
  uint64_t a = 0, b = 0;
  friend bool operator==(const Sig& x, const Sig& y) = default;
  friend auto operator<=>(const Sig& x, const Sig& y) = default;
};
constexpr uint64_t kLaneB = 0x9ae16a3b2f90404fULL;

Sig sig_seed(uint64_t tag) { return {mix64(tag), mix64(tag ^ kLaneB)}; }
Sig sig_comb(Sig h, Sig v) { return {hcombine(h.a, v.a), hcombine(h.b, mix64(v.b ^ kLaneB))}; }
Sig sig_u64(Sig h, uint64_t v) { return sig_comb(h, {v, v}); }
Sig sig_str(Sig h, std::string_view s) { return sig_comb(h, {hstr(s), mix64(hstr(s) ^ kLaneB)}); }

// Fold a port-grouped operand list, commutative-normalizing WITHIN each
// sink-port class (semdiff's fold_operands rule): `a+b == b+a` on Sum's added
// port, but an added operand never trades places with a subtracted one.
Sig fold_operands(Sig base, absl::flat_hash_map<int, std::vector<Sig>>& by_port) {
  std::vector<int> ports;
  ports.reserve(by_port.size());
  for (auto& [p, vs] : by_port) {
    (void)vs;
    ports.emplace_back(p);
  }
  std::sort(ports.begin(), ports.end());
  for (int p : ports) {
    auto& vs = by_port[p];
    std::sort(vs.begin(), vs.end());
    base = sig_u64(base, static_cast<uint64_t>(static_cast<uint32_t>(p)));
    for (const auto& v : vs) {
      base = sig_comb(base, v);
    }
  }
  return base;
}

[[nodiscard]] bool is_stateful(Ntype_op op) {
  return op == Ntype_op::Flop || op == Ntype_op::Latch || op == Ntype_op::Fflop || op == Ntype_op::Memory;
}

// Distinct driver pins a node actually presents to the region: the (pid, bits,
// sign) set, deduped and sorted. Read off out-edges plus nothing else -- an
// unused, unexported driver pin plays no part in what ABC maps.
Sig fold_driver_shape(const hhds::Node_class& n) {
  std::vector<std::tuple<uint32_t, int32_t, bool>> pins;
  for (const auto& e : n.out_edges()) {
    const auto pid = static_cast<uint32_t>(e.driver.get_port_id());
    bool       dup = false;
    for (const auto& [p, b, s] : pins) {
      (void)b;
      (void)s;
      if (p == pid) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      pins.emplace_back(pid, gu::bits_of(e.driver), !gu::is_unsign(e.driver));
    }
  }
  std::sort(pins.begin(), pins.end());
  Sig h = sig_seed(0x0d21f0e5);
  for (const auto& [p, b, s] : pins) {
    h = sig_u64(h, (static_cast<uint64_t>(p) << 33U) | (static_cast<uint64_t>(static_cast<uint32_t>(b)) << 1U)
                       | static_cast<uint64_t>(s));
  }
  return h;
}

// ---------------------------------------------------------------------------
// The digest
// ---------------------------------------------------------------------------
struct Digest_ctx {
  absl::flat_hash_set<hhds::Node_class> member;
  const char*                           why = "";

  struct Leaf {
    hhds::Pin_class pin;
    Sig             tok;                                     // refined across rounds
    std::vector<std::pair<hhds::Node_class, uint32_t>> use;  // in-region (consumer, sink pid)
  };
  std::vector<Leaf>                             leaves;
  absl::flat_hash_map<hhds::Pin_class, size_t>  leaf_of;
  absl::flat_hash_map<hhds::Node_class, Sig>    sig;   // current round
  absl::flat_hash_map<hhds::Node_class, Sig>    prev;  // previous round (feedback reads)
  bool                                          bad = false;
};

// The stable part of a node's identity: everything except its fanin.
Sig node_anchor(const hhds::Node_class& n, bool& bad) {
  const auto op = gu::type_op_of(n);
  Sig        h  = sig_u64(sig_seed(0x5e3d1ff0), static_cast<uint64_t>(op));
  h             = sig_comb(h, fold_driver_shape(n));
  if (op == Ntype_op::Sub) {
    auto io = n.get_subnode_io();
    if (!io) {
      bad = true;  // dangling instance: nothing stable to key on
      return h;
    }
    h = sig_str(h, io->get_name());
    if (gu::has_name(n)) {
      h = sig_str(h, gu::node_name_of(n));
    }
  } else if (is_stateful(op)) {
    // Read-back rebuilds a register under its ORIGINAL name and LEC pairs
    // flops by name, so the name is part of the mapped function. Anonymous
    // state has no identity an edit preserves: refuse (the semdiff rule).
    if (!gu::has_name(n)) {
      bad = true;
      return h;
    }
    h = sig_str(sig_u64(h, 0x57a7eULL), gu::node_name_of(n));
  }
  return h;
}

// One forward signature pass. rb.nodes span order is the src forward_class
// order: loop-breaks first, then topological -- so a comb node's in-region
// comb drivers are always already signed, and only feedback INTO a loop-break
// reads the previous round.
void sig_pass(const livehd::partition::Region_body& rb, Digest_ctx& cx, int round) {
  cx.prev = std::move(cx.sig);
  cx.sig.clear();
  for (const auto& n : rb.nodes) {
    bool bad    = false;
    Sig  anchor = node_anchor(n, bad);
    if (bad) {
      cx.bad = true;
      cx.why = "anonymous state or dangling instance";
      return;
    }
    const bool lb = n.is_loop_break();

    absl::flat_hash_map<int, std::vector<Sig>> by_port;
    for (const auto& e : n.inp_edges()) {
      const int pid = static_cast<int>(static_cast<uint32_t>(e.sink.get_port_id()));
      Sig       d;
      if (gu::is_const_pin(e.driver)) {
        d = sig_str(sig_seed(0xc0157ULL), gu::hydrate_const(e.driver).serialize());
      } else {
        auto dn = e.driver.get_master_node();
        if (cx.member.contains(dn)) {
          // Loop-break fanin may reach FORWARD in span order (a flop fed by
          // later comb); those read the previous round's map -- round 0 uses
          // the driver's anchor alone, refined on later rounds.
          const auto& m  = lb ? cx.prev : cx.sig;
          auto        it = m.find(dn);
          Sig         base;
          if (it != m.end()) {
            base = it->second;
          } else if (lb && round == 0) {
            bool b2 = false;
            base    = node_anchor(dn, b2);
            if (b2) {
              cx.bad = true;
              cx.why = "anonymous state or dangling instance";
              return;
            }
          } else if (auto it2 = cx.sig.find(dn); it2 != cx.sig.end()) {
            base = it2->second;
          } else {
            // A COMB node consuming a member that has no signature yet means
            // the span is not topological here -- a real combinational cycle
            // inside the region (possible after free window merges; the
            // bit-blast breaks it with const0 + a warning). An anchor-only
            // token would let two different cycles collide: refuse.
            cx.bad = true;
            cx.why = "combinational cycle inside the region";
            return;
          }
          d = sig_u64(base, static_cast<uint64_t>(static_cast<uint32_t>(e.driver.get_port_id())));
        } else {
          auto lit = cx.leaf_of.find(e.driver);
          if (lit == cx.leaf_of.end()) {
            cx.bad = true;  // walk/port desync: refuse rather than guess
            cx.why = "boundary walk desync";
            return;
          }
          d = cx.leaves[lit->second].tok;
        }
      }
      by_port[pid].emplace_back(d);
    }
    cx.sig[n] = fold_operands(anchor, by_port);
  }
}

}  // namespace

std::string Region_digest::hex() const { return std::format("{:016x}{:016x}", h0, h1); }

Region_digest region_digest(const livehd::partition::Region_body& rb, uint64_t opts_sig) {
  Region_digest out;
  Digest_ctx    cx;
  cx.member.reserve(rb.nodes.size());
  for (const auto& n : rb.nodes) {
    cx.member.insert(n);
  }

  // Discover boundary leaves and their in-region fanout.
  for (const auto& n : rb.nodes) {
    for (const auto& e : n.inp_edges()) {
      if (gu::is_const_pin(e.driver)) {
        continue;
      }
      auto dn = e.driver.get_master_node();
      if (cx.member.contains(dn)) {
        continue;
      }
      auto [it, fresh] = cx.leaf_of.try_emplace(e.driver, cx.leaves.size());
      if (fresh) {
        cx.leaves.emplace_back();
        cx.leaves.back().pin = e.driver;
      }
      cx.leaves[it->second].use.emplace_back(n, static_cast<uint32_t>(e.sink.get_port_id()));
    }
  }

  // Initial leaf tokens. A primary graph input is anchored by its DECLARED
  // name (stable across edits, and the same name the port inherits); any other
  // crossing pin starts as bare (bits, sign) and is told apart by how the
  // region uses it (the refinement below) -- never by wire name or nid, both
  // of which shift under an edit.
  for (auto& l : cx.leaves) {
    if (gu::is_graph_input_pin(l.pin)) {
      l.tok = sig_str(sig_seed(0x91a71ULL), gu::pin_name_of(l.pin));
    } else {
      l.tok = sig_seed(0xb0d17ULL);
    }
    l.tok = sig_u64(l.tok, (static_cast<uint64_t>(static_cast<uint32_t>(gu::bits_of(l.pin))) << 1U)
                               | static_cast<uint64_t>(!gu::is_unsign(l.pin)));
  }

  // Weisfeiler-Lehman rounds: sign the region, refine each leaf by the
  // signatures of its consumers, re-sign. Two leaves the refinement cannot
  // separate would make the cached-port stitch a guess -- handled below by
  // refusal, so the round count only trades cache coverage, never soundness.
  constexpr int kRounds = 3;
  for (int round = 0; round < kRounds; ++round) {
    sig_pass(rb, cx, round);
    if (cx.bad) {
      out.refused = cx.why;
      return out;
    }
    if (round + 1 == kRounds) {
      break;
    }
    bool all_distinct = true;
    {
      std::vector<Sig> toks;
      toks.reserve(cx.leaves.size());
      for (const auto& l : cx.leaves) {
        toks.emplace_back(l.tok);
      }
      std::sort(toks.begin(), toks.end());
      all_distinct = std::adjacent_find(toks.begin(), toks.end()) == toks.end();
    }
    if (all_distinct) {
      break;
    }
    for (auto& l : cx.leaves) {
      std::vector<Sig> us;
      us.reserve(l.use.size());
      for (const auto& [cn, pid] : l.use) {
        auto it = cx.sig.find(cn);
        us.emplace_back(sig_u64(it == cx.sig.end() ? Sig{} : it->second, pid));
      }
      std::sort(us.begin(), us.end());
      Sig t = sig_comb(sig_seed(0x3ef1eULL), l.tok);
      for (const auto& u : us) {
        t = sig_comb(t, u);
      }
      l.tok = t;
    }
  }

  // Boundary ranks. Every rank must name exactly one port and every port one
  // rank; anything else -- a duplicate token (symmetric inputs), a port whose
  // driver the walk never met -- is a refusal, not a guess.
  absl::flat_hash_map<hhds::Pin_class, size_t> port_of_pin;
  for (size_t j = 0; j < rb.inputs.size(); ++j) {
    port_of_pin[rb.inputs[j].src_driver] = j;
  }
  if (port_of_pin.size() != rb.inputs.size() || cx.leaves.size() != rb.inputs.size()) {
    out.refused = "input ports do not match the crossing-pin walk";
    return out;
  }
  {
    std::vector<std::pair<Sig, size_t>> order;  // (token, leaf idx)
    order.reserve(cx.leaves.size());
    for (size_t i = 0; i < cx.leaves.size(); ++i) {
      order.emplace_back(cx.leaves[i].tok, i);
    }
    std::sort(order.begin(), order.end());
    for (size_t i = 1; i < order.size(); ++i) {
      if (order[i].first == order[i - 1].first) {
        out.refused = "ambiguous boundary: two inputs the region uses identically";
        return out;
      }
    }
    out.in_by_rank.reserve(order.size());
    for (const auto& [tok, li] : order) {
      (void)tok;
      auto it = port_of_pin.find(cx.leaves[li].pin);
      if (it == port_of_pin.end()) {
        out.in_by_rank.clear();
        out.refused = "input ports do not match the crossing-pin walk";
        return out;
      }
      out.in_by_rank.emplace_back(it->second);
    }
  }

  std::vector<std::pair<Sig, size_t>> outs;  // (token, output port idx)
  outs.reserve(rb.outputs.size());
  for (size_t j = 0; j < rb.outputs.size(); ++j) {
    const auto& p  = rb.outputs[j];
    auto        dn = p.src_driver.get_master_node();
    auto        it = cx.sig.find(dn);
    if (it == cx.sig.end()) {
      out.refused = "output port exports a non-member driver";
      return out;
    }
    Sig t = sig_comb(sig_seed(0x0f0e1ULL), it->second);
    t     = sig_u64(t, static_cast<uint64_t>(static_cast<uint32_t>(p.src_driver.get_port_id())));
    t     = sig_u64(t, (static_cast<uint64_t>(static_cast<uint32_t>(p.bits)) << 1U) | static_cast<uint64_t>(p.sign));
    outs.emplace_back(t, j);
  }
  std::sort(outs.begin(), outs.end());
  for (size_t i = 1; i < outs.size(); ++i) {
    if (outs[i].first == outs[i - 1].first) {
      out.refused = "ambiguous boundary: two outputs with identical cones";
      return out;
    }
  }
  out.out_by_rank.reserve(outs.size());
  for (const auto& [tok, j] : outs) {
    (void)tok;
    out.out_by_rank.emplace_back(j);
  }

  // Final fold: sorted token multisets, so neither nid allocation nor walk
  // order can leak in.
  std::vector<Sig> node_toks;
  node_toks.reserve(rb.nodes.size());
  for (const auto& n : rb.nodes) {
    node_toks.emplace_back(cx.sig.at(n));
  }
  std::sort(node_toks.begin(), node_toks.end());

  Sig h = sig_u64(sig_seed(0xabc1ULL), opts_sig);
  h     = sig_u64(h, rb.nodes.size());
  h     = sig_u64(h, cx.leaves.size());
  h     = sig_u64(h, rb.outputs.size());
  for (const auto& t : node_toks) {
    h = sig_comb(h, t);
  }
  {
    std::vector<Sig> ltoks;
    ltoks.reserve(cx.leaves.size());
    for (const auto& l : cx.leaves) {
      ltoks.emplace_back(l.tok);
    }
    std::sort(ltoks.begin(), ltoks.end());
    for (const auto& t : ltoks) {
      h = sig_comb(h, t);
    }
  }
  for (const auto& [t, j] : outs) {
    (void)j;
    h = sig_comb(h, t);
  }

  out.h0    = h.a;
  out.h1    = h.b;
  out.valid = true;
  return out;
}

// ---------------------------------------------------------------------------
// Cross-library flat-body clone
// ---------------------------------------------------------------------------
namespace {

// find-or-clone a child GraphIO DECL into `lib`, opaque. Unlike partition's
// resolve_or_clone_subdef this never refuses a bodied def: into the CACHE
// library a child design instance stays an IO-only shell on purpose (the cache
// stores region netlists, not the hierarchy), and on the way back out find_io
// binds the instance to the real, already-partitioned def by name.
std::shared_ptr<hhds::GraphIO> decl_into(hhds::GraphLibrary* lib, const std::shared_ptr<hhds::GraphIO>& child) {
  if (!child) {
    return nullptr;
  }
  if (auto have = lib->find_io(child->get_name())) {
    return have;
  }
  auto io = lib->create_io(std::string{child->get_name()});
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

// Clone `src`'s flat body (a mapped region: 1-bit cell Subs, flops, mask glue,
// consts) into `dst`, whose IO pins are already declared and materialized.
// `in_map`/`out_map` rename src IO port names to dst ones. Returns false on
// any shape it cannot carry -- the caller falls back to a normal ABC run.
bool clone_flat_body(hhds::Graph* src, hhds::Graph* dst, hhds::GraphLibrary* dst_lib,
                     const absl::flat_hash_map<std::string, std::string>& in_map,
                     const absl::flat_hash_map<std::string, std::string>& out_map) {
  absl::flat_hash_map<hhds::Node_class, hhds::Node_class> node_map;

  for (auto n : src->fast_class()) {
    if (n.is_invalid() || gu::is_builtin_node(n)) {
      continue;
    }
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::IO || op == Ntype_op::Invalid || op == Ntype_op::Nconst) {
      continue;  // IO maps through the port renames; consts are recreated per edge
    }
    auto neo = gu::create_typed_node(*dst, op);
    if (op == Ntype_op::Sub) {
      auto io = decl_into(dst_lib, n.get_subnode_io());
      if (!io) {
        return false;
      }
      neo.set_subnode(io);
    }
    if (gu::has_name(n)) {
      neo.attr(hhds::attrs::name).set(std::string{gu::node_name_of(n)});
    }
    if (auto a = n.attr(livehd::attrs::lut); a.has()) {
      neo.attr(livehd::attrs::lut).set(std::string{a.get()});
    }
    if (auto a = n.attr(hhds::attrs::srcid); a.has() && a.get() != 0) {
      neo.attr(hhds::attrs::srcid).set(dst->source_locator().import_from(src->source_locator(), a.get()));
    }
    node_map[n] = neo;
  }

  auto gio = src->get_io();
  if (!gio) {
    return false;
  }

  // Renamed graph-output pins, pre-resolved (a region can export hundreds of
  // ports; a per-edge decl scan would be quadratic).
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class> out_pin_map;
  for (const auto& d : gio->get_output_pin_decls()) {
    auto op = src->get_output_pin(d.name);
    if (op.is_invalid()) {
      continue;
    }
    auto it = out_map.find(d.name);
    if (it == out_map.end()) {
      return false;
    }
    out_pin_map[op] = dst->get_output_pin(it->second);
  }

  // A cloned sink: a mapped node's own sink pin, or a renamed graph output.
  bool sink_fail = false;
  auto sink_of   = [&](const hhds::Pin_class& s) -> hhds::Pin_class {
    auto sn = s.get_master_node();
    if (auto it = node_map.find(sn); it != node_map.end()) {
      return it->second.create_sink_pin(s.get_port_id());
    }
    if (auto it = out_pin_map.find(s); it != out_pin_map.end()) {
      return it->second;
    }
    sink_fail = true;
    return {};
  };

  // Wire DRIVER-major, replaying each driver pin's stored out-edge order. Edge
  // creation order is what cgen's statement order (and so the emitted Verilog
  // bytes) follows, and a driver's out-edge list preserves the mapper's
  // original insertion sequence -- so a warm clone reproduces the cold
  // mapping's bytes exactly. (Sink-side order is a non-issue: a mapped netlist
  // has single-driver sinks.) Graph inputs first, in declaration order, then
  // nodes in fast_class (creation) order.
  auto wire_out_edges = [&](const hhds::Pin_class& d, const hhds::Pin_class& nd) -> bool {
    for (const auto& e : d.get_master_node().out_edges()) {
      if (!(e.driver == d)) {
        continue;  // another pin of the same node: its own pass handles it
      }
      auto sp = sink_of(e.sink);
      if (sp.is_invalid() || sink_fail) {
        return false;
      }
      nd.connect_sink(sp);
    }
    return true;
  };

  for (const auto& din : gio->get_input_pin_decls()) {
    auto ip = src->get_input_pin(din.name);
    if (ip.is_invalid()) {
      continue;
    }
    auto it = in_map.find(din.name);
    if (it == in_map.end()) {
      return false;
    }
    if (!wire_out_edges(ip, dst->get_input_pin(it->second))) {
      return false;
    }
  }

  for (auto orig : src->fast_class()) {
    auto mit = node_map.find(orig);
    if (mit == node_map.end()) {
      continue;
    }
    // Distinct driver pins in first-out-edge order, then replay each one.
    std::vector<hhds::Pin_class> dpins;
    for (const auto& e : orig.out_edges()) {
      if (std::find(dpins.begin(), dpins.end(), e.driver) == dpins.end()) {
        dpins.emplace_back(e.driver);
      }
    }
    for (const auto& d : dpins) {
      auto np = mit->second.create_driver_pin(d.get_port_id());
      if (auto b = gu::bits_of(d); b != 0) {
        gu::set_bits(np, b);
      }
      if (!gu::is_unsign(d)) {
        gu::set_sign(np);
      }
      if (auto a = d.attr(livehd::attrs::pin_name); a.has()) {
        gu::set_pin_name(np, std::string{a.get()});
      }
      if (auto o = d.attr(livehd::attrs::pin_offset); o.has()) {
        np.attr(livehd::attrs::pin_offset).set(o.get());
      }
      if (!wire_out_edges(d, np)) {
        return false;
      }
    }
  }

  // Constant fan-in last: consts emit no statements, so their edge order
  // cannot move a line -- but every const-driven sink must still exist.
  for (auto orig : src->fast_class()) {
    auto mit = node_map.find(orig);
    if (mit == node_map.end()) {
      continue;
    }
    for (const auto& e : orig.inp_edges()) {
      if (gu::is_const_pin(e.driver)) {
        gu::create_const(*dst, gu::hydrate_const(e.driver)).connect_sink(mit->second.create_sink_pin(e.sink.get_port_id()));
      }
    }
  }
  for (const auto& d : gio->get_output_pin_decls()) {
    auto opin = src->get_output_pin(d.name);
    if (opin.is_invalid()) {
      continue;  // declared but undriven: mirror it
    }
    for (const auto& e : opin.inp_edges()) {
      if (gu::is_const_pin(e.driver)) {
        auto it = out_map.find(d.name);
        if (it == out_map.end()) {
          return false;
        }
        gu::create_const(*dst, gu::hydrate_const(e.driver)).connect_sink(dst->get_output_pin(it->second));
      }
    }
  }

  // Declared-but-edgeless Sub outputs: readers probe every declared output pin
  // and hhds asserts on one that was never materialized (the partition /
  // flatten rule). fast_class order for the same determinism reason as above.
  for (auto orig : src->fast_class()) {
    auto mit = node_map.find(orig);
    if (mit == node_map.end() || gu::type_op_of(orig) != Ntype_op::Sub) {
      continue;
    }
    auto neo = mit->second;
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
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Incr_cache
// ---------------------------------------------------------------------------

Incr_cache::Incr_cache(std::string dir, uint64_t salt) : dir_(std::move(dir)), salt_(salt) {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);

  std::ifstream in(dir_ + "/abc_cache.json", std::ios::binary);
  if (!in) {
    return;
  }
  std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  rapidjson::Document doc;
  doc.Parse(body.data(), body.size());
  // A corrupt, old-schema or wrong-salt file starts the cache cold -- it is
  // only ever a speedup, never a source of truth.
  if (doc.HasParseError() || !doc.IsObject()) {
    return;
  }
  if (auto s = doc.FindMember("schema"); s == doc.MemberEnd() || !s->value.IsInt() || s->value.GetInt() != 1) {
    return;
  }
  const std::string want = std::format("{:016x}", salt_);
  if (auto s = doc.FindMember("salt"); s == doc.MemberEnd() || !s->value.IsString() || want != s->value.GetString()) {
    return;
  }
  auto r = doc.FindMember("regions");
  if (r == doc.MemberEnd() || !r->value.IsObject()) {
    return;
  }
  for (auto it = r->value.MemberBegin(); it != r->value.MemberEnd(); ++it) {
    if (!it->value.IsObject()) {
      continue;
    }
    Row         row;
    const auto& v = it->value;
    auto        gets = [&](const char* k) -> std::string {
      auto m = v.FindMember(k);
      return m != v.MemberEnd() && m->value.IsString() ? m->value.GetString() : std::string{};
    };
    row.module = gets("module");
    if (auto m = v.FindMember("in"); m != v.MemberEnd() && m->value.IsArray()) {
      for (const auto& e : m->value.GetArray()) {
        if (e.IsString()) {
          row.in.emplace_back(e.GetString());
        }
      }
    }
    if (auto m = v.FindMember("out"); m != v.MemberEnd() && m->value.IsArray()) {
      for (const auto& e : m->value.GetArray()) {
        if (e.IsString()) {
          row.out.emplace_back(e.GetString());
        }
      }
    }
    if (auto m = v.FindMember("gates"); m != v.MemberEnd() && m->value.IsInt()) {
      row.gates = m->value.GetInt();
    }
    if (auto m = v.FindMember("area"); m != v.MemberEnd() && m->value.IsNumber()) {
      row.area = m->value.GetDouble();
    }
    if (auto m = v.FindMember("delay"); m != v.MemberEnd() && m->value.IsNumber()) {
      row.delay = static_cast<float>(m->value.GetDouble());
    }
    if (auto m = v.FindMember("crit_out_rank"); m != v.MemberEnd() && m->value.IsInt()) {
      row.crit_out_rank = m->value.GetInt();
    }
    row.crit_src = gets("crit_src");
    if (auto m = v.FindMember("div_blackbox"); m != v.MemberEnd() && m->value.IsInt()) {
      row.div_blackbox = m->value.GetInt();
    }
    if (!row.module.empty()) {
      rows_.emplace(it->name.GetString(), std::move(row));
    }
  }
}

hhds::GraphLibrary& Incr_cache::lib() { return livehd::Hhds_graph_library::instance(dir_); }

const Incr_cache::Row* Incr_cache::lookup(const Region_digest& d) const {
  if (!d.valid) {
    return nullptr;
  }
  auto it = rows_.find(d.hex());
  return it == rows_.end() ? nullptr : &it->second;
}

bool Incr_cache::reuse(const livehd::partition::Region_body& rb, const Region_digest& d, const Row& row,
                       hhds::GraphLibrary* outlib) {
  if (row.in.size() != rb.inputs.size() || row.out.size() != rb.outputs.size() || d.in_by_rank.size() != rb.inputs.size()
      || d.out_by_rank.size() != rb.outputs.size()) {
    return false;
  }
  auto gio = lib().find_io(row.module);
  if (!gio) {
    return false;
  }
  auto cached = gio->get_graph();
  if (!cached) {
    return false;
  }

  // Canonical rank r pairs the cached run's port row.in[r] with THIS run's port
  // rb.inputs[in_by_rank[r]] -- names and port order shift with nids across an
  // edit, the rank is a pure function of the region content both runs share.
  absl::flat_hash_map<std::string, std::string> in_map, out_map;
  for (size_t r = 0; r < row.in.size(); ++r) {
    in_map[row.in[r]] = rb.inputs[d.in_by_rank[r]].name;
  }
  for (size_t r = 0; r < row.out.size(); ++r) {
    out_map[row.out[r]] = rb.outputs[d.out_by_rank[r]].name;
  }
  if (in_map.size() != rb.inputs.size() || out_map.size() != rb.outputs.size()) {
    return false;  // duplicate cached names: malformed row
  }

  const bool ok = clone_flat_body(cached.get(), rb.body, outlib, in_map, out_map);
  if (ok) {
    ++hits_;
  }
  return ok;
}

void Incr_cache::store(const livehd::partition::Region_body& rb, const Region_digest& d, const Region_qor& q) {
  if (!d.valid) {
    return;
  }
  const std::string key  = d.hex();
  const std::string name = "r_" + key;
  auto&             l    = lib();

  // A body may linger from a run under an older salt (rows dropped, library
  // kept). Same digest scheme or not, replace it wholesale.
  if (auto stale = l.find_io(name)) {
    l.delete_graph(stale->get_gid());
    l.delete_graphio(name);
  }

  auto src_gio = rb.body->get_io();
  if (!src_gio) {
    return;
  }
  auto gio = l.create_io(name);
  for (const auto& p : src_gio->get_input_pin_decls()) {
    gio->add_input(p.name, p.port_id, p.loop_break);
    if (p.bits != 0) {
      gio->set_bits(p.name, p.bits);
    }
    gio->set_unsign(p.name, p.unsign);
  }
  for (const auto& p : src_gio->get_output_pin_decls()) {
    gio->add_output(p.name, p.port_id, p.loop_break);
    if (p.bits != 0) {
      gio->set_bits(p.name, p.bits);
    }
    gio->set_unsign(p.name, p.unsign);
  }
  auto g = gio->create_graph();
  if (!g) {
    return;
  }

  absl::flat_hash_map<std::string, std::string> ident_in, ident_out;
  for (const auto& p : rb.inputs) {
    ident_in[p.name] = p.name;
  }
  for (const auto& p : rb.outputs) {
    ident_out[p.name] = p.name;
  }
  if (!clone_flat_body(rb.body, g.get(), &l, ident_in, ident_out)) {
    livehd::diag::warn("pass.abc", "cache-store", "io")
        .msg("pass.abc: could not snapshot region '{}' into the cache (mapped normally, not cached)", rb.module_name)
        .emit();
    l.delete_graph(gio->get_gid());
    l.delete_graphio(name);
    return;
  }
  g->commit();

  Row row;
  row.module = name;
  row.in.resize(d.in_by_rank.size());
  for (size_t r = 0; r < d.in_by_rank.size(); ++r) {
    row.in[r] = rb.inputs[d.in_by_rank[r]].name;
  }
  row.out.resize(d.out_by_rank.size());
  for (size_t r = 0; r < d.out_by_rank.size(); ++r) {
    row.out[r] = rb.outputs[d.out_by_rank[r]].name;
  }
  row.gates        = q.gates;
  row.area         = q.area;
  row.delay        = q.delay;
  row.crit_src     = q.crit_src;
  row.div_blackbox = q.div_blackbox;
  row.crit_out_rank = -1;
  if (!q.crit_output.empty()) {
    for (size_t r = 0; r < d.out_by_rank.size(); ++r) {
      if (rb.outputs[d.out_by_rank[r]].name == q.crit_output) {
        row.crit_out_rank = static_cast<int>(r);
        break;
      }
    }
  }

  rows_[key] = std::move(row);
  dirty_     = true;
  ++stores_;
}

void Incr_cache::save() {
  if (!dirty_) {
    return;
  }
  // Deterministic emission (sorted keys) + atomic tmp+rename: the
  // formal_cache.json discipline.
  std::vector<const std::string*> keys;
  keys.reserve(rows_.size());
  for (const auto& [k, v] : rows_) {
    (void)v;
    keys.emplace_back(&k);
  }
  std::sort(keys.begin(), keys.end(), [](const auto* a, const auto* b) { return *a < *b; });

  auto jesc = [](std::string_view s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
      if (c == '"' || c == '\\') {
        o += '\\';
      }
      o += c;
    }
    return o;
  };

  std::string out = std::format("{{\"schema\":1,\"salt\":\"{:016x}\",\"regions\":{{", salt_);
  bool        first = true;
  for (const auto* k : keys) {
    const auto& r = rows_.at(*k);
    if (!first) {
      out += ",";
    }
    first = false;
    out += std::format("\"{}\":{{\"module\":\"{}\",\"in\":[", *k, jesc(r.module));
    for (size_t i = 0; i < r.in.size(); ++i) {
      out += std::format("{}\"{}\"", i != 0 ? "," : "", jesc(r.in[i]));
    }
    out += "],\"out\":[";
    for (size_t i = 0; i < r.out.size(); ++i) {
      out += std::format("{}\"{}\"", i != 0 ? "," : "", jesc(r.out[i]));
    }
    out += std::format("],\"gates\":{},\"area\":{},\"delay\":{},\"crit_out_rank\":{},\"crit_src\":\"{}\",\"div_blackbox\":{}}}",
                       r.gates,
                       r.area,
                       r.delay,
                       r.crit_out_rank,
                       jesc(r.crit_src),
                       r.div_blackbox);
  }
  out += "}}";

  const std::string path = dir_ + "/abc_cache.json";
  const std::string tmp  = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
      return;
    }
    f << out;
  }
  std::rename(tmp.c_str(), path.c_str());

  livehd::Hhds_graph_library::save(dir_);
}

uint64_t incr_opts_hash(std::string_view comb_flow, std::string_view seq_flow, int adder, int block_size, int multiplier) {
  uint64_t h = hstr(comb_flow);
  h          = hcombine(h, hstr(seq_flow));
  h          = hcombine(h, static_cast<uint64_t>(static_cast<uint32_t>(adder)));
  h          = hcombine(h, static_cast<uint64_t>(static_cast<uint32_t>(block_size)));
  h          = hcombine(h, static_cast<uint64_t>(static_cast<uint32_t>(multiplier)));
  return h;
}

uint64_t Incr_cache::make_salt(std::string_view library_path, bool map_register, bool map_memory, std::string_view dff_cell,
                               bool use_proven_assume, bool use_all_assume) {
  // Bump the schema tag whenever the mapper's read-back or the digest changes
  // shape: stale bodies must never survive a semantic change.
  uint64_t h = hstr("abc-incr-v1");
  std::ifstream f{std::string{library_path}, std::ios::binary};
  if (f) {
    // Salt the liberty CONTENT: the same path with edited cells is a
    // different mapping target.
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    h = hcombine(h, hstr(bytes));
  } else {
    h = hcombine(h, hstr(library_path));
  }
  h = hcombine(h, static_cast<uint64_t>(map_register) << 1U | static_cast<uint64_t>(map_memory));
  h = hcombine(h, hstr(dff_cell));
  h = hcombine(h, static_cast<uint64_t>(use_proven_assume) << 1U | static_cast<uint64_t>(use_all_assume));
  return h;
}

}  // namespace livehd::abc

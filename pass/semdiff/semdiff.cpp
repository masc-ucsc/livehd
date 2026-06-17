// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "semdiff.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "node_util.hpp"

namespace livehd::semdiff {

namespace gu = livehd::graph_util;

namespace {

// ---- hashing ---------------------------------------------------------------
// A self-contained 64-bit mixer (no external dep). Signatures are compared by
// value equality across the two designs, hashed in one process, so the mix only
// needs to be deterministic within the run.
constexpr uint64_t mix64(uint64_t x) {
  x ^= x >> 33U;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33U;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33U;
  return x;
}
constexpr uint64_t hcombine(uint64_t h, uint64_t v) {
  return mix64(h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U)));
}
uint64_t hstr(std::string_view s) {
  uint64_t h = 1469598103934665603ULL;  // FNV-1a
  for (char c : s) {
    h ^= static_cast<unsigned char>(c);
    h *= 1099511628211ULL;
  }
  return h;
}

// State cells are cut points (their data input is not followed): structure does
// not flow through them unless matching_names seeds them by hierarchical name.
bool is_state(Ntype_op op) {
  return op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch || op == Ntype_op::Memory;
}

// Name-normalization mirror of pass/lec/encode.cpp's flop_state_key (kept local
// so pass/semdiff stays free of the cvc5-heavy pass/lec dep): strip the SSA /
// hierarchical decoration so the same RTL register matches across front-ends.
std::string normalize_reg_name(std::string_view raw) {
  std::string_view s = raw;
  if (!s.empty() && s.front() == '$') {
    if (auto e = s.find('$', 1); e != std::string_view::npos) {
      s.remove_prefix(e + 1);
    }
  }
  if (auto p = s.find("___ssa_"); p != std::string_view::npos) {
    s = s.substr(0, p);
  }
  return std::string{s};
}

std::string state_key(hhds::Graph* g, const hhds::Node_class& node) {
  auto pn = gu::pin_name_of(node.get_driver_pin(0));
  if (pn.empty()) {
    pn = gu::node_name_of(node);
  }
  if (auto nm = normalize_reg_name(pn); !nm.empty()) {
    return "n:" + nm;
  }
  auto ref = node.attr(hhds::attrs::srcid);
  if (ref.has()) {
    auto span = g->source_locator().resolve_span(ref.get());
    if (!span.file.empty()) {
      return std::format("s:{}-{}@{}", span.start_byte.value_or(0), span.end_byte.value_or(0), span.file);
    }
  }
  return "f:" + std::to_string(static_cast<uint64_t>(node.get_debug_nid()));
}

// Width of a node's first sized driver pin (0 = unknown). Folded into the key so
// differing widths never falsely match (unsigned bits = magnitude+1 trap).
int32_t node_out_bits(const hhds::Node_class& node) {
  for (const auto& e : node.out_edges()) {
    if (auto b = gu::bits_of(e.driver); b != 0) {
      return b;
    }
  }
  return 0;
}

// op + width + subnode identity: the local "kind" part of a node's key, shared
// by the forward and backward signatures (a Sub also folds in its def gid so two
// instances of different modules never match).
uint64_t node_kind_key(const hhds::Node_class& node) {
  uint64_t h = hcombine(0x5e3d1ff0ULL, static_cast<uint64_t>(gu::type_op_of(node)));
  h          = hcombine(h, static_cast<uint64_t>(static_cast<uint32_t>(node_out_bits(node))));
  if (auto gid = node.get_subnode_gid(); gid != hhds::Gid_invalid) {
    h = hcombine(h, static_cast<uint64_t>(gid));
  }
  return h;
}

// Fold a port-grouped operand list into a signature: commutative-normalize
// WITHIN each sink-port class (sort the operand sigs that share a port) but
// never across ports — so Sum's added (port A) and subtracted (port B) operands
// stay distinct while `a+b == b+a`.
uint64_t fold_operands(uint64_t base, absl::flat_hash_map<int, std::vector<uint64_t>>& by_port) {
  std::vector<int> ports;
  ports.reserve(by_port.size());
  for (auto& [p, _] : by_port) {
    ports.push_back(p);
  }
  std::sort(ports.begin(), ports.end());
  uint64_t h = base;
  for (int p : ports) {
    h        = hcombine(h, static_cast<uint64_t>(static_cast<uint32_t>(p)) | (1ULL << 40U));  // port marker
    auto& vs = by_port[p];
    std::sort(vs.begin(), vs.end());
    for (uint64_t v : vs) {
      h = hcombine(h, v);
    }
  }
  return h;
}

// Per-side analysis: forward/backward signatures + node order.
struct Side {
  hhds::Graph*                                      g = nullptr;
  std::vector<hhds::Node_class>                     order;   // forward_class order
  absl::flat_hash_map<hhds::Class_index, uint64_t>  fsig;    // forward signature
  absl::flat_hash_map<hhds::Class_index, uint64_t>  bsig;    // backward signature
  absl::flat_hash_set<uint64_t>                     fvals;   // fsig value set
  absl::flat_hash_set<uint64_t>                     bvals;   // bsig value set
};

Side analyze(hhds::Graph* g, const Semdiff_options& opts) {
  Side s;
  s.g = g;

  // Seed state cells (cut points). With matching_names they get a cross-side
  // identity by hierarchical name so structure flows through them in BOTH
  // directions; without it they stay frontiers (a renamed flop => a gap).
  if (opts.matching_names) {
    for (auto node : g->forward_class()) {
      if (is_state(gu::type_op_of(node))) {
        uint64_t seed             = hcombine(hstr("\x01state"), hstr(state_key(g, node)));
        s.fsig[node.get_class_index()] = seed;
        s.bsig[node.get_class_index()] = seed;
      }
    }
  }

  // ---- Forward pass: inputs/consts -> outputs (topological). A node is ready
  // when every fanin signal already has a forward signature.
  for (auto node : g->forward_class()) {
    s.order.push_back(node);
    auto ci = node.get_class_index();
    auto op = gu::type_op_of(node);

    if (s.fsig.contains(ci)) {  // pre-seeded state cell
      s.fvals.insert(s.fsig[ci]);
      continue;
    }
    if (op == Ntype_op::Nconst) {
      uint64_t h = hcombine(hstr("\x01const"), hstr(gu::const_value_of(node)));
      h          = hcombine(h, node_kind_key(node));
      s.fsig[ci] = h;
      s.fvals.insert(h);
      continue;
    }
    if (is_state(op)) {
      continue;  // unseeded state cell => forward frontier
    }

    bool                                       ready = true;
    absl::flat_hash_map<int, std::vector<uint64_t>> by_port;
    for (const auto& e : node.inp_edges()) {
      const auto& drv = e.driver;
      uint64_t    dsig;
      if (gu::is_graph_input_pin(drv)) {
        dsig = hcombine(hstr("\x01in"), hstr(drv.get_pin_name()));
      } else if (gu::is_const_pin(drv)) {
        // A constant operand (incl. the pid-encoded const that drives e.g. a
        // get_mask's mask) is anchored by value — its CONST_NODE is not a
        // forward_class node, so resolve it here or forward would stall and the
        // node would fall back to a coarser backward (symmetric) match.
        dsig = hcombine(hstr("\x01const"), hstr(gu::hydrate_const(drv).serialize()));
      } else {
        auto it = s.fsig.find(drv.get_master_node().get_class_index());
        if (it == s.fsig.end()) {
          ready = false;
          break;
        }
        dsig = hcombine(it->second, static_cast<uint64_t>(static_cast<uint32_t>(drv.get_port_id())));
      }
      by_port[e.sink.get_port_id()].push_back(dsig);
    }
    if (!ready) {
      continue;  // past a frontier => no forward signature
    }
    uint64_t h = fold_operands(node_kind_key(node), by_port);
    s.fsig[ci] = h;
    s.fvals.insert(h);
  }

  // ---- Backward pass: outputs -> inputs (reverse topological). A node is ready
  // when every fanout signal already has a backward signature.
  for (auto it = s.order.rbegin(); it != s.order.rend(); ++it) {
    auto node = *it;
    auto ci   = node.get_class_index();
    auto op   = gu::type_op_of(node);

    if (s.bsig.contains(ci)) {  // pre-seeded state cell
      s.bvals.insert(s.bsig[ci]);
      continue;
    }
    if (is_state(op)) {
      continue;  // unseeded state cell => backward frontier
    }

    bool                                       ready = true;
    bool                                       any   = false;
    absl::flat_hash_map<int, std::vector<uint64_t>> by_port;
    for (const auto& e : node.out_edges()) {
      any = true;
      const auto& snk = e.sink;
      uint64_t    usig;
      if (gu::is_graph_output_pin(snk)) {
        usig = hcombine(hstr("\x01out"), hstr(snk.get_pin_name()));
      } else {
        auto it2 = s.bsig.find(snk.get_master_node().get_class_index());
        if (it2 == s.bsig.end()) {
          ready = false;
          break;
        }
        usig = hcombine(it2->second, static_cast<uint64_t>(static_cast<uint32_t>(snk.get_port_id())));
      }
      by_port[snk.get_port_id()].push_back(usig);
    }
    if (!ready || !any) {
      continue;  // a dead node or one past a frontier has no backward signature
    }
    uint64_t h = fold_operands(node_kind_key(node), by_port);
    s.bsig[ci] = h;
    s.bvals.insert(h);
  }

  return s;
}

// The match class of a node: the (tag, value) pair that keys its cross-side id,
// so corresponding nodes — and a structurally identical symmetric set — collapse
// onto one id.
//
// Forward is AUTHORITATIVE: when forward-from-inputs reached a node (it has a
// forward signature) it has decided the node's identity from its input cone. If
// that signature has a counterpart on the other side it is a forward match;
// otherwise the node is genuinely distinct and backward must NOT rescue it (a
// backward symmetric group would otherwise re-merge two inputs forward just told
// apart, hiding the difference). Backward only fills in nodes PAST the forward
// frontier — the ones forward never reached (no forward signature).
using Classkey = std::pair<char, uint64_t>;

std::optional<Classkey> class_of(const Side& side, hhds::Class_index ci, const absl::flat_hash_set<uint64_t>& fcommon,
                                 const absl::flat_hash_set<uint64_t>& bcommon) {
  if (auto it = side.fsig.find(ci); it != side.fsig.end()) {
    return fcommon.contains(it->second) ? std::optional<Classkey>{Classkey{'F', it->second}} : std::nullopt;
  }
  if (auto it = side.bsig.find(ci); it != side.bsig.end() && bcommon.contains(it->second)) {
    return Classkey{'B', it->second};
  }
  return std::nullopt;
}

// Stamp `id` on the node and each of its distinct driver (output) pins.
void stamp(const hhds::Node_class& node, uint32_t id) {
  gu::set_match(node, id);
  absl::flat_hash_set<int> seen;
  for (const auto& e : node.out_edges()) {
    if (seen.insert(e.driver.get_port_id()).second) {
      gu::set_match(e.driver, id);
    }
  }
}

}  // namespace

Match_result structural_match(hhds::Graph* a, hhds::Graph* b, const Semdiff_options& opts) {
  Match_result res;
  if (a == nullptr || b == nullptr) {
    return res;
  }

  Side sa = analyze(a, opts);
  Side sb = analyze(b, opts);

  // Signatures present on BOTH sides are matchable.
  absl::flat_hash_set<uint64_t> fcommon;
  for (uint64_t v : sa.fvals) {
    if (sb.fvals.contains(v)) {
      fcommon.insert(v);
    }
  }
  absl::flat_hash_set<uint64_t> bcommon;
  for (uint64_t v : sa.bvals) {
    if (sb.bvals.contains(v)) {
      bcommon.insert(v);
    }
  }

  // Assign ids in a's forward_class order (deterministic); b reuses the map, so
  // a class shared across the two graphs lands on the same id on both sides.
  absl::flat_hash_map<Classkey, uint32_t> class2id;
  uint32_t                                next_id = 1;
  auto assign = [&](Side& side, uint32_t& matched, uint32_t& unmatched) {
    for (const auto& node : side.order) {
      auto     ck = class_of(side, node.get_class_index(), fcommon, bcommon);
      uint32_t id = 0;
      if (ck) {
        auto [it, inserted] = class2id.try_emplace(*ck, next_id);
        if (inserted) {
          ++next_id;
        }
        id = it->second;
        ++matched;
      } else {
        ++unmatched;
      }
      stamp(node, id);
    }
  };
  assign(sa, res.a_matched, res.a_unmatched);
  assign(sb, res.b_matched, res.b_unmatched);
  res.regions = next_id - 1;

  // id_granularity=region: union ids that are adjacent through a matched edge in
  // `a`, then renumber. Ids are shared across sides, so the remap applies to b
  // too — re-stamp both graphs by reading back the pair id.
  if (opts.id_granularity == "region" && res.regions > 0) {
    std::vector<uint32_t> uf(res.regions + 1);
    for (uint32_t i = 0; i <= res.regions; ++i) {
      uf[i] = i;
    }
    std::function<uint32_t(uint32_t)> find = [&](uint32_t x) {
      while (uf[x] != x) {
        uf[x] = uf[uf[x]];
        x     = uf[x];
      }
      return x;
    };
    auto uni = [&](uint32_t x, uint32_t y) {
      x = find(x);
      y = find(y);
      if (x != y) {
        uf[std::max(x, y)] = std::min(x, y);
      }
    };
    for (auto node : a->forward_class()) {
      uint32_t u = gu::match_of(node);
      if (u == 0) {
        continue;
      }
      for (const auto& e : node.out_edges()) {
        if (gu::is_graph_output_pin(e.sink)) {
          continue;
        }
        uint32_t v = gu::match_of(e.sink.get_master_node());
        if (v != 0) {
          uni(u, v);
        }
      }
    }
    absl::flat_hash_map<uint32_t, uint32_t> root2region;
    uint32_t                                next_region = 1;
    auto                                    region_of   = [&](uint32_t id) -> uint32_t {
      uint32_t r           = find(id);
      auto [it, inserted] = root2region.try_emplace(r, next_region);
      if (inserted) {
        ++next_region;
      }
      return it->second;
    };
    for (hhds::Graph* g : {a, b}) {
      for (auto node : g->forward_class()) {
        uint32_t id = gu::match_of(node);
        if (id != 0) {
          stamp(node, region_of(id));
        }
      }
    }
    res.regions = next_region - 1;
  }

  uint32_t total = res.a_matched + res.a_unmatched + res.b_matched + res.b_unmatched;
  res.similarity = total == 0 ? 1.0 : static_cast<double>(res.a_matched + res.b_matched) / static_cast<double>(total);

  if (opts.verbose) {
    std::print("semdiff: ref {}/{} matched, impl {}/{} matched, {} regions, similarity {:.3f}\n",
               res.a_matched,
               res.a_matched + res.a_unmatched,
               res.b_matched,
               res.b_matched + res.b_unmatched,
               res.regions,
               res.similarity);
  }
  return res;
}

}  // namespace livehd::semdiff
